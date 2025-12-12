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
    packetsSent(0),
    packetsReceived(0)
{
    // Initialize tables
    for (int i = 0; i < MAX_ROUTES; i++) {
        routeTable[i].valid = false;
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

    instance = this;
}

Mesh::~Mesh() {
    instance = nullptr;
}

void Mesh::begin(uint32_t nodeAddr, Radio* radioPtr, Crypto* cryptoPtr) {
    nodeAddress = nodeAddr;
    radio = radioPtr;
    crypto = cryptoPtr;

    // Set up radio RX callback
    radio->setRxCallback(radioRxCallback);

    Serial.print("[MESH] LNK-22 initialized with address 0x");
    Serial.println(nodeAddress, HEX);
    Serial.print("[MESH] Default channel: ");
    Serial.println(currentChannel);
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

void Mesh::update() {
    // Clean up expired routes and neighbors
    static unsigned long lastCleanup = 0;
    unsigned long now = millis();

    if (now - lastCleanup > 10000) {  // Every 10 seconds
        cleanupRoutes();
        cleanupNeighbors();
        cleanupRequests();
        cleanupSeenPackets();
        lastCleanup = now;
    }

    // Handle ACK timeouts and retransmissions
    handleAckTimeouts();
}

bool Mesh::sendMessage(uint32_t dest, const uint8_t* data, uint16_t len, bool needsAck) {
    if (len > MAX_PAYLOAD_SIZE) {
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
    packet.header.packet_id = generatePacketId();
    packet.header.source = nodeAddress;
    packet.header.destination = dest;
    packet.header.hop_count = 0;
    packet.header.seq_number = nextSeqNumber++;
    packet.header.payload_length = len;

    // Copy payload
    memcpy(packet.payload, data, len);

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
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid) {
            Serial.print("  ");
            Serial.print(nodeNaming.getNodeName(routeTable[i].destination));
            Serial.print(" via ");
            Serial.print(nodeNaming.getNodeName(routeTable[i].next_hop));
            Serial.print(" (");
            Serial.print(routeTable[i].hop_count);
            Serial.print(" hops, Q:");
            Serial.print(routeTable[i].quality);
            Serial.print(", ");
            Serial.print((now - routeTable[i].timestamp) / 1000);
            Serial.println("s)");
            count++;
        }
    }
    if (count == 0) {
        Serial.println("  (no routes)");
    }
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
            unsigned long oldest = 0;
            uint16_t totalPkts = 0;
            if (neighbors[i].interfaces & IFACE_LORA) {
                if (neighbors[i].lora.last_seen > oldest) oldest = neighbors[i].lora.last_seen;
                totalPkts += neighbors[i].lora.packets_received;
            }
            if (neighbors[i].interfaces & IFACE_BLE) {
                if (neighbors[i].ble.last_seen > oldest) oldest = neighbors[i].ble.last_seen;
                totalPkts += neighbors[i].ble.packets_received;
            }

            Serial.print(" (");
            Serial.print(totalPkts);
            Serial.print(" pkts, ");
            Serial.print((now - oldest) / 1000);
            Serial.println("s ago)");
            count++;
        }
    }
    if (count == 0) {
        Serial.println("  (no neighbors)");
    }
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

    // Data packet deduplication - check if we've seen this packet recently
    if (packet->header.type == PKT_DATA) {
        if (hasSeenPacket(packet->header.source, packet->header.packet_id)) {
            Serial.print("[MESH] DEDUP: Dropping duplicate packet ");
            Serial.print(packet->header.packet_id);
            Serial.print(" from 0x");
            Serial.println(packet->header.source, HEX);
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
        // Validate payload content - must have at least 1 byte and contain printable text
        if (packet->header.payload_length == 0) {
            Serial.println("[MESH] REJECTED: Empty payload");
            return;
        }

        // Check if payload contains mostly printable characters (text message validation)
        int printableCount = 0;
        for (uint16_t i = 0; i < packet->header.payload_length; i++) {
            uint8_t c = packet->payload[i];
            // Count printable ASCII chars, newlines, tabs, and UTF-8 continuation bytes
            if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t' || (c >= 128)) {
                printableCount++;
            }
        }

        // At least 30% of content should be printable for text messages
        // Lowered from 50% because some protocols have metadata bytes
        if (printableCount < (int)(packet->header.payload_length / 3)) {
            Serial.print("[MESH] REJECTED: Non-printable payload (");
            Serial.print(printableCount);
            Serial.print("/");
            Serial.print(packet->header.payload_length);
            Serial.print(" printable) HEX: ");
            // Print first 16 bytes as hex for debugging
            for (uint16_t i = 0; i < packet->header.payload_length && i < 16; i++) {
                if (packet->payload[i] < 16) Serial.print("0");
                Serial.print(packet->payload[i], HEX);
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
            Serial.println(" (BROADCAST)");
        } else {
            Serial.println(" (DIRECT)");
        }
        Serial.println("----------------------------------------");
        Serial.write(packet->payload, packet->header.payload_length);
        Serial.println();
        Serial.println("========================================\n");

        // Notify BLE app of received message
        extern LNK22BLEService bleService;
        if (bleService.isConnected()) {
            uint8_t msgType = isBroadcast(packet) ? 0x02 : 0x01;  // 0x02=broadcast, 0x01=direct
            bleService.notifyMessage(
                msgType,
                packet->header.source,
                packet->header.channel_id,
                millis() / 1000,
                packet->payload,
                packet->header.payload_length
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
    removePendingAck(packet->header.packet_id);
    Serial.print("[MESH] ACK received for packet ");
    Serial.println(packet->header.packet_id);
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
    // Hello packets are primarily for neighbor discovery
    // Already handled by updateNeighbor() call
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

    // Search routing table
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
    // Find existing entry or empty slot
    int slot = -1;
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid && routeTable[i].destination == dest) {
            slot = i;
            break;
        }
        if (slot == -1 && !routeTable[i].valid) {
            slot = i;
        }
    }

    if (slot == -1) {
        // Table full, find oldest entry
        unsigned long oldest = millis();
        for (int i = 0; i < MAX_ROUTES; i++) {
            if (routeTable[i].timestamp < oldest) {
                oldest = routeTable[i].timestamp;
                slot = i;
            }
        }
    }

    // Add or update route
    if (slot != -1) {
        routeTable[slot].destination = dest;
        routeTable[slot].next_hop = next_hop;
        routeTable[slot].hop_count = hop_count;
        routeTable[slot].quality = quality;
        routeTable[slot].timestamp = millis();
        routeTable[slot].valid = true;
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
            // Check each interface for timeout and remove stale ones
            if ((neighbors[i].interfaces & IFACE_LORA) && (now - neighbors[i].lora.last_seen > ROUTE_TIMEOUT)) {
                neighbors[i].interfaces &= ~IFACE_LORA;
                #if DEBUG_MESH
                Serial.print("[MESH] LoRa path expired for: 0x");
                Serial.println(neighbors[i].address, HEX);
                #endif
            }
            if ((neighbors[i].interfaces & IFACE_BLE) && (now - neighbors[i].ble.last_seen > ROUTE_TIMEOUT)) {
                neighbors[i].interfaces &= ~IFACE_BLE;
                #if DEBUG_MESH
                Serial.print("[MESH] BLE path expired for: 0x");
                Serial.println(neighbors[i].address, HEX);
                #endif
            }
            if ((neighbors[i].interfaces & IFACE_LAN) && (now - neighbors[i].lan.last_seen > ROUTE_TIMEOUT)) {
                neighbors[i].interfaces &= ~IFACE_LAN;
            }
            if ((neighbors[i].interfaces & IFACE_WAN) && (now - neighbors[i].wan.last_seen > ROUTE_TIMEOUT)) {
                neighbors[i].interfaces &= ~IFACE_WAN;
            }

            // If no interfaces remain, neighbor is fully expired
            if (neighbors[i].interfaces == IFACE_NONE) {
                neighbors[i].valid = false;
                #if DEBUG_MESH
                Serial.print("[MESH] Neighbor fully expired: 0x");
                Serial.println(neighbors[i].address, HEX);
                #endif
            } else {
                // Recalculate best interface
                neighbors[i].preferred_iface = selectBestInterface(&neighbors[i]);
            }
        }
    }
}

