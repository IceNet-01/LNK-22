/**
 * @file ble_service.h
 * @brief LNK-22 BLE GATT Service
 *
 * Bluetooth Low Energy service for iOS/Android app communication.
 * Provides messaging, device control, and status monitoring over BLE.
 */

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <Arduino.h>
#include <bluefruit.h>

// ============================================================================
// BLE Service UUIDs
// ============================================================================

// Base UUID: 4C4E4B32-XXXX-1000-8000-00805F9B34FB (LNK2 in ASCII)
#define LNK22_UUID_BASE     0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, \
                            0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x4E, 0x4C

// Service UUID: 4C4E4B32-0001-1000-8000-00805F9B34FB
#define LNK22_SERVICE_UUID          0x0001

// Characteristic UUIDs
#define LNK22_MSG_RX_UUID           0x0002  // Write: Send messages
#define LNK22_MSG_TX_UUID           0x0003  // Notify: Receive messages
#define LNK22_COMMAND_UUID          0x0004  // Write: Device commands
#define LNK22_STATUS_UUID           0x0005  // Read/Notify: Device status
#define LNK22_NEIGHBORS_UUID        0x0006  // Read/Notify: Neighbor list
#define LNK22_ROUTES_UUID           0x0007  // Read/Notify: Routing table
#define LNK22_CONFIG_UUID           0x0008  // Read/Write: Configuration
#define LNK22_GPS_UUID              0x0009  // Read/Notify: GPS position
#define LNK22_NODENAME_UUID         0x000A  // Read/Write: Node name (17 bytes)

// ============================================================================
// Command Codes
// ============================================================================

enum BLECommand {
    CMD_SEND_BEACON     = 0x01,
    CMD_SWITCH_CHANNEL  = 0x02,
    CMD_SET_TX_POWER    = 0x03,
    CMD_REQUEST_STATUS  = 0x10,
    CMD_REQUEST_NEIGHBORS = 0x11,
    CMD_REQUEST_ROUTES  = 0x12,
    CMD_CLEAR_ROUTES    = 0x20,
    CMD_REBOOT          = 0xFE,
    CMD_FACTORY_RESET   = 0xFF
};

// ============================================================================
// Status Flags
// ============================================================================

#define STATUS_FLAG_ENCRYPTION  0x01
#define STATUS_FLAG_GPS         0x02
#define STATUS_FLAG_DISPLAY     0x04

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Device status structure (24 bytes)
 */
struct __attribute__((packed)) BLEDeviceStatus {
    uint32_t nodeAddress;
    uint32_t txCount;
    uint32_t rxCount;
    uint16_t neighborCount;
    uint16_t routeCount;
    uint8_t  channel;
    int8_t   txPower;
    uint8_t  battery;
    uint8_t  flags;
    uint32_t uptime;
};

/**
 * @brief Neighbor entry structure (16 bytes)
 */
struct __attribute__((packed)) BLENeighborEntry {
    uint32_t address;
    int16_t  rssi;
    int8_t   snr;
    uint8_t  quality;
    uint32_t lastSeen;
    uint32_t packetCount;
};

/**
 * @brief Route entry structure (14 bytes)
 */
struct __attribute__((packed)) BLERouteEntry {
    uint32_t destination;
    uint32_t nextHop;
    uint8_t  hopCount;
    uint8_t  quality;
    uint32_t timestamp;
};

/**
 * @brief GPS position structure (22 bytes)
 */
struct __attribute__((packed)) BLEGPSPosition {
    double   latitude;
    double   longitude;
    float    altitude;
    uint8_t  satellites;
    uint8_t  valid;
};

/**
 * @brief Device configuration structure (12 bytes)
 */
struct __attribute__((packed)) BLEDeviceConfig {
    uint8_t  channel;
    int8_t   txPower;
    uint8_t  spreadingFactor;
    uint8_t  reserved1;
    uint32_t bandwidth;
    uint8_t  flags;
    uint16_t beaconInterval;
    uint8_t  reserved2;
};

// ============================================================================
// Callback Types
// ============================================================================

typedef void (*MessageCallback)(uint8_t type, uint32_t source, uint32_t destination, uint8_t channel, const uint8_t* payload, size_t length);
typedef void (*CommandCallback)(uint8_t command, const uint8_t* params, size_t length);
typedef void (*ConfigCallback)(const BLEDeviceConfig* config);

// ============================================================================
// BLE Service Class
// ============================================================================

class LNK22BLEService {
public:
    LNK22BLEService();

