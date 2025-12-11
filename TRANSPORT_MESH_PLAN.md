# LNK-22 Multi-Transport Mesh Architecture Plan

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

*Document created: Session planning for LNK-22 multi-transport mesh*
*Last updated: December 2024*
