/**
 * LNK-22 Mesh Networking Implementation
 * AODV routing with 8-channel support
 */

#include "mesh.h"
#include "../naming/naming.h"
#include "../ble/ble_service.h"
#include "../ble/ble_relay.h"
#include "../config.h"

#if FEATURE_HYBRID_MAC
#include "../mac/mac_hybrid.h"
#endif

Mesh* Mesh::instance = nullptr;

Mesh::Mesh() :
    nodeAddress(0),
    radio(nullptr),
    crypto(nullptr),
    nextPacketId(1),
    nextSeqNumber(0),
    nextRouteRequestId(1),
    currentChannel(DEFAULT_CHANNEL),
    encryptionEnabled(true),       // Enable encryption by default
    networkIdFiltering(true),      // Enable network ID filtering by default
    networkId16(0),
    packetsSent(0),
    packetsReceived(0),
    deliveryCallback(nullptr)
{
    // Initialize tables
    for (int i = 0; i < MAX_ROUTES; i++) {
        routeTable[i].valid = false;
        routeTable[i].is_primary = false;  // Phase 3.3
    }
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        neighbors[i].valid = false;
    }
    for (int i = 0; i < MAX_RETRIES * 4; i++) {
        pendingAcks[i].valid = false;
    }
    for (int i = 0; i < 16; i++) {
        seenRequests[i].valid = false;
    }
    for (int i = 0; i < SEEN_PACKETS_SIZE; i++) {
        seenPackets[i].valid = false;
    }
    // Phase 2.2: Initialize RTT table
    for (int i = 0; i < MAX_RTT_ENTRIES; i++) {
        rttTable[i].valid = false;
    }

    // Phase 3.4: Initialize partition detection state
    topologyHash = 0;
    lastTopologyHash = 0;
    topologyChangeCount = 0;
    lastTopologyBroadcast = 0;
    partitionEvents = 0;

    instance = this;
}

Mesh::~Mesh() {
    instance = nullptr;
}

void Mesh::begin(uint32_t nodeAddr, Radio* radioPtr, Crypto* cryptoPtr) {
    nodeAddress = nodeAddr;
    radio = radioPtr;
    crypto = cryptoPtr;

    // Get network ID from crypto layer
    if (crypto != nullptr) {
        networkId16 = crypto->getNetworkId16();
    }

    // Set up radio RX callback
    radio->setRxCallback(radioRxCallback);

    Serial.print("[MESH] LNK-22 initialized with address 0x");
    Serial.println(nodeAddress, HEX);
    Serial.print("[MESH] Network ID: 0x");
    Serial.println(networkId16, HEX);
    Serial.print("[MESH] Default channel: ");
    Serial.println(currentChannel);
    Serial.print("[MESH] Encryption: ");
    Serial.println(encryptionEnabled ? "ENABLED" : "DISABLED");
    Serial.print("[MESH] Network ID filtering: ");
    Serial.println(networkIdFiltering ? "ENABLED" : "DISABLED");
}

void Mesh::setChannel(uint8_t channel) {
    if (channel >= NUM_CHANNELS) {
        Serial.print("[MESH] Invalid channel ");
        Serial.print(channel);
        Serial.print(", must be 0-");
        Serial.println(NUM_CHANNELS - 1);
        return;
    }

    currentChannel = channel;
    Serial.print("[MESH] Switched to channel ");
    Serial.println(currentChannel);
}

void Mesh::setDeliveryCallback(DeliveryCallback callback) {
    deliveryCallback = callback;
}

void Mesh::update() {
    // Clean up expired routes and neighbors
    static unsigned long lastCleanup = 0;
    static unsigned long lastRouteRefresh = 0;
    unsigned long now = millis();

    if (now - lastCleanup > 10000) {  // Every 10 seconds
        cleanupRoutes();
        cleanupNeighbors();
        cleanupRequests();
        cleanupSeenPackets();
        lastCleanup = now;
    }

    // PHASE 3.1: Proactive route refresh every 30 seconds
    // Check for routes approaching expiration and send HELLO to refresh them
    if (now - lastRouteRefresh > 30000) {
        refreshStaleRoutes();
        lastRouteRefresh = now;
    }

    // PHASE 3.4: Partition detection and topology broadcasting
    // Check for topology changes whenever neighbors change
    checkTopologyChange();

    // Periodically broadcast topology summary to neighbors
    if (now - lastTopologyBroadcast > TOPOLOGY_BROADCAST_INTERVAL) {
        broadcastTopologySummary();
    }

    // Handle ACK timeouts and retransmissions
    handleAckTimeouts();
}

bool Mesh::sendMessage(uint32_t dest, const uint8_t* data, uint16_t len, bool needsAck) {
    // CRYPTO_OVERHEAD = nonce (24) + MAC tag (16) = 40 bytes
    const uint16_t CRYPTO_OVERHEAD = 40;

    // PHASE 2.3: Flow control - check TX window before sending ACK-required packets
    if (needsAck && !canSendMore()) {
        Serial.print("[MESH] TX window full (");
        Serial.print(getPendingCount());
        Serial.print("/");
        Serial.print(TX_WINDOW_SIZE);
        Serial.println(") - try again later");
        return false;
    }

    // Check payload size considering encryption overhead
    if (encryptionEnabled && len + CRYPTO_OVERHEAD > MAX_PAYLOAD_SIZE) {
        Serial.println("[MESH] Payload too large for encryption!");
        return false;
    }
    if (!encryptionEnabled && len > MAX_PAYLOAD_SIZE) {
        Serial.println("[MESH] Payload too large!");
        return false;
    }

    Packet packet;
    memset(&packet, 0, sizeof(Packet));

    // Fill header
    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = PKT_DATA;
    packet.header.flags = needsAck ? FLAG_ACK_REQ : 0;
    packet.header.channel_id = currentChannel;  // Set current channel
    packet.header.network_id = networkId16;     // PHASE 1.4: Set network ID
    packet.header.packet_id = generatePacketId();
    packet.header.source = nodeAddress;
    packet.header.destination = dest;
    packet.header.hop_count = 0;
    packet.header.seq_number = nextSeqNumber++;

    // PHASE 1.2+1.3: Encrypt payload if encryption is enabled
    if (encryptionEnabled && crypto != nullptr) {
        uint16_t encryptedLen = 0;
        if (crypto->encrypt(data, len, packet.payload, &encryptedLen)) {
            packet.header.payload_length = encryptedLen;
            packet.header.flags |= FLAG_ENCRYPTED;  // Set encrypted flag
            Serial.print("[MESH] TX [ENCRYPTED] ");
            Serial.print(len);
            Serial.print(" -> ");
            Serial.print(encryptedLen);
            Serial.println(" bytes");
        } else {
            Serial.println("[MESH] Encryption failed! Sending unencrypted.");
            memcpy(packet.payload, data, len);
            packet.header.payload_length = len;
        }
    } else {
        // Copy payload unencrypted
        memcpy(packet.payload, data, len);
        packet.header.payload_length = len;
    }

    // Find next hop and set smart TTL
    uint32_t next_hop = dest;
    if (!isBroadcast(&packet)) {
        uint8_t routeHops = 0;
        if (!findRoute(dest, &next_hop, &routeHops)) {
            Serial.println("[MESH] No route to destination, initiating discovery...");
            initiateRouteDiscovery(dest);
            return false;
        }
        // Smart TTL: known hops + safety margin, capped at MAX_TTL
        packet.header.ttl = min((uint8_t)(routeHops + TTL_SAFETY_MARGIN), (uint8_t)MAX_TTL);
    } else {
        // Broadcasts use limited TTL to prevent network flooding
        packet.header.ttl = BROADCAST_TTL;
        next_hop = 0xFFFFFFFF;
    }

    packet.header.next_hop = next_hop;

    // Send packet
    if (radio->send(&packet)) {
        packetsSent++;

        // If ACK required, add to pending list
        if (needsAck && !isBroadcast(&packet)) {
            addPendingAck(packet.header.packet_id, dest, &packet);
        }

        #if DEBUG_MESH
        Serial.print("[MESH] Sent packet ID ");
        Serial.print(packet.header.packet_id);
        Serial.print(" to 0x");
        Serial.println(dest, HEX);
        #endif

        return true;
    }

    return false;
}

void Mesh::sendBeacon() {
    Packet packet;
    memset(&packet, 0, sizeof(Packet));

    // Fill header
    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = PKT_BEACON;
    packet.header.ttl = 1;  // Don't forward beacons
    packet.header.flags = FLAG_BROADCAST;
    packet.header.channel_id = currentChannel;  // Set current channel
    packet.header.network_id = networkId16;     // PHASE 1.4: Set network ID
    packet.header.packet_id = generatePacketId();
    packet.header.source = nodeAddress;
    packet.header.destination = 0xFFFFFFFF;
    packet.header.next_hop = 0xFFFFFFFF;
    packet.header.hop_count = 0;
    packet.header.seq_number = nextSeqNumber++;
    packet.header.payload_length = sizeof(BeaconPacket);

    // Fill beacon payload
    BeaconPacket* beacon = (BeaconPacket*)packet.payload;
    strncpy(beacon->name, "LNK-22 Node", sizeof(beacon->name));
    beacon->capabilities = 0;
    beacon->timestamp = millis() / 1000;

    radio->send(&packet);
    packetsSent++;

    #if DEBUG_MESH
    Serial.println("[MESH] Sent beacon");
    #endif
}

