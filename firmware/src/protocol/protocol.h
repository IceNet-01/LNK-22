/**
 * LNK-22 Protocol Definitions
 * Packet structures, types, and channel support
 */

#ifndef LNK22_PROTOCOL_H
#define LNK22_PROTOCOL_H

#include <stdint.h>

// Packet types
enum PacketType {
    PKT_DATA       = 0x01,  // User data packet
    PKT_ACK        = 0x02,  // Acknowledgment
    PKT_ROUTE_REQ  = 0x03,  // Route request
    PKT_ROUTE_REP  = 0x04,  // Route reply
    PKT_ROUTE_ERR  = 0x05,  // Route error
    PKT_HELLO      = 0x06,  // Neighbor discovery
    PKT_TELEMETRY  = 0x07,  // Node status/telemetry
    PKT_BEACON     = 0x08,  // Network beacon
    PKT_TIME_SYNC  = 0x0A   // Time synchronization (MAC layer)
};

// Packet flags
#define FLAG_ACK_REQ    0x01  // Acknowledgment requested
#define FLAG_ENCRYPTED  0x02  // Payload is encrypted
#define FLAG_BROADCAST  0x04  // Broadcast packet
#define FLAG_RETRANS    0x08  // Retransmitted packet

// Message types (application layer)
enum MessageType {
    MSG_TEXT     = 0x01,  // Text message
    MSG_POSITION = 0x02,  // GPS coordinates
    MSG_SENSOR   = 0x03,  // Sensor data
    MSG_COMMAND  = 0x04,  // Device command
    MSG_FILE     = 0x05   // File transfer
};

// Network packet header (21 bytes fixed)
struct __attribute__((packed)) PacketHeader {
    uint8_t version : 4;         // Protocol version (LNK-22 = v2)
    uint8_t type : 4;            // Packet type
    uint8_t ttl;                 // Time to live
    uint8_t flags;               // Control flags
    uint8_t channel_id;          // Channel ID (0-7 for 8 channels)
    uint16_t packet_id;          // Unique packet ID
    uint32_t source;             // Source address
    uint32_t destination;        // Destination address
    uint32_t next_hop;           // Next hop address
    uint8_t hop_count;           // Number of hops
    uint8_t seq_number;          // Sequence number
    uint16_t payload_length;     // Payload length
};

// Complete packet structure
struct __attribute__((packed)) Packet {
    PacketHeader header;
    uint8_t payload[255];        // Variable length payload
};

// Route request payload
struct __attribute__((packed)) RouteRequest {
    uint32_t request_id;         // Unique request ID
    uint8_t hop_count;           // Current hop count
};

// Route reply payload
struct __attribute__((packed)) RouteReply {
    uint32_t request_id;         // Corresponding request ID
    uint8_t hop_count;           // Total hop count
    uint8_t quality;             // Link quality metric
};

// Route error payload
struct __attribute__((packed)) RouteError {
    uint32_t unreachable_dest;   // Unreachable destination
    uint32_t failed_next_hop;    // Failed next hop
};

// Hello packet payload (neighbor discovery)
struct __attribute__((packed)) HelloPacket {
    uint8_t neighbor_count;      // Number of known neighbors
    int16_t rssi;                // Last received RSSI
    int8_t snr;                  // Last received SNR
};

// Telemetry payload
struct __attribute__((packed)) TelemetryPacket {
    uint8_t battery_level;       // Battery percentage (0-100)
    int16_t temperature;         // Temperature in 0.1°C
    uint32_t uptime;             // Uptime in seconds
    uint16_t packets_sent;       // Total packets sent
    uint16_t packets_received;   // Total packets received
};

// Beacon payload
struct __attribute__((packed)) BeaconPacket {
    char name[32];               // Node name
    uint8_t capabilities;        // Capability flags
    uint32_t timestamp;          // Unix timestamp
};

// Data message header
struct __attribute__((packed)) DataMessage {
    uint8_t msg_type;            // Message type
    uint8_t reserved;            // Reserved for future use
    uint16_t msg_length;         // Message length
    uint8_t data[];              // Variable length data
};

// Position message (GPS coordinates)
struct __attribute__((packed)) PositionMessage {
    int32_t latitude;            // Latitude * 10^7 (e.g., 37.7749 * 10^7)
    int32_t longitude;           // Longitude * 10^7
    int32_t altitude;            // Altitude in meters * 100
    uint8_t satellites;          // Number of satellites
    uint8_t fix_type;            // Fix type (0=no fix, 2=2D, 3=3D)
    uint16_t heading;            // Heading in degrees * 100
    uint16_t speed;              // Speed in m/s * 100
    uint32_t timestamp;          // Unix timestamp or millis()
};

// Helper functions
inline uint16_t getPacketSize(const Packet* pkt) {
    return sizeof(PacketHeader) + pkt->header.payload_length;
}

inline bool isValidPacketType(uint8_t type) {
    return (type >= PKT_DATA && type <= PKT_BEACON) || type == PKT_TIME_SYNC;
}

inline bool needsAck(const Packet* pkt) {
    return pkt->header.flags & FLAG_ACK_REQ;
}

inline bool isEncrypted(const Packet* pkt) {
    return pkt->header.flags & FLAG_ENCRYPTED;
}

inline bool isBroadcast(const Packet* pkt) {
    return pkt->header.destination == 0xFFFFFFFF;
}

// Channel helper functions
inline uint8_t getChannel(const Packet* pkt) {
    return pkt->header.channel_id;
}

inline void setChannel(Packet* pkt, uint8_t channel) {
    pkt->header.channel_id = channel & 0x07; // Ensure 0-7 range
}

inline bool isOnChannel(const Packet* pkt, uint8_t channel) {
    return pkt->header.channel_id == channel;
}

#endif // LNK22_PROTOCOL_H
