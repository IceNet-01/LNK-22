/**
 * @file mac_hybrid.h
 * @brief LNK-22 Hybrid TDMA/CSMA-CA MAC Layer
 *
 * Implements a sophisticated channel access protocol combining:
 * - TDMA (Time Division Multiple Access) for scheduled slots
 * - CSMA-CA for random access with backoff
 * - Time source election for network-wide synchronization
 *
 * Inspired by Link-16 and tactical mesh networking protocols.
 *
 * Features:
 * - Dynamic slot allocation based on traffic
 * - Automatic time sync across mesh nodes
 * - GPS-disciplined timing when available
 * - Graceful fallback to crystal oscillator
 * - Adaptive between TDMA and CSMA modes
 */

#ifndef MAC_HYBRID_H
#define MAC_HYBRID_H

#include <Arduino.h>
#include "../protocol/protocol.h"

// =============================================================================
// Time Slot Configuration
// =============================================================================

// Frame duration in milliseconds
#define FRAME_DURATION_MS 1000      // 1 second frame

// Number of slots per frame
#define SLOTS_PER_FRAME 10          // 10 slots = 100ms each

// Slot duration in milliseconds
#define SLOT_DURATION_MS (FRAME_DURATION_MS / SLOTS_PER_FRAME)  // 100ms

// Guard time between slots (accounts for propagation delay)
#define GUARD_TIME_MS 5             // 5ms guard

// Usable slot time after guard
#define USABLE_SLOT_MS (SLOT_DURATION_MS - GUARD_TIME_MS)  // 95ms

// Maximum time error before resync (microseconds)
#define MAX_TIME_ERROR_US 5000      // 5ms

// =============================================================================
// Time Source Types and Priorities
// =============================================================================

// Time source types (higher value = higher priority)
typedef enum {
    TIME_SOURCE_CRYSTAL = 0,    // Local crystal oscillator (lowest priority)
    TIME_SOURCE_SYNCED  = 1,    // Synced from another node
    TIME_SOURCE_SERIAL  = 2,    // Synced from host computer via serial
    TIME_SOURCE_NTP     = 3,    // NTP synchronized (if network available)
    TIME_SOURCE_GPS     = 4     // GPS PPS (highest priority)
} TimeSourceType;

// Time source election message
struct __attribute__((packed)) TimeSyncMessage {
    uint32_t timestamp_sec;     // Unix timestamp seconds
    uint32_t timestamp_usec;    // Microseconds within second
    uint8_t  source_type;       // TimeSourceType
    uint8_t  hop_count;         // Hops from original source
    uint8_t  stratum;           // Like NTP stratum (0 = ref clock)
    uint8_t  reserved;
    uint32_t source_node;       // Node that originated this time
    int32_t  offset_us;         // Estimated offset from true time
};

// =============================================================================
// TDMA Slot Types
// =============================================================================

typedef enum {
    SLOT_TYPE_FREE      = 0,    // Slot available for CSMA
    SLOT_TYPE_RESERVED  = 1,    // Reserved for this node (TDMA)
    SLOT_TYPE_PEER      = 2,    // Reserved by another node
    SLOT_TYPE_BEACON    = 3,    // Network beacon slot
    SLOT_TYPE_CONTENTION= 4     // Contention-based access slot
} SlotType;

// Slot allocation entry
struct SlotAllocation {
    SlotType type;
    uint32_t owner;             // Node that owns this slot
    uint32_t expires;           // When allocation expires (millis)
    uint8_t  priority;          // Priority level
    bool     valid;
};

// =============================================================================
// CSMA-CA Parameters
// =============================================================================

// Minimum backoff window (slots)
#define CSMA_MIN_BACKOFF 1

// Maximum backoff window (slots)
#define CSMA_MAX_BACKOFF 32

// Clear Channel Assessment duration (ms)
#define CCA_DURATION_MS 10

// RSSI threshold for channel busy (dBm)
#define CCA_THRESHOLD_DBM -90

// =============================================================================
// MAC State
// =============================================================================

typedef enum {
    MAC_STATE_IDLE,             // Not transmitting or receiving
    MAC_STATE_CCA,              // Performing clear channel assessment
    MAC_STATE_BACKOFF,          // Waiting for backoff to expire
    MAC_STATE_TX,               // Transmitting
    MAC_STATE_RX,               // Receiving
    MAC_STATE_SYNC              // Synchronizing time
} MACState;

// =============================================================================
// Statistics
// =============================================================================