uint8_t Mesh::getNeighborCount() const {
    uint8_t count = 0;
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (neighbors[i].valid) {
            count++;
        }
    }
    return count;
}

uint8_t Mesh::getRouteCount() const {
    uint8_t count = 0;
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid) {
            count++;
        }
    }
    return count;
}

// Phase 2.3: Flow control - count pending ACKs
uint8_t Mesh::getPendingCount() const {
    uint8_t count = 0;
    for (int i = 0; i < MAX_RETRIES * 4; i++) {
        if (pendingAcks[i].valid) {
            count++;
        }
    }
    return count;
}

// Phase 2.3: Flow control - check if TX window has space
bool Mesh::canSendMore() const {
    return getPendingCount() < TX_WINDOW_SIZE;
}

bool Mesh::getNeighbor(uint8_t index, uint32_t* address, int16_t* rssi, int8_t* snr) {
    uint8_t found = 0;
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (neighbors[i].valid) {
            if (found == index) {
                if (address) *address = neighbors[i].address;
                // Get signal from preferred interface
                uint8_t pref = neighbors[i].preferred_iface;
                if (rssi) {
                    if (pref == IFACE_LORA) *rssi = neighbors[i].lora.rssi;
                    else if (pref == IFACE_BLE) *rssi = neighbors[i].ble.rssi;
                    else *rssi = neighbors[i].lora.rssi;  // Default to LoRa
                }
                if (snr) {
                    if (pref == IFACE_LORA) *snr = neighbors[i].lora.snr;
                    else if (pref == IFACE_BLE) *snr = neighbors[i].ble.snr;
                    else *snr = neighbors[i].lora.snr;
                }
                return true;
            }
            found++;
        }
    }
    return false;
}

void Mesh::printRoutes() {
    Serial.println("\n=== Routing Table ===");

    unsigned long now = millis();
    int count = 0;

    // Group routes by destination for multipath display
    uint32_t printedDests[MAX_ROUTES];
    int printedCount = 0;

    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid) {
            // Check if we've already printed routes for this destination
            bool alreadyPrinted = false;
            for (int j = 0; j < printedCount; j++) {
                if (printedDests[j] == routeTable[i].destination) {
                    alreadyPrinted = true;
                    break;
                }
            }
            if (alreadyPrinted) continue;

            // Mark this destination as printed
            printedDests[printedCount++] = routeTable[i].destination;

            // Print destination header
            Serial.print("  ");
            Serial.print(nodeNaming.getNodeName(routeTable[i].destination));

            // Count routes to this destination
            int routesToDest = countRoutesToDest(routeTable[i].destination);
            if (routesToDest > 1) {
                Serial.print(" [");
                Serial.print(routesToDest);
                Serial.print(" paths]");
            }
            Serial.println(":");

            // Print all routes to this destination
            for (int k = 0; k < MAX_ROUTES; k++) {
                if (routeTable[k].valid && routeTable[k].destination == routeTable[i].destination) {
                    unsigned long ageMs = now - routeTable[k].timestamp;
                    unsigned long ageSec = ageMs / 1000;
                    uint8_t score = calculateRouteScore(&routeTable[k]);

                    // PHASE 3.3: Show primary/backup indicator
                    Serial.print("    ");
                    if (routeTable[k].is_primary) {
                        Serial.print("* ");  // Primary route
                    } else {
                        Serial.print("  ");  // Backup route
                    }

                    Serial.print("via ");
                    Serial.print(nodeNaming.getNodeName(routeTable[k].next_hop));
                    Serial.print(" (");
                    Serial.print(routeTable[k].hop_count);
                    Serial.print("h, Q:");
                    Serial.print(routeTable[k].quality);
                    Serial.print(", S:");
                    Serial.print(score);  // Route score
                    Serial.print(", ");
                    Serial.print(ageSec);
                    Serial.print("s");

                    // PHASE 3.1: Show route freshness status
                    if (ageMs > ROUTE_REFRESH_TIME) {
                        Serial.print(" STALE");  // >4min - needs refresh
                    } else if (ageMs > ROUTE_REFRESH_TIME / 2) {
                        Serial.print(" AGING");  // >2min - getting old
                    }

                    Serial.println(")");
                    count++;
                }
            }
        }
    }
    if (count == 0) {
        Serial.println("  (no routes)");
    }
    Serial.print("Routes: ");
    Serial.print(count);
    Serial.print("/");
    Serial.print(MAX_ROUTES);
    Serial.print(" | Timeout: ");
    Serial.print(ROUTE_TIMEOUT / 60000);
    Serial.print("min | Refresh: ");
    Serial.print(ROUTE_REFRESH_TIME / 60000);
    Serial.println("min");
    Serial.println("Legend: * = primary route, h = hops, Q = quality, S = score");
    Serial.println("===================\n");
}

// Get interface name string (member function)
const char* Mesh::getInterfaceName(uint8_t iface) {
    switch (iface) {
        case IFACE_LORA: return "LoRa";
        case IFACE_BLE:  return "BLE";
        case IFACE_LAN:  return "LAN";
        case IFACE_WAN:  return "WAN";
        default:         return "UNK";
    }
}

// Get comma-separated list of all interfaces from bitmask
void Mesh::getInterfaceList(uint8_t interfaces, char* buf, size_t bufSize) {
    buf[0] = '\0';
    bool first = true;

    if (interfaces & IFACE_BLE) {
        strncat(buf, "BLE", bufSize - strlen(buf) - 1);
        first = false;
    }
    if (interfaces & IFACE_LAN) {
        if (!first) strncat(buf, ",", bufSize - strlen(buf) - 1);
        strncat(buf, "LAN", bufSize - strlen(buf) - 1);
        first = false;
    }
    if (interfaces & IFACE_LORA) {
        if (!first) strncat(buf, ",", bufSize - strlen(buf) - 1);
        strncat(buf, "LoRa", bufSize - strlen(buf) - 1);
        first = false;
    }
    if (interfaces & IFACE_WAN) {
        if (!first) strncat(buf, ",", bufSize - strlen(buf) - 1);
        strncat(buf, "WAN", bufSize - strlen(buf) - 1);
        first = false;
    }
    if (interfaces == IFACE_NONE || strlen(buf) == 0) {
        strncpy(buf, "UNK", bufSize);
    }
}

// Select best interface based on priority (BLE > LAN > LoRa > WAN)
uint8_t Mesh::selectBestInterface(const Neighbor* neighbor) {
    if (!neighbor) return IFACE_NONE;

    // Check interfaces in priority order
    if (neighbor->interfaces & IFACE_BLE) return IFACE_BLE;
    if (neighbor->interfaces & IFACE_LAN) return IFACE_LAN;
    if (neighbor->interfaces & IFACE_LORA) return IFACE_LORA;
    if (neighbor->interfaces & IFACE_WAN) return IFACE_WAN;

    return IFACE_NONE;
}

void Mesh::printNeighbors() {
    Serial.println("\n=== Neighbor Table ===");

    unsigned long now = millis();
    int count = 0;
    char ifaceList[32];

    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (neighbors[i].valid) {
            getInterfaceList(neighbors[i].interfaces, ifaceList, sizeof(ifaceList));

            Serial.print("  ");
            Serial.print(nodeNaming.getNodeName(neighbors[i].address));
            Serial.print(" [");
            Serial.print(ifaceList);
            Serial.print("] *");
            Serial.print(getInterfaceName(neighbors[i].preferred_iface));
            Serial.print("*");

            // Show signal info for each available interface
            if (neighbors[i].interfaces & IFACE_LORA) {
                Serial.print(" LoRa:");
                Serial.print(neighbors[i].lora.rssi);
                Serial.print("/");
                Serial.print(neighbors[i].lora.snr);
            }
            if (neighbors[i].interfaces & IFACE_BLE) {
                Serial.print(" BLE:");
                Serial.print(neighbors[i].ble.rssi);
            }
            if (neighbors[i].interfaces & IFACE_LAN) {
                Serial.print(" LAN:ok");
            }

            // Show total packets and age since any interface updated
            unsigned long mostRecent = 0;
            uint16_t totalPkts = 0;
            if (neighbors[i].interfaces & IFACE_LORA) {
                if (neighbors[i].lora.last_seen > mostRecent) mostRecent = neighbors[i].lora.last_seen;
                totalPkts += neighbors[i].lora.packets_received;
            }
            if (neighbors[i].interfaces & IFACE_BLE) {
                if (neighbors[i].ble.last_seen > mostRecent) mostRecent = neighbors[i].ble.last_seen;
                totalPkts += neighbors[i].ble.packets_received;
            }

            unsigned long age = (now - mostRecent) / 1000;
            Serial.print(" (");
            Serial.print(totalPkts);
            Serial.print(" pkts, ");
            Serial.print(age);
            Serial.print("s ago");

            // PHASE 3.2: Show liveness status based on NEIGHBOR_TIMEOUT
            unsigned long ageMs = now - mostRecent;
            if (ageMs > NEIGHBOR_TIMEOUT * 3 / 4) {
                Serial.print(" STALE");  // >45s - about to expire
            } else if (ageMs > NEIGHBOR_TIMEOUT / 2) {
                Serial.print(" AGING");  // >30s - getting old
            }
            // < 30s = fresh, no indicator needed

            Serial.println(")");
            count++;
        }
    }
    if (count == 0) {
        Serial.println("  (no neighbors)");
    }
    Serial.print("Timeout: ");
    Serial.print(NEIGHBOR_TIMEOUT / 1000);
    Serial.print("s | Topology: 0x");
    Serial.print(topologyHash, HEX);
    Serial.print(" | Partitions: ");
    Serial.println(partitionEvents);
    Serial.println("====================\n");
}

