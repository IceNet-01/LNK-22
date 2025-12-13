# LNK-22 Development Roadmap

## Vision: True Autonomous, Self-Healing, TCP/IP-Like Mesh Communications

This roadmap outlines the path to achieving enterprise-grade mesh networking with:
- Autonomous operation and self-healing capabilities
- TCP/IP-like reliability guarantees
- Public and private mesh network support
- Verifiable encryption for end users

---

## Phase 1: Security Foundations
**Status**: [ ] Not Started | [ ] In Progress | [x] Complete

### 1.1 Replace Hardcoded Network Key
**Priority**: CRITICAL
**Location**: `firmware/src/crypto/crypto.cpp:192`
**Issue**: Network key is hardcoded as `0x42` repeated - anyone can decrypt traffic
**Status**: ✅ COMPLETE

**Tasks**:
- [x] Generate random network key on first boot
- [x] Store key in flash (LittleFS)
- [x] Add serial command `psk set <key>` to configure
- [x] Add serial command `psk show` to display current key hash
- [x] Support key import/export for network setup

**Implementation Notes**:
- New random PSK generated on first boot (not hardcoded 0x42)
- PSK can be set from passphrase: `psk set <passphrase>`
- PSK can be exported/imported: `psk export` / `psk import <hex>`
- Network ID derived from PSK hash
- Crypto statistics tracked (encrypt/decrypt success/fail)
- `crypto` command shows full crypto status

### 1.2 Enable Packet Encryption (was "Packet Signing")
**Priority**: CRITICAL
**Location**: `firmware/src/mesh/mesh.cpp:sendMessage()`, `handleDataPacket()`
**Issue**: `encrypt()` and `decrypt()` functions exist but were NEVER called
**Status**: ✅ COMPLETE

**Tasks**:
- [x] Wire up encryption in `mesh.cpp:sendMessage()`
- [x] Wire up decryption in `mesh.cpp:handleDataPacket()`
- [x] Set `FLAG_ENCRYPTED` when encrypting packets
- [x] Add `encrypt on/off` command to toggle encryption
- [x] Log encryption/decryption status

**Implementation Notes**:
- ChaCha20-Poly1305 AEAD encryption now active for all DATA packets
- 40-byte overhead (24-byte nonce + 16-byte MAC)
- `[ENCRYPTED]` indicator shown in message display
- `encrypt` / `encrypt on` / `encrypt off` commands available
- Encryption enabled by default on boot

### 1.3 Encryption Visibility
**Priority**: HIGH
**Location**: `firmware/src/protocol/protocol.h:26`
**Issue**: `FLAG_ENCRYPTED` defined but never set - users can't verify encryption
**Status**: ✅ COMPLETE

**Tasks**:
- [x] Set `FLAG_ENCRYPTED` when encrypting packets
- [x] Display `[ENCRYPTED]` indicator on serial TX/RX
- [x] Add crypto stats: encryptions/decryptions succeeded/failed
- [x] Add `crypto status` serial command
- [x] Show encryption status in `status` command

**Implementation Notes**:
- Serial output shows `[ENCRYPTED]` for encrypted messages
- Crypto statistics tracked: encryptSuccess, encryptFail, decryptSuccess, decryptFail
- `crypto` command displays full crypto status with statistics
- `status` command shows encryption enabled/disabled state

### 1.4 Network ID Implementation
**Priority**: HIGH
**Location**: `firmware/src/protocol/protocol.h:40-53`
**Issue**: No network ID in packet headers - can't isolate networks
**Status**: ✅ COMPLETE

**Tasks**:
- [x] Add `network_id` field to packet header
- [x] Derive network ID from PSK hash
- [x] Filter packets by network ID
- [x] Display network ID in status output
- [x] Add `netid` / `netid on` / `netid off` commands

**Implementation Notes**:
- Added 16-bit `network_id` field to PacketHeader (now 23 bytes)
- Network ID derived from truncated PSK hash (`crypto->getNetworkId16()`)
- All packet creation functions set network ID
- Incoming packets filtered by network ID when filtering enabled
- `netid` command shows network ID and filtering status
- Filtering enabled by default to isolate networks

---

## Phase 2: Reliability (TCP/IP-Like)
**Status**: [ ] Not Started | [x] In Progress | [ ] Complete

### 2.1 Exponential Backoff
**Priority**: HIGH
**Location**: `firmware/src/mesh/mesh.cpp:1153-1199`
**Issue**: Fixed 5s timeout with immediate retry causes network congestion
**Status**: ✅ COMPLETE

