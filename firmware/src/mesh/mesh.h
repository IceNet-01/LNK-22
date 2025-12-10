/**
 * MeshNet Mesh Networking Layer
 * Handles routing, neighbor discovery, and message forwarding
 */

#ifndef MESHNET_MESH_H
#define MESHNET_MESH_H

#include <Arduino.h>
#include "../config.h"
#include "../protocol/protocol.h"
#include "../radio/radio.h"
#include "../crypto/crypto.h"

// Route table entry
struct RouteEntry {
    uint32_t destination;
    uint32_t next_hop;
    uint8_t hop_count;
    uint8_t quality;
    unsigned long timestamp;
    bool valid;
};

// Neighbor table entry
struct Neighbor {
    uint32_t address;
    int16_t rssi;
    int8_t snr;
    unsigned long last_seen;
    uint16_t packets_received;
    bool valid;
};

// Pending ACK entry
struct PendingAck {
    uint16_t packet_id;
    uint32_t destination;
    unsigned long timestamp;
    uint8_t retries;
    Packet packet;
    bool valid;
};

// Route request tracking (to avoid loops)
struct RouteRequest {
    uint32_t originator;
    uint32_t request_id;
    unsigned long timestamp;
    bool valid;
};

class Mesh {
public:
    Mesh();
    ~Mesh();

    // Initialize mesh network
    void begin(uint32_t nodeAddr, Radio* radioPtr, Crypto* cryptoPtr);

    // Update mesh state (call in loop)
    void update();

    // Send message to destination
    bool sendMessage(uint32_t dest, const uint8_t* data, uint16_t len, bool needsAck = true);

    // Send position update
    bool sendPosition(uint32_t dest, const PositionMessage* position, bool needsAck = false);

    // Send beacon
    void sendBeacon();

    // Statistics
    uint32_t getPacketsSent() const { return packetsSent; }
    uint32_t getPacketsReceived() const { return packetsReceived; }
    uint8_t getNeighborCount() const;
    uint8_t getRouteCount() const;

    // Debug output
    void printRoutes();
    void printNeighbors();

private:
    // Node state
    uint32_t nodeAddress;
    Radio* radio;
    Crypto* crypto;
    uint16_t nextPacketId;
    uint8_t nextSeqNumber;
    uint32_t nextRouteRequestId;

    // Statistics
    uint32_t packetsSent;
    uint32_t packetsReceived;

    // Tables
    RouteEntry routeTable[MAX_ROUTES];
    Neighbor neighbors[MAX_NEIGHBORS];
    PendingAck pendingAcks[MAX_RETRIES * 4];
    RouteRequest seenRequests[16];  // Track recent route requests

    // Packet handlers
    void handleReceivedPacket(Packet* packet, int16_t rssi, int8_t snr);
    void handleDataPacket(Packet* packet);
    void handleAckPacket(Packet* packet);
    void handleRouteReqPacket(Packet* packet);
    void handleRouteRepPacket(Packet* packet);
    void handleRouteErrPacket(Packet* packet);
    void handleHelloPacket(Packet* packet, int16_t rssi, int8_t snr);
    void handleBeaconPacket(Packet* packet);

    // Routing
    bool findRoute(uint32_t dest, uint32_t* next_hop);
    void addRoute(uint32_t dest, uint32_t next_hop, uint8_t hop_count, uint8_t quality);
    void removeRoute(uint32_t dest);
    void initiateRouteDiscovery(uint32_t dest);
    void cleanupRoutes();

    // Neighbors
    void updateNeighbor(uint32_t addr, int16_t rssi, int8_t snr);
    bool isNeighbor(uint32_t addr);
    void cleanupNeighbors();

    // ACK handling
    void sendAck(uint32_t dest, uint16_t packet_id);
    void addPendingAck(uint16_t packet_id, uint32_t dest, const Packet* pkt);
    void removePendingAck(uint16_t packet_id);
    void handleAckTimeouts();

    // Packet forwarding
    bool forwardPacket(Packet* packet);

    // Utilities
    uint16_t generatePacketId();
    uint8_t calculateLinkQuality(int16_t rssi, int8_t snr);

    // Route request tracking
    bool hasSeenRequest(uint32_t originator, uint32_t request_id);
    void recordRequest(uint32_t originator, uint32_t request_id);
    void cleanupRequests();

    // Static callback for radio RX
    static void radioRxCallback(Packet* packet, int16_t rssi, int8_t snr);
    static Mesh* instance;  // For callback
};

#endif // MESHNET_MESH_H