// Private methods

void Mesh::radioRxCallback(Packet* packet, int16_t rssi, int8_t snr) {
    if (instance) {
        instance->handleReceivedPacket(packet, rssi, snr);
    }
}

void Mesh::handleReceivedPacket(Packet* packet, int16_t rssi, int8_t snr) {
    // VALIDATION: Check packet header for sanity
    // This filters out ghost packets from RF noise that pass CRC

    // 1. Validate protocol version (must be 2 for LNK-22)
    if (packet->header.version != PROTOCOL_VERSION) {
        Serial.print("[MESH] REJECTED: Invalid protocol version ");
        Serial.println(packet->header.version);
        return;
    }

    // 2. Validate packet type (must be 0x01-0x08)
    if (!isValidPacketType(packet->header.type)) {
        Serial.print("[MESH] REJECTED: Invalid packet type 0x");
        Serial.println(packet->header.type, HEX);
        return;
    }

    // 3. Validate source address (must be non-zero and not broadcast)
    if (packet->header.source == 0 || packet->header.source == 0xFFFFFFFF) {
        Serial.print("[MESH] REJECTED: Invalid source address 0x");
        Serial.println(packet->header.source, HEX);
        return;
    }

    // 4. Validate TTL (must be 1-15)
    if (packet->header.ttl == 0 || packet->header.ttl > MAX_TTL) {
        Serial.print("[MESH] REJECTED: Invalid TTL ");
        Serial.println(packet->header.ttl);
        return;
    }

    // 5. Validate channel (must be 0-7)
    if (packet->header.channel_id >= NUM_CHANNELS) {
        Serial.print("[MESH] REJECTED: Invalid channel ");
        Serial.println(packet->header.channel_id);
        return;
    }

    // 6. Validate payload length (must be reasonable)
    if (packet->header.payload_length > MAX_PAYLOAD_SIZE) {
        Serial.print("[MESH] REJECTED: Payload too large ");
        Serial.println(packet->header.payload_length);
        return;
    }

    // DEBUG: Show ALL received packets that pass validation
    Serial.print("[DEBUG] RAW RX: type=");
    Serial.print(packet->header.type);
    Serial.print(" from=0x");
    Serial.print(packet->header.source, HEX);
    Serial.print(" chan=");
    Serial.print(packet->header.channel_id);
    Serial.print(" rssi=");
    Serial.print(rssi);
    Serial.print(" snr=");
    Serial.println(snr);

    // Ignore our own packets
    if (packet->header.source == nodeAddress) {
        Serial.print("[DEBUG] Ignoring own packet (source=0x");
        Serial.print(packet->header.source, HEX);
        Serial.print(" nodeAddress=0x");
        Serial.print(nodeAddress, HEX);
        Serial.println(")");
        return;
    }

    // Channel filtering - ignore packets on different channels
    if (packet->header.channel_id != currentChannel) {
        Serial.print("[DEBUG] FILTERED: packet on channel ");
        Serial.print(packet->header.channel_id);
        Serial.print(" (we're on channel ");
        Serial.print(currentChannel);
        Serial.println(")");
        return;
    }

    // PHASE 1.4: Network ID filtering - ignore packets from different networks
    if (networkIdFiltering && packet->header.network_id != networkId16) {
        Serial.print("[MESH] FILTERED: wrong network ID 0x");
        Serial.print(packet->header.network_id, HEX);
        Serial.print(" (we're on 0x");
        Serial.print(networkId16, HEX);
        Serial.println(")");
        return;
    }

    // PHASE 2.4: Receiver-side duplicate detection with re-ACK
    // When we receive a duplicate DATA packet, we re-send the ACK (if requested)
    // but don't process the payload again. This handles lost ACKs.
    if (packet->header.type == PKT_DATA) {
        if (hasSeenPacket(packet->header.source, packet->header.packet_id)) {
            Serial.print("[MESH] DEDUP: Duplicate packet ");
            Serial.print(packet->header.packet_id);
            Serial.print(" from 0x");
            Serial.print(packet->header.source, HEX);

            // Re-ACK the duplicate if ACK was requested (original ACK may have been lost)
            if (needsAck(packet)) {
                Serial.println(" - Re-ACKing");
                sendAck(packet->header.source, packet->header.packet_id);
            } else {
                Serial.println(" - Dropping (no ACK needed)");
            }
            return;
        }
        // Record this packet as seen
        recordPacket(packet->header.source, packet->header.packet_id);
    }

    packetsReceived++;

    #if DEBUG_MESH
    Serial.print("[MESH] RX packet type ");
    Serial.print(packet->header.type);
    Serial.print(" from 0x");
    Serial.print(packet->header.source, HEX);
    Serial.print(" RSSI: ");
    Serial.print(rssi);
    Serial.print(" SNR: ");
    Serial.println(snr);
    #endif

    // Update neighbor info
    updateNeighbor(packet->header.source, rssi, snr);

    // Forward to BLE relay clients (if any connected)
    if (bleRelay.isActive() && bleRelay.getClientCount() > 0) {
        bleRelay.relayFromLoRa(packet, rssi, snr);
    }

    // Route packet based on type
    switch (packet->header.type) {
        case PKT_DATA:
            handleDataPacket(packet);
            break;
        case PKT_ACK:
            handleAckPacket(packet);
            break;
        case PKT_ROUTE_REQ:
            handleRouteReqPacket(packet);
            break;
        case PKT_ROUTE_REP:
            handleRouteRepPacket(packet);
            break;
        case PKT_ROUTE_ERR:
            handleRouteErrPacket(packet);
            break;
        case PKT_HELLO:
            handleHelloPacket(packet, rssi, snr);
            break;
        case PKT_BEACON:
            handleBeaconPacket(packet);
            break;
#if FEATURE_HYBRID_MAC
        case PKT_TIME_SYNC:
            handleTimeSyncPacket(packet, rssi);
            break;
#endif
        default:
            Serial.println("[MESH] Unknown packet type!");
            break;
    }
}

void Mesh::handleDataPacket(Packet* packet) {
    // Check if packet is for us
    if (packet->header.destination == nodeAddress || isBroadcast(packet)) {
        // Validate payload content - must have at least 1 byte
        if (packet->header.payload_length == 0) {
            Serial.println("[MESH] REJECTED: Empty payload");
            return;
        }

        // PHASE 1.2+1.3: Decrypt payload if encrypted
        uint8_t decryptedPayload[MAX_PAYLOAD_SIZE];
        uint8_t* payloadPtr = packet->payload;
        uint16_t payloadLen = packet->header.payload_length;
        bool wasEncrypted = false;

        if (isEncrypted(packet) && crypto != nullptr) {
            uint16_t decryptedLen = 0;
            if (crypto->decrypt(packet->payload, packet->header.payload_length,
                               decryptedPayload, &decryptedLen)) {
                payloadPtr = decryptedPayload;
                payloadLen = decryptedLen;
                wasEncrypted = true;
                Serial.print("[MESH] RX [ENCRYPTED] ");
                Serial.print(packet->header.payload_length);
                Serial.print(" -> ");
                Serial.print(decryptedLen);
                Serial.println(" bytes decrypted");
            } else {
                Serial.println("[MESH] REJECTED: Decryption failed (wrong PSK?)");
                return;
            }
        } else if (isEncrypted(packet) && crypto == nullptr) {
            Serial.println("[MESH] REJECTED: Encrypted packet but crypto not available");
            return;
        }

        // Check if payload contains mostly printable characters (text message validation)
        int printableCount = 0;
        for (uint16_t i = 0; i < payloadLen; i++) {
            uint8_t c = payloadPtr[i];
            // Count printable ASCII chars, newlines, tabs, and UTF-8 continuation bytes
            if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t' || (c >= 128)) {
                printableCount++;
            }
        }

        // At least 30% of content should be printable for text messages
        // Lowered from 50% because some protocols have metadata bytes
        if (printableCount < (int)(payloadLen / 3)) {
            Serial.print("[MESH] REJECTED: Non-printable payload (");
            Serial.print(printableCount);
            Serial.print("/");
            Serial.print(payloadLen);
            Serial.print(" printable) HEX: ");
            // Print first 16 bytes as hex for debugging
            for (uint16_t i = 0; i < payloadLen && i < 16; i++) {
                if (payloadPtr[i] < 16) Serial.print("0");
                Serial.print(payloadPtr[i], HEX);
                Serial.print(" ");
            }
            Serial.println();
            return;
        }

        // Print message prominently for serial/web client
        Serial.println("\n========================================");
        Serial.print("MESSAGE from 0x");
        Serial.print(packet->header.source, HEX);
        if (isBroadcast(packet)) {
            Serial.print(" (BROADCAST)");
        } else {
            Serial.print(" (DIRECT)");
        }
        if (wasEncrypted) {
            Serial.print(" [ENCRYPTED]");
        }
        Serial.println();
        Serial.println("----------------------------------------");
        Serial.write(payloadPtr, payloadLen);
        Serial.println();
        Serial.println("========================================\n");

        // Notify BLE app of received message (use decrypted payload)
        extern LNK22BLEService bleService;
        if (bleService.isConnected()) {
            uint8_t msgType = isBroadcast(packet) ? 0x02 : 0x01;  // 0x02=broadcast, 0x01=direct
            bleService.notifyMessage(
                msgType,
                packet->header.source,
                packet->header.channel_id,
                millis() / 1000,
                payloadPtr,   // Use decrypted payload
                payloadLen    // Use decrypted length
            );
        }

        // Send ACK if requested
        if (needsAck(packet)) {
            sendAck(packet->header.source, packet->header.packet_id);
        }
    } else {
        // Forward packet
        forwardPacket(packet);
    }
}

