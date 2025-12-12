/**
 * @file ble_mesh_scanner.h
 * @brief LNK-22 BLE Mesh Scanner
 *
 * Scans for other LNK-22 devices via BLE to discover neighbors
 * that are within BLE range (more efficient than LoRa for nearby nodes).
 */

#ifndef BLE_MESH_SCANNER_H
#define BLE_MESH_SCANNER_H

#include <Arduino.h>
#include <bluefruit.h>

// Forward declaration
class Mesh;

// LNK-22 manufacturer data in BLE advertisements
// Format: [2 bytes: company ID = 0xFFFF] [4 bytes: node address] [1 byte: channel]
#define LNK22_MANUFACTURER_ID  0xFFFF  // Custom/test ID
#define LNK22_ADV_DATA_LEN     7

// Scan result entry
struct BLEMeshNode {
    uint32_t address;       // LNK-22 node address
    int8_t   rssi;          // BLE RSSI
    uint8_t  channel;       // Node's current channel
    unsigned long lastSeen; // When discovered
    bool valid;
};

#define MAX_BLE_MESH_NODES 16

class BLEMeshScanner {
public:
    BLEMeshScanner();

    /**
     * @brief Initialize the BLE mesh scanner
     * @param nodeAddr This node's address (to include in advertisements)
     * @param meshPtr Pointer to mesh for neighbor updates
     * @return true if successful
     */
    bool begin(uint32_t nodeAddr, Mesh* meshPtr);

    /**
     * @brief Update loop - call from main loop
     * Handles periodic scanning and advertising
     */
    void update();

    /**
     * @brief Start BLE scanning for other LNK-22 nodes
     */
    void startScan();

    /**
     * @brief Stop scanning
     */
    void stopScan();

    /**
     * @brief Check if scanner is active
     */
    bool isScanning() const { return _scanning; }

    /**
     * @brief Get count of discovered BLE mesh nodes
     */
    uint8_t getNodeCount() const;

    /**
     * @brief Check if this node is the LoRa leader in BLE mesh
     * Leader = lowest address among all BLE mesh peers (including self)
     * Used for LoRa forwarding election - only leader sends to LoRa
     */
    bool isLoRaLeader() const;

    /**
     * @brief Get the lowest address in the BLE mesh (including self)
     */
    uint32_t getLowestMeshAddress() const;

    /**
     * @brief Enable/disable the scanner
     */
    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }

    /**
     * @brief Print discovered BLE mesh nodes
     */
    void printNodes();

private:
    uint32_t _nodeAddr;
    Mesh* _mesh;
    bool _enabled;
    bool _scanning;
    bool _advertising;
    unsigned long _lastScanTime;
    unsigned long _lastAdvUpdate;

    // Discovered nodes
    BLEMeshNode _nodes[MAX_BLE_MESH_NODES];

    // Advertising data
    uint8_t _advData[LNK22_ADV_DATA_LEN];

    // Setup methods
    void setupAdvertising();
    void setupScanning();

    // Node management
    void addOrUpdateNode(uint32_t addr, int8_t rssi, uint8_t channel);
    void cleanupNodes();

    // Static callbacks
    static void scanCallback(ble_gap_evt_adv_report_t* report);
    static BLEMeshScanner* _instance;
};

// Global instance
extern BLEMeshScanner bleMeshScanner;

#endif // BLE_MESH_SCANNER_H
