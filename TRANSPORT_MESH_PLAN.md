# LNK-22 Multi-Transport Mesh Architecture Plan

## Inspiration: Link-16

LNK-22 is inspired by **Link-16**, the military tactical data link system. The goal is to create a **"Link-16 for everyone"** - a resilient, secure, multi-transport mesh network accessible to civilians.

### Link-16 Concepts → LNK-22 Implementation

| Link-16 Feature | LNK-22 Implementation |
|-----------------|----------------------|
| **TDMA** - Time-division multiple access | Time-slotted transmission for collision avoidance |
| **JTIDS terminals** - Multi-transport capable | Multi-transport stack (LAN/BLE/LoRa/WAN) |
| **Crypto** - End-to-end secure | X25519 + AES-256, forward secrecy (Double Ratchet) |
| **Network Participation Groups (NPGs)** | Encrypted channels/groups |
| **Track data sharing** - Position/status | GPS broadcast + node status in routing table |
| **Jam-resistant** - Freq hopping, spread spectrum | LoRa chirp spread spectrum + multi-transport redundancy |
| **MIDS terminals** - Various form factors | Radio nodes, phones, web clients all participating |
| **Interoperability** - Common picture | Unified routing table across all transports |

### Core Philosophy (from Link-16)

1. **Resilience** - No single point of failure, mesh degrades gracefully
2. **Interoperability** - Different device types all speak same protocol
3. **Secure by default** - Everything encrypted, always
4. **Real-time** - Fastest path automatically selected
5. **Common Operating Picture** - Every node sees same network state

---

## Overview

This document outlines the plan to evolve LNK-22 from a LoRa-only mesh into a **multi-transport mesh network** that automatically selects the best available path between nodes. Similar to how phones seamlessly switch between WiFi and cellular, LNK-22 will route messages over LAN, BLE, LoRa, or WAN trunks - whatever gets the message there fastest and most reliably.

---

## Transport Priority Stack

Messages always take the **fastest available path**, with automatic failover:

| Priority | Transport | Range | Latency | Use Case |
|----------|-----------|-------|---------|----------|
| 1 | **LAN** (WebRTC/WebSocket) | Same network | ~5ms | Web clients on same WiFi |
| 2 | **BLE** | ~100m | ~50ms | Nearby phones/computers |
| 3 | **LoRa** | 1-10+ km | ~500ms | Long range, no infrastructure |
| 4 | **WAN Trunk** | Global | ~80ms+ | Internet bridge between sites |

---

## Multi-Path Routing Table ("ARP Table")

### Key Principle: **Keep ALL paths, never prune weak ones**

The routing table maintains every known path to every destination, not just the best one. This enables **instant failover** - if the active path fails, the next-best path is already known.

### Table Structure

| Destination | Transport | Quality | RSSI | Latency | Hops | Status | Last Seen |
|-------------|-----------|---------|------|---------|------|--------|-----------|
| Node-A | LAN | 98% | n/a | 5ms | 1 | **active** | 2s |
| Node-A | BLE | 85% | -52 | 50ms | 1 | ready | 5s |
| Node-A | LoRa | 60% | -78 | 500ms | 1 | ready | 10s |
| Node-A | LoRa | 45% | -95 | 800ms | 2 | ready | 15s |
| Node-B | BLE | 90% | -45 | 40ms | 1 | **active** | 3s |
| Node-B | LoRa | 70% | -68 | 400ms | 1 | ready | 8s |
| Node-X | WAN | 75% | n/a | 80ms | trunk+1 | **active** | 4s |
| Node-X | WAN→LoRa | 50% | n/a | 600ms | trunk+2 | ready | 12s |

### Routing Behavior

- **Never delete a path** - mark as stale, keep for potential resurrection
- **Instant failover** - next-best path already in table, zero discovery time
- **Path aging** - update "last seen", quality degrades over time
- **Multiple same-transport paths** - keep alternate LoRa routes via different hops
- **Mixed-transport paths** - BLE→LoRa, WAN→LoRa, etc.

### Failover Logic (Pseudocode)

```javascript
if (activePath.failed || activePath.timeout) {
    nextBest = paths
        .filter(p => p.destination == target && p.status == 'ready')
        .sortBy(p => p.quality)
        .first();

    if (nextBest) {
        activePath.status = 'failed';
        nextBest.status = 'active';
        route(message, nextBest);
    } else {
        queue(message); // DTN store-and-forward
    }
}
```

---

## Component 1: LAN Discovery (Web Clients)

Web clients on the same network should automatically discover each other.

### Implementation Options

1. **WebRTC Data Channels** - P2P between browsers, NAT traversal built-in
2. **WebSocket via local relay** - Simple server running on any node's machine
3. **mDNS/Bonjour style** - Announce as `lnk22._mesh._tcp.local`

### Flow