void Mesh::handleAckPacket(Packet* packet) {
    // Find the pending ACK entry to get the destination and sent_time before removing
    uint32_t destination = 0;
    unsigned long sentTime = 0;
    uint8_t retries = 0;
    for (int i = 0; i < MAX_RETRIES * 4; i++) {
        if (pendingAcks[i].valid && pendingAcks[i].packet_id == packet->header.packet_id) {
            destination = pendingAcks[i].destination;
            sentTime = pendingAcks[i].sent_time;
            retries = pendingAcks[i].retries;
            break;
        }
    }

    removePendingAck(packet->header.packet_id);

    // Phase 2.2: Only measure RTT if this was the first transmission (no retries)
    // Retransmitted packets have ambiguous RTT (Karn's algorithm)
    if (sentTime > 0 && retries == 0) {
        unsigned long rtt = millis() - sentTime;
        updateRTT(destination, rtt);
        Serial.print("[MESH] ACK received for packet ");
        Serial.print(packet->header.packet_id);
        Serial.print(" (RTT: ");
        Serial.print(rtt);
        Serial.println("ms)");
    } else {
        Serial.print("[MESH] ACK received for packet ");
        Serial.print(packet->header.packet_id);
        if (retries > 0) {
            Serial.print(" (retransmit, RTT not measured)");
        }
        Serial.println();
    }

    // Notify delivery callback of successful delivery
    if (deliveryCallback && destination != 0) {
        deliveryCallback(packet->header.packet_id, destination, 2);  // 2 = DELIVERY_ACKED
    }
}

void Mesh::handleRouteReqPacket(Packet* packet) {
    // AODV Route Request handling
    ::RouteRequest* req = (::RouteRequest*)packet->payload;

    #if DEBUG_MESH
    Serial.print("[MESH] ROUTE_REQ from 0x");
    Serial.print(packet->header.source, HEX);
    Serial.print(" for dest 0x");
    Serial.println(packet->header.destination, HEX);
    #endif

    // Check if we've seen this request before (avoid loops)
    if (hasSeenRequest(packet->header.source, req->request_id)) {
        #if DEBUG_MESH
        Serial.println("[MESH] Duplicate ROUTE_REQ, dropping");
        #endif
        return;
    }

    // Record that we've seen this request
    recordRequest(packet->header.source, req->request_id);

    // Create reverse route to originator
    uint8_t quality = calculateLinkQuality(radio->getLastRSSI(), radio->getLastSNR());
    addRoute(packet->header.source, packet->header.source,
             packet->header.hop_count + 1, quality);

    // Check if we are the destination
    if (packet->header.destination == nodeAddress) {
        // We are the destination - send ROUTE_REP
        #if DEBUG_MESH
        Serial.println("[MESH] We are destination, sending ROUTE_REP");
        #endif

        Packet reply;
        memset(&reply, 0, sizeof(Packet));

        reply.header.version = PROTOCOL_VERSION;
        reply.header.type = PKT_ROUTE_REP;
        reply.header.ttl = MAX_TTL;
        reply.header.network_id = networkId16;  // PHASE 1.4: Set network ID
        reply.header.packet_id = generatePacketId();
        reply.header.source = nodeAddress;
        reply.header.destination = packet->header.source;
        reply.header.next_hop = packet->header.source;
        reply.header.hop_count = 0;
        reply.header.seq_number = nextSeqNumber++;
        reply.header.payload_length = sizeof(::RouteReply);

        ::RouteReply* rep = (::RouteReply*)reply.payload;
        rep->request_id = req->request_id;
        rep->hop_count = 0;
        rep->quality = quality;

        radio->send(&reply);
        packetsSent++;

    } else {
        // We are not the destination - rebroadcast if TTL allows
        if (packet->header.ttl > 1) {
            packet->header.ttl--;
            packet->header.hop_count++;
            req->hop_count++;

            #if DEBUG_MESH
            Serial.println("[MESH] Rebroadcasting ROUTE_REQ");
            #endif

            // Small random delay to avoid collisions
            delay(random(10, 50));

            radio->send(packet);
            packetsSent++;
        }
    }
}

void Mesh::handleRouteRepPacket(Packet* packet) {
    // AODV Route Reply handling
    ::RouteReply* rep = (::RouteReply*)packet->payload;

    #if DEBUG_MESH
    Serial.print("[MESH] ROUTE_REP from 0x");
    Serial.print(packet->header.source, HEX);
    Serial.print(" hops=");
    Serial.println(rep->hop_count);
    #endif

    // Check if this reply is for us
    if (packet->header.destination == nodeAddress) {
        // This is the final destination of the reply
        // Add route to the original destination
        addRoute(packet->header.source,
                 packet->header.source,
                 rep->hop_count + 1,
                 rep->quality);

        #if DEBUG_MESH
        Serial.print("[MESH] Route established to 0x");
        Serial.println(packet->header.source, HEX);
        #endif

    } else {
        // Forward the reply towards the originator
        uint32_t next_hop;
        if (findRoute(packet->header.destination, &next_hop)) {
            packet->header.next_hop = next_hop;
            packet->header.hop_count++;
            rep->hop_count++;

            #if DEBUG_MESH
            Serial.println("[MESH] Forwarding ROUTE_REP");
            #endif

            radio->send(packet);
            packetsSent++;

            // Also add/update route to the source of the reply
            uint8_t quality = calculateLinkQuality(radio->getLastRSSI(), radio->getLastSNR());
            addRoute(packet->header.source, packet->header.source,
                     rep->hop_count, quality);
        }
    }
}

void Mesh::handleRouteErrPacket(Packet* packet) {
    // Route Error handling
    ::RouteError* err = (::RouteError*)packet->payload;

    #if DEBUG_MESH
    Serial.print("[MESH] ROUTE_ERR: dest 0x");
    Serial.print(err->unreachable_dest, HEX);
    Serial.print(" via 0x");
    Serial.println(err->failed_next_hop, HEX);
    #endif

    // Remove the failed route
    removeRoute(err->unreachable_dest);
}

void Mesh::handleHelloPacket(Packet* packet, int16_t rssi, int8_t snr) {
    // PHASE 3.1: HELLO packets refresh routes through the sender
    // When we receive a HELLO from a node, refresh all routes that use it as next-hop
    uint32_t sender = packet->header.source;
    unsigned long now = millis();
    int refreshedCount = 0;

    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid && routeTable[i].next_hop == sender) {
            routeTable[i].timestamp = now;  // Refresh route timestamp
            refreshedCount++;
        }
    }

    if (refreshedCount > 0) {
        #if DEBUG_MESH
        Serial.print("[MESH] HELLO from 0x");
        Serial.print(sender, HEX);
        Serial.print(" refreshed ");
        Serial.print(refreshedCount);
        Serial.println(" route(s)");
        #endif
    }

    // Also update neighbor (already called before this handler, but confirm here)
    updateNeighbor(sender, rssi, snr);

    // PHASE 3.4: Check for topology summary payload (5 bytes: hash + neighbor count)
    if (packet->header.payload_length >= 5) {
        handleTopologySummary(packet);
    }
}

void Mesh::handleBeaconPacket(Packet* packet) {
    BeaconPacket* beacon = (BeaconPacket*)packet->payload;
    Serial.print("[MESH] Beacon from: ");
    Serial.println(beacon->name);
}

bool Mesh::findRoute(uint32_t dest, uint32_t* next_hop, uint8_t* hop_count) {
    // Check if destination is a direct neighbor
    if (isNeighbor(dest)) {
        *next_hop = dest;
        if (hop_count) *hop_count = 1;  // Direct neighbor = 1 hop
        return true;
    }

    // PHASE 3.3: Search for PRIMARY route first, then any valid route
    // First pass: look for primary route
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid && routeTable[i].destination == dest && routeTable[i].is_primary) {
            *next_hop = routeTable[i].next_hop;
            if (hop_count) *hop_count = routeTable[i].hop_count;
            return true;
        }
    }

    // Second pass: any valid route (fallback if no primary marked)
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid && routeTable[i].destination == dest) {
            *next_hop = routeTable[i].next_hop;
            if (hop_count) *hop_count = routeTable[i].hop_count;
            return true;
        }
    }

    return false;
}

