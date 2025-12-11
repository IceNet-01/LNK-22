/**
 * @file adaptive_datarate.h
 * @brief LNK-22 Adaptive Data Rate System
 *
 * Automatically adjusts spreading factor based on link quality to optimize
 * range vs speed tradeoff per-link.
 */

#ifndef ADAPTIVE_DATARATE_H
#define ADAPTIVE_DATARATE_H

#include <Arduino.h>
#include "../config.h"

// ADR Configuration
#define ADR_HISTORY_SIZE 8          // Number of samples to average
#define ADR_UPDATE_INTERVAL 60000   // Evaluate ADR every 60 seconds
#define ADR_HYSTERESIS_DB 5         // Hysteresis to prevent oscillation

// Spreading Factor levels
#define SF_MIN 7
#define SF_MAX 12

// Link quality thresholds for SF selection
// Higher RSSI = can use lower SF (faster)
// Lower RSSI = must use higher SF (more range)
struct ADRThreshold {
    int16_t rssiThreshold;  // RSSI must be above this
    int8_t snrThreshold;    // SNR must be above this
    uint8_t spreadingFactor;
};

// Default thresholds (can be tuned per-deployment)
const ADRThreshold ADR_THRESHOLDS[] = {
    { -70,   8, 7 },   // Excellent signal: use SF7 (fastest)
    { -85,   5, 8 },   // Good signal: use SF8
    { -100,  0, 9 },   // Moderate signal: use SF9
    { -110, -5, 10 },  // Weak signal: use SF10
    { -120, -10, 11 }, // Very weak signal: use SF11
    { -140, -15, 12 }  // Marginal signal: use SF12 (maximum range)
};
#define ADR_THRESHOLD_COUNT 6

// Per-link ADR state
struct LinkADRState {
    uint32_t peerAddress;           // Remote node address
    int16_t rssiHistory[ADR_HISTORY_SIZE];
    int8_t snrHistory[ADR_HISTORY_SIZE];
    uint8_t historyIndex;
    uint8_t historyCount;
    uint8_t currentSF;              // Current spreading factor for this link
    uint8_t recommendedSF;          // Our recommended SF based on measurements
    uint8_t peerPreferredSF;        // Peer's advertised preferred SF
    uint8_t negotiatedSF;           // Agreed SF for this link (max of both)
    uint32_t lastUpdate;            // Last time we evaluated this link
    uint32_t lastPeerUpdate;        // Last time we heard peer's SF preference
    uint32_t packetsSuccess;        // Successful packet count
    uint32_t packetsTotal;          // Total packet attempts
    bool peerSFKnown;               // Have we received peer's SF preference?
    bool valid;
};

// ADR Advertisement (included in beacons)
struct __attribute__((packed)) ADRAdvertisement {
    uint8_t preferredSF;            // Our preferred receiving SF
    uint8_t minSF;                  // Minimum SF we can receive
    uint8_t maxSF;                  // Maximum SF we can receive
    uint8_t txPower;                // Our current TX power
};

// ADR Statistics
struct ADRStats {
    uint32_t sfChanges;
    uint32_t packetsAtSF7;
    uint32_t packetsAtSF8;
    uint32_t packetsAtSF9;
    uint32_t packetsAtSF10;
    uint32_t packetsAtSF11;
    uint32_t packetsAtSF12;
};

class AdaptiveDataRate {
public:
    AdaptiveDataRate();

    /**
     * @brief Initialize ADR system
     * @param defaultSF Default spreading factor (from config)
     */
    void begin(uint8_t defaultSF = LORA_SPREADING_FACTOR);

    /**
     * @brief Record a received packet's signal quality
     * @param peerAddress Address of the sender
     * @param rssi Received signal strength
     * @param snr Signal-to-noise ratio
     * @param success Whether packet was received successfully
     */
    void recordRx(uint32_t peerAddress, int16_t rssi, int8_t snr, bool success = true);

    /**
     * @brief Record a transmitted packet result
     * @param peerAddress Destination address
     * @param success Whether ACK was received (if applicable)
     */
    void recordTx(uint32_t peerAddress, bool success);

    /**
     * @brief Get recommended SF for a peer
     * @param peerAddress Remote node address
     * @return Recommended spreading factor (7-12)
     */
    uint8_t getRecommendedSF(uint32_t peerAddress);

