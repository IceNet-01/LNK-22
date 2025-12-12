/**
 * @file ble_relay.cpp
 * @brief LNK-22 BLE Mesh Relay Implementation
 *
 * Bridges BLE-connected devices into the LoRa mesh network.
 */

#include "ble_relay.h"
#include "ble_service.h"
#include "../mesh/mesh.h"
#include "../config.h"

// Singleton instance
BLEMeshRelay* BLEMeshRelay::_instance = nullptr;
BLEMeshRelay bleRelay;

// BLE Relay Characteristic UUID (extends LNK22 service)
// 4C4E4B32-000B-1000-8000-00805F9B34FB
static const uint8_t UUID_RELAY[] = {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x32, 0x4B, 0x0B, 0x00, 0x4E, 0x4C};

BLEMeshRelay::BLEMeshRelay()
    : _nodeAddr(0)
    , _active(false)
    , _bleTobleEnabled(true)
    , _nextVirtualAddr(0)
    , _relayChar(BLEUuid(UUID_RELAY))
{
    _instance = this;

    // Initialize clients
    for (int i = 0; i < MAX_BLE_RELAY_CLIENTS; i++) {
        _clients[i].active = false;
    }

    // Initialize dedup cache
    for (int i = 0; i < BLE_DEDUP_CACHE_SIZE; i++) {
        _dedupCache[i].valid = false;
    }

    // Initialize stats
    memset(&_stats, 0, sizeof(_stats));
}

bool BLEMeshRelay::begin(uint32_t nodeAddr) {
    _nodeAddr = nodeAddr;

    // Generate virtual address base for BLE clients
    // Use high byte 0xBE to indicate BLE-connected nodes
    // Format: 0xBExxxxxx where xxxxxx is based on node address
    _nextVirtualAddr = 0xBE000000 | (nodeAddr & 0x00FFFFFF);

    // Setup the relay characteristic
    _relayChar.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP | CHR_PROPS_NOTIFY);
    _relayChar.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    _relayChar.setMaxLen(247);  // Max BLE packet
    _relayChar.setWriteCallback(relayWriteCallback);
    _relayChar.begin();

    _active = true;

    Serial.println("[BLE-RELAY] Initialized");
    Serial.print("[BLE-RELAY] Virtual addr base: 0x");
    Serial.println(_nextVirtualAddr, HEX);

    return true;
}

void BLEMeshRelay::update() {
    if (!_active) return;

    static unsigned long lastCleanup = 0;
    unsigned long now = millis();

    // Cleanup every 10 seconds
    if (now - lastCleanup > 10000) {
        cleanupClients();
        cleanupDedupCache();
        lastCleanup = now;
    }
}

uint8_t BLEMeshRelay::getClientCount() const {
    uint8_t count = 0;
    for (int i = 0; i < MAX_BLE_RELAY_CLIENTS; i++) {
        if (_clients[i].active) count++;
    }
    return count;
}

bool BLEMeshRelay::relayFromLoRa(const Packet* packet, int16_t rssi, int8_t snr) {
    if (!_active || getClientCount() == 0) return false;

    // Don't relay our own packets
    if (packet->header.source == _nodeAddr) return false;

    // Don't relay packets we've already seen from BLE
    if (isDuplicate((uint8_t*)packet, sizeof(PacketHeader) + packet->header.payload_length)) {
        _stats.duplicates++;
        return false;
    }

    // Build relay message
    // Format: [type:1][rssi:2][snr:1][packet_header+payload]
    uint16_t totalLen = 4 + sizeof(PacketHeader) + packet->header.payload_length;
    uint8_t* buffer = (uint8_t*)malloc(totalLen);
    if (!buffer) {
        _stats.errors++;
        return false;
    }

    buffer[0] = RELAY_MSG_DATA;
    memcpy(&buffer[1], &rssi, 2);
    buffer[3] = (uint8_t)snr;
    memcpy(&buffer[4], &packet->header, sizeof(PacketHeader));
    if (packet->header.payload_length > 0) {
        memcpy(&buffer[4 + sizeof(PacketHeader)], packet->payload, packet->header.payload_length);
    }

    // Record message to prevent relay loops
    recordMessage((uint8_t*)packet, sizeof(PacketHeader) + packet->header.payload_length);

    // Send to all BLE relay clients
    bool sent = sendToAllClients(buffer, totalLen);
    free(buffer);

    if (sent) {
        _stats.loraToBleMsgs++;
        #if DEBUG_MESH
        Serial.print("[BLE-RELAY] LoRa->BLE: from 0x");
        Serial.print(packet->header.source, HEX);
        Serial.print(" type=");
        Serial.println(packet->header.type);
        #endif
    }

    return sent;
}

