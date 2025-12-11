/**
 * @file dtn.h
 * @brief LNK-22 Delay-Tolerant Networking (DTN) Bundle Protocol
 *
 * Implements a simplified Bundle Protocol for challenged networks:
 * - Bundle fragmentation and reassembly
 * - Custody transfer
 * - Epidemic routing option
 * - Time-based expiration
 * - Opportunistic forwarding
 */

#ifndef DTN_H
#define DTN_H

#include <Arduino.h>
#include "../config.h"

// DTN Configuration
#define DTN_MAX_BUNDLES 16           // Maximum bundles in transit
#define DTN_MAX_BUNDLE_SIZE 512      // Max bundle payload size
#define DTN_DEFAULT_TTL 86400000     // 24 hour default TTL (ms)
#define DTN_FRAGMENT_SIZE 200        // Fragment size for large bundles
#define DTN_MAX_FRAGMENTS 8          // Max fragments per bundle
#define DTN_CUSTODY_TIMEOUT 60000    // Wait 60s for custody ACK
#define DTN_MAX_COPIES 3             // Max epidemic copies

// Bundle flags
#define BUNDLE_FLAG_FRAGMENT     0x01   // This is a fragment
#define BUNDLE_FLAG_CUSTODY      0x02   // Request custody transfer
#define BUNDLE_FLAG_PRIORITY     0x04   // High priority bundle
#define BUNDLE_FLAG_REPORT_DELIV 0x08   // Report on delivery
#define BUNDLE_FLAG_EPIDEMIC     0x10   // Use epidemic routing

// Bundle class of service
enum BundlePriority {
    BUNDLE_BULK = 0,        // Best effort, low priority
    BUNDLE_NORMAL = 1,      // Normal priority
    BUNDLE_EXPEDITED = 2,   // High priority
    BUNDLE_EMERGENCY = 3    // Emergency traffic
};

// Bundle status
enum BundleStatus {
    BUNDLE_EMPTY = 0,
    BUNDLE_PENDING,         // Waiting to be sent
    BUNDLE_IN_TRANSIT,      // Being transmitted
    BUNDLE_CUSTODY_WAIT,    // Waiting for custody ACK
    BUNDLE_DELIVERED,       // Successfully delivered
    BUNDLE_EXPIRED,         // TTL expired
    BUNDLE_FAILED           // Delivery failed
};

// Bundle header (simplified Bundle Protocol)
struct __attribute__((packed)) BundleHeader {
    uint32_t bundleId;       // Unique bundle ID
    uint32_t source;         // Source EID (node address)
    uint32_t destination;    // Destination EID
    uint32_t custodian;      // Current custodian
    uint32_t creationTime;   // Bundle creation timestamp
    uint32_t ttl;            // Time to live (ms from creation)
    uint16_t payloadLength;  // Total payload length
    uint8_t flags;           // Bundle flags
    uint8_t priority;        // Class of service
    uint8_t fragmentOffset;  // Fragment offset (if fragmented)
    uint8_t fragmentCount;   // Total fragments
    uint8_t hopCount;        // Number of hops traversed
    uint8_t maxHops;         // Maximum hops allowed
};

// Full bundle structure (in memory)
struct Bundle {
    BundleHeader header;
    uint8_t payload[DTN_MAX_BUNDLE_SIZE];
    uint32_t receivedAt;             // When we received it
    uint32_t lastForwardAttempt;     // Last forward attempt
    uint8_t forwardCount;            // Times forwarded (epidemic)
    uint8_t fragmentsReceived;       // For reassembly
    uint8_t fragmentMask;            // Which fragments received
    BundleStatus status;
    bool valid;
};

// Custody transfer report
struct __attribute__((packed)) CustodySignal {
    uint32_t bundleId;
    uint32_t custodian;      // New custodian
    uint8_t accepted;        // 1 = custody accepted, 0 = refused
    uint8_t reason;          // Reason code if refused
};

// DTN statistics
struct DTNStats {
    uint32_t bundlesCreated;
    uint32_t bundlesReceived;
    uint32_t bundlesForwarded;
    uint32_t bundlesDelivered;
    uint32_t bundlesExpired;
    uint32_t custodyTransfers;
    uint32_t fragmentsSent;
    uint32_t fragmentsReceived;
};

