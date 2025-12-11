/**
 * @file ble_service.cpp
 * @brief LNK-22 BLE GATT Service Implementation
 *
 * Implementation of the Bluetooth Low Energy service for iOS/Android
 * app communication with LNK-22 mesh radios.
 */

#include "ble_service.h"
#include "../config.h"

// Singleton instance
LNK22BLEService* LNK22BLEService::_instance = nullptr;
LNK22BLEService bleService;

// Custom UUID helper
static uint8_t makeUUID128(uint16_t shortUUID, uint8_t* uuid128) {
    // Base UUID: 4C4E4B32-XXXX-1000-8000-00805F9B34FB
    const uint8_t baseUUID[] = {
        0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x32, 0x4B, 0x00, 0x00, 0x4E, 0x4C
    };
    memcpy(uuid128, baseUUID, 16);
    uuid128[12] = shortUUID & 0xFF;
    uuid128[13] = (shortUUID >> 8) & 0xFF;
    return 16;
}

// ============================================================================
// Constructor
// ============================================================================

LNK22BLEService::LNK22BLEService()
    : _service(BLEUuid(makeUUID128Arr(LNK22_SERVICE_UUID)))
    , _msgRxChar(BLEUuid(makeUUID128Arr(LNK22_MSG_RX_UUID)))
    , _msgTxChar(BLEUuid(makeUUID128Arr(LNK22_MSG_TX_UUID)))
    , _commandChar(BLEUuid(makeUUID128Arr(LNK22_COMMAND_UUID)))
    , _statusChar(BLEUuid(makeUUID128Arr(LNK22_STATUS_UUID)))
    , _neighborsChar(BLEUuid(makeUUID128Arr(LNK22_NEIGHBORS_UUID)))
    , _routesChar(BLEUuid(makeUUID128Arr(LNK22_ROUTES_UUID)))
    , _configChar(BLEUuid(makeUUID128Arr(LNK22_CONFIG_UUID)))
    , _gpsChar(BLEUuid(makeUUID128Arr(LNK22_GPS_UUID)))
    , _messageCallback(nullptr)
    , _commandCallback(nullptr)
    , _configCallback(nullptr)
    , _connected(false)
    , _connHandle(BLE_CONN_HANDLE_INVALID)
{
    _instance = this;
    memset(&_currentStatus, 0, sizeof(_currentStatus));
    memset(&_currentConfig, 0, sizeof(_currentConfig));
    memset(&_currentGPS, 0, sizeof(_currentGPS));
}

// Helper to create UUID array
static uint8_t* makeUUID128Arr(uint16_t shortUUID) {
    static uint8_t uuid[16];
    const uint8_t baseUUID[] = {
        0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x32, 0x4B, 0x00, 0x00, 0x4E, 0x4C
    };
    memcpy(uuid, baseUUID, 16);
    uuid[12] = shortUUID & 0xFF;
    uuid[13] = (shortUUID >> 8) & 0xFF;
    return uuid;
}

// ============================================================================
// Initialization
// ============================================================================

bool LNK22BLEService::begin(const char* deviceName) {
    // Initialize Bluefruit
    if (!Bluefruit.begin()) {
        Serial.println("[BLE] Failed to initialize Bluefruit");
        return false;
    }

    // Set device name
    Bluefruit.setName(deviceName);

    // Set TX power
    Bluefruit.setTxPower(4); // 4 dBm

    // Set connection callbacks
    Bluefruit.Periph.setConnectCallback(connectCallback);
    Bluefruit.Periph.setDisconnectCallback(disconnectCallback);

    // Setup the service
    setupService();

    Serial.println("[BLE] LNK-22 BLE Service initialized");
    return true;
}

void LNK22BLEService::setupService() {
    // Begin the service
    _service.begin();

    // Setup all characteristics
    setupCharacteristics();
}

