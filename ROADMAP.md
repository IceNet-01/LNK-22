# LNK-22 Development Roadmap

## Vision: True Autonomous, Self-Healing, TCP/IP-Like Mesh Communications

This roadmap outlines the path to achieving enterprise-grade mesh networking with:
- Autonomous operation and self-healing capabilities
- TCP/IP-like reliability guarantees
- Public and private mesh network support
- Verifiable encryption for end users

---

## Phase 1: Security Foundations
**Status**: [ ] Not Started | [x] In Progress | [ ] Complete

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

### 1.2 Enable Packet Signing
**Priority**: CRITICAL
**Location**: `firmware/src/crypto/crypto.cpp:139-166`
**Issue**: `sign()` and `verify()` functions exist but are NEVER called

**Tasks**:
- [ ] Wire up signing in `mesh.cpp:sendMessage()`
- [ ] Wire up verification in `mesh.cpp:handleReceivedPacket()`
- [ ] Add `FLAG_SIGNED` to packet headers
- [ ] Reject unsigned packets in secure mode
- [ ] Log signature verification failures

### 1.3 Encryption Visibility
**Priority**: HIGH
**Location**: `firmware/src/protocol/protocol.h:26`
**Issue**: `FLAG_ENCRYPTED` defined but never set - users can't verify encryption

**Tasks**:
- [ ] Set `FLAG_ENCRYPTED` when encrypting packets
- [ ] Display `[ENCRYPTED]` indicator on serial TX/RX
- [ ] Add crypto stats: encryptions/decryptions succeeded/failed
- [ ] Add `crypto status` serial command
- [ ] Send encryption status to iOS app via BLE

### 1.4 Network ID Implementation
**Priority**: HIGH
**Location**: `firmware/src/protocol/protocol.h:40-53`
**Issue**: No network ID in packet headers - can't isolate networks

**Tasks**:
- [ ] Add `network_id` field to packet header
- [ ] Derive network ID from PSK hash
- [ ] Filter packets by network ID
- [ ] Display network ID in status output

---

## Phase 2: Reliability (TCP/IP-Like)
**Status**: [ ] Not Started | [ ] In Progress | [ ] Complete

### 2.1 Exponential Backoff
**Priority**: HIGH
**Location**: `firmware/src/mesh/mesh.cpp:1067-1100`
**Issue**: Fixed 5s timeout with immediate retry causes network congestion

**Tasks**:
- [ ] Implement exponential backoff: 5s, 10s, 20s, 40s
- [ ] Add random jitter (0-500ms) to prevent synchronized retries
- [ ] Cap maximum timeout at 60s
- [ ] Track retry statistics

### 2.2 Adaptive RTT Estimation
**Priority**: MEDIUM
**Location**: `firmware/src/mesh/mesh.cpp`
**Issue**: Fixed timeout regardless of actual network conditions

**Tasks**:
- [ ] Measure RTT on each ACK
- [ ] Implement TCP-style SRTT calculation
- [ ] Calculate RTO = SRTT + 4*RTTVAR
- [ ] Store per-destination RTT metrics
- [ ] Display RTT in neighbor/route tables

### 2.3 Flow Control
**Priority**: MEDIUM
**Location**: `firmware/src/mesh/mesh.cpp:103-166`
**Issue**: No transmit window - can't throttle sender

**Tasks**:
- [ ] Implement sliding window (4 outstanding packets)
- [ ] Add `canSendMore()` check before transmission
- [ ] Implement backpressure to application layer
- [ ] Track window utilization statistics

### 2.4 Receiver Duplicate Detection
**Priority**: MEDIUM
**Location**: `firmware/src/mesh/mesh.cpp`
**Issue**: Deduplication only for forwarding, not for receiver

**Tasks**:
- [ ] Track received packet IDs per source
- [ ] Re-ACK duplicates without processing
- [ ] Implement circular buffer for recent IDs
- [ ] Auto-expire old entries

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
**Status**: [ ] Not Started | [ ] In Progress | [ ] Complete

### 3.1 Proactive Route Maintenance
**Priority**: MEDIUM
**Location**: `firmware/src/mesh/mesh.cpp:886-899`
**Issue**: Routes only refreshed on-demand - causes latency spikes

**Tasks**:
- [ ] Add route refresh at 4 minutes (before 5-min timeout)
- [ ] Send lightweight HELLO to next-hop
- [ ] Update route timestamp on HELLO response
- [ ] Track route freshness in status output

### 3.2 Neighbor Liveness Monitoring
**Priority**: HIGH
**Location**: `firmware/src/mesh/mesh.cpp`
**Issue**: No mechanism to detect broken links until packet loss

**Tasks**:
- [ ] Track `last_heard` timestamp per neighbor
- [ ] Implement neighbor timeout (60 seconds)
- [ ] Proactively invalidate routes through dead neighbors
- [ ] Generate ROUTE_ERROR for affected destinations
- [ ] Display neighbor liveness in status

### 3.3 Multipath Routing
**Priority**: MEDIUM
**Location**: `firmware/src/mesh/mesh.cpp:785-803`
**Issue**: Only single route per destination - no backup paths

**Tasks**:
- [ ] Store multiple routes per destination (up to 3)
- [ ] Mark primary vs backup routes
- [ ] Implement route quality scoring
- [ ] Automatic failover to backup on primary failure
- [ ] Display all routes in routing table

### 3.4 Network Partition Detection
**Priority**: LOW
**Location**: `firmware/src/mesh/mesh.cpp`
**Issue**: No mechanism to detect or heal network splits

**Tasks**:
- [ ] Calculate topology hash from neighbor set
- [ ] Periodically broadcast topology summary
- [ ] Detect partition when hash changes unexpectedly
- [ ] Trigger aggressive route discovery on partition
- [ ] Log partition events

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
| 1.1 | Configurable PSK | ✅ Complete | - |
| 1.2 | Packet Signing | In Progress | - |
| 1.3 | Encryption Visibility | Not Started | - |
| 1.4 | Network ID | Not Started | - |
| 2.1 | Exponential Backoff | Not Started | - |
| 2.2 | Adaptive RTT | Not Started | - |
| 2.3 | Flow Control | Not Started | - |
| 2.4 | Receiver Dedup | Not Started | - |
| 2.5 | Memory Optimization | Not Started | - |
| 3.1 | Proactive Routes | Not Started | - |
| 3.2 | Neighbor Liveness | Not Started | - |
| 3.3 | Multipath Routing | Not Started | - |
| 3.4 | Partition Detection | Not Started | - |
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

*Last Updated: 2024-12-12*