    /**
     * @brief Get current SF for a peer
     * @param peerAddress Remote node address
     * @return Current spreading factor being used
     */
    uint8_t getCurrentSF(uint32_t peerAddress);

    /**
     * @brief Get negotiated SF for sending to a peer
     * @param peerAddress Remote node address
     * @return Negotiated SF (considers both our recommendation and peer's preference)
     */
    uint8_t getNegotiatedSF(uint32_t peerAddress);

    /**
     * @brief Record peer's SF preference (from beacon/hello)
     * @param peerAddress Remote node address
     * @param preferredSF Peer's advertised preferred SF
     */
    void recordPeerSF(uint32_t peerAddress, uint8_t preferredSF);

    /**
     * @brief Get our ADR advertisement data (for including in beacons)
     */
    ADRAdvertisement getAdvertisement();

    /**
     * @brief Get the global default SF
     */
    uint8_t getDefaultSF() const { return defaultSF; }

    /**
     * @brief Set the global default SF
     */
    void setDefaultSF(uint8_t sf);

    /**
     * @brief Periodic update - evaluates all links and adjusts SF
     */
    void update();

    /**
     * @brief Get link quality for a peer (0-100%)
     */
    uint8_t getLinkQuality(uint32_t peerAddress);

    /**
     * @brief Get average RSSI for a peer
     */
    int16_t getAverageRSSI(uint32_t peerAddress);

    /**
     * @brief Get average SNR for a peer
     */
    int8_t getAverageSNR(uint32_t peerAddress);

    /**
     * @brief Get statistics
     */
    ADRStats getStats() const { return stats; }

    /**
     * @brief Print ADR status to serial
     */
    void printStatus();

    /**
     * @brief Clear all link states
     */
    void clear();

    /**
     * @brief Enable/disable ADR globally
     */
    void setEnabled(bool enabled) { adrEnabled = enabled; }
    bool isEnabled() const { return adrEnabled; }

    // === Network Discovery / SF Scanning ===

    /**
     * @brief Start SF scan to discover active networks
     * Scans all spreading factors to find active traffic
     * @param durationPerSF How long to listen on each SF (ms)
     */
    void startSFScan(uint32_t durationPerSF = 5000);

    /**
     * @brief Check if SF scan is in progress
     */
    bool isScanInProgress() const { return scanInProgress; }

    /**
     * @brief Update scan progress (call from loop during scan)
     * @return true if scan complete
     */
    bool updateScan();

    /**
     * @brief Get scan results
     */
    struct ScanResult {
        uint8_t sf;
        uint8_t packetsHeard;
        int16_t bestRssi;
        int8_t bestSnr;
        bool active;
    };
    ScanResult getScanResult(uint8_t sf);

    /**
     * @brief Get best SF from scan results
     * @return SF with most activity, or defaultSF if none found
     */
    uint8_t getBestSFFromScan();

    /**
     * @brief Callback for when a packet is received during scan
     */
    void onScanPacketReceived(uint8_t sf, int16_t rssi, int8_t snr);

    /**
     * @brief Set callback for SF changes (called during scan)
     */
    void setSFChangeCallback(void (*callback)(uint8_t newSF)) { sfChangeCallback = callback; }

private:
    static const int MAX_LINKS = 16;

    LinkADRState links[MAX_LINKS];
    ADRStats stats;
    uint8_t defaultSF;
    bool adrEnabled;
    uint32_t lastGlobalUpdate;

    // SF Scan state
    bool scanInProgress;
    uint8_t scanCurrentSF;
    uint32_t scanStartTime;
    uint32_t scanDurationPerSF;
    ScanResult scanResults[6];  // SF7-SF12

    // Callback to change radio SF
    void (*sfChangeCallback)(uint8_t newSF);

    int findLink(uint32_t peerAddress);
    int findOrCreateLink(uint32_t peerAddress);
    uint8_t calculateSF(int16_t avgRssi, int8_t avgSnr);
    void evaluateLink(LinkADRState* link);
};

// Global instance
extern AdaptiveDataRate adaptiveDataRate;

#endif // ADAPTIVE_DATARATE_H
