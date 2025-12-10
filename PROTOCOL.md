# MeshNet Protocol Specification v1.0

## Introduction

MeshNet defines a lightweight, efficient mesh networking protocol for LoRa devices. The protocol is designed for low-power, long-range communication with automatic message routing.

## Design Goals

1. **Efficiency**: Minimal overhead for constrained devices
2. **Reliability**: Acknowledgments and retransmissions
3. **Security**: End-to-end encryption by default
4. **Scalability**: Support networks of 100+ nodes
5. **Simplicity**: Easy to implement and understand

## Protocol Layers

### 1. Physical Layer

**Radio Parameters**:
- Frequency: 915 MHz (US), 868 MHz (EU), 433 MHz (Asia)
- Modulation: LoRa
- Spreading Factor: 7-12 (adaptive)
- Bandwidth: 125 kHz, 250 kHz, 500 kHz
- Coding Rate: 4/5, 4/6, 4/7, 4/8
- TX Power: 2-20 dBm (configurable)

### 2. MAC Layer

**Frame Structure**:
```
┌──────────┬──────────┬─────────┬─────────┬──────────┐
│ Preamble │  Header  │ Payload │   CRC   │ Postamble│
└──────────┴──────────┴─────────┴─────────┴──────────┘
    8B         4B       0-255B      2B         4B
```

**Medium Access**:
- CSMA/CA (Carrier Sense Multiple Access with Collision Avoidance)
- Random backoff: 0-255ms
- Listen Before Talk (LBT)

### 3. Network Layer

**Packet Format**:
```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
┌───────────────────────────────────────────────────────────────┐
│  Ver  │  Type │  TTL  │  Flags  │         Packet ID           │
├───────────────────────────────────────────────────────────────┤
│                        Source Address                         │
├───────────────────────────────────────────────────────────────┤
│                     Destination Address                       │
├───────────────────────────────────────────────────────────────┤
│                      Next Hop Address                         │
├───────────────────────────────────────────────────────────────┤
│    Hop Count    │   Seq Number   │       Payload Length      │
├───────────────────────────────────────────────────────────────┤
│                         Payload...                            │
└───────────────────────────────────────────────────────────────┘
```

**Field Descriptions**:
- **Version** (4 bits): Protocol version (currently 1)
- **Type** (4 bits): Packet type (DATA, ACK, ROUTE_REQ, ROUTE_REP, etc.)
- **TTL** (8 bits): Time to live (decremented at each hop)
- **Flags** (8 bits): Control flags (ACK_REQ, ENCRYPTED, etc.)
- **Packet ID** (16 bits): Unique packet identifier
- **Source Address** (32 bits): Origin node address
- **Destination Address** (32 bits): Final destination (0xFFFFFFFF = broadcast)
- **Next Hop Address** (32 bits): Next node in route
- **Hop Count** (8 bits): Number of hops traversed
- **Seq Number** (8 bits): Sequence number for ordering
- **Payload Length** (16 bits): Size of payload in bytes

### 4. Routing Protocol

**Algorithm**: AODV (Ad-hoc On-Demand Distance Vector) variant

**Route Discovery**:
1. Source broadcasts ROUTE_REQ packet
2. Intermediate nodes rebroadcast (with hop count increment)
3. Destination sends ROUTE_REP unicast back to source
4. Route cached in routing table

**Route Maintenance**:
- Routes expire after 300 seconds of inactivity
- ROUTE_ERR sent when link breaks
- Automatic route repair on failure

**Routing Table Entry**:
```c
struct RouteEntry {
    uint32_t destination;
    uint32_t next_hop;
    uint8_t hop_count;
    uint32_t timestamp;
    uint8_t quality;  // Link quality 0-255
};
```

### 5. Transport Layer

**Reliable Delivery**:
- ACK packets for confirmed delivery
- Retransmission on timeout (exponential backoff)
- Max 3 retries before failure

**Unreliable Delivery**:
- Fire and forget
- No ACK required
- Lower latency

### 6. Security

**Encryption**:
- Algorithm: ChaCha20-Poly1305
- Key Exchange: X25519 (ECDH)
- Authentication: Ed25519 signatures

**Key Management**:
- Pre-shared network key (256-bit)
- Per-session ephemeral keys
- Perfect forward secrecy

**Packet Encryption**:
```
┌──────────┬─────────┬───────────────┬─────────┐
│  Nonce   │  Header │   Encrypted   │   Tag   │
│  (12B)   │  (20B)  │   Payload     │  (16B)  │
└──────────┴─────────┴───────────────┴─────────┘
```

## Packet Types

| Type | Value | Description |
|------|-------|-------------|
| DATA | 0x01 | User data packet |
| ACK | 0x02 | Acknowledgment |
| ROUTE_REQ | 0x03 | Route request |
| ROUTE_REP | 0x04 | Route reply |
| ROUTE_ERR | 0x05 | Route error |
| HELLO | 0x06 | Neighbor discovery |
| TELEMETRY | 0x07 | Node status/telemetry |
| BEACON | 0x08 | Network beacon |

## Message Types (Application Layer)

| Type | Value | Description |
|------|-------|-------------|
| TEXT | 0x01 | Text message |
| POSITION | 0x02 | GPS coordinates |
| SENSOR | 0x03 | Sensor data |
| COMMAND | 0x04 | Device command |
| FILE | 0x05 | File transfer |

## Node Addressing

- **Address**: 32-bit unique identifier
- **Generation**: SHA256(device_serial)[0:4]
- **Broadcast**: 0xFFFFFFFF
- **Reserved**: 0x00000000

## Power Management

**Sleep Modes**:
- **Active**: Full operation
- **Idle**: CPU sleep, radio RX
- **Deep Sleep**: Everything off except RTC

**Duty Cycling**:
- Configurable wake intervals (1s - 1hr)
- Synchronized wake windows for mesh coordination

## Quality of Service

**Priority Levels**:
- **Critical** (3): Emergency, route control
- **High** (2): Voice, real-time data
- **Normal** (1): Text messages
- **Low** (0): Bulk data, telemetry

## Error Handling

**CRC**: 16-bit CRC on all packets
**Timeouts**: Configurable per packet type
**Error Codes**:
- 0x01: Route not found
- 0x02: Destination unreachable
- 0x03: TTL expired
- 0x04: Authentication failed
- 0x05: Decryption failed

## Performance Targets

- **Latency**: <2s for single-hop, <10s for 5 hops
- **Throughput**: 5-50 kbps (depends on SF)
- **Reliability**: >95% delivery within 3 hops
- **Range**: 1-15 km (line of sight)
- **Battery Life**: 7+ days on 2000mAh (with duty cycling)

## Future Extensions

- Multi-channel support
- Adaptive data rates
- Compression
- Store-and-forward
- Repeater mode