// Callbacks
typedef void (*BundleDeliveredCallback)(uint32_t bundleId, const uint8_t* payload, uint16_t length);
typedef void (*BundleStatusCallback)(uint32_t bundleId, BundleStatus status);

class DTNManager {
public:
    DTNManager();

    /**
     * @brief Initialize DTN subsystem
     * @param nodeAddr This node's address (EID)
     */
    void begin(uint32_t nodeAddr);

    /**
     * @brief Update - call from main loop
     */
    void update();

    /**
     * @brief Create and queue a new bundle
     * @param destination Destination EID
     * @param payload Data payload
     * @param length Payload length
     * @param priority Bundle priority
     * @param ttl Time to live in ms (0 for default)
     * @param flags Bundle flags
     * @return Bundle ID or 0 on failure
     */
    uint32_t createBundle(uint32_t destination, const uint8_t* payload, uint16_t length,
                          BundlePriority priority = BUNDLE_NORMAL,
                          uint32_t ttl = 0, uint8_t flags = 0);

    /**
     * @brief Handle incoming bundle/fragment
     * @param fromAddress Source of transmission
     * @param data Bundle data
     * @param length Data length
     * @return true if processed successfully
     */
    bool handleIncoming(uint32_t fromAddress, const uint8_t* data, uint16_t length);

    /**
     * @brief Handle custody signal
     * @param signal Custody transfer signal
     */
    void handleCustodySignal(const CustodySignal* signal);

    /**
     * @brief Check if we have bundles for a reachable node
     * @param peerAddress Newly reachable peer
     */
    void onPeerDiscovered(uint32_t peerAddress);

    /**
     * @brief Get bundle by ID
     * @param bundleId Bundle to find
     * @return Pointer to bundle or nullptr
     */
    Bundle* getBundleById(uint32_t bundleId);

    /**
     * @brief Get pending bundle count
     */
    uint8_t getPendingCount();

    /**
     * @brief Get total bundle count
     */
    uint8_t getTotalBundles();

    /**
     * @brief Clear expired bundles
     */
    void cleanup();

    /**
     * @brief Clear all bundles
     */
    void clear();

    /**
     * @brief Get statistics
     */
    DTNStats getStats() const { return stats; }

    /**
     * @brief Print status
     */
    void printStatus();

    /**
     * @brief Set callbacks
     */
    void setDeliveryCallback(BundleDeliveredCallback cb) { onDelivered = cb; }
    void setStatusCallback(BundleStatusCallback cb) { onStatus = cb; }

    /**
     * @brief Set send function
     */
    void setSendFunction(bool (*sendFn)(uint32_t dest, const uint8_t* data, uint16_t len));

    /**
     * @brief Set reachability check function
     */
    void setReachableFunction(bool (*reachFn)(uint32_t dest));

    /**
     * @brief Enable/disable epidemic routing
     */
    void setEpidemicMode(bool enabled) { epidemicEnabled = enabled; }

private:
    Bundle bundles[DTN_MAX_BUNDLES];
    uint32_t nodeAddress;
    uint32_t nextBundleId;
    bool epidemicEnabled;

    DTNStats stats;

    BundleDeliveredCallback onDelivered;
    BundleStatusCallback onStatus;
    bool (*sendPacket)(uint32_t, const uint8_t*, uint16_t);
    bool (*isReachable)(uint32_t);

    // Bundle management
    int findEmptySlot();
    int findBundleById(uint32_t bundleId);
    int findBundleForDest(uint32_t destination);

    // Processing
    bool processLocalDelivery(Bundle* bundle);
    bool forwardBundle(Bundle* bundle);
    bool sendCustodySignal(uint32_t to, uint32_t bundleId, bool accepted, uint8_t reason);

    // Fragmentation
    bool sendFragmented(Bundle* bundle);
    bool reassembleFragment(const BundleHeader* header, const uint8_t* payload);

    // Maintenance
    void checkExpired();
    void retryPending();
};

// Global instance
extern DTNManager dtnManager;

#endif // DTN_H
