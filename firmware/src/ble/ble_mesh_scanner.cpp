/**
 * @file ble_mesh_scanner.cpp
 * @brief LNK-22 BLE Mesh Scanner Implementation
 */

#include "ble_mesh_scanner.h"
#include "../mesh/mesh.h"
#include "../config.h"

// Singleton instance
BLEMeshScanner* BLEMeshScanner::_instance = nullptr;
BLEMeshScanner bleMeshScanner;

// Scan interval (ms) - scan for 2 seconds every 10 seconds
#define BLE_SCAN_INTERVAL   10000
#define BLE_SCAN_DURATION   2000
#define BLE_NODE_TIMEOUT    60000  // 60 seconds

BLEMeshScanner::BLEMeshScanner()
    : _nodeAddr(0)
    , _mesh(nullptr)
    , _enabled(true)
    , _scanning(false)
    , _advertising(false)
    , _lastScanTime(0)
    , _lastAdvUpdate(0)
{
    _instance = this;
    memset(_nodes, 0, sizeof(_nodes));
    memset(_advData, 0, sizeof(_advData));
}

bool BLEMeshScanner::begin(uint32_t nodeAddr, Mesh* meshPtr) {
    _nodeAddr = nodeAddr;
    _mesh = meshPtr;

    // Build manufacturer data for advertising
    // Format: [company ID: 2 bytes] [node address: 4 bytes] [channel: 1 byte]
    _advData[0] = LNK22_MANUFACTURER_ID & 0xFF;
    _advData[1] = (LNK22_MANUFACTURER_ID >> 8) & 0xFF;
    memcpy(&_advData[2], &_nodeAddr, 4);
    _advData[6] = 0;  // Channel (updated in setupAdvertising)

    setupAdvertising();
    setupScanning();

    Serial.println("[BLE-MESH] Scanner initialized");
    Serial.print("[BLE-MESH] Node addr: 0x");
    Serial.println(_nodeAddr, HEX);

    return true;
}

void BLEMeshScanner::setupAdvertising() {
    // Add manufacturer-specific data to existing BLE advertisements
    // This allows other LNK-22 devices to discover us while we're advertising

    // Note: We add to existing advertising, not replace it
    // The main BLE service handles primary advertising
}

void BLEMeshScanner::setupScanning() {
    // Configure scanner for passive scanning
    Bluefruit.Scanner.setRxCallback(scanCallback);
    Bluefruit.Scanner.restartOnDisconnect(false);
    Bluefruit.Scanner.filterRssi(-90);  // Only report if RSSI > -90
    Bluefruit.Scanner.setInterval(160, 80);  // 100ms interval, 50ms window
    Bluefruit.Scanner.useActiveScan(false);  // Passive scan
}

void BLEMeshScanner::update() {
    if (!_enabled) return;

    unsigned long now = millis();

    // Periodic scanning
    if (!_scanning && (now - _lastScanTime > BLE_SCAN_INTERVAL)) {
        startScan();
    }

    // Stop scan after duration
    if (_scanning && (now - _lastScanTime > BLE_SCAN_DURATION)) {
        stopScan();
    }

    // Cleanup old nodes
    cleanupNodes();
}

void BLEMeshScanner::startScan() {
    if (_scanning) return;

    // Start scanning
    Bluefruit.Scanner.start(0);  // 0 = don't stop automatically
    _scanning = true;
    _lastScanTime = millis();

    #if DEBUG_MESH
    Serial.println("[BLE-MESH] Scan started");
    #endif
}

void BLEMeshScanner::stopScan() {
    if (!_scanning) return;

    Bluefruit.Scanner.stop();
    _scanning = false;

    #if DEBUG_MESH
    Serial.println("[BLE-MESH] Scan stopped");
    #endif
}

void BLEMeshScanner::setEnabled(bool enabled) {
    _enabled = enabled;
    if (!enabled && _scanning) {
        stopScan();
    }
}

uint8_t BLEMeshScanner::getNodeCount() const {
    uint8_t count = 0;
    for (int i = 0; i < MAX_BLE_MESH_NODES; i++) {
        if (_nodes[i].valid) count++;
    }
    return count;
}

uint32_t BLEMeshScanner::getLowestMeshAddress() const {
    uint32_t lowest = _nodeAddr;  // Start with our own address
    for (int i = 0; i < MAX_BLE_MESH_NODES; i++) {
        if (_nodes[i].valid && _nodes[i].address < lowest) {
            lowest = _nodes[i].address;
        }
    }
    return lowest;
}

bool BLEMeshScanner::isLoRaLeader() const {
    // If no BLE mesh peers, we're always the leader (isolated radio)
    if (getNodeCount() == 0) {
        return true;
    }
    // Leader is the node with the lowest address
    return _nodeAddr == getLowestMeshAddress();
}