void Mesh::sendAck(uint32_t dest, uint16_t packet_id) {
    Packet packet;
    memset(&packet, 0, sizeof(Packet));

    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = PKT_ACK;
    packet.header.ttl = MAX_TTL;
    packet.header.packet_id = packet_id;  // Echo original packet ID
    packet.header.source = nodeAddress;
    packet.header.destination = dest;
    packet.header.next_hop = dest;  // Direct to source
    packet.header.payload_length = 0;

    radio->send(&packet);
}

void Mesh::addPendingAck(uint16_t packet_id, uint32_t dest, const Packet* pkt) {
    // Find empty slot
    for (int i = 0; i < MAX_RETRIES * 4; i++) {
        if (!pendingAcks[i].valid) {
            pendingAcks[i].packet_id = packet_id;
            pendingAcks[i].destination = dest;
            pendingAcks[i].timestamp = millis();
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
            if (now - pendingAcks[i].timestamp > ACK_TIMEOUT) {
                if (pendingAcks[i].retries < MAX_RETRIES) {
                    // Retransmit
                    Serial.print("[MESH] Retransmitting packet ");
                    Serial.print(pendingAcks[i].packet_id);
                    Serial.print(" (retry ");
                    Serial.print(pendingAcks[i].retries + 1);
                    Serial.println(")");

                    radio->send(&pendingAcks[i].packet);
                    pendingAcks[i].retries++;
                    pendingAcks[i].timestamp = now;
                } else {
                    // Max retries reached
                    Serial.print("[MESH] Packet ");
                    Serial.print(pendingAcks[i].packet_id);
                    Serial.println(" failed after max retries");
                    pendingAcks[i].valid = false;
                }
            }
        }
    }
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
