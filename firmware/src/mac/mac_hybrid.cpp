/**
 * @file mac_hybrid.cpp
 * @brief LNK-22 Hybrid TDMA/CSMA-CA MAC Layer Implementation
 *
 * Implements Link-16 inspired channel access with time synchronization.
 */

#include "mac_hybrid.h"
#include "../radio/radio.h"
#include "../config.h"

// Global instance
HybridMAC hybridMAC;

HybridMAC::HybridMAC()
    : _nodeAddr(0)
    , _initialized(false)
    , _tdmaEnabled(true)
    , _currentTimeSource(TIME_SOURCE_CRYSTAL)
    , _refTimestamp(0)
    , _refMicros(0)
    , _lastSyncTime(0)
    , _timeOffset(0)
    , _stratum(15)  // Start at max stratum (unsynced)
    , _frameNumber(0)
    , _currentSlot(0)
    , _slotStartTime(0)
    , _state(MAC_STATE_IDLE)
    , _backoffWindow(CSMA_MIN_BACKOFF)
    , _backoffCounter(0)
    , _retryCount(0)
{
    // Initialize slots
    for (int i = 0; i < SLOTS_PER_FRAME; i++) {
        _slots[i].type = SLOT_TYPE_FREE;
        _slots[i].owner = 0;
        _slots[i].expires = 0;
        _slots[i].priority = 0;
        _slots[i].valid = false;
    }

    // Initialize TX queue
    for (int i = 0; i < TX_QUEUE_SIZE; i++) {
        _txQueue[i].valid = false;
    }

    // Initialize stats
    memset(&_stats, 0, sizeof(_stats));
}

bool HybridMAC::begin(uint32_t nodeAddr) {
    _nodeAddr = nodeAddr;

    // Calculate initial frame/slot based on system time
    uint32_t now = millis();
    _frameNumber = now / FRAME_DURATION_MS;
    _currentSlot = (now % FRAME_DURATION_MS) / SLOT_DURATION_MS;
    _slotStartTime = (now / SLOT_DURATION_MS) * SLOT_DURATION_MS;

    // Reserve a slot based on our address (simple hash)
    uint8_t preferredSlot = (_nodeAddr % (SLOTS_PER_FRAME - 1)) + 1;  // Skip slot 0 (beacon)
    _slots[preferredSlot].type = SLOT_TYPE_RESERVED;
    _slots[preferredSlot].owner = _nodeAddr;
    _slots[preferredSlot].expires = 0;  // Never expires (our slot)
    _slots[preferredSlot].valid = true;

    // Slot 0 is always beacon/contention
    _slots[0].type = SLOT_TYPE_BEACON;
    _slots[0].valid = true;

    _initialized = true;

    Serial.println("[MAC] Hybrid TDMA/CSMA-CA initialized");
    Serial.print("[MAC] Node slot: ");
    Serial.println(preferredSlot);
    Serial.print("[MAC] Frame duration: ");
    Serial.print(FRAME_DURATION_MS);
    Serial.print("ms, Slots: ");
    Serial.println(SLOTS_PER_FRAME);

    return true;
}

void HybridMAC::update() {
    if (!_initialized) return;

    uint32_t now = millis();

    // Update frame and slot tracking
    uint32_t newFrame = now / FRAME_DURATION_MS;
    uint8_t newSlot = (now % FRAME_DURATION_MS) / SLOT_DURATION_MS;

    // Frame transition
    if (newFrame != _frameNumber) {
        _frameNumber = newFrame;
        updateSlots();  // Check slot expirations
    }

    // Slot transition
    if (newSlot != _currentSlot) {
        _currentSlot = newSlot;
        _slotStartTime = now;

        // Reset state on slot boundary
        if (_state == MAC_STATE_BACKOFF) {
            _backoffCounter--;
        }
    }

    // Update time synchronization
    updateTimeSync();

    // Process transmit queue
    processTransmitQueue();
}