1. Web client starts, announces presence on LAN
2. Discovers other LNK-22 web clients
3. Establishes direct connection (WebRTC preferred)
4. Adds LAN paths to routing table
5. Messages to nearby nodes go via LAN instead of radio

### Web Client Changes

- Add WebRTC peer connection manager
- LAN discovery service (mDNS or signaling server)
- Route selection logic: check LAN paths first

---

## Component 2: BLE Mesh Relay

Phones and computers become mesh nodes via Bluetooth, like BitChat but integrated with LoRa backbone.

### Firmware Changes (RAK4631)

- Extend BLE service to advertise as mesh relay
- Add BLE-to-LoRa message forwarding
- New BLE characteristics for mesh relay data
- Track BLE neighbors in routing table

### iOS App Changes

- Scan for other LNK-22 iOS clients via BLE
- BLE-to-BLE mesh when no radio connected
- Forward between BLE mesh and LoRa mesh when radio available
- Background BLE advertising/scanning

### Web Client Changes

- Web Bluetooth API support
- Connect to radios via BLE (not just USB serial)
- BLE peer discovery for other web clients (where supported)

### Auto-Deconfliction

- **BLE preferred** when peer in range (faster, lower power)
- **LoRa fallback** when BLE not reachable
- Per-destination transport selection based on routing table
- Seamless handoff as nodes move in/out of BLE range

---

## Component 3: WAN Trunks

Bridge separate mesh networks over the internet, creating a global mesh.

### Two Pieces Required

#### A. WAN Bridge Server (Persistent/Installable)

Runs on server, Pi, VPS, or always-on machine:

```
lnk22-bridge
├── Encrypted WebSocket server (wss://)
├── Auto-registers with discovery service OR manual IP/domain
├── Authenticates connecting web clients/bridges
├── Routes packets between connected mesh networks
├── Maintains trunk routing table
├── Multi-trunk support (connect 3+ sites)
└── Optional: Public relay for NAT traversal
```

**Installation options:**
- Docker container
- systemd service
- Standalone binary (Go/Rust for easy deployment)
- npm package for Node.js

**Discovery:**
- mDNS on LAN: `lnk22-bridge._mesh._tcp.local`
- Optional public registry for internet-wide discovery
- Manual IP/domain configuration

#### B. Web Client Trunk Mode

Any web client can:
- **Connect to a bridge** - become gateway to remote mesh
- **BE a bridge** temporarily - browser open = bridge active
- Show trunk status, latency, connected sites in UI

### WAN Routing

```
[Site A: Home]                    [Site B: Office]
    │                                   │
 Web Client ──────WAN Trunk──────► WAN Bridge
    │              (internet)           │
 LoRa Radio                        LoRa Radio
    │                                   │
 Node-1, Node-2                   Node-X, Node-Y
```

1. Bridge at Site B advertises on LAN + optional public registry
2. Web client at Site A connects to bridge (discovers or manual)
3. Trunk established, routing tables merge
4. Node-1 sends to Node-X → routes via WAN trunk automatically
5. If trunk drops → message queued (DTN) until trunk restored

### Security

- **TLS** for transport encryption
- **Mesh-layer encryption** still applies (end-to-end)
- Bridge can't read message content, just routes
- **Trunk authentication** - pre-shared key or certificate
- Optional: Tor/I2P transport for anonymity

---

## Implementation Phases

### Phase 1: Foundation (Current Sprint)
- [ ] Fix web client console log (scroll, noise, appearance)
- [ ] Easy UI for secure encrypted links
- [ ] Easy UI for encrypted channels
- [ ] Add Bluetooth (Web Bluetooth API) to web client

### Phase 2: BLE Mesh
- [ ] Firmware: BLE mesh relay mode
- [ ] iOS: BLE-to-BLE mesh between phones
- [ ] Web: BLE peer discovery
- [ ] Routing table: Add transport type column
- [ ] Auto-deconfliction: BLE preferred, LoRa fallback

### Phase 3: LAN Discovery
- [ ] WebRTC peer connections between web clients
- [ ] LAN discovery service (mDNS or signaling)
- [ ] Route selection: LAN > BLE > LoRa
- [ ] UI: Show LAN-connected peers

### Phase 4: WAN Trunks
- [ ] WAN Bridge server (standalone installable)
- [ ] Bridge discovery (mDNS + optional public)
- [ ] Web client trunk connection UI
- [ ] Cross-site routing table merge
- [ ] Trunk authentication and encryption

### Phase 5: Polish
- [ ] Unified transport status UI
- [ ] Path visualization (show which transport in use)
- [ ] Latency/quality graphs per transport
- [ ] DTN integration for trunk failures
- [ ] Documentation and deployment guides

---

## Challenges & Mitigations