void Mesh::addRoute(uint32_t dest, uint32_t next_hop, uint8_t hop_count, uint8_t quality) {
    // PHASE 3.3: Multipath routing - store up to MAX_ROUTES_PER_DEST routes per destination

    // First, check if this exact route (same dest AND same next_hop) already exists
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid &&
            routeTable[i].destination == dest &&
            routeTable[i].next_hop == next_hop) {
            // Update existing route
            routeTable[i].hop_count = hop_count;
            routeTable[i].quality = quality;
            routeTable[i].timestamp = millis();
            updatePrimaryRoute(dest);
            return;
        }
    }

    // Check how many routes we have to this destination
    int routeCount = countRoutesToDest(dest);

    if (routeCount >= MAX_ROUTES_PER_DEST) {
        // Already have max routes to this dest - only add if better quality
        // Find the worst route to this destination
        int worstSlot = -1;
        uint8_t worstQuality = 255;
        for (int i = 0; i < MAX_ROUTES; i++) {
            if (routeTable[i].valid && routeTable[i].destination == dest) {
                if (routeTable[i].quality < worstQuality) {
                    worstQuality = routeTable[i].quality;
                    worstSlot = i;
                }
            }
        }

        if (worstSlot != -1 && quality > worstQuality) {
            // Replace worst route with this better one
            routeTable[worstSlot].next_hop = next_hop;
            routeTable[worstSlot].hop_count = hop_count;
            routeTable[worstSlot].quality = quality;
            routeTable[worstSlot].timestamp = millis();
            updatePrimaryRoute(dest);
            #if DEBUG_MESH
            Serial.print("[MESH] Replaced worse route to 0x");
            Serial.println(dest, HEX);
            #endif
        }
        return;
    }

    // Find empty slot for new route
    int slot = -1;
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (!routeTable[i].valid) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        // Table full, find oldest entry (not to this destination)
        unsigned long oldest = millis();
        for (int i = 0; i < MAX_ROUTES; i++) {
            if (routeTable[i].destination != dest && routeTable[i].timestamp < oldest) {
                oldest = routeTable[i].timestamp;
                slot = i;
            }
        }
    }

    // Add new route
    if (slot != -1) {
        routeTable[slot].destination = dest;
        routeTable[slot].next_hop = next_hop;
        routeTable[slot].hop_count = hop_count;
        routeTable[slot].quality = quality;
        routeTable[slot].timestamp = millis();
        routeTable[slot].valid = true;
        routeTable[slot].is_primary = false;  // Will be set by updatePrimaryRoute
        updatePrimaryRoute(dest);

        #if DEBUG_MESH
        Serial.print("[MESH] Added route ");
        Serial.print(countRoutesToDest(dest));
        Serial.print("/");
        Serial.print(MAX_ROUTES_PER_DEST);
        Serial.print(" to 0x");
        Serial.println(dest, HEX);
        #endif
    }
}

void Mesh::removeRoute(uint32_t dest) {
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid && routeTable[i].destination == dest) {
            routeTable[i].valid = false;
            break;
        }
    }
}

void Mesh::initiateRouteDiscovery(uint32_t dest) {
    // Initiate AODV route discovery by broadcasting ROUTE_REQ

    #if DEBUG_MESH
    Serial.print("[MESH] Initiating route discovery for 0x");
    Serial.println(dest, HEX);
    #endif

    Packet packet;
    memset(&packet, 0, sizeof(Packet));

    // Fill header
    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = PKT_ROUTE_REQ;
    packet.header.ttl = MAX_TTL;
    packet.header.flags = FLAG_BROADCAST;
    packet.header.network_id = networkId16;  // PHASE 1.4: Set network ID
    packet.header.packet_id = generatePacketId();
    packet.header.source = nodeAddress;
    packet.header.destination = dest;
    packet.header.next_hop = 0xFFFFFFFF;
    packet.header.hop_count = 0;
    packet.header.seq_number = nextSeqNumber++;
    packet.header.payload_length = sizeof(::RouteRequest);

    // Fill payload
    ::RouteRequest* req = (::RouteRequest*)packet.payload;
    req->request_id = nextRouteRequestId++;
    req->hop_count = 0;

    // Record our own request
    recordRequest(nodeAddress, req->request_id);

    // Broadcast the request
    radio->send(&packet);
    packetsSent++;
}

void Mesh::cleanupRoutes() {
    unsigned long now = millis();
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid) {
            if (now - routeTable[i].timestamp > ROUTE_TIMEOUT) {
                routeTable[i].valid = false;
                #if DEBUG_MESH
                Serial.print("[MESH] Route expired: 0x");
                Serial.println(routeTable[i].destination, HEX);
                #endif
            }
        }
    }
}

// ============================================
// Phase 3.1: Proactive Route Maintenance
// ============================================

void Mesh::refreshStaleRoutes() {
    // Check routes approaching expiration (at ROUTE_REFRESH_TIME = 4 minutes)
    // and send HELLO to the next-hop to refresh them
    unsigned long now = millis();
    int refreshedCount = 0;

    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid) {
            unsigned long age = now - routeTable[i].timestamp;

            // If route is approaching expiration (between 4-5 minutes old)
            if (age > ROUTE_REFRESH_TIME && age < ROUTE_TIMEOUT) {
                // Send HELLO to next-hop to keep route alive
                sendHelloToNextHop(routeTable[i].next_hop);
                refreshedCount++;

                // Limit to one refresh per cycle to avoid congestion
                break;
            }
        }
    }

    #if DEBUG_MESH
    if (refreshedCount > 0) {
        Serial.print("[MESH] Proactive route refresh: ");
        Serial.print(refreshedCount);
        Serial.println(" route(s)");
    }
    #endif
}

void Mesh::sendHelloToNextHop(uint32_t nextHop) {
    // Send a lightweight HELLO packet to refresh routes through this node
    Packet packet;
    memset(&packet, 0, sizeof(Packet));

    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = PKT_HELLO;
    packet.header.ttl = 1;  // Single hop only
    packet.header.flags = 0;
    packet.header.network_id = networkId16;
    packet.header.packet_id = generatePacketId();
    packet.header.source = nodeAddress;
    packet.header.destination = nextHop;
    packet.header.next_hop = nextHop;
    packet.header.hop_count = 0;
    packet.header.seq_number = nextSeqNumber++;
    packet.header.payload_length = 0;

    radio->send(&packet);
    packetsSent++;

    #if DEBUG_MESH
    Serial.print("[MESH] Sent route-refresh HELLO to 0x");
    Serial.println(nextHop, HEX);
    #endif
}

// ============================================
// Phase 3.3: Multipath Routing Functions
// ============================================

int Mesh::countRoutesToDest(uint32_t dest) {
    int count = 0;
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid && routeTable[i].destination == dest) {
            count++;
        }
    }
    return count;
}

uint8_t Mesh::calculateRouteScore(const RouteEntry* route) {
    // Score combines quality (higher=better) and hop count (lower=better)
    // Score = quality - (hop_count * 20)
    // This means each extra hop costs 20 quality points
    int score = (int)route->quality - ((int)route->hop_count * 20);
    if (score < 0) score = 0;
    if (score > 255) score = 255;
    return (uint8_t)score;
}

void Mesh::updatePrimaryRoute(uint32_t dest) {
    // Find the best route to this destination and mark it as primary
    int bestSlot = -1;
    uint8_t bestScore = 0;

    // First, clear all primary flags for this destination
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid && routeTable[i].destination == dest) {
            routeTable[i].is_primary = false;
        }
    }

    // Find the best route (highest score)
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid && routeTable[i].destination == dest) {
            uint8_t score = calculateRouteScore(&routeTable[i]);
            if (bestSlot == -1 || score > bestScore) {
                bestScore = score;
                bestSlot = i;
            }
        }
    }

    // Mark the best route as primary
    if (bestSlot != -1) {
        routeTable[bestSlot].is_primary = true;
    }
}

bool Mesh::failoverRoute(uint32_t dest) {
    // Called when primary route fails - try to switch to backup
    // Find current primary and mark it invalid
    int primarySlot = -1;
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid && routeTable[i].destination == dest && routeTable[i].is_primary) {
            primarySlot = i;
            break;
        }
    }

    if (primarySlot != -1) {
        // Invalidate the failed primary route
        routeTable[primarySlot].valid = false;
        routeTable[primarySlot].is_primary = false;

        Serial.print("[MESH] Primary route to 0x");
        Serial.print(dest, HEX);
        Serial.println(" failed, attempting failover");
    }

    // Try to promote a backup route
    int routeCount = countRoutesToDest(dest);
    if (routeCount > 0) {
        updatePrimaryRoute(dest);
        Serial.print("[MESH] Failover successful - ");
        Serial.print(routeCount);
        Serial.println(" backup route(s) available");
        return true;
    }

    Serial.print("[MESH] Failover failed - no backup routes to 0x");
    Serial.println(dest, HEX);
    return false;
}