void HybridMAC::setTimeSource(TimeSourceType type, uint32_t timestamp_sec, uint32_t timestamp_usec) {
    // Only accept if better than current source
    if (type >= _currentTimeSource) {
        _currentTimeSource = type;
        _refTimestamp = timestamp_sec;
        _refMicros = micros();
        _timeOffset = timestamp_usec;
        _lastSyncTime = millis();

        // Update stratum based on source
        switch (type) {
            case TIME_SOURCE_GPS:
                _stratum = 1;
                break;
            case TIME_SOURCE_NTP:
                _stratum = 2;
                break;
            case TIME_SOURCE_SERIAL:
                _stratum = 3;  // Host computer time (usually NTP-synced)
                break;
            case TIME_SOURCE_SYNCED:
                _stratum = min(_stratum, (uint8_t)14);
                break;
            default:
                _stratum = 15;
                break;
        }

        _stats.timeSyncs++;

        #if DEBUG_MESH
        Serial.print("[MAC] Time source updated: ");
        Serial.print(type);
        Serial.print(" stratum: ");
        Serial.println(_stratum);
        #endif
    }
}

bool HybridMAC::queueTransmit(const Packet* packet, uint8_t priority) {
    // Find empty queue slot
    for (int i = 0; i < TX_QUEUE_SIZE; i++) {
        if (!_txQueue[i].valid) {
            memcpy(&_txQueue[i].packet, packet, sizeof(Packet));
            _txQueue[i].priority = priority;
            _txQueue[i].queueTime = millis();
            _txQueue[i].valid = true;
            return true;
        }
    }

    // Queue full
    return false;
}

bool HybridMAC::reserveSlot(uint8_t slotIndex) {
    if (slotIndex >= SLOTS_PER_FRAME) return false;

    // Check if slot is available
    if (_slots[slotIndex].type == SLOT_TYPE_FREE ||
        (_slots[slotIndex].type == SLOT_TYPE_PEER && _slots[slotIndex].expires < millis())) {

        _slots[slotIndex].type = SLOT_TYPE_RESERVED;
        _slots[slotIndex].owner = _nodeAddr;
        _slots[slotIndex].expires = 0;  // Our slots don't expire
        _slots[slotIndex].valid = true;

        _stats.slotAllocations++;
        return true;
    }

    return false;
}

void HybridMAC::releaseSlot(uint8_t slotIndex) {
    if (slotIndex >= SLOTS_PER_FRAME) return;

    if (_slots[slotIndex].type == SLOT_TYPE_RESERVED &&
        _slots[slotIndex].owner == _nodeAddr) {

        _slots[slotIndex].type = SLOT_TYPE_FREE;
        _slots[slotIndex].valid = false;
    }
}

uint8_t HybridMAC::getCurrentSlot() const {
    return _currentSlot;
}

uint32_t HybridMAC::getCurrentFrame() const {
    return _frameNumber;
}

uint32_t HybridMAC::getTimeToNextSlot() const {
    uint32_t now = millis();
    uint32_t slotEnd = _slotStartTime + SLOT_DURATION_MS;
    if (now >= slotEnd) return 0;
    return slotEnd - now;
}

uint8_t HybridMAC::getTimeQuality() const {
    // Quality based on source type and age of sync
    uint8_t baseQuality = 0;
    switch (_currentTimeSource) {
        case TIME_SOURCE_GPS:     baseQuality = 100; break;
        case TIME_SOURCE_NTP:     baseQuality = 90; break;
        case TIME_SOURCE_SERIAL:  baseQuality = 80; break;
        case TIME_SOURCE_SYNCED:  baseQuality = 60; break;
        case TIME_SOURCE_CRYSTAL: baseQuality = 20; break;
        default:                  baseQuality = 20; break;
    }

    // Degrade quality based on time since last sync
    uint32_t sinceSyncMs = millis() - _lastSyncTime;
    uint32_t degradation = sinceSyncMs / 60000;  // 1% per minute
    if (degradation > baseQuality) return 0;
    return baseQuality - degradation;
}

