/**
 * MeshNet Mesh Networking Implementation
 */

#include "mesh.h"

Mesh* Mesh::instance = nullptr;

Mesh::Mesh() :
    nodeAddress(0),
    radio(nullptr),
    crypto(nullptr),
    nextPacketId(1),
    nextSeqNumber(0),
    nextRouteRequestId(1),
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

    Serial.print("[MESH] Initialized with address 0x");
    Serial.println(nodeAddress, HEX);
}

void Mesh::update() {
    // Clean up expired routes and neighbors
    static unsigned long lastCleanup = 0;
    unsigned long now = millis();

    if (now - lastCleanup > 10000) {  // Every 10 seconds
        cleanupRoutes();
        cleanupNeighbors();
        cleanupRequests();
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
    packet.header.ttl = MAX_TTL;
    packet.header.flags = needsAck ? FLAG_ACK_REQ : 0;
    packet.header.packet_id = generatePacketId();
    packet.header.source = nodeAddress;
    packet.header.destination = dest;
    packet.header.hop_count = 0;
    packet.header.seq_number = nextSeqNumber++;
    packet.header.payload_length = len;

    // Copy payload
    memcpy(packet.payload, data, len);

    // Find next hop
    uint32_t next_hop = dest;
    if (!isBroadcast(&packet)) {
        if (!findRoute(dest, &next_hop)) {
            Serial.println("[MESH] No route to destination, initiating discovery...");
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
    packet.header.packet_id = generatePacketId();
    packet.header.source = nodeAddress;
    packet.header.destination = 0xFFFFFFFF;
    packet.header.next_hop = 0xFFFFFFFF;
    packet.header.hop_count = 0;
    packet.header.seq_number = nextSeqNumber++;
    packet.header.payload_length = sizeof(BeaconPacket);

    // Fill beacon payload
    BeaconPacket* beacon = (BeaconPacket*)packet.payload;
    strncpy(beacon->name, "MeshNet Node", sizeof(beacon->name));
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

void Mesh::printRoutes() {
    Serial.println("\n=== Routing Table ===");
    Serial.println("Destination  Next Hop     Hops  Quality  Age(s)");
    Serial.println("----------------------------------------------");

    unsigned long now = millis();
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid) {
            Serial.print("0x");
            Serial.print(routeTable[i].destination, HEX);
            Serial.print("  0x");
            Serial.print(routeTable[i].next_hop, HEX);
            Serial.print("  ");
            Serial.print(routeTable[i].hop_count);
            Serial.print("     ");
            Serial.print(routeTable[i].quality);
            Serial.print("      ");
            Serial.println((now - routeTable[i].timestamp) / 1000);
        }
    }
    Serial.println("===================\n");
}

void Mesh::printNeighbors() {
    Serial.println("\n=== Neighbor Table ===");
    Serial.println("Address      RSSI   SNR   Pkts  Age(s)");
    Serial.println("----------------------------------------");

    unsigned long now = millis();
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (neighbors[i].valid) {
            Serial.print("0x");
            Serial.print(neighbors[i].address, HEX);
            Serial.print("  ");
            Serial.print(neighbors[i].rssi);
            Serial.print("  ");
            Serial.print(neighbors[i].snr);
            Serial.print("  ");
            Serial.print(neighbors[i].packets_received);
            Serial.print("  ");
            Serial.println((now - neighbors[i].last_seen) / 1000);
        }
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
    // Ignore our own packets
    if (packet->header.source == nodeAddress) {
        return;
    }

    packetReceived++;

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
        default:
            Serial.println("[MESH] Unknown packet type!");
            break;
    }
}

void Mesh::handleDataPacket(Packet* packet) {
    // Check if packet is for us
    if (packet->header.destination == nodeAddress || isBroadcast(packet)) {
        Serial.print("[MESH] Received message: ");
        Serial.write(packet->payload, packet->header.payload_length);
        Serial.println();

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

bool Mesh::findRoute(uint32_t dest, uint32_t* next_hop) {
    // Check if destination is a direct neighbor
    if (isNeighbor(dest)) {
        *next_hop = dest;
        return true;
    }

    // Search routing table
    for (int i = 0; i < MAX_ROUTES; i++) {
        if (routeTable[i].valid && routeTable[i].destination == dest) {
            *next_hop = routeTable[i].next_hop;
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
    ::RouteRequest* req = (::RouteRequest*)packet->payload;
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

void Mesh::updateNeighbor(uint32_t addr, int16_t rssi, int8_t snr) {
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
        // Table full, find oldest entry
        unsigned long oldest = millis();
        for (int i = 0; i < MAX_NEIGHBORS; i++) {
            if (neighbors[i].last_seen < oldest) {
                oldest = neighbors[i].last_seen;
                slot = i;
            }
        }
    }

    // Update neighbor
    if (slot != -1) {
        neighbors[slot].address = addr;
        neighbors[slot].rssi = rssi;
        neighbors[slot].snr = snr;
        neighbors[slot].last_seen = millis();
        neighbors[slot].packets_received++;
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
            if (now - neighbors[i].last_seen > ROUTE_TIMEOUT) {
                neighbors[i].valid = false;
                #if DEBUG_MESH
                Serial.print("[MESH] Neighbor expired: 0x");
                Serial.println(neighbors[i].address, HEX);
                #endif
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