void LNK22BLEService::setupCharacteristics() {
    // -------------------------------------------------------------------------
    // Message RX Characteristic (Write)
    // -------------------------------------------------------------------------
    _msgRxChar.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
    _msgRxChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    _msgRxChar.setMaxLen(247); // Max BLE packet size
    _msgRxChar.setWriteCallback(msgRxWriteCallback);
    _msgRxChar.begin();

    // -------------------------------------------------------------------------
    // Message TX Characteristic (Notify)
    // -------------------------------------------------------------------------
    _msgTxChar.setProperties(CHR_PROPS_NOTIFY);
    _msgTxChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    _msgTxChar.setMaxLen(247);
    _msgTxChar.begin();

    // -------------------------------------------------------------------------
    // Command Characteristic (Write)
    // -------------------------------------------------------------------------
    _commandChar.setProperties(CHR_PROPS_WRITE);
    _commandChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    _commandChar.setMaxLen(32);
    _commandChar.setWriteCallback(commandWriteCallback);
    _commandChar.begin();

    // -------------------------------------------------------------------------
    // Status Characteristic (Read/Notify)
    // -------------------------------------------------------------------------
    _statusChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    _statusChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    _statusChar.setFixedLen(sizeof(BLEDeviceStatus));
    _statusChar.begin();

    // -------------------------------------------------------------------------
    // Neighbors Characteristic (Read/Notify)
    // -------------------------------------------------------------------------
    _neighborsChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    _neighborsChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    _neighborsChar.setMaxLen(247);
    _neighborsChar.begin();

    // -------------------------------------------------------------------------
    // Routes Characteristic (Read/Notify)
    // -------------------------------------------------------------------------
    _routesChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    _routesChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    _routesChar.setMaxLen(247);
    _routesChar.begin();

    // -------------------------------------------------------------------------
    // Configuration Characteristic (Read/Write)
    // -------------------------------------------------------------------------
    _configChar.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
    _configChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    _configChar.setFixedLen(sizeof(BLEDeviceConfig));
    _configChar.setWriteCallback(configWriteCallback);
    _configChar.begin();

    // -------------------------------------------------------------------------
    // GPS Characteristic (Read/Notify)
    // -------------------------------------------------------------------------
    _gpsChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    _gpsChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    _gpsChar.setFixedLen(sizeof(BLEGPSPosition));
    _gpsChar.begin();
}

// ============================================================================
// Update Loop
// ============================================================================

void LNK22BLEService::update() {
    // Nothing to do in update for now
    // Notifications are sent immediately when data changes
}

// ============================================================================
// Connection Management
// ============================================================================

bool LNK22BLEService::isConnected() {
    return _connected && (_connHandle != BLE_CONN_HANDLE_INVALID);
}

void LNK22BLEService::startAdvertising() {
    // Advertising packet
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(_service);

    // Scan response packet
    Bluefruit.ScanResponse.addName();

    // Start advertising
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);    // in units of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
    Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising

    Serial.println("[BLE] Advertising started");
}

void LNK22BLEService::stopAdvertising() {
    Bluefruit.Advertising.stop();
    Serial.println("[BLE] Advertising stopped");
}

void LNK22BLEService::disconnect() {
    if (_connHandle != BLE_CONN_HANDLE_INVALID) {
        Bluefruit.disconnect(_connHandle);
    }
}

// ============================================================================
// Static Callbacks
// ============================================================================

void LNK22BLEService::connectCallback(uint16_t conn_handle) {
    if (_instance) {
        _instance->_connected = true;
        _instance->_connHandle = conn_handle;

        // Get connection info
        BLEConnection* conn = Bluefruit.Connection(conn_handle);
        char central_name[32] = {0};
        conn->getPeerName(central_name, sizeof(central_name));

        Serial.print("[BLE] Connected to: ");
        Serial.println(central_name);

        // Send initial status
        _instance->notifyStatus(_instance->_currentStatus);
    }
}

void LNK22BLEService::disconnectCallback(uint16_t conn_handle, uint8_t reason) {
    (void)conn_handle;

    if (_instance) {
        _instance->_connected = false;
        _instance->_connHandle = BLE_CONN_HANDLE_INVALID;

        Serial.print("[BLE] Disconnected, reason: 0x");
        Serial.println(reason, HEX);
    }
}