bool HybridMAC::isTimeSynced() const {
    // Consider synced if we have a non-crystal source and it's recent
    if (_currentTimeSource == TIME_SOURCE_CRYSTAL) return false;

    uint32_t sinceSyncMs = millis() - _lastSyncTime;
    return sinceSyncMs < 300000;  // 5 minutes
}

void HybridMAC::printStatus() {
    Serial.println("\n=== Hybrid MAC Status ===");
    Serial.print("TDMA Enabled: ");
    Serial.println(_tdmaEnabled ? "YES" : "NO");
    Serial.print("Current Frame: ");
    Serial.println(_frameNumber);
    Serial.print("Current Slot: ");
    Serial.print(_currentSlot);
    Serial.print("/");
    Serial.println(SLOTS_PER_FRAME);

    // Time source info
    Serial.print("Time Source: ");
    switch (_currentTimeSource) {
        case TIME_SOURCE_GPS:     Serial.print("GPS"); break;
        case TIME_SOURCE_NTP:     Serial.print("NTP"); break;
        case TIME_SOURCE_SERIAL:  Serial.print("SERIAL"); break;
        case TIME_SOURCE_SYNCED:  Serial.print("SYNCED"); break;
        case TIME_SOURCE_CRYSTAL: Serial.print("CRYSTAL"); break;
        default:                  Serial.print("UNKNOWN"); break;
    }
    Serial.print(" (stratum ");
    Serial.print(_stratum);
    Serial.print(", quality ");
    Serial.print(getTimeQuality());
    Serial.println("%)");

    // Slot allocations
    Serial.println("\nSlot Allocations:");
    for (int i = 0; i < SLOTS_PER_FRAME; i++) {
        Serial.print("  [");
        Serial.print(i);
        Serial.print("] ");
        switch (_slots[i].type) {
            case SLOT_TYPE_FREE:       Serial.print("FREE"); break;
            case SLOT_TYPE_RESERVED:   Serial.print("RESERVED"); break;
            case SLOT_TYPE_PEER:       Serial.print("PEER"); break;
            case SLOT_TYPE_BEACON:     Serial.print("BEACON"); break;
            case SLOT_TYPE_CONTENTION: Serial.print("CONTENTION"); break;
        }
        if (_slots[i].type == SLOT_TYPE_RESERVED || _slots[i].type == SLOT_TYPE_PEER) {
            Serial.print(" (0x");
            Serial.print(_slots[i].owner, HEX);
            Serial.print(")");
        }
        if (i == _currentSlot) {
            Serial.print(" <-- CURRENT");
        }
        Serial.println();
    }

    // Statistics
    Serial.println("\nStatistics:");
    Serial.print("  TDMA TX: ");
    Serial.println(_stats.tdmaTransmissions);
    Serial.print("  CSMA TX: ");
    Serial.println(_stats.csmaTransmissions);
    Serial.print("  Collisions: ");
    Serial.println(_stats.collisions);
    Serial.print("  CCA Busy: ");
    Serial.println(_stats.ccaBusy);
    Serial.print("  Time Syncs: ");
    Serial.println(_stats.timeSyncs);
    Serial.println("=========================\n");
}