    /**
     * @brief Initialize the BLE service
     * @param deviceName Name to advertise
     * @return true if successful
     */
    bool begin(const char* deviceName = "LNK-22");

    /**
     * @brief Update loop - call from main loop
     */
    void update();

    /**
     * @brief Check if a client is connected
     * @return true if connected
     */
    bool isConnected();

    /**
     * @brief Start advertising
     * @param nodeAddress The node's 32-bit mesh address for BLE discovery
     */
    void startAdvertising(uint32_t nodeAddress = 0);

    /**
     * @brief Stop advertising
     */
    void stopAdvertising();

    /**
     * @brief Disconnect current client
     */
    void disconnect();

    // -------------------------------------------------------------------------
    // Notification Methods
    // -------------------------------------------------------------------------

    /**
     * @brief Send a received message to the connected app
     * @param type Message type
     * @param source Source node address
     * @param channel Channel ID
     * @param timestamp Message timestamp
     * @param payload Message payload
     * @param length Payload length
     */
    void notifyMessage(uint8_t type, uint32_t source, uint8_t channel,
                       uint32_t timestamp, const uint8_t* payload, size_t length);

    /**
     * @brief Send status update to the connected app
     * @param status Device status
     */
    void notifyStatus(const BLEDeviceStatus& status);

    /**
     * @brief Send neighbor list to the connected app
     * @param neighbors Array of neighbor entries
     * @param count Number of neighbors
     */
    void notifyNeighbors(const BLENeighborEntry* neighbors, size_t count);

    /**
     * @brief Send routing table to the connected app
     * @param routes Array of route entries
     * @param count Number of routes
     */
    void notifyRoutes(const BLERouteEntry* routes, size_t count);

    /**
     * @brief Send GPS position to the connected app
     * @param position GPS position data
     */
    void notifyGPS(const BLEGPSPosition& position);

    // -------------------------------------------------------------------------
    // Callback Registration
    // -------------------------------------------------------------------------

    /**
     * @brief Set callback for received messages from app
     */
    void onMessage(MessageCallback callback);

    /**
     * @brief Set callback for commands from app
     */
    void onCommand(CommandCallback callback);

    /**
     * @brief Set callback for configuration changes from app
     */
    void onConfig(ConfigCallback callback);

    // -------------------------------------------------------------------------
    // Status Methods
    // -------------------------------------------------------------------------

    /**
     * @brief Update the device status (for read requests)
     * @param status Current device status
     */
    void setStatus(const BLEDeviceStatus& status);

    /**
     * @brief Update the configuration (for read requests)
     * @param config Current configuration
     */
    void setConfig(const BLEDeviceConfig& config);

    /**
     * @brief Update GPS position (for read requests)
     * @param position Current GPS position
     */
    void setGPSPosition(const BLEGPSPosition& position);

private:
    // BLE Service and Characteristics
    BLEService        _service;
    BLECharacteristic _msgRxChar;
    BLECharacteristic _msgTxChar;
    BLECharacteristic _commandChar;
    BLECharacteristic _statusChar;
    BLECharacteristic _neighborsChar;
    BLECharacteristic _routesChar;
    BLECharacteristic _configChar;
    BLECharacteristic _gpsChar;

    // Callbacks
    MessageCallback _messageCallback;
    CommandCallback _commandCallback;
    ConfigCallback  _configCallback;

    // Current state
    BLEDeviceStatus _currentStatus;
    BLEDeviceConfig _currentConfig;
    BLEGPSPosition  _currentGPS;
    bool _connected;
    bool _paired;

    // Connection handle
    uint16_t _connHandle;

    // Pairing PIN (default: 123456)
    uint32_t _pairingPin = 123456;

    // Internal methods
    void setupService();
    void setupCharacteristics();
    void setupCallbacks();

    // Static callback wrappers
    static void connectCallback(uint16_t conn_handle);
    static void disconnectCallback(uint16_t conn_handle, uint8_t reason);
    static void msgRxWriteCallback(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len);
    static void commandWriteCallback(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len);
    static void configWriteCallback(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len);

    // Pairing callback wrappers
    static bool pairingPasskeyCallback(uint16_t conn_handle, uint8_t const passkey[6], bool match_request);
    static void securedCallback(uint16_t conn_handle);
    static void pairingCompleteCallback(uint16_t conn_handle, uint8_t auth_status);

    // Singleton instance for callbacks
    static LNK22BLEService* _instance;
};

// Global BLE service instance
extern LNK22BLEService bleService;

#endif // BLE_SERVICE_H
