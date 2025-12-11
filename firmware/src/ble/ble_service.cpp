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

// Pre-computed UUID arrays for each service/characteristic
// BLE UUID format (little-endian): FB349B5F-8000-0080-0010-324B{short}4E4C
// Displayed as: 4C4E{short}-4B32-1000-8000-00805F9B34FB
static const uint8_t UUID_SERVICE[]    = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x32, 0x4B, 0x01, 0x00, 0x4E, 0x4C};  // 0x0001
static const uint8_t UUID_MSG_RX[]     = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x32, 0x4B, 0x02, 0x00, 0x4E, 0x4C};  // 0x0002
static const uint8_t UUID_MSG_TX[]     = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x32, 0x4B, 0x03, 0x00, 0x4E, 0x4C};  // 0x0003
static const uint8_t UUID_COMMAND[]    = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x32, 0x4B, 0x04, 0x00, 0x4E, 0x4C};  // 0x0004
static const uint8_t UUID_STATUS[]     = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x32, 0x4B, 0x05, 0x00, 0x4E, 0x4C};  // 0x0005
static const uint8_t UUID_NEIGHBORS[]  = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x32, 0x4B, 0x06, 0x00, 0x4E, 0x4C};  // 0x0006
static const uint8_t UUID_ROUTES[]     = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x32, 0x4B, 0x07, 0x00, 0x4E, 0x4C};  // 0x0007
static const uint8_t UUID_CONFIG[]     = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x32, 0x4B, 0x08, 0x00, 0x4E, 0x4C};  // 0x0008
static const uint8_t UUID_GPS[]        = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x32, 0x4B, 0x09, 0x00, 0x4E, 0x4C};  // 0x0009

// ============================================================================
// Constructor
// ============================================================================

LNK22BLEService::LNK22BLEService()
    : _service(BLEUuid(UUID_SERVICE))
    , _msgRxChar(BLEUuid(UUID_MSG_RX))
    , _msgTxChar(BLEUuid(UUID_MSG_TX))
    , _commandChar(BLEUuid(UUID_COMMAND))
    , _statusChar(BLEUuid(UUID_STATUS))
    , _neighborsChar(BLEUuid(UUID_NEIGHBORS))
    , _routesChar(BLEUuid(UUID_ROUTES))
    , _configChar(BLEUuid(UUID_CONFIG))
    , _gpsChar(BLEUuid(UUID_GPS))
    , _messageCallback(nullptr)
    , _commandCallback(nullptr)
    , _configCallback(nullptr)
    , _connected(false)
    , _paired(false)
    , _connHandle(BLE_CONN_HANDLE_INVALID)
    , _pairingPin(123456)
{
    _instance = this;
    memset(&_currentStatus, 0, sizeof(_currentStatus));
    memset(&_currentConfig, 0, sizeof(_currentConfig));
    memset(&_currentGPS, 0, sizeof(_currentGPS));
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

    // -------------------------------------------------------------------------
    // Setup PIN Pairing (like Meshtastic)
    // -------------------------------------------------------------------------
    // Use fixed PIN 123456 for pairing (can be changed via config later)
    // IO Caps: display=true (show PIN), yes_no=false, keyboard=false
    Bluefruit.Security.setIOCaps(true, false, false);

    // Set static PIN (forces Legacy Pairing with passkey entry)
    Bluefruit.Security.setPIN((const char*)String(_pairingPin).c_str());

    // Register callbacks
    Bluefruit.Security.setPairPasskeyCallback(pairingPasskeyCallback);
    Bluefruit.Security.setSecuredCallback(securedCallback);
    Bluefruit.Security.setPairCompleteCallback(pairingCompleteCallback);

    Serial.print("[BLE] PIN pairing enabled, PIN: ");
    Serial.println(_pairingPin);

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

// ============================================================================
// Pairing Callbacks
// ============================================================================

bool LNK22BLEService::pairingPasskeyCallback(uint16_t conn_handle, uint8_t const passkey[6], bool match_request) {
    (void)conn_handle;
    (void)match_request;

    Serial.println("[BLE] Pairing requested");
    Serial.print("[BLE] Enter PIN on your device: ");
    for (int i = 0; i < 6; i++) {
        Serial.print((char)passkey[i]);
    }
    Serial.println();

    // Always accept (the PIN was already set in begin())
    return true;
}

void LNK22BLEService::securedCallback(uint16_t conn_handle) {
    BLEConnection* conn = Bluefruit.Connection(conn_handle);

    if (!conn->secured()) {
        // Pairing failed - kick out
        Serial.println("[BLE] Pairing failed, disconnecting");
        conn->disconnect();
    } else {
        Serial.println("[BLE] Connection secured (paired)");
    }
}

void LNK22BLEService::pairingCompleteCallback(uint16_t conn_handle, uint8_t auth_status) {
    if (auth_status == BLE_GAP_SEC_STATUS_SUCCESS) {
        Serial.println("[BLE] Pairing successful!");
        if (_instance) {
            _instance->_paired = true;
        }
    } else {
        Serial.print("[BLE] Pairing failed, status: 0x");
        Serial.println(auth_status, HEX);
        if (_instance) {
            _instance->_paired = false;
        }
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