void HybridMAC::handleTimeSyncMessage(const TimeSyncMessage* msg, int16_t rssi) {
    if (!msg) return;

    // Only accept from better or equal source
    TimeSourceType msgSource = (TimeSourceType)msg->source_type;
    uint8_t msgStratum = msg->stratum + 1;  // Our stratum would be +1

    // Decision: accept if source is better, or same source with lower stratum
    bool accept = false;
    if (msgSource > _currentTimeSource) {
        accept = true;
    } else if (msgSource == _currentTimeSource && msgStratum < _stratum) {
        accept = true;
    }

    if (accept) {
        // Calculate our offset from this message
        uint32_t now = micros();
        uint32_t msgTime = msg->timestamp_usec;

        // Account for propagation delay (rough estimate based on RSSI)
        // Closer = stronger signal = less delay
        int32_t propDelay = 100;  // Base 100us

        _currentTimeSource = TIME_SOURCE_SYNCED;
        _refTimestamp = msg->timestamp_sec;
        _timeOffset = msg->offset_us + propDelay;
        _refMicros = now;
        _stratum = msgStratum;
        _lastSyncTime = millis();

        _stats.timeSyncs++;

        #if DEBUG_MESH
        Serial.print("[MAC] Time synced from 0x");
        Serial.print(msg->source_node, HEX);
        Serial.print(", stratum: ");
        Serial.println(_stratum);
        #endif
    }
}

void HybridMAC::broadcastTimeSync() {
    // Only broadcast if we have a good time source
    if (_currentTimeSource == TIME_SOURCE_CRYSTAL && _stratum >= 14) {
        return;  // Don't pollute network with bad time
    }

    TimeSyncMessage msg;
    msg.timestamp_sec = _refTimestamp + (millis() - _lastSyncTime) / 1000;
    msg.timestamp_usec = (micros() - _refMicros) % 1000000;
    msg.source_type = _currentTimeSource;
    msg.hop_count = 0;  // Will be incremented by receivers
    msg.stratum = _stratum;
    msg.reserved = 0;
    msg.source_node = _nodeAddr;
    msg.offset_us = _timeOffset;

    // Create packet and queue for transmission
    Packet packet;
    memset(&packet, 0, sizeof(Packet));
    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = PKT_TIME_SYNC;  // Time sync packet type (0x0A)
    packet.header.ttl = 1;  // Don't forward time syncs
    packet.header.flags = FLAG_BROADCAST;
    packet.header.source = _nodeAddr;
    packet.header.destination = 0xFFFFFFFF;
    packet.header.payload_length = sizeof(TimeSyncMessage);

    memcpy(packet.payload, &msg, sizeof(TimeSyncMessage));

    queueTransmit(&packet, 10);  // High priority
}

// ============================================================================
// Private Methods
// ============================================================================

void HybridMAC::updateTimeSync() {
    // Periodic time sync broadcast if we have a good time source
    static uint32_t lastBroadcast = 0;
    if (_currentTimeSource >= TIME_SOURCE_SERIAL) {  // GPS, NTP, or Serial
        if (millis() - lastBroadcast > 60000) {  // Every minute
            broadcastTimeSync();
            lastBroadcast = millis();
        }
    }

    // Degrade time source if too old
    if (_currentTimeSource != TIME_SOURCE_CRYSTAL) {
        if (millis() - _lastSyncTime > 600000) {  // 10 minutes
            _currentTimeSource = TIME_SOURCE_CRYSTAL;
            _stratum = 15;
            Serial.println("[MAC] Time source degraded to crystal");
        }
    }
}

void HybridMAC::updateSlots() {
    // Check for expired peer allocations
    uint32_t now = millis();
    for (int i = 0; i < SLOTS_PER_FRAME; i++) {
        if (_slots[i].type == SLOT_TYPE_PEER && _slots[i].expires > 0) {
            if (now >= _slots[i].expires) {
                _slots[i].type = SLOT_TYPE_FREE;
                _slots[i].valid = false;
                _stats.slotExpirations++;
            }
        }
    }
}

