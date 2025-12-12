/**
 * @file ble_relay.h
 * @brief LNK-22 BLE Mesh Relay
 *
 * Enables phones and computers to act as mesh relay nodes via BLE.
 * Bridges traffic between BLE-connected devices and the LoRa mesh.
 *
 * Features:
 * - Relay LoRa mesh messages to BLE-connected clients
 * - Forward BLE client messages into LoRa mesh
 * - Support multiple simultaneous BLE relay clients
 * - Message deduplication to prevent loops
 * - Optional BLE-to-BLE relay (mesh extension via phone chain)
 */

#ifndef BLE_RELAY_H
#define BLE_RELAY_H

#include <Arduino.h>
#include <bluefruit.h>
#include "../protocol/protocol.h"

// Maximum BLE relay clients
#define MAX_BLE_RELAY_CLIENTS 4

// Message deduplication cache size
#define BLE_DEDUP_CACHE_SIZE 32

// Relay message types
enum BLERelayMsgType {
    RELAY_MSG_DATA      = 0x01,  // Normal data message
    RELAY_MSG_BEACON    = 0x02,  // Beacon/discovery
    RELAY_MSG_ROUTE_REQ = 0x03,  // Route request
    RELAY_MSG_ROUTE_REP = 0x04,  // Route reply
    RELAY_MSG_ACK       = 0x05,  // Acknowledgment
    RELAY_MSG_REGISTER  = 0x10,  // Client registration
    RELAY_MSG_UNREGISTER= 0x11,  // Client unregistration
    RELAY_MSG_HEARTBEAT = 0x12,  // Keep-alive
    RELAY_MSG_STATUS    = 0x20,  // Relay status update
};

// Relay client info
struct BLERelayClient {
    uint32_t virtualAddr;    // Virtual mesh address for this client
    uint16_t connHandle;     // BLE connection handle
    uint32_t lastSeen;       // Last activity timestamp
    uint32_t msgRelayed;     // Messages relayed count
    bool     active;         // Is client active
    bool     relayEnabled;   // Does client want to relay messages
    char     name[17];       // Client name
};

// Message deduplication entry
struct BLEDedupEntry {
    uint32_t hash;           // Message hash
    uint32_t timestamp;      // When seen
    bool     valid;
};

// Relay statistics
struct BLERelayStats {
    uint32_t loraToBleMsgs;  // Messages from LoRa to BLE
    uint32_t bleToLoraMsgs;  // Messages from BLE to LoRa
    uint32_t bleToBle;       // Messages between BLE clients
    uint32_t duplicates;     // Duplicates filtered
    uint32_t errors;         // Errors encountered
};

/**
 * @brief BLE Mesh Relay Manager
 *
 * Handles bridging between BLE-connected clients and the LoRa mesh.
 */
class BLEMeshRelay {
public:
    BLEMeshRelay();

    /**
     * @brief Initialize the BLE relay system
     * @param nodeAddr This node's mesh address
     * @return true if successful
     */
    bool begin(uint32_t nodeAddr);

    /**
     * @brief Update loop - call from main loop
     */
    void update();

    /**
     * @brief Check if relay is active
     */
    bool isActive() const { return _active; }

    /**
     * @brief Get number of connected relay clients
     */
    uint8_t getClientCount() const;

    /**
     * @brief Handle incoming LoRa mesh packet for relay
     * @param packet The received packet
     * @param rssi Signal strength
     * @param snr Signal-to-noise ratio
     * @return true if packet was relayed to BLE clients
     */
    bool relayFromLoRa(const Packet* packet, int16_t rssi, int8_t snr);

    /**
     * @brief Handle incoming BLE message for mesh relay
     * @param connHandle BLE connection handle
     * @param data Raw message data
     * @param len Data length
     * @return true if message was forwarded to mesh
     */
    bool relayFromBLE(uint16_t connHandle, const uint8_t* data, uint16_t len);

    /**
     * @brief Register a new BLE relay client
     * @param connHandle BLE connection handle
     * @param name Client name (optional)
     * @param relayEnabled Whether client wants to relay
     * @return Virtual mesh address assigned to client, 0 on failure
     */
    uint32_t registerClient(uint16_t connHandle, const char* name = nullptr, bool relayEnabled = true);

    /**
     * @brief Unregister a BLE relay client
     * @param connHandle BLE connection handle
     */
    void unregisterClient(uint16_t connHandle);

    /**
     * @brief Check if a connection is a registered relay client
     */
    bool isRelayClient(uint16_t connHandle) const;

    /**
     * @brief Get virtual address for a BLE client
     */
    uint32_t getClientAddr(uint16_t connHandle) const;

    /**
     * @brief Get relay statistics
     */
    const BLERelayStats& getStats() const { return _stats; }

    /**
     * @brief Print relay status to Serial
     */
    void printStatus();

    /**
     * @brief Enable/disable BLE-to-BLE relay
     */
    void setBLERelayEnabled(bool enabled) { _bleTobleEnabled = enabled; }

private:
    uint32_t _nodeAddr;
    bool _active;
    bool _bleTobleEnabled;

    // Relay clients
    BLERelayClient _clients[MAX_BLE_RELAY_CLIENTS];

    // Message deduplication
    BLEDedupEntry _dedupCache[BLE_DEDUP_CACHE_SIZE];

    // Statistics
    BLERelayStats _stats;

    // Virtual address counter (for assigning addresses to BLE clients)
    uint32_t _nextVirtualAddr;

    // BLE characteristic for relay data
    BLECharacteristic _relayChar;

    // Internal methods
    bool isDuplicate(const uint8_t* data, uint16_t len);
    void recordMessage(const uint8_t* data, uint16_t len);
    uint32_t hashMessage(const uint8_t* data, uint16_t len);
    void cleanupDedupCache();
    void cleanupClients();

    BLERelayClient* findClient(uint16_t connHandle);
    BLERelayClient* findClientByAddr(uint32_t addr);
    int findFreeClientSlot();

    bool sendToClient(BLERelayClient* client, const uint8_t* data, uint16_t len);
    bool sendToAllClients(const uint8_t* data, uint16_t len, uint16_t excludeHandle = 0xFFFF);

    // Static callback wrappers
    static void relayWriteCallback(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len);
    static BLEMeshRelay* _instance;
};

// Global instance
extern BLEMeshRelay bleRelay;

#endif // BLE_RELAY_H