void Mesh::updateNeighbor(uint32_t addr, int16_t rssi, int8_t snr, uint8_t iface) {
    // Find existing entry or empty slot
    int slot = -1;
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (neighbors[i].valid && neighbors[i].address == addr) {
            slot = i;
            break;
        }
        if (slot == -1 && !neighbors[i].valid) {
            slot = i;
        }
    }

    if (slot == -1) {
        // Table full, find oldest entry based on last activity across all interfaces
        unsigned long oldest = millis();
        for (int i = 0; i < MAX_NEIGHBORS; i++) {
            unsigned long lastActivity = 0;
            if (neighbors[i].interfaces & IFACE_LORA && neighbors[i].lora.last_seen > lastActivity)
                lastActivity = neighbors[i].lora.last_seen;
            if (neighbors[i].interfaces & IFACE_BLE && neighbors[i].ble.last_seen > lastActivity)
                lastActivity = neighbors[i].ble.last_seen;

            if (lastActivity < oldest) {
                oldest = lastActivity;
                slot = i;
            }
        }
    }

    // Update neighbor
    if (slot != -1) {
        bool isNew = !neighbors[slot].valid || neighbors[slot].address != addr;

        if (isNew) {
            // Initialize new neighbor entry
            memset(&neighbors[slot], 0, sizeof(Neighbor));
            neighbors[slot].address = addr;
        }

        // Add this interface to the bitmask
        neighbors[slot].interfaces |= iface;

        // Update per-interface signal info
        unsigned long now = millis();
        switch (iface) {
            case IFACE_LORA:
                neighbors[slot].lora.rssi = rssi;
                neighbors[slot].lora.snr = snr;
                neighbors[slot].lora.last_seen = now;
                neighbors[slot].lora.packets_received++;
                break;
            case IFACE_BLE:
                neighbors[slot].ble.rssi = rssi;
                neighbors[slot].ble.snr = snr;
                neighbors[slot].ble.last_seen = now;
                neighbors[slot].ble.packets_received++;
                break;
            case IFACE_LAN:
                neighbors[slot].lan.rssi = 0;  // N/A for LAN
                neighbors[slot].lan.last_seen = now;
                neighbors[slot].lan.packets_received++;
                break;
            case IFACE_WAN:
                neighbors[slot].wan.rssi = 0;  // N/A for WAN
                neighbors[slot].wan.last_seen = now;
                neighbors[slot].wan.packets_received++;
                break;
        }

        // Select best interface for routing
        neighbors[slot].preferred_iface = selectBestInterface(&neighbors[slot]);
        neighbors[slot].valid = true;
    }
}

bool Mesh::isNeighbor(uint32_t addr) {
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (neighbors[i].valid && neighbors[i].address == addr) {
            return true;
        }
    }
    return false;
}

void Mesh::cleanupNeighbors() {
    unsigned long now = millis();
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (neighbors[i].valid) {
            // PHASE 3.2: Use NEIGHBOR_TIMEOUT (60s) instead of ROUTE_TIMEOUT (5min)
            // Check each interface for timeout and remove stale ones
            if ((neighbors[i].interfaces & IFACE_LORA) && (now - neighbors[i].lora.last_seen > NEIGHBOR_TIMEOUT)) {
                neighbors[i].interfaces &= ~IFACE_LORA;
                #if DEBUG_MESH
                Serial.print("[MESH] LoRa path expired for: 0x");
                Serial.println(neighbors[i].address, HEX);
                #endif
            }
            if ((neighbors[i].interfaces & IFACE_BLE) && (now - neighbors[i].ble.last_seen > NEIGHBOR_TIMEOUT)) {
                neighbors[i].interfaces &= ~IFACE_BLE;
                #if DEBUG_MESH
                Serial.print("[MESH] BLE path expired for: 0x");
                Serial.println(neighbors[i].address, HEX);
                #endif
            }
            if ((neighbors[i].interfaces & IFACE_LAN) && (now - neighbors[i].lan.last_seen > NEIGHBOR_TIMEOUT)) {
                neighbors[i].interfaces &= ~IFACE_LAN;
            }
            if ((neighbors[i].interfaces & IFACE_WAN) && (now - neighbors[i].wan.last_seen > NEIGHBOR_TIMEOUT)) {
                neighbors[i].interfaces &= ~IFACE_WAN;
            }

            // If no interfaces remain, neighbor is fully expired
            if (neighbors[i].interfaces == IFACE_NONE) {
                uint32_t deadNeighbor = neighbors[i].address;
                neighbors[i].valid = false;

                Serial.print("[MESH] Neighbor DEAD: 0x");
                Serial.print(deadNeighbor, HEX);
                Serial.println(" (60s timeout)");

                // PHASE 3.2: Proactively invalidate routes through this dead neighbor
                invalidateRoutesVia(deadNeighbor);
            } else {
                // Recalculate best interface
                neighbors[i].preferred_iface = selectBestInterface(&neighbors[i]);
            }
        }
    }
}

// ============================================
// Phase 3.2: Neighbor Liveness - Route Invalidation
// ============================================

void Mesh::invalidateRoutesVia(uint32_t deadNeighbor) {
    // Find all routes that use deadNeighbor as next-hop and invalidate them
    int invalidatedCount = 0;

    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid && routeTable[i].next_hop == deadNeighbor) {
            uint32_t dest = routeTable[i].destination;

            Serial.print("[MESH] Route invalidated: 0x");
            Serial.print(dest, HEX);
            Serial.print(" via dead neighbor 0x");
            Serial.println(deadNeighbor, HEX);

            routeTable[i].valid = false;

            // Send ROUTE_ERROR to notify network
            sendRouteError(dest, deadNeighbor);
            invalidatedCount++;
        }
    }

    if (invalidatedCount > 0) {
        Serial.print("[MESH] Invalidated ");
        Serial.print(invalidatedCount);
        Serial.println(" route(s) through dead neighbor");
    }
}

void Mesh::sendRouteError(uint32_t unreachableDest, uint32_t failedNextHop) {
    Packet packet;
    memset(&packet, 0, sizeof(Packet));

    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = PKT_ROUTE_ERR;
    packet.header.ttl = MAX_TTL;
    packet.header.flags = FLAG_BROADCAST;  // Broadcast ROUTE_ERROR
    packet.header.network_id = networkId16;
    packet.header.packet_id = generatePacketId();
    packet.header.source = nodeAddress;
    packet.header.destination = 0xFFFFFFFF;  // Broadcast
    packet.header.next_hop = 0xFFFFFFFF;
    packet.header.hop_count = 0;
    packet.header.seq_number = nextSeqNumber++;
    packet.header.payload_length = sizeof(::RouteError);

    ::RouteError* err = (::RouteError*)packet.payload;
    err->unreachable_dest = unreachableDest;
    err->failed_next_hop = failedNextHop;

    radio->send(&packet);
    packetsSent++;

    #if DEBUG_MESH
    Serial.print("[MESH] Sent ROUTE_ERROR for 0x");
    Serial.print(unreachableDest, HEX);
    Serial.print(" (failed hop: 0x");
    Serial.print(failedNextHop, HEX);
    Serial.println(")");
    #endif
}

// ============================================================================
// PHASE 3.4: Network Partition Detection
// ============================================================================

// Calculate a hash of the current neighbor set for topology comparison
// Uses FNV-1a hash algorithm for simplicity and good distribution
uint32_t Mesh::calculateTopologyHash() {
    uint32_t hash = 2166136261;  // FNV offset basis
    const uint32_t fnvPrime = 16777619;

    // Sort neighbor addresses for consistent hash regardless of discovery order
    uint32_t sortedAddrs[MAX_NEIGHBORS];
    int count = 0;

    // Collect valid neighbor addresses
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (neighbors[i].valid) {
            sortedAddrs[count++] = neighbors[i].address;
        }
    }

    // Simple insertion sort (small array)
    for (int i = 1; i < count; i++) {
        uint32_t key = sortedAddrs[i];
        int j = i - 1;
        while (j >= 0 && sortedAddrs[j] > key) {
            sortedAddrs[j + 1] = sortedAddrs[j];
            j--;
        }
        sortedAddrs[j + 1] = key;
    }

    // Hash sorted addresses
    for (int i = 0; i < count; i++) {
        // Hash each byte of the address
        for (int b = 0; b < 4; b++) {
            uint8_t byte = (sortedAddrs[i] >> (b * 8)) & 0xFF;
            hash ^= byte;
            hash *= fnvPrime;
        }
    }

    // Include neighbor count in hash for extra differentiation
    hash ^= count;
    hash *= fnvPrime;

    return hash;
}

// Check for topology changes and detect potential network partitions
void Mesh::checkTopologyChange() {
    uint32_t newHash = calculateTopologyHash();

    if (newHash != topologyHash) {
        // Topology changed
        lastTopologyHash = topologyHash;
        topologyHash = newHash;
        topologyChangeCount++;

        #if DEBUG_MESH
        Serial.print("[MESH] TOPOLOGY CHANGE #");
        Serial.print(topologyChangeCount);
        Serial.print(": 0x");
        Serial.print(lastTopologyHash, HEX);
        Serial.print(" -> 0x");
        Serial.println(topologyHash, HEX);
        #endif

        // Check for potential partition (multiple rapid changes)
        if (topologyChangeCount >= PARTITION_DETECT_THRESHOLD) {
            partitionEvents++;
            Serial.print("[MESH] *** PARTITION DETECTED *** (event #");
            Serial.print(partitionEvents);
            Serial.println(")");

            // Trigger aggressive route discovery
            triggerAggressiveDiscovery();

            // Reset change counter after handling
            topologyChangeCount = 0;
        }
    } else {
        // Topology stable - decay the change counter
        if (topologyChangeCount > 0) {
            topologyChangeCount--;
        }
    }
}