| Challenge | Mitigation |
|-----------|------------|
| **Message deduplication** | Strong dedup by message ID (already implemented) |
| **Loop prevention** | TTL + seen-message cache (already implemented) |
| **Latency mismatch** | Per-transport timeout settings |
| **NAT traversal** | WebRTC handles this; bridge relay as fallback |
| **Battery drain (BLE scanning)** | Adaptive scan intervals, only scan when needed |
| **Security across transports** | End-to-end encryption regardless of transport |

---

## Architecture Diagram

```
                         ┌─────────────────────────────────────┐
                         │           INTERNET (WAN)            │
                         │  ┌─────────┐       ┌─────────┐      │
                         │  │ Bridge  │◄─────►│ Bridge  │      │
                         │  │ Site A  │       │ Site B  │      │
                         │  └────┬────┘       └────┬────┘      │
                         └───────┼─────────────────┼───────────┘
                                 │                 │
            ┌────────────────────┼─────┐    ┌──────┼────────────────┐
            │          LAN (WiFi)      │    │     LAN (WiFi)       │
            │  ┌──────┐    ┌──────┐    │    │  ┌──────┐  ┌──────┐  │
            │  │ Web  │◄──►│ Web  │    │    │  │ Web  │◄►│ Web  │  │
            │  │Client│    │Client│    │    │  │Client│  │Client│  │
            │  └──┬───┘    └──┬───┘    │    │  └──┬───┘  └──────┘  │
            │     │  BLE     │         │    │     │                │
            │     ▼          ▼         │    │     ▼                │
            │  ┌──────┐   ┌──────┐     │    │  ┌──────┐            │
            │  │Phone │◄─►│Radio │     │    │  │Radio │            │
            │  │ iOS  │BLE│RAK4631    │    │  │RAK4631│            │
            │  └──────┘   └──┬───┘     │    │  └──┬───┘            │
            └────────────────┼─────────┘    └─────┼────────────────┘
                             │LoRa                │LoRa
                             ▼                    ▼
                    ┌─────────────────────────────────────┐
                    │          LoRa RF (Long Range)       │
                    │  ┌──────┐  ┌──────┐  ┌──────┐       │
                    │  │Node 1│◄►│Node 2│◄►│Node 3│       │
                    │  └──────┘  └──────┘  └──────┘       │
                    └─────────────────────────────────────┘
```

---

## Summary

LNK-22 evolves from a LoRa mesh into a **true multi-transport mesh**:

- **LAN** for speed when on same network
- **BLE** for nearby devices without radios
- **LoRa** for long-range infrastructure-free comms
- **WAN** for global reach across the internet

All unified under one routing system that:
- Keeps all known paths (never prune)
- Auto-selects best available transport
- Instant failover to next-best path
- End-to-end encryption regardless of transport

This creates a resilient, redundant mesh that works everywhere - from a single room to across the world.

---

---

## TDMA (Time-Division Multiple Access)

Link-16 uses TDMA to prevent collisions and ensure fair access. LNK-22 should implement a similar system for LoRa transmissions.

### Why TDMA for LoRa?

- **Collision avoidance** - LoRa has long air time, collisions waste significant time
- **Predictable latency** - Know when your slot is, guaranteed transmit window
- **Power efficiency** - Nodes can sleep between their slots
- **Fairness** - Every node gets equal access

### TDMA Design for LNK-22

#### Time Structure

```
┌──────────────────── TDMA Frame (e.g., 10 seconds) ────────────────────┐
│ Slot 0 │ Slot 1 │ Slot 2 │ Slot 3 │ ... │ Slot N │ Contention │ Beacon │
│ Node-A │ Node-B │ Node-C │ Node-D │     │        │   Window   │  Slot  │
└────────┴────────┴────────┴────────┴─────┴────────┴────────────┴────────┘
```

#### Slot Types

| Slot Type | Purpose |
|-----------|---------|
| **Assigned Slots** | Dedicated to specific node, no contention |
| **Contention Window** | Random access for new nodes, urgent messages |
| **Beacon Slot** | Network sync, time distribution, new node discovery |

#### Slot Assignment

1. **Self-organizing** - Nodes claim slots based on their address hash
2. **Collision detection** - If two nodes claim same slot, higher address yields
3. **Dynamic allocation** - Busy nodes can request additional slots
4. **Slot trading** - Nodes can donate unused slots to neighbors

#### Time Synchronization

- **GPS time** - Nodes with GPS fix share precise time
- **Beacon sync** - Nodes without GPS sync to beacon slot
- **Drift compensation** - Track clock drift, adjust slot timing
- **Guard intervals** - Small gaps between slots for timing tolerance

#### Slot Duration Calculation

For SF10, 125kHz bandwidth:
- Max payload: 200 bytes
- Air time: ~370ms
- Slot size: 500ms (with guard interval)
- 10-second frame = 20 slots
- 16 assigned + 2 contention + 2 beacon