**Tasks**:
- [x] Implement exponential backoff: 5s, 10s, 20s, 40s
- [x] Add random jitter (0-500ms) to prevent synchronized retries
- [x] Cap maximum timeout at 60s
- [x] Track retry statistics

**Implementation Notes**:
- Timeout doubles each retry using bit shift: `ACK_TIMEOUT << retries`
- Capped at `ACK_TIMEOUT_MAX` (60s) to prevent infinite waits
- Random jitter (0-500ms) added to `timestamp` to desynchronize retries
- Verbose logging shows next timeout value for debugging

### 2.2 Adaptive RTT Estimation
**Priority**: MEDIUM
**Location**: `firmware/src/mesh/mesh.cpp:1228-1325`
**Issue**: Fixed timeout regardless of actual network conditions
**Status**: ✅ COMPLETE

**Tasks**:
- [x] Measure RTT on each ACK
- [x] Implement TCP-style SRTT calculation
- [x] Calculate RTO = SRTT + 4*RTTVAR
- [x] Store per-destination RTT metrics
- [ ] Display RTT in neighbor/route tables (deferred)

**Implementation Notes**:
- `RTTMetrics` struct stores per-destination: SRTT, RTTVAR, RTO, sample count
- RTT measured only on first transmission (Karn's algorithm)
- TCP RFC 6298 algorithm with integer math (SRTT scaled by 8, RTTVAR by 4)
- RTO clamped to [ACK_TIMEOUT, ACK_TIMEOUT_MAX] range
- `getAdaptiveTimeout()` returns RTO or default if no samples
- Exponential backoff now uses adaptive RTO as base

### 2.3 Flow Control
**Priority**: MEDIUM
**Location**: `firmware/src/mesh/mesh.cpp:266-280`
**Issue**: No transmit window - can't throttle sender
**Status**: ✅ COMPLETE

**Tasks**:
- [x] Implement sliding window (4 outstanding packets)
- [x] Add `canSendMore()` check before transmission
- [x] Implement backpressure to application layer
- [ ] Track window utilization statistics (deferred)

**Implementation Notes**:
- `TX_WINDOW_SIZE = 4` limits outstanding ACK-required packets
- `getPendingCount()` returns current number of pending ACKs
- `canSendMore()` returns true if window has space
- `sendMessage()` returns false if window is full
- Only ACK-required packets count against window (broadcasts exempt)

### 2.4 Receiver Duplicate Detection
**Priority**: MEDIUM
**Location**: `firmware/src/mesh/mesh.cpp`
**Issue**: Deduplication only for forwarding, not for receiver
**Status**: ✅ COMPLETE

**Tasks**:
- [x] Track received packet IDs per source
- [x] Re-ACK duplicates without processing
- [x] Implement circular buffer for recent IDs
- [x] Auto-expire old entries

**Implementation Notes**:
- Receiver now re-ACKs duplicate DATA packets (handles lost ACKs)
- Original ACK may have been lost, so we re-send without processing payload
- Uses existing `seenPackets[]` circular buffer (32 entries)
- Entries auto-expire after 30 seconds (`SEEN_PACKET_TIMEOUT`)

### 2.5 Memory Optimization
**Priority**: LOW
**Location**: `firmware/src/mesh/mesh.h:59-66`
**Issue**: Stores entire packet (276 bytes) for retries - wastes 3.3KB RAM

**Tasks**:
- [ ] Separate TX buffer from ACK tracking
- [ ] Store only packet metadata in PendingAck
- [ ] Reference TX buffer by index
- [ ] Implement circular TX buffer

---

## Phase 3: Self-Healing Mesh
**Status**: [ ] Not Started | [ ] In Progress | [x] Complete

### 3.1 Proactive Route Maintenance
**Priority**: MEDIUM
**Location**: `firmware/src/mesh/mesh.cpp`
**Issue**: Routes only refreshed on-demand - causes latency spikes
**Status**: ✅ COMPLETE

**Tasks**:
- [x] Add route refresh at 4 minutes (before 5-min timeout)
- [x] Send lightweight HELLO to next-hop
- [x] Update route timestamp on HELLO response
- [x] Track route freshness in status output

**Implementation Notes**:
- `ROUTE_REFRESH_TIME = 240000` (4 minutes) triggers proactive refresh
- `refreshStaleRoutes()` runs every 30s, sends HELLO to stale routes' next-hop
- `sendHelloToNextHop()` sends lightweight PKT_HELLO (TTL=1, no payload)
- `handleHelloPacket()` refreshes timestamps of all routes through sender
- `printRoutes()` shows freshness: fresh (<2min), AGING (2-4min), STALE (>4min)

### 3.2 Neighbor Liveness Monitoring
**Priority**: HIGH
**Location**: `firmware/src/mesh/mesh.cpp`
**Issue**: No mechanism to detect broken links until packet loss
**Status**: ✅ COMPLETE

**Tasks**:
- [x] Track `last_heard` timestamp per neighbor
- [x] Implement neighbor timeout (60 seconds)
- [x] Proactively invalidate routes through dead neighbors
- [x] Generate ROUTE_ERROR for affected destinations
- [x] Display neighbor liveness in status

**Implementation Notes**:
- `NEIGHBOR_TIMEOUT = 60000` (60 seconds) for faster dead neighbor detection
- `invalidateRoutesVia()` finds and removes all routes through dead neighbor
- `sendRouteError()` broadcasts ROUTE_ERROR to notify network
- `printNeighbors()` shows liveness status: fresh (<30s), AGING (30-45s), STALE (>45s)
- Routes are auto-removed when neighbor times out, triggering ROUTE_ERROR broadcast

### 3.3 Multipath Routing
**Priority**: MEDIUM
**Location**: `firmware/src/mesh/mesh.cpp:785-803`
**Issue**: Only single route per destination - no backup paths
**Status**: ✅ COMPLETE

**Tasks**:
- [x] Store multiple routes per destination (up to 3)
- [x] Mark primary vs backup routes
- [x] Implement route quality scoring
- [x] Automatic failover to backup on primary failure
- [x] Display all routes in routing table

**Implementation Notes**:
- `MAX_ROUTES_PER_DEST = 3` limits routes stored per destination
- `RouteEntry.is_primary` flag marks the best route (used by `findRoute()`)
- `calculateRouteScore()` computes score: `quality - (hop_count * 20)` (clamped 0-255)
- `updatePrimaryRoute()` recalculates best route when routes change
- `failoverRoute()` invalidates failed primary and promotes backup
- `countRoutesToDest()` counts routes for a destination
- `addRoute()` stores up to 3 routes per dest, replaces worst if full
- `printRoutes()` groups routes by destination, shows `*` for primary, displays score

### 3.4 Network Partition Detection
**Priority**: LOW
**Location**: `firmware/src/mesh/mesh.cpp`
**Issue**: No mechanism to detect or heal network splits
**Status**: ✅ COMPLETE

**Tasks**:
- [x] Calculate topology hash from neighbor set
- [x] Periodically broadcast topology summary
- [x] Detect partition when hash changes unexpectedly
- [x] Trigger aggressive route discovery on partition
- [x] Log partition events

**Implementation Notes**:
- `TOPOLOGY_BROADCAST_INTERVAL = 60000` broadcasts topology summary every 60s
- `PARTITION_DETECT_THRESHOLD = 3` consecutive topology changes triggers partition detection
- `calculateTopologyHash()` uses FNV-1a hash of sorted neighbor addresses
- `checkTopologyChange()` detects changes, increments counter, triggers partition handling
- `broadcastTopologySummary()` sends HELLO with topology payload (hash + neighbor count)
- `handleTopologySummary()` processes peer topology info, detects mismatches
- `triggerAggressiveDiscovery()` sends beacons, re-discovers routes, verifies neighbors
- `printNeighbors()` shows topology hash and partition event count

---

## Phase 4: Network Isolation (Public/Private)
**Status**: [ ] Not Started | [ ] In Progress | [ ] Complete

### 4.1 Network Type Configuration
**Priority**: HIGH
**Location**: `firmware/src/mesh/mesh.h`
**Issue**: No concept of network types or access control

**Tasks**:
- [ ] Define network types: PUBLIC, PRIVATE, INVITE, HYBRID
- [ ] Add network configuration structure
- [ ] Implement `network mode <type>` command
- [ ] Store configuration in flash
- [ ] Display network type in status

### 4.2 PSK Authentication
**Priority**: HIGH
**Location**: `firmware/src/crypto/crypto.cpp`
**Issue**: No authentication mechanism for private networks

**Tasks**:
- [ ] Implement challenge-response protocol
- [ ] Generate random challenge on new neighbor
- [ ] Require valid response before accepting packets
- [ ] Cache authenticated nodes
- [ ] Timeout authentication after period

### 4.3 Node Whitelist
**Priority**: MEDIUM
**Location**: `firmware/src/mesh/mesh.cpp`
**Issue**: No way to restrict which nodes can join

**Tasks**:
- [ ] Implement whitelist storage (up to 64 nodes)
- [ ] Add `whitelist add <addr>` command
- [ ] Add `whitelist remove <addr>` command
- [ ] Add `whitelist show` command
- [ ] Enforce whitelist in INVITE mode

### 4.4 Key Rotation
**Priority**: LOW
**Location**: `firmware/src/crypto/crypto.cpp`
**Issue**: No mechanism to update network keys

**Tasks**:
- [ ] Define key update message format
- [ ] Implement admin signature verification
- [ ] Schedule key activation time
- [ ] Broadcast key update to network
- [ ] Graceful transition with overlap period

---

## Implementation Progress

| Phase | Component | Status | Commit |
|-------|-----------|--------|--------|
| 1.1 | Configurable PSK | ✅ Complete | 7729ff5 |
| 1.2 | Packet Encryption | ✅ Complete | - |
| 1.3 | Encryption Visibility | ✅ Complete | - |
| 1.4 | Network ID | ✅ Complete | - |
| 2.1 | Exponential Backoff | ✅ Complete | - |
| 2.2 | Adaptive RTT | ✅ Complete | - |
| 2.3 | Flow Control | ✅ Complete | - |
| 2.4 | Receiver Dedup | ✅ Complete | - |
| 2.5 | Memory Optimization | Not Started | - |
| 3.1 | Proactive Routes | ✅ Complete | - |
| 3.2 | Neighbor Liveness | ✅ Complete | - |
| 3.3 | Multipath Routing | ✅ Complete | - |
| 3.4 | Partition Detection | ✅ Complete | - |
| 4.1 | Network Types | Not Started | - |
| 4.2 | PSK Authentication | Not Started | - |
| 4.3 | Node Whitelist | Not Started | - |
| 4.4 | Key Rotation | Not Started | - |

---

## Testing Strategy

### Security Testing
- [ ] Impersonation test: Can Node A send packets as Node B?
- [ ] Replay test: Can old packets be replayed?
- [ ] Network isolation: Can private network see public traffic?
- [ ] Key mismatch: Different PSKs should fail gracefully

### Reliability Testing
- [ ] Packet loss: 50% loss, measure retry behavior
- [ ] Congestion: 10 nodes sending, measure throughput
- [ ] RTT variance: Measure timeout accuracy

### Self-Healing Testing
- [ ] Node failure: Remove node, measure recovery time
- [ ] Link failure: Block path, verify failover
- [ ] Partition: Split network, verify healing

---

## Version Targets

- **v2.0.0**: Phase 1 Complete (Security Foundations)
- **v2.1.0**: Phase 2 Complete (TCP/IP-Like Reliability)
- **v2.2.0**: Phase 3 Complete (Self-Healing)
- **v2.3.0**: Phase 4 Complete (Network Isolation)
- **v3.0.0**: Full Production Release

---

*Last Updated: 2025-12-12*

---

## Development Log

### Session: 2025-12-12 (Continued)

**Completed:**
1. **Phase 2.4: Receiver Duplicate Detection** - Modified `handleReceivedPacket()` to re-ACK duplicate DATA packets instead of silently dropping them. This handles the case where the original ACK was lost and the sender is retransmitting.

2. **Phase 3.2: Neighbor Liveness Monitoring (HIGH)** - Added 60-second neighbor timeout (`NEIGHBOR_TIMEOUT`), faster than route timeout. When a neighbor dies, `invalidateRoutesVia()` removes all routes through it and `sendRouteError()` broadcasts ROUTE_ERROR. `printNeighbors()` shows liveness status.

3. **Phase 3.1: Proactive Route Maintenance** - Added route refresh at 4 minutes (`ROUTE_REFRESH_TIME`). `refreshStaleRoutes()` runs every 30s, `sendHelloToNextHop()` sends lightweight HELLO. `handleHelloPacket()` refreshes route timestamps. `printRoutes()` shows freshness status.

**Next Steps for Future Sessions:**
1. **Phase 3.4: Network Partition Detection** - Topology hash, partition detection, aggressive route discovery
2. **Phase 4: Network Isolation** - PUBLIC/PRIVATE/INVITE modes, challenge-response PSK auth, node whitelist

**Key Files Modified:**
- `firmware/src/config.h` - Added `NEIGHBOR_TIMEOUT`, `ROUTE_REFRESH_TIME`
- `firmware/src/mesh/mesh.h` - Added `invalidateRoutesVia()`, `sendRouteError()`, `refreshStaleRoutes()`, `sendHelloToNextHop()`
- `firmware/src/mesh/mesh.cpp` - Implemented all new functions, updated `cleanupNeighbors()`, `handleHelloPacket()`, `printNeighbors()`, `printRoutes()`

**Firmware Stats:** RAM 53.7%, Flash 41.0%

---

### Session: 2025-12-12 (Phase 3.3 Completion)

**Completed:**
1. **Phase 3.3: Multipath Routing** - Full implementation of multipath routing with up to 3 routes per destination:
   - Added `MAX_ROUTES_PER_DEST = 3` to config.h
   - Modified `RouteEntry` struct to add `is_primary` flag in mesh.h
   - Implemented `countRoutesToDest()` - counts routes to a destination
   - Implemented `calculateRouteScore()` - score = quality - (hop_count * 20), clamped 0-255
   - Implemented `updatePrimaryRoute()` - recalculates best route when routes change
   - Implemented `failoverRoute()` - invalidates failed primary and promotes backup
   - Modified `addRoute()` - stores up to 3 routes per dest, replaces worst if full
   - Modified `findRoute()` - prefers primary route, falls back to any valid route
   - Updated `printRoutes()` - groups by destination, shows path count, primary indicator (*), score

**Example `routes` output:**
```
=== Routing Table ===
  NodeB [2 paths]:
    * via NodeA (1h, Q:180, S:160, 45s)
      via NodeC (2h, Q:150, S:110, 120s AGING)
Routes: 2/32 | Timeout: 5min | Refresh: 4min
Legend: * = primary route, h = hops, Q = quality, S = score
===================
```

**Key Files Modified:**
- `firmware/src/config.h` - Added `MAX_ROUTES_PER_DEST`
- `firmware/src/mesh/mesh.h` - Added `is_primary` to RouteEntry, multipath method declarations
- `firmware/src/mesh/mesh.cpp` - Implemented multipath functions, updated addRoute(), findRoute(), printRoutes()

**Firmware Stats:** RAM 53.7%, Flash 41.1%

---

### Session: 2025-12-13 (Phase 3.4 Completion - Phase 3 COMPLETE)

**Completed:**
1. **Phase 3.4: Network Partition Detection** - Full implementation of network partition detection and healing:
   - Added `TOPOLOGY_BROADCAST_INTERVAL = 60000` to config.h (60s broadcast interval)
   - Added `PARTITION_DETECT_THRESHOLD = 3` (consecutive changes to trigger partition)
   - Added partition detection state variables to mesh.h: `topologyHash`, `lastTopologyHash`, `topologyChangeCount`, `lastTopologyBroadcast`, `partitionEvents`
   - Implemented `calculateTopologyHash()` - FNV-1a hash of sorted neighbor addresses
   - Implemented `checkTopologyChange()` - detects topology changes, triggers partition on threshold
   - Implemented `broadcastTopologySummary()` - sends HELLO with topology payload
   - Implemented `handleTopologySummary()` - processes peer topology, detects mismatches
   - Implemented `triggerAggressiveDiscovery()` - beacons + route discovery + neighbor verification
   - Updated `handleHelloPacket()` to process topology summary payloads
   - Updated `printNeighbors()` to show topology hash and partition event count
   - Updated `update()` loop with partition detection calls

**Example `neighbors` output:**
```
=== Neighbor Table ===
  Alpha [LoRa] *LoRa* LoRa:-65/8 (15 pkts, 12s ago)
  Bravo [LoRa,BLE] *BLE* LoRa:-72/6 BLE:-55 (8 pkts, 5s ago)
Timeout: 60s | Topology: 0xA3F7B2C1 | Partitions: 0
====================
```

**Key Files Modified:**
- `firmware/src/config.h` - Added `TOPOLOGY_BROADCAST_INTERVAL`, `PARTITION_DETECT_THRESHOLD`
- `firmware/src/mesh/mesh.h` - Added partition state vars and method declarations
- `firmware/src/mesh/mesh.cpp` - Implemented all partition detection functions

**Firmware Stats:** RAM 53.7%, Flash 41.5%

**Phase 3 (Self-Healing Mesh) is now COMPLETE!**

**Next Steps for Future Sessions:**
1. **Phase 4.1: Network Type Configuration** - PUBLIC/PRIVATE/INVITE/HYBRID modes
2. **Phase 4.2: PSK Authentication** - Challenge-response authentication
3. **Phase 4.3: Node Whitelist** - Restrict which nodes can join
4. **Phase 4.4: Key Rotation** - Network key update mechanism

---

### How to Resume Development

If starting a fresh session, tell Claude:
1. Read `/home/mesh/LNK-22/ROADMAP.md` for current status
2. Continue with the next pending phase (check Implementation Progress table)
3. Always update ROADMAP.md after completing each phase
4. Compile with `pio run` after each change to verify
