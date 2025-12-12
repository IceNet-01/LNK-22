/**
 * LNK-22 Mesh Networking Layer
 * Handles AODV routing, neighbor discovery, message forwarding, and channels
 */

#ifndef LNK22_MESH_H
#define LNK22_MESH_H

#include <Arduino.h>
#include "../config.h"
#include "../protocol/protocol.h"
#include "../radio/radio.h"
#include "../crypto/crypto.h"

// Route table entry (Phase 3.3: supports multiple routes per destination)
struct RouteEntry {
    uint32_t destination;
    uint32_t next_hop;
    uint8_t hop_count;
    uint8_t quality;         // 0-255, higher is better
    unsigned long timestamp;
    bool valid;
    bool is_primary;         // Phase 3.3: Primary route flag (best route to dest)
};

// Interface type bitmask flags for multi-path tracking
#define IFACE_NONE    0x00
#define IFACE_LORA    0x01  // Discovered via LoRa radio
#define IFACE_BLE     0x02  // Discovered via Bluetooth mesh
#define IFACE_LAN     0x04  // Discovered via local network (future)
#define IFACE_WAN     0x08  // Discovered via internet (future)

// Interface priority (lower = better, for path selection)
#define IFACE_PRIORITY_BLE   1   // BLE is fastest/most efficient
#define IFACE_PRIORITY_LAN   2   // LAN next
#define IFACE_PRIORITY_LORA  3   // LoRa for long range
#define IFACE_PRIORITY_WAN   4   // WAN as fallback

// Per-interface signal quality
struct InterfaceInfo {
    int16_t rssi;
    int8_t snr;
    unsigned long last_seen;
    uint16_t packets_received;
};

// Neighbor table entry with multi-path support
struct Neighbor {
    uint32_t address;
    uint8_t interfaces;         // Bitmask of available interfaces (IFACE_*)
    uint8_t preferred_iface;    // Best interface to use (IFACE_*)
    InterfaceInfo lora;         // LoRa signal info
    InterfaceInfo ble;          // BLE signal info
    InterfaceInfo lan;          // LAN info (future)
    InterfaceInfo wan;          // WAN info (future)
    bool valid;
};

// Pending ACK entry
struct PendingAck {
    uint16_t packet_id;
    uint32_t destination;
    unsigned long timestamp;       // Time of last (re)transmission (for timeout)
    unsigned long sent_time;       // Original send time (for RTT measurement)
    uint8_t retries;
    Packet packet;
    bool valid;
};

// RTT metrics per destination (Phase 2.2)
#define MAX_RTT_ENTRIES 16
struct RTTMetrics {
    uint32_t destination;
    uint32_t srtt;           // Smoothed RTT (ms, scaled by 8)
    uint32_t rttvar;         // RTT variance (ms, scaled by 4)
    uint32_t rto;            // Computed retransmission timeout (ms)
    uint32_t samples;        // Number of RTT samples
    bool valid;
};

// Route request tracking (to avoid loops)
struct SeenRequest {
    uint32_t originator;
    uint32_t request_id;
    unsigned long timestamp;
    bool valid;
};

// Data packet deduplication (to prevent loops and duplicates)
#define SEEN_PACKETS_SIZE 32
#define SEEN_PACKET_TIMEOUT 30000  // 30 seconds

// Delivery callback type - called when ACK received or delivery fails
// status: 0=pending, 1=sent, 2=acked, 3=failed, 4=no_route
typedef void (*DeliveryCallback)(uint16_t packetId, uint32_t destination, uint8_t status);

struct SeenPacket {
    uint32_t source;
    uint16_t packet_id;
    unsigned long timestamp;
    bool valid;
};

// TTL constants
#define BROADCAST_TTL 5          // Limited range for broadcasts
#define TTL_SAFETY_MARGIN 2      // Extra hops beyond known route

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

    // Channel management (8 channels: 0-7)
    void setChannel(uint8_t channel);
    uint8_t getChannel() const { return currentChannel; }
    bool isValidChannel(uint8_t channel) const { return channel < NUM_CHANNELS; }

    // Encryption management
    void setEncryptionEnabled(bool enabled) { encryptionEnabled = enabled; }
    bool isEncryptionEnabled() const { return encryptionEnabled; }

    // Network ID filtering (Phase 1.4)
    void setNetworkIdFiltering(bool enabled) { networkIdFiltering = enabled; }
    bool isNetworkIdFilteringEnabled() const { return networkIdFiltering; }
    uint16_t getNetworkId16() const { return networkId16; }

    // Flow control (Phase 2.3)
    bool canSendMore() const;          // Check if TX window has space
    uint8_t getPendingCount() const;   // Get number of pending ACKs

    // Statistics
    uint32_t getPacketsSent() const { return packetsSent; }
    uint32_t getPacketsReceived() const { return packetsReceived; }
    uint8_t getNeighborCount() const;
    uint8_t getRouteCount() const;

    // Debug output
    void printRoutes();
    void printNeighbors();

    // Get neighbor info for display (returns true if valid, fills output params)
    bool getNeighbor(uint8_t index, uint32_t* address, int16_t* rssi, int8_t* snr);

    // Update neighbor table (public for BLE mesh scanner to use)
    void updateNeighbor(uint32_t addr, int16_t rssi, int8_t snr, uint8_t iface = IFACE_LORA);

    // Set callback for delivery status notifications
    void setDeliveryCallback(DeliveryCallback callback);

    // Get the last packet ID used (for tracking delivery status)
    uint16_t getLastPacketId() const { return nextPacketId - 1; }