bool BLEMeshRelay::relayFromBLE(uint16_t connHandle, const uint8_t* data, uint16_t len) {
    if (!_active || len < 1) return false;

    BLERelayClient* client = findClient(connHandle);
    if (!client) {
        // Not a registered relay client
        return false;
    }

    uint8_t msgType = data[0];
    client->lastSeen = millis();

    // Keep mesh neighbor entry updated for this BLE client
    extern Mesh mesh;
    mesh.updateNeighbor(client->virtualAddr, -30, 10, IFACE_BLE);

    switch (msgType) {
        case RELAY_MSG_REGISTER: {
            // Client registration is handled separately
            return true;
        }

        case RELAY_MSG_UNREGISTER: {
            unregisterClient(connHandle);
            return true;
        }

        case RELAY_MSG_HEARTBEAT: {
            // Just update lastSeen (already done above)
            return true;
        }

        case RELAY_MSG_DATA: {
            // Format: [type:1][dest:4][flags:1][payload]
            if (len < 6) return false;

            uint32_t dest;
            memcpy(&dest, &data[1], 4);
            uint8_t flags = data[5];
            const uint8_t* payload = (len > 6) ? &data[6] : nullptr;
            uint16_t payloadLen = (len > 6) ? len - 6 : 0;

            // Check for duplicates
            if (isDuplicate(data, len)) {
                _stats.duplicates++;
                return false;
            }
            recordMessage(data, len);

            // BLE-to-BLE relay (if enabled and not a direct message)
            if (_bleTobleEnabled && (dest == 0xFFFFFFFF || findClientByAddr(dest) != nullptr)) {
                // Forward to other BLE clients
                sendToAllClients(data, len, connHandle);
                _stats.bleToBle++;
            }

            // Forward to LoRa mesh
            extern Mesh mesh;
            bool needsAck = (flags & FLAG_ACK_REQ) != 0;

            if (payloadLen > 0) {
                // Create mesh packet with client's virtual address as source
                Packet packet;
                memset(&packet, 0, sizeof(Packet));

                packet.header.version = PROTOCOL_VERSION;
                packet.header.type = PKT_DATA;
                packet.header.ttl = MAX_TTL;
                packet.header.flags = flags;
                packet.header.channel_id = mesh.getChannel();
                packet.header.source = client->virtualAddr;  // Use client's virtual address
                packet.header.destination = dest;
                packet.header.payload_length = payloadLen;

                memcpy(packet.payload, payload, payloadLen);

                // Send via radio directly (bypass mesh sendMessage to use virtual addr)
                extern Radio radio;
                if (radio.send(&packet)) {
                    _stats.bleToLoraMsgs++;
                    client->msgRelayed++;

                    #if DEBUG_MESH
                    Serial.print("[BLE-RELAY] BLE->LoRa: client 0x");
                    Serial.print(client->virtualAddr, HEX);
                    Serial.print(" -> 0x");
                    Serial.println(dest, HEX);
                    #endif

                    return true;
                }
            }
            break;
        }

        default:
            Serial.print("[BLE-RELAY] Unknown message type: 0x");
            Serial.println(msgType, HEX);
            break;
    }

    return false;
}

uint32_t BLEMeshRelay::registerClient(uint16_t connHandle, const char* name, bool relayEnabled) {
    // Check if already registered
    BLERelayClient* existing = findClient(connHandle);
    if (existing) {
        return existing->virtualAddr;
    }

    // Find free slot
    int slot = findFreeClientSlot();
    if (slot < 0) {
        Serial.println("[BLE-RELAY] No free client slots!");
        _stats.errors++;
        return 0;
    }

    // Assign virtual address
    uint32_t addr = _nextVirtualAddr++;

    // Setup client
    BLERelayClient* client = &_clients[slot];
    client->virtualAddr = addr;
    client->connHandle = connHandle;
    client->lastSeen = millis();
    client->msgRelayed = 0;
    client->active = true;
    client->relayEnabled = relayEnabled;

    if (name && strlen(name) > 0) {
        strncpy(client->name, name, 16);
        client->name[16] = '\0';
    } else {
        snprintf(client->name, 17, "BLE-%04X", addr & 0xFFFF);
    }

    Serial.print("[BLE-RELAY] Client registered: ");
    Serial.print(client->name);
    Serial.print(" (0x");
    Serial.print(addr, HEX);
    Serial.println(")");

    // Add BLE client as a mesh neighbor so it appears in neighbor list
    extern Mesh mesh;
    mesh.updateNeighbor(addr, -30, 10, IFACE_BLE);  // Strong signal for direct BLE connection

    // Send registration confirmation
    uint8_t response[9];
    response[0] = RELAY_MSG_STATUS;
    memcpy(&response[1], &addr, 4);
    memcpy(&response[5], &_nodeAddr, 4);
    sendToClient(client, response, 9);

    return addr;
}