void BLEMeshScanner::printNodes() {
    Serial.println("\n=== BLE Mesh Nodes ===");
    unsigned long now = millis();
    int count = 0;

    for (int i = 0; i < MAX_BLE_MESH_NODES; i++) {
        if (_nodes[i].valid) {
            Serial.print("  0x");
            Serial.print(_nodes[i].address, HEX);
            Serial.print(" RSSI:");
            Serial.print(_nodes[i].rssi);
            Serial.print(" CH:");
            Serial.print(_nodes[i].channel);
            Serial.print(" (");
            Serial.print((now - _nodes[i].lastSeen) / 1000);
            Serial.println("s ago)");
            count++;
        }
    }

    if (count == 0) {
        Serial.println("  (no BLE mesh nodes found)");
    }
    Serial.println("=====================\n");
}

void BLEMeshScanner::addOrUpdateNode(uint32_t addr, int8_t rssi, uint8_t channel) {
    // Don't add ourselves
    if (addr == _nodeAddr) return;

    int slot = -1;

    // Find existing or empty slot
    for (int i = 0; i < MAX_BLE_MESH_NODES; i++) {
        if (_nodes[i].valid && _nodes[i].address == addr) {
            slot = i;
            break;
        }
        if (slot == -1 && !_nodes[i].valid) {
            slot = i;
        }
    }

    // Find oldest if no slot
    if (slot == -1) {
        unsigned long oldest = millis();
        for (int i = 0; i < MAX_BLE_MESH_NODES; i++) {
            if (_nodes[i].lastSeen < oldest) {
                oldest = _nodes[i].lastSeen;
                slot = i;
            }
        }
    }

    if (slot != -1) {
        bool isNew = !_nodes[slot].valid || _nodes[slot].address != addr;

        _nodes[slot].address = addr;
        _nodes[slot].rssi = rssi;
        _nodes[slot].channel = channel;
        _nodes[slot].lastSeen = millis();
        _nodes[slot].valid = true;

        if (isNew) {
            Serial.print("[BLE-MESH] Discovered: 0x");
            Serial.print(addr, HEX);
            Serial.print(" RSSI:");
            Serial.println(rssi);
        }

        // Update mesh neighbor table with BLE interface
        if (_mesh) {
            _mesh->updateNeighbor(addr, rssi, 0, IFACE_BLE);
        }
    }
}

void BLEMeshScanner::cleanupNodes() {
    unsigned long now = millis();

    for (int i = 0; i < MAX_BLE_MESH_NODES; i++) {
        if (_nodes[i].valid) {
            if (now - _nodes[i].lastSeen > BLE_NODE_TIMEOUT) {
                #if DEBUG_MESH
                Serial.print("[BLE-MESH] Node expired: 0x");
                Serial.println(_nodes[i].address, HEX);
                #endif
                _nodes[i].valid = false;
            }
        }
    }
}

// Static scan callback
void BLEMeshScanner::scanCallback(ble_gap_evt_adv_report_t* report) {
    if (!_instance) return;

    // Look for LNK-22 manufacturer data in the advertisement
    uint8_t buffer[32];
    uint8_t len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, buffer, sizeof(buffer));

    if (len >= LNK22_ADV_DATA_LEN) {
        // Check company ID
        uint16_t companyId = buffer[0] | (buffer[1] << 8);
        if (companyId == LNK22_MANUFACTURER_ID) {
            // Extract node address and channel
            uint32_t nodeAddr;
            memcpy(&nodeAddr, &buffer[2], 4);
            uint8_t channel = buffer[6];

            // Debug output
            Serial.print("[BLE-MESH] Found mfg data! Addr: 0x");
            Serial.print(nodeAddr, HEX);
            Serial.print(" RSSI: ");
            Serial.println(report->rssi);

            // Add/update the node
            _instance->addOrUpdateNode(nodeAddr, report->rssi, channel);

            // Resume and return - we found what we need
            Bluefruit.Scanner.resume();
            return;
        }
    }

    // Also check device name for "LNK-" prefix in scan response (name format is LNK-XXXX)
    len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, buffer, sizeof(buffer));
    if (len == 0) {
        len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, buffer, sizeof(buffer));
    }

    if (len >= 4) {
        buffer[len] = '\0';
        // Check for "LNK-" prefix (name format is LNK-XXXX where XXXX is 4 hex digits)
        if (strncmp((char*)buffer, "LNK-", 4) == 0) {
            // Found an LNK device by name - log it for debug
            Serial.print("[BLE-MESH] Found LNK by name: ");
            Serial.print((char*)buffer);
            Serial.print(" RSSI:");
            Serial.println(report->rssi);

            // Try to extract address from name if format is "LNK-XXXX" (4 hex digits)
            if (len >= 8 && len <= 9) {
                // Name is "LNK-XXXX" format - extract last 4 chars
                char addrStr[5];
                memcpy(addrStr, &buffer[4], 4);
                addrStr[4] = '\0';
                uint32_t nodeAddr = strtoul(addrStr, NULL, 16);
                if (nodeAddr != 0) {
                    // Note: This is only partial address - manufacturer data has full address
                    Serial.print("[BLE-MESH] Partial addr from name: 0x");
                    Serial.println(nodeAddr, HEX);
                }
            }
        }
    }

    // Resume scanning
    Bluefruit.Scanner.resume();
}