void LNK22BLEService::msgRxWriteCallback(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    (void)conn_handle;
    (void)chr;

    if (_instance && _instance->_messageCallback && len >= 6) {
        // Parse message: [type:1][destination:4][channel:1][payload:N]
        uint8_t type = data[0];
        uint32_t destination;
        memcpy(&destination, &data[1], 4);
        uint8_t channel = data[5];

        const uint8_t* payload = (len > 6) ? &data[6] : nullptr;
        size_t payloadLen = (len > 6) ? len - 6 : 0;

        _instance->_messageCallback(type, destination, channel, payload, payloadLen);
    }
}

void LNK22BLEService::commandWriteCallback(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    (void)conn_handle;
    (void)chr;

    if (_instance && _instance->_commandCallback && len >= 1) {
        uint8_t command = data[0];
        const uint8_t* params = (len > 1) ? &data[1] : nullptr;
        size_t paramsLen = (len > 1) ? len - 1 : 0;

        _instance->_commandCallback(command, params, paramsLen);
    }
}

void LNK22BLEService::configWriteCallback(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    (void)conn_handle;
    (void)chr;

    if (_instance && _instance->_configCallback && len >= sizeof(BLEDeviceConfig)) {
        BLEDeviceConfig config;
        memcpy(&config, data, sizeof(BLEDeviceConfig));
        _instance->_configCallback(&config);
    }
}

// ============================================================================
// Notification Methods
// ============================================================================

void LNK22BLEService::notifyMessage(uint8_t type, uint32_t source, uint8_t channel,
                                     uint32_t timestamp, const uint8_t* payload, size_t length) {
    if (!isConnected()) return;

    // Build notification packet
    // Format: [type:1][source:4][channel:1][timestamp:4][payload:N]
    size_t totalLen = 10 + length;
    uint8_t* buffer = (uint8_t*)malloc(totalLen);
    if (!buffer) return;

    buffer[0] = type;
    memcpy(&buffer[1], &source, 4);
    buffer[5] = channel;
    memcpy(&buffer[6], &timestamp, 4);
    if (payload && length > 0) {
        memcpy(&buffer[10], payload, length);
    }

    _msgTxChar.notify(_connHandle, buffer, totalLen);
    free(buffer);
}

void LNK22BLEService::notifyStatus(const BLEDeviceStatus& status) {
    if (!isConnected()) return;

    _currentStatus = status;
    _statusChar.notify(_connHandle, (uint8_t*)&status, sizeof(status));
}

void LNK22BLEService::notifyNeighbors(const BLENeighborEntry* neighbors, size_t count) {
    if (!isConnected() || count == 0) return;

    // Limit to what fits in one notification
    size_t maxCount = 247 / sizeof(BLENeighborEntry);
    if (count > maxCount) count = maxCount;

    _neighborsChar.notify(_connHandle, (uint8_t*)neighbors, count * sizeof(BLENeighborEntry));
}

void LNK22BLEService::notifyRoutes(const BLERouteEntry* routes, size_t count) {
    if (!isConnected() || count == 0) return;

    // Limit to what fits in one notification
    size_t maxCount = 247 / sizeof(BLERouteEntry);
    if (count > maxCount) count = maxCount;

    _routesChar.notify(_connHandle, (uint8_t*)routes, count * sizeof(BLERouteEntry));
}

void LNK22BLEService::notifyGPS(const BLEGPSPosition& position) {
    if (!isConnected()) return;

    _currentGPS = position;
    _gpsChar.notify(_connHandle, (uint8_t*)&position, sizeof(position));
}

// ============================================================================
// Callback Registration
// ============================================================================

void LNK22BLEService::onMessage(MessageCallback callback) {
    _messageCallback = callback;
}

void LNK22BLEService::onCommand(CommandCallback callback) {
    _commandCallback = callback;
}

void LNK22BLEService::onConfig(ConfigCallback callback) {
    _configCallback = callback;
}

// ============================================================================
// Status Methods
// ============================================================================

void LNK22BLEService::setStatus(const BLEDeviceStatus& status) {
    _currentStatus = status;
    _statusChar.write((uint8_t*)&status, sizeof(status));
}

void LNK22BLEService::setConfig(const BLEDeviceConfig& config) {
    _currentConfig = config;
    _configChar.write((uint8_t*)&config, sizeof(config));
}

void LNK22BLEService::setGPSPosition(const BLEGPSPosition& position) {
    _currentGPS = position;
    _gpsChar.write((uint8_t*)&position, sizeof(position));
}