void BLEMeshRelay::unregisterClient(uint16_t connHandle) {
    BLERelayClient* client = findClient(connHandle);
    if (client) {
        Serial.print("[BLE-RELAY] Client unregistered: ");
        Serial.print(client->name);
        Serial.print(" (relayed ");
        Serial.print(client->msgRelayed);
        Serial.println(" msgs)");

        client->active = false;
    }
}

bool BLEMeshRelay::isRelayClient(uint16_t connHandle) const {
    for (int i = 0; i < MAX_BLE_RELAY_CLIENTS; i++) {
        if (_clients[i].active && _clients[i].connHandle == connHandle) {
            return true;
        }
    }
    return false;
}

uint32_t BLEMeshRelay::getClientAddr(uint16_t connHandle) const {
    for (int i = 0; i < MAX_BLE_RELAY_CLIENTS; i++) {
        if (_clients[i].active && _clients[i].connHandle == connHandle) {
            return _clients[i].virtualAddr;
        }
    }
    return 0;
}

void BLEMeshRelay::printStatus() {
    Serial.println("\n=== BLE Mesh Relay Status ===");
    Serial.print("Active: ");
    Serial.println(_active ? "YES" : "NO");
    Serial.print("BLE-to-BLE Relay: ");
    Serial.println(_bleTobleEnabled ? "Enabled" : "Disabled");
    Serial.print("Clients: ");
    Serial.println(getClientCount());

    Serial.println("\nConnected Clients:");
    for (int i = 0; i < MAX_BLE_RELAY_CLIENTS; i++) {
        if (_clients[i].active) {
            Serial.print("  ");
            Serial.print(_clients[i].name);
            Serial.print(" (0x");
            Serial.print(_clients[i].virtualAddr, HEX);
            Serial.print(") - ");
            Serial.print(_clients[i].msgRelayed);
            Serial.print(" msgs, last seen ");
            Serial.print((millis() - _clients[i].lastSeen) / 1000);
            Serial.println("s ago");
        }
    }

    Serial.println("\nStatistics:");
    Serial.print("  LoRa -> BLE: ");
    Serial.println(_stats.loraToBleMsgs);
    Serial.print("  BLE -> LoRa: ");
    Serial.println(_stats.bleToLoraMsgs);
    Serial.print("  BLE -> BLE:  ");
    Serial.println(_stats.bleToBle);
    Serial.print("  Duplicates:  ");
    Serial.println(_stats.duplicates);
    Serial.print("  Errors:      ");
    Serial.println(_stats.errors);
    Serial.println("============================\n");
}

// ============================================================================
// Private Methods
// ============================================================================

bool BLEMeshRelay::isDuplicate(const uint8_t* data, uint16_t len) {
    uint32_t hash = hashMessage(data, len);

    for (int i = 0; i < BLE_DEDUP_CACHE_SIZE; i++) {
        if (_dedupCache[i].valid && _dedupCache[i].hash == hash) {
            return true;
        }
    }
    return false;
}

void BLEMeshRelay::recordMessage(const uint8_t* data, uint16_t len) {
    uint32_t hash = hashMessage(data, len);
    unsigned long now = millis();

    // Find oldest or empty slot
    int slot = 0;
    unsigned long oldest = now;

    for (int i = 0; i < BLE_DEDUP_CACHE_SIZE; i++) {
        if (!_dedupCache[i].valid) {
            slot = i;
            break;
        }
        if (_dedupCache[i].timestamp < oldest) {
            oldest = _dedupCache[i].timestamp;
            slot = i;
        }
    }

    _dedupCache[slot].hash = hash;
    _dedupCache[slot].timestamp = now;
    _dedupCache[slot].valid = true;
}