private:
    // Node state
    uint32_t nodeAddress;
    Radio* radio;
    Crypto* crypto;
    uint16_t nextPacketId;
    uint8_t nextSeqNumber;
    uint32_t nextRouteRequestId;
    uint8_t currentChannel;  // Current channel (0-7)
    bool encryptionEnabled;  // Enable encryption for DATA packets
    bool networkIdFiltering; // Enable network ID filtering (Phase 1.4)
    uint16_t networkId16;    // 16-bit truncated network ID from PSK

    // Statistics
    uint32_t packetsSent;
    uint32_t packetsReceived;

    // Tables
    RouteEntry routeTable[MAX_ROUTES];
    Neighbor neighbors[MAX_NEIGHBORS];
    PendingAck pendingAcks[MAX_RETRIES * 4];
    SeenRequest seenRequests[16];  // Track recent route requests
    SeenPacket seenPackets[SEEN_PACKETS_SIZE];  // Track recent data packets for dedup
    RTTMetrics rttTable[MAX_RTT_ENTRIES];  // Phase 2.2: Per-destination RTT metrics

    // Delivery callback
    DeliveryCallback deliveryCallback;

    // Packet handlers
    void handleReceivedPacket(Packet* packet, int16_t rssi, int8_t snr);
    void handleDataPacket(Packet* packet);
    void handleAckPacket(Packet* packet);
    void handleRouteReqPacket(Packet* packet);
    void handleRouteRepPacket(Packet* packet);
    void handleRouteErrPacket(Packet* packet);
    void handleHelloPacket(Packet* packet, int16_t rssi, int8_t snr);
    void handleBeaconPacket(Packet* packet);
#if FEATURE_HYBRID_MAC
    void handleTimeSyncPacket(Packet* packet, int16_t rssi);
#endif

    // Routing
    bool findRoute(uint32_t dest, uint32_t* next_hop, uint8_t* hop_count = nullptr);
    void addRoute(uint32_t dest, uint32_t next_hop, uint8_t hop_count, uint8_t quality);
    void removeRoute(uint32_t dest);
    void initiateRouteDiscovery(uint32_t dest);
    void cleanupRoutes();

    // Phase 3.1: Proactive Route Maintenance
    void refreshStaleRoutes();
    void sendHelloToNextHop(uint32_t nextHop);

    // Phase 3.3: Multipath Routing
    int countRoutesToDest(uint32_t dest);          // Count routes to destination
    void updatePrimaryRoute(uint32_t dest);        // Recalculate primary route
    bool failoverRoute(uint32_t dest);             // Switch to backup route
    uint8_t calculateRouteScore(const RouteEntry* route);  // Quality scoring

    // Neighbors (updateNeighbor is public, above)
    bool isNeighbor(uint32_t addr);
    void cleanupNeighbors();
    uint8_t selectBestInterface(const Neighbor* neighbor);
    const char* getInterfaceName(uint8_t iface);
    void getInterfaceList(uint8_t interfaces, char* buf, size_t bufSize);

    // Phase 3.2: Neighbor Liveness - invalidate routes through dead neighbors
    void invalidateRoutesVia(uint32_t deadNeighbor);
    void sendRouteError(uint32_t unreachableDest, uint32_t failedNextHop);

    // ACK handling
    void sendAck(uint32_t dest, uint16_t packet_id);
    void addPendingAck(uint16_t packet_id, uint32_t dest, const Packet* pkt);
    void removePendingAck(uint16_t packet_id);
    void handleAckTimeouts();

    // RTT handling (Phase 2.2)
    void updateRTT(uint32_t dest, unsigned long rtt);
    RTTMetrics* getRTTMetrics(uint32_t dest);
    uint32_t getAdaptiveTimeout(uint32_t dest);

    // Packet forwarding
    bool forwardPacket(Packet* packet);

    // Utilities
    uint16_t generatePacketId();
    uint8_t calculateLinkQuality(int16_t rssi, int8_t snr);

    // Route request tracking
    bool hasSeenRequest(uint32_t originator, uint32_t request_id);
    void recordRequest(uint32_t originator, uint32_t request_id);
    void cleanupRequests();

    // Data packet deduplication
    bool hasSeenPacket(uint32_t source, uint16_t packet_id);
    void recordPacket(uint32_t source, uint16_t packet_id);
    void cleanupSeenPackets();

    // Static callback for radio RX
    static void radioRxCallback(Packet* packet, int16_t rssi, int8_t snr);
    static Mesh* instance;  // For callback
};

#endif // LNK22_MESH_H