void HybridMAC::processTransmitQueue() {
    if (!canTransmitNow()) return;

    TxQueueEntry* entry = getNextPacket();
    if (!entry) return;

    // Decide: TDMA or CSMA?
    bool useTDMA = false;
    if (_tdmaEnabled && isTimeSynced()) {
        // Check if current slot is ours
        if (_slots[_currentSlot].type == SLOT_TYPE_RESERVED &&
            _slots[_currentSlot].owner == _nodeAddr) {
            useTDMA = true;
        }
    }

    if (useTDMA) {
        // TDMA: Transmit immediately in our slot
        extern Radio radio;
        if (radio.send(&entry->packet)) {
            _stats.tdmaTransmissions++;
            entry->valid = false;  // Remove from queue
        }
    } else {
        // CSMA-CA: Check channel first
        switch (_state) {
            case MAC_STATE_IDLE:
                _state = MAC_STATE_CCA;
                break;

            case MAC_STATE_CCA:
                if (performCCA()) {
                    // Channel clear, transmit
                    extern Radio radio;
                    if (radio.send(&entry->packet)) {
                        _stats.csmaTransmissions++;
                        entry->valid = false;
                        _state = MAC_STATE_IDLE;
                        _backoffWindow = CSMA_MIN_BACKOFF;  // Reset backoff
                        _retryCount = 0;
                    }
                } else {
                    // Channel busy, start backoff
                    _stats.ccaBusy++;
                    startBackoff();
                }
                break;

            case MAC_STATE_BACKOFF:
                if (_backoffCounter == 0) {
                    _state = MAC_STATE_CCA;  // Try again
                }
                break;

            default:
                break;
        }
    }
}

bool HybridMAC::canTransmitNow() {
    // Check if we're in guard time
    uint32_t slotProgress = millis() - _slotStartTime;
    if (slotProgress > USABLE_SLOT_MS) {
        return false;  // In guard time
    }

    // Check slot type
    SlotType slotType = _slots[_currentSlot].type;

    if (_tdmaEnabled && isTimeSynced()) {
        // In TDMA mode, respect slot allocations
        if (slotType == SLOT_TYPE_RESERVED && _slots[_currentSlot].owner == _nodeAddr) {
            return true;  // Our TDMA slot
        }
        if (slotType == SLOT_TYPE_PEER) {
            return false;  // Someone else's slot
        }
        if (slotType == SLOT_TYPE_BEACON && _currentSlot == 0) {
            return true;  // Beacon slot - contention OK
        }
    }

    // CSMA mode - can transmit in any non-peer slot
    return slotType != SLOT_TYPE_PEER;
}

bool HybridMAC::performCCA() {
    extern Radio radio;

    // Check RSSI
    int16_t rssi = radio.getLastRSSI();
    if (rssi > CCA_THRESHOLD_DBM) {
        return false;  // Channel busy
    }

    return true;  // Channel clear
}

void HybridMAC::startBackoff() {
    _state = MAC_STATE_BACKOFF;

    // Exponential backoff
    _backoffWindow = min(_backoffWindow * 2, (uint8_t)CSMA_MAX_BACKOFF);
    _backoffCounter = random(1, _backoffWindow);

    _stats.backoffs++;
    _retryCount++;

    if (_retryCount > 5) {
        // Too many retries, give up on this packet
        _stats.collisions++;
        _state = MAC_STATE_IDLE;
        _retryCount = 0;
        _backoffWindow = CSMA_MIN_BACKOFF;
    }
}

uint32_t HybridMAC::getCurrentTimeMicros() {
    return micros() + _timeOffset;
}

uint32_t HybridMAC::getSlotStartTime(uint8_t slot) {
    // Calculate when this slot starts in the current frame
    return (_frameNumber * FRAME_DURATION_MS) + (slot * SLOT_DURATION_MS);
}

bool HybridMAC::isInSlot(uint8_t slot) {
    return _currentSlot == slot;
}

HybridMAC::TxQueueEntry* HybridMAC::getNextPacket() {
    // Find highest priority packet
    TxQueueEntry* best = nullptr;
    uint8_t bestPriority = 0;

    for (int i = 0; i < TX_QUEUE_SIZE; i++) {
        if (_txQueue[i].valid) {
            if (!best || _txQueue[i].priority > bestPriority) {
                best = &_txQueue[i];
                bestPriority = _txQueue[i].priority;
            }
        }
    }

    return best;
}