#### TDMA + Multi-Transport

- **LoRa only** - TDMA applies to LoRa transmissions
- **BLE/LAN** - No TDMA needed (collision handling built-in)
- **Hybrid** - If LoRa slot busy, try BLE/LAN instead
- **Slot reservation** - High-priority traffic can reserve slots

### Implementation Phases

1. **Phase 1: Basic TDMA**
   - Fixed slot assignment by node address
   - Beacon-based synchronization
   - Simple contention window

2. **Phase 2: Adaptive TDMA**
   - Dynamic slot allocation based on traffic
   - Slot trading between nodes
   - Priority-based slot assignment

3. **Phase 3: GPS-Synchronized TDMA**
   - Microsecond precision with GPS
   - Network-wide time coherence
   - Reduced guard intervals

---

## Common Operating Picture (COP)

Every node maintains awareness of the entire network state.

### COP Data Structure

Each node tracks:

```javascript
{
  nodes: {
    "Node-A": {
      address: "0x12345678",
      name: "Alpha",
      position: { lat: 40.123, lon: -74.456, alt: 100 },
      lastSeen: 1702345678,
      battery: 85,
      status: "active",
      transports: ["LoRa", "BLE"],
      tdmaSlot: 3
    },
    // ... all known nodes
  },
  paths: [
    { dest: "Node-A", transport: "LAN", quality: 98, latency: 5 },
    { dest: "Node-A", transport: "BLE", quality: 85, latency: 50 },
    { dest: "Node-A", transport: "LoRa", quality: 60, latency: 500 },
    // ... all known paths
  ],
  channels: {
    "TeamAlpha": { members: 5, lastActivity: 1702345600 },
    // ... all joined channels
  },
  network: {
    totalNodes: 12,
    avgLatency: 150,
    healthScore: 92
  }
}
```

### COP Synchronization

- **Periodic beacons** - Nodes broadcast their state
- **Delta updates** - Only send changes, not full state
- **Gossip protocol** - Nodes share what they've heard
- **Consistency** - Eventually consistent, not real-time

### COP Visualization (Web Client)

- **Network map** - All nodes with positions (if GPS available)
- **Path visualization** - Show active transport for each link
- **Health dashboard** - Network-wide statistics
- **Timeline** - Historical view of network changes

---

## Security Model

### Encryption Layers

| Layer | Purpose | Algorithm |
|-------|---------|-----------|
| **Transport** | Protect data in transit | TLS 1.3 (WAN), BLE encryption |
| **Mesh** | End-to-end encryption | X25519 + AES-256-GCM |
| **Forward Secrecy** | Protect past messages | Double Ratchet (Signal protocol) |
| **Channel** | Group encryption | Shared symmetric key + ratchet |

### Key Management

- **Node identity** - Ed25519 keypair, generated on first boot
- **Link keys** - X25519 key exchange per secure link
- **Channel keys** - Derived from channel name + secret
- **Key rotation** - Automatic rotation for forward secrecy

### Authentication

- **Node verification** - Sign messages with identity key
- **Channel membership** - Must know channel key to decrypt
- **Link establishment** - Mutual authentication during key exchange
- **Replay protection** - Sequence numbers + timestamp validation

### Threat Model

| Threat | Mitigation |
|--------|------------|
| **Eavesdropping** | End-to-end encryption |
| **Message tampering** | Authenticated encryption (GCM) |
| **Replay attacks** | Sequence numbers, timestamps |
| **Node impersonation** | Identity key signatures |
| **Traffic analysis** | Padding, dummy traffic (future) |
| **Jamming** | Multi-transport redundancy |

---

## Message Types

### Control Messages

| Type | Purpose | Transport |
|------|---------|-----------|
| **BEACON** | Node announcement, TDMA sync | LoRa broadcast |
| **ROUTE_REQ** | AODV route discovery | All transports |
| **ROUTE_REPLY** | AODV route response | All transports |
| **LINK_REQ** | Secure link establishment | Direct |
| **LINK_ACK** | Link establishment response | Direct |
| **CHANNEL_ANNOUNCE** | Channel membership broadcast | LoRa broadcast |
| **TIME_SYNC** | TDMA time synchronization | LoRa broadcast |
| **PATH_UPDATE** | Routing table changes | All transports |

### Data Messages

| Type | Purpose | Encryption |
|------|---------|------------|
| **TEXT** | User text message | Link or channel key |
| **POSITION** | GPS coordinates | Optional |
| **STATUS** | Node status update | Optional |
| **FILE** | File transfer (chunked) | Link key |
| **EMERGENCY** | SOS broadcast | None (must reach everyone) |

---

*Document created: Session planning for LNK-22 multi-transport mesh*
*Inspiration: Link-16 tactical data link*
*Last updated: December 2024*