// Broadcast topology summary to allow peers to detect inconsistencies
void Mesh::broadcastTopologySummary() {
    Packet packet;
    memset(&packet, 0, sizeof(Packet));

    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = PKT_HELLO;  // Use HELLO with topology payload
    packet.header.ttl = 1;           // Only to direct neighbors
    packet.header.flags = FLAG_BROADCAST;
    packet.header.network_id = networkId16;
    packet.header.packet_id = generatePacketId();
    packet.header.source = nodeAddress;
    packet.header.destination = 0xFFFFFFFF;  // Broadcast
    packet.header.next_hop = 0xFFFFFFFF;
    packet.header.hop_count = 0;
    packet.header.seq_number = nextSeqNumber++;

    // Payload: topology hash (4 bytes) + neighbor count (1 byte)
    uint32_t hash = topologyHash;
    packet.payload[0] = (hash >> 0) & 0xFF;
    packet.payload[1] = (hash >> 8) & 0xFF;
    packet.payload[2] = (hash >> 16) & 0xFF;
    packet.payload[3] = (hash >> 24) & 0xFF;
    packet.payload[4] = getNeighborCount();
    packet.header.payload_length = 5;

    radio->send(&packet);
    packetsSent++;
    lastTopologyBroadcast = millis();

    #if DEBUG_MESH
    Serial.print("[MESH] Broadcast topology: hash=0x");
    Serial.print(topologyHash, HEX);
    Serial.print(", neighbors=");
    Serial.println(getNeighborCount());
    #endif
}

// Handle received topology summary from a neighbor
void Mesh::handleTopologySummary(Packet* packet) {
    if (packet->header.payload_length < 5) return;

    // Extract peer's topology info
    uint32_t peerHash = packet->payload[0] |
                       (packet->payload[1] << 8) |
                       (packet->payload[2] << 16) |
                       (packet->payload[3] << 24);
    uint8_t peerNeighborCount = packet->payload[4];

    // Compare with our view - significant difference may indicate partition
    int ourCount = getNeighborCount();
    int countDiff = abs((int)peerNeighborCount - ourCount);

    // If peer sees very different neighbor count, might be partition forming
    if (countDiff >= 2 && (ourCount > 0 || peerNeighborCount > 0)) {
        #if DEBUG_MESH
        Serial.print("[MESH] Topology mismatch from 0x");
        Serial.print(packet->header.source, HEX);
        Serial.print(": peer has ");
        Serial.print(peerNeighborCount);
        Serial.print(" neighbors, we have ");
        Serial.println(ourCount);
        #endif

        // Increment change counter as this indicates topology instability
        topologyChangeCount++;
    }
}

// Trigger aggressive route discovery when partition is detected
void Mesh::triggerAggressiveDiscovery() {
    Serial.println("[MESH] Triggering aggressive route discovery...");

    // 1. Broadcast multiple beacons to re-announce presence
    sendBeacon();

    // 2. Re-discover routes to all known destinations
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid && routeTable[i].is_primary) {
            uint32_t dest = routeTable[i].destination;

            #if DEBUG_MESH
            Serial.print("[MESH] Re-discovering route to 0x");
            Serial.println(dest, HEX);
            #endif

            // Initiate new route discovery
            initiateRouteDiscovery(dest);
        }
    }

    // 3. Send HELLO to all known neighbors to verify connectivity
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (neighbors[i].valid) {
            sendHelloToNextHop(neighbors[i].address);
        }
    }
}

void Mesh::sendAck(uint32_t dest, uint16_t packet_id) {
    Packet packet;
    memset(&packet, 0, sizeof(Packet));

    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = PKT_ACK;
    packet.header.ttl = MAX_TTL;
    packet.header.network_id = networkId16;  // PHASE 1.4: Set network ID
    packet.header.packet_id = packet_id;     // Echo original packet ID
    packet.header.source = nodeAddress;
    packet.header.destination = dest;
    packet.header.next_hop = dest;  // Direct to source
    packet.header.payload_length = 0;

    radio->send(&packet);
}

void Mesh::addPendingAck(uint16_t packet_id, uint32_t dest, const Packet* pkt) {
    // Find empty slot
    unsigned long now = millis();
    for (int i = 0; i < MAX_RETRIES * 4; i++) {
        if (!pendingAcks[i].valid) {
            pendingAcks[i].packet_id = packet_id;
            pendingAcks[i].destination = dest;
            pendingAcks[i].timestamp = now;
            pendingAcks[i].sent_time = now;  // Phase 2.2: Track original send time for RTT
            pendingAcks[i].retries = 0;
            memcpy(&pendingAcks[i].packet, pkt, sizeof(Packet));
            pendingAcks[i].valid = true;
            break;
        }
    }
}

void Mesh::removePendingAck(uint16_t packet_id) {
    for (int i = 0; i < MAX_RETRIES * 4; i++) {
        if (pendingAcks[i].valid && pendingAcks[i].packet_id == packet_id) {
            pendingAcks[i].valid = false;
            break;
        }
    }
}

void Mesh::handleAckTimeouts() {
    unsigned long now = millis();

    for (int i = 0; i < MAX_RETRIES * 4; i++) {
        if (pendingAcks[i].valid) {
            // PHASE 2.1 + 2.2: Exponential backoff with adaptive base timeout
            // First timeout uses adaptive RTO from RTT measurements
            // Then doubles each retry: RTO -> 2*RTO -> 4*RTO (capped at 60s)
            uint32_t baseTimeout = getAdaptiveTimeout(pendingAcks[i].destination);
            unsigned long backoffTimeout = baseTimeout << pendingAcks[i].retries;  // Left shift = multiply by 2^retries
            if (backoffTimeout > ACK_TIMEOUT_MAX) {
                backoffTimeout = ACK_TIMEOUT_MAX;
            }

            if (now - pendingAcks[i].timestamp > backoffTimeout) {
                if (pendingAcks[i].retries < MAX_RETRIES) {
                    // Add random jitter (0-500ms) to prevent synchronized retries
                    unsigned long jitter = random(0, ACK_JITTER_MAX);

                    Serial.print("[MESH] Retransmitting packet ");
                    Serial.print(pendingAcks[i].packet_id);
                    Serial.print(" (retry ");
                    Serial.print(pendingAcks[i].retries + 1);
                    Serial.print(", next timeout: ");
                    unsigned long nextTimeout = ACK_TIMEOUT << (pendingAcks[i].retries + 1);
                    if (nextTimeout > ACK_TIMEOUT_MAX) nextTimeout = ACK_TIMEOUT_MAX;
                    Serial.print(nextTimeout / 1000);
                    Serial.println("s)");

                    radio->send(&pendingAcks[i].packet);
                    pendingAcks[i].retries++;
                    pendingAcks[i].timestamp = now + jitter;  // Apply jitter to next timeout
                } else {
                    // Max retries reached
                    Serial.print("[MESH] Packet ");
                    Serial.print(pendingAcks[i].packet_id);
                    Serial.println(" failed after max retries");

                    // Notify delivery callback of failure
                    if (deliveryCallback) {
                        deliveryCallback(pendingAcks[i].packet_id, pendingAcks[i].destination, 3);  // 3 = DELIVERY_FAILED
                    }

                    pendingAcks[i].valid = false;
                }
            }
        }
    }
}

// ============================================
// Phase 2.2: RTT Estimation Functions
// Implements TCP-style SRTT calculation (RFC 6298)
// ============================================

RTTMetrics* Mesh::getRTTMetrics(uint32_t dest) {
    // Find existing entry
    for (int i = 0; i < MAX_RTT_ENTRIES; i++) {
        if (rttTable[i].valid && rttTable[i].destination == dest) {
            return &rttTable[i];
        }
    }
    return nullptr;
}