struct MACStats {
    uint32_t tdmaTransmissions;
    uint32_t csmaTransmissions;
    uint32_t collisions;
    uint32_t ccaBusy;
    uint32_t backoffs;
    uint32_t timeSyncs;
    uint32_t slotAllocations;
    uint32_t slotExpirations;
    int32_t  avgTimeError;      // Average time error in microseconds
};

// =============================================================================
// Hybrid MAC Class
// =============================================================================

class HybridMAC {
public:
    HybridMAC();

    /**
     * @brief Initialize the hybrid MAC layer
     * @param nodeAddr This node's mesh address
     * @return true if successful
     */
    bool begin(uint32_t nodeAddr);

    /**
     * @brief Update loop - call frequently from main loop
     * Must be called at least once per slot
     */
    void update();

    /**
     * @brief Set time source (call when GPS fix changes, etc)
     * @param type Time source type
     * @param timestamp_sec Current Unix timestamp seconds
     * @param timestamp_usec Microseconds within second
     */
    void setTimeSource(TimeSourceType type, uint32_t timestamp_sec, uint32_t timestamp_usec);

    /**
     * @brief Request transmission of a packet
     * @param packet Packet to transmit
     * @param priority Priority level (higher = more important)
     * @return true if packet queued for transmission
     */
    bool queueTransmit(const Packet* packet, uint8_t priority = 0);

    /**
     * @brief Reserve a slot for regular transmissions
     * @param slotIndex Slot index (0 to SLOTS_PER_FRAME-1)
     * @return true if slot reserved
     */
    bool reserveSlot(uint8_t slotIndex);

    /**
     * @brief Release a previously reserved slot
     * @param slotIndex Slot index
     */
    void releaseSlot(uint8_t slotIndex);

    /**
     * @brief Get current slot index in frame
     */
    uint8_t getCurrentSlot() const;

    /**
     * @brief Get current frame number
     */
    uint32_t getCurrentFrame() const;

    /**
     * @brief Get time until next slot boundary
     */
    uint32_t getTimeToNextSlot() const;

    /**
     * @brief Get MAC statistics
     */
    const MACStats& getStats() const { return _stats; }

    /**
     * @brief Get current time source
     */
    TimeSourceType getTimeSource() const { return _currentTimeSource; }

    /**
     * @brief Get time quality (0-100, higher is better)
     */
    uint8_t getTimeQuality() const;

    /**
     * @brief Print status to Serial
     */
    void printStatus();

    /**
     * @brief Enable/disable TDMA mode (CSMA-only if disabled)
     */
    void setTDMAEnabled(bool enabled) { _tdmaEnabled = enabled; }

    /**
     * @brief Check if we have a usable time reference
     */
    bool isTimeSynced() const;

    /**
     * @brief Process received time sync message
     */
    void handleTimeSyncMessage(const TimeSyncMessage* msg, int16_t rssi);

    /**
     * @brief Broadcast time sync message
     */
    void broadcastTimeSync();

private:
    uint32_t _nodeAddr;
    bool _initialized;
    bool _tdmaEnabled;

    // Time synchronization
    TimeSourceType _currentTimeSource;
    uint32_t _refTimestamp;         // Reference Unix timestamp
    uint32_t _refMicros;            // micros() at reference point
    uint32_t _lastSyncTime;         // Last time we synced
    int32_t _timeOffset;            // Offset from reference in microseconds
    uint8_t _stratum;               // Our stratum level

    // Frame and slot tracking
    uint32_t _frameNumber;
    uint8_t _currentSlot;
    uint32_t _slotStartTime;

    // Slot allocations
    SlotAllocation _slots[SLOTS_PER_FRAME];

    // Transmission queue
    static const int TX_QUEUE_SIZE = 8;
    struct TxQueueEntry {
        Packet packet;
        uint8_t priority;
        uint32_t queueTime;
        bool valid;
    };
    TxQueueEntry _txQueue[TX_QUEUE_SIZE];

    // CSMA-CA state
    MACState _state;
    uint8_t _backoffWindow;
    uint8_t _backoffCounter;
    uint8_t _retryCount;

    // Statistics
    MACStats _stats;

    // Internal methods
    void updateTimeSync();
    void updateSlots();
    void processTransmitQueue();
    bool canTransmitNow();
    bool performCCA();
    void startBackoff();
    uint32_t getCurrentTimeMicros();
    uint32_t getSlotStartTime(uint8_t slot);
    bool isInSlot(uint8_t slot);
    void electTimeSource();
    TxQueueEntry* getNextPacket();
};

// Global instance
extern HybridMAC hybridMAC;

#endif // MAC_HYBRID_H