uint32_t BLEMeshRelay::hashMessage(const uint8_t* data, uint16_t len) {
    // Simple FNV-1a hash
    uint32_t hash = 2166136261;
    for (uint16_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619;
    }
    return hash;
}

void BLEMeshRelay::cleanupDedupCache() {
    unsigned long now = millis();

    for (int i = 0; i < BLE_DEDUP_CACHE_SIZE; i++) {
        if (_dedupCache[i].valid) {
            // Expire after 60 seconds
            if (now - _dedupCache[i].timestamp > 60000) {
                _dedupCache[i].valid = false;
            }
        }
    }
}

void BLEMeshRelay::cleanupClients() {
    unsigned long now = millis();

    for (int i = 0; i < MAX_BLE_RELAY_CLIENTS; i++) {
        if (_clients[i].active) {
            // Check if BLE connection is still valid
            BLEConnection* conn = Bluefruit.Connection(_clients[i].connHandle);
            if (!conn || !conn->connected()) {
                Serial.print("[BLE-RELAY] Client disconnected: ");
                Serial.println(_clients[i].name);
                _clients[i].active = false;
                continue;
            }

            // Timeout after 2 minutes of inactivity
            if (now - _clients[i].lastSeen > 120000) {
                Serial.print("[BLE-RELAY] Client timeout: ");
                Serial.println(_clients[i].name);
                _clients[i].active = false;
            }
        }
    }
}

BLERelayClient* BLEMeshRelay::findClient(uint16_t connHandle) {
    for (int i = 0; i < MAX_BLE_RELAY_CLIENTS; i++) {
        if (_clients[i].active && _clients[i].connHandle == connHandle) {
            return &_clients[i];
        }
    }
    return nullptr;
}

BLERelayClient* BLEMeshRelay::findClientByAddr(uint32_t addr) {
    for (int i = 0; i < MAX_BLE_RELAY_CLIENTS; i++) {
        if (_clients[i].active && _clients[i].virtualAddr == addr) {
            return &_clients[i];
        }
    }
    return nullptr;
}

int BLEMeshRelay::findFreeClientSlot() {
    for (int i = 0; i < MAX_BLE_RELAY_CLIENTS; i++) {
        if (!_clients[i].active) {
            return i;
        }
    }
    return -1;
}

bool BLEMeshRelay::sendToClient(BLERelayClient* client, const uint8_t* data, uint16_t len) {
    if (!client || !client->active) return false;

    // Check connection is still valid
    BLEConnection* conn = Bluefruit.Connection(client->connHandle);
    if (!conn || !conn->connected()) {
        client->active = false;
        return false;
    }

    // Send via relay characteristic
    return _relayChar.notify(client->connHandle, data, len);
}

bool BLEMeshRelay::sendToAllClients(const uint8_t* data, uint16_t len, uint16_t excludeHandle) {
    bool sent = false;

    for (int i = 0; i < MAX_BLE_RELAY_CLIENTS; i++) {
        if (_clients[i].active && _clients[i].relayEnabled) {
            if (_clients[i].connHandle != excludeHandle) {
                if (sendToClient(&_clients[i], data, len)) {
                    sent = true;
                }
            }
        }
    }

    return sent;
}

// ============================================================================
// Static Callbacks
// ============================================================================

void BLEMeshRelay::relayWriteCallback(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    (void)chr;

    if (_instance && len > 0) {
        uint8_t msgType = data[0];

        // Handle registration specially
        if (msgType == RELAY_MSG_REGISTER) {
            // Format: [type:1][relay_enabled:1][name_len:1][name:N]
            bool relayEnabled = (len > 1) ? data[1] : true;
            const char* name = nullptr;
            if (len > 3 && data[2] > 0) {
                static char nameBuf[17];
                uint8_t nameLen = min(data[2], (uint8_t)16);
                memcpy(nameBuf, &data[3], nameLen);
                nameBuf[nameLen] = '\0';
                name = nameBuf;
            }

            _instance->registerClient(conn_handle, name, relayEnabled);
            return;
        }

        // For other messages, must be registered
        if (_instance->isRelayClient(conn_handle)) {
            _instance->relayFromBLE(conn_handle, data, len);
        } else {
            Serial.println("[BLE-RELAY] Unregistered client attempted relay");
        }
    }
}