void Mesh::updateRTT(uint32_t dest, unsigned long rtt) {
    // Find or create RTT entry for destination
    RTTMetrics* metrics = getRTTMetrics(dest);

    if (metrics == nullptr) {
        // Find empty slot
        for (int i = 0; i < MAX_RTT_ENTRIES; i++) {
            if (!rttTable[i].valid) {
                metrics = &rttTable[i];
                metrics->destination = dest;
                metrics->valid = true;
                metrics->samples = 0;
                break;
            }
        }
    }

    if (metrics == nullptr) {
        // Table full - could implement LRU eviction, for now just drop
        return;
    }

    // TCP-style RTT calculation (RFC 6298)
    // Using integer math with scaling: SRTT scaled by 8, RTTVAR scaled by 4
    if (metrics->samples == 0) {
        // First measurement
        metrics->srtt = rtt * 8;           // SRTT = R (scaled by 8)
        metrics->rttvar = rtt * 2;         // RTTVAR = R/2 (scaled by 4, so *2)
    } else {
        // Subsequent measurements
        // RTTVAR = (1 - beta) * RTTVAR + beta * |SRTT - R|
        // beta = 1/4, so: RTTVAR = RTTVAR + (|SRTT/8 - R| - RTTVAR)/4
        int32_t srtt_unscaled = metrics->srtt / 8;
        int32_t delta = (int32_t)rtt - srtt_unscaled;
        if (delta < 0) delta = -delta;

        // RTTVAR = RTTVAR + (|delta| - RTTVAR) / 4
        // Integer: RTTVAR = (3 * RTTVAR + delta * 4) / 4
        metrics->rttvar = (3 * metrics->rttvar + delta * 4) / 4;

        // SRTT = (1 - alpha) * SRTT + alpha * R
        // alpha = 1/8, so: SRTT = SRTT + (R - SRTT/8)/8
        // Integer: SRTT = SRTT - SRTT/8 + R = (7 * SRTT + R * 8) / 8
        metrics->srtt = (7 * metrics->srtt + rtt * 8) / 8;
    }

    // RTO = SRTT + max(G, K * RTTVAR) where K = 4, G = clock granularity
    // We use G = 100ms (10Hz tick rate is common on embedded)
    // RTO = SRTT/8 + max(100, 4 * RTTVAR/4) = SRTT/8 + max(100, RTTVAR)
    uint32_t rto = metrics->srtt / 8 + ((metrics->rttvar > 100) ? metrics->rttvar : 100);

    // Clamp RTO to sensible bounds
    if (rto < ACK_TIMEOUT) rto = ACK_TIMEOUT;        // Min 5s (base timeout)
    if (rto > ACK_TIMEOUT_MAX) rto = ACK_TIMEOUT_MAX; // Max 60s

    metrics->rto = rto;
    metrics->samples++;

    #if DEBUG_MESH
    Serial.print("[RTT] Dest 0x");
    Serial.print(dest, HEX);
    Serial.print(": sample=");
    Serial.print(rtt);
    Serial.print("ms, SRTT=");
    Serial.print(metrics->srtt / 8);
    Serial.print("ms, RTTVAR=");
    Serial.print(metrics->rttvar);
    Serial.print("ms, RTO=");
    Serial.print(metrics->rto);
    Serial.print("ms (");
    Serial.print(metrics->samples);
    Serial.println(" samples)");
    #endif
}

uint32_t Mesh::getAdaptiveTimeout(uint32_t dest) {
    RTTMetrics* metrics = getRTTMetrics(dest);
    if (metrics != nullptr && metrics->samples > 0) {
        return metrics->rto;
    }
    // No RTT data yet, use default timeout
    return ACK_TIMEOUT;
}

bool Mesh::forwardPacket(Packet* packet) {
    // Decrement TTL
    if (packet->header.ttl == 0) {
        Serial.println("[MESH] TTL expired, dropping packet");
        return false;
    }
    packet->header.ttl--;
    packet->header.hop_count++;

    // Find route to destination
    uint32_t next_hop;
    if (!findRoute(packet->header.destination, &next_hop)) {
        Serial.println("[MESH] No route for forwarding");
        return false;
    }

    packet->header.next_hop = next_hop;

    // Forward packet
    if (radio->send(packet)) {
        #if DEBUG_MESH
        Serial.print("[MESH] Forwarded packet to 0x");
        Serial.println(next_hop, HEX);
        #endif
        return true;
    }

    return false;
}

uint16_t Mesh::generatePacketId() {
    return nextPacketId++;
}

uint8_t Mesh::calculateLinkQuality(int16_t rssi, int8_t snr) {
    // Simple quality metric based on RSSI and SNR
    // Returns 0-255, higher is better
    int quality = 0;

    // RSSI component (0-128)
    if (rssi > -50) quality += 128;
    else if (rssi > -100) quality += (rssi + 100) * 128 / 50;

    // SNR component (0-127)
    if (snr > 10) quality += 127;
    else if (snr > -10) quality += (snr + 10) * 127 / 20;

    return min(quality, 255);
}

// AODV Route Request Tracking
bool Mesh::hasSeenRequest(uint32_t originator, uint32_t request_id) {
    for (int i = 0; i < 16; i++) {
        if (seenRequests[i].valid &&
            seenRequests[i].originator == originator &&
            seenRequests[i].request_id == request_id) {
            return true;
        }
    }
    return false;
}

void Mesh::recordRequest(uint32_t originator, uint32_t request_id) {
    // Find empty slot or oldest entry
    int slot = -1;
    unsigned long oldest = millis();

    for (int i = 0; i < 16; i++) {
        if (!seenRequests[i].valid) {
            slot = i;
            break;
        }
        if (seenRequests[i].timestamp < oldest) {
            oldest = seenRequests[i].timestamp;
            slot = i;
        }
    }

    if (slot != -1) {
        seenRequests[slot].originator = originator;
        seenRequests[slot].request_id = request_id;
        seenRequests[slot].timestamp = millis();
        seenRequests[slot].valid = true;
    }
}

void Mesh::cleanupRequests() {
    unsigned long now = millis();
    for (int i = 0; i < 16; i++) {
        if (seenRequests[i].valid) {
            // Expire requests after 30 seconds
            if (now - seenRequests[i].timestamp > 30000) {
                seenRequests[i].valid = false;
            }
        }
    }
}

// ============================================================================
// Data Packet Deduplication
// ============================================================================

bool Mesh::hasSeenPacket(uint32_t source, uint16_t packet_id) {
    for (int i = 0; i < SEEN_PACKETS_SIZE; i++) {
        if (seenPackets[i].valid &&
            seenPackets[i].source == source &&
            seenPackets[i].packet_id == packet_id) {
            return true;
        }
    }
    return false;
}

void Mesh::recordPacket(uint32_t source, uint16_t packet_id) {
    // Find empty slot or oldest entry
    int slot = -1;
    unsigned long oldest = millis();

    for (int i = 0; i < SEEN_PACKETS_SIZE; i++) {
        if (!seenPackets[i].valid) {
            slot = i;
            break;
        }
        if (seenPackets[i].timestamp < oldest) {
            oldest = seenPackets[i].timestamp;
            slot = i;
        }
    }

    if (slot != -1) {
        seenPackets[slot].source = source;
        seenPackets[slot].packet_id = packet_id;
        seenPackets[slot].timestamp = millis();
        seenPackets[slot].valid = true;
    }
}

void Mesh::cleanupSeenPackets() {
    unsigned long now = millis();
    for (int i = 0; i < SEEN_PACKETS_SIZE; i++) {
        if (seenPackets[i].valid) {
            if (now - seenPackets[i].timestamp > SEEN_PACKET_TIMEOUT) {
                seenPackets[i].valid = false;
            }
        }
    }
}

bool Mesh::sendPosition(uint32_t dest, const PositionMessage* position, bool needsAck) {
    if (!position) {
        return false;
    }

    Packet packet;
    memset(&packet, 0, sizeof(Packet));

    // Fill header
    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = PKT_DATA;
    packet.header.ttl = MAX_TTL;
    packet.header.flags = needsAck ? FLAG_ACK_REQ : 0;
    packet.header.network_id = networkId16;  // PHASE 1.4: Set network ID
    packet.header.packet_id = generatePacketId();
    packet.header.source = nodeAddress;
    packet.header.destination = dest;
    packet.header.hop_count = 0;
    packet.header.seq_number = nextSeqNumber++;

    // Create position message
    DataMessage* msg = (DataMessage*)packet.payload;
    msg->msg_type = MSG_POSITION;
    msg->reserved = 0;
    msg->msg_length = sizeof(PositionMessage);

    // Copy position data
    memcpy(msg->data, position, sizeof(PositionMessage));

    packet.header.payload_length = sizeof(DataMessage) + sizeof(PositionMessage);

    // Find next hop
    uint32_t next_hop = dest;
    if (!isBroadcast(&packet)) {
        if (!findRoute(dest, &next_hop)) {
            #if DEBUG_MESH
            Serial.println("[MESH] No route to destination for position update");
            #endif
            initiateRouteDiscovery(dest);
            return false;
        }
    } else {
        next_hop = 0xFFFFFFFF;
    }

    packet.header.next_hop = next_hop;

    // Send packet
    if (radio->send(&packet)) {
        packetsSent++;

        #if DEBUG_MESH
        Serial.print("[MESH] Sent position to 0x");
        Serial.println(dest, HEX);
        #endif

        return true;
    }

    return false;
}

#if FEATURE_HYBRID_MAC
void Mesh::handleTimeSyncPacket(Packet* packet, int16_t rssi) {
    // Extract time sync message from payload
    if (packet->header.payload_length >= sizeof(TimeSyncMessage)) {
        const TimeSyncMessage* msg = (const TimeSyncMessage*)packet->payload;

        #if DEBUG_MESH
        Serial.print("[MESH] Time sync from 0x");
        Serial.print(packet->header.source, HEX);
        Serial.print(" source_type=");
        Serial.print(msg->source_type);
        Serial.print(" stratum=");
        Serial.println(msg->stratum);
        #endif

        // Forward to MAC layer
        hybridMAC.handleTimeSyncMessage(msg, rssi);
    }
}
#endif
