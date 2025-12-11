/**
 * @file emergency.h
 * @brief LNK-22 Emergency SOS System
 *
 * Provides emergency broadcast capability that overrides normal operation
 * to maximize visibility and reach in distress situations.
 */

#ifndef EMERGENCY_H
#define EMERGENCY_H

#include <Arduino.h>
#include "../config.h"
#include "../protocol/protocol.h"

// Emergency Configuration
#define SOS_BROADCAST_INTERVAL 10000   // Broadcast SOS every 10 seconds
#define SOS_MAX_DURATION 3600000       // Auto-cancel after 1 hour (ms)
#define SOS_BOOST_TX_POWER 22          // Maximum TX power during SOS
#define SOS_SPREADING_FACTOR 12        // Maximum range SF during SOS

// SOS Status flags
#define SOS_FLAG_GPS_VALID     0x01    // GPS coordinates are valid
#define SOS_FLAG_BATTERY_LOW   0x02    // Battery is critically low
#define SOS_FLAG_MEDICAL       0x04    // Medical emergency
#define SOS_FLAG_FIRE          0x08    // Fire emergency
#define SOS_FLAG_RESCUE        0x10    // Needs rescue
#define SOS_FLAG_ACKNOWLEDGED  0x80    // Someone has acknowledged

// Emergency packet type (sent as high-priority data)
struct __attribute__((packed)) SOSMessage {
    uint8_t sosType;             // Type of emergency
    uint8_t flags;               // Status flags
    int32_t latitude;            // GPS lat * 10^7 (if available)
    int32_t longitude;           // GPS lon * 10^7 (if available)
    int32_t altitude;            // Altitude in cm
    uint8_t batteryLevel;        // Battery percentage
    uint32_t timestamp;          // When SOS was initiated
    char message[64];            // Custom distress message
};

// Emergency types
enum EmergencyType {
    EMERGENCY_NONE = 0,
    EMERGENCY_GENERAL = 1,       // General distress
    EMERGENCY_MEDICAL = 2,       // Medical emergency
    EMERGENCY_FIRE = 3,          // Fire
    EMERGENCY_RESCUE = 4,        // Lost/stranded, needs rescue
    EMERGENCY_SECURITY = 5,      // Security threat
    EMERGENCY_TEST = 0xFF        // Test alert (not a real emergency)
};

// Received SOS tracking
struct ReceivedSOS {
    uint32_t nodeAddress;        // Who sent the SOS
    SOSMessage message;          // Their SOS message
    int16_t rssi;                // Signal strength
    int8_t snr;                  // Signal quality
    uint32_t receivedAt;         // When we received it
    bool acknowledged;           // Have we acknowledged it
    bool valid;
};

// Emergency statistics
struct EmergencyStats {
    uint32_t sosActivations;
    uint32_t sosBroadcasts;
    uint32_t sosReceived;
    uint32_t sosAcknowledged;
};

class Emergency {
public:
    Emergency();

    /**
     * @brief Initialize emergency system
     */
    void begin();

    /**
     * @brief Update - call from main loop
     */
    void update();

    /**
     * @brief Activate SOS mode
     * @param type Type of emergency
     * @param message Custom message (optional)
     * @return true if SOS activated
     */
    bool activateSOS(EmergencyType type, const char* message = nullptr);

    /**
     * @brief Cancel active SOS
     */
    void cancelSOS();

    /**
     * @brief Check if SOS is active
     */
    bool isSOSActive() const { return sosActive; }

    /**
     * @brief Get current SOS type
     */
    EmergencyType getSOSType() const { return currentType; }

    /**
     * @brief Set GPS coordinates for SOS broadcasts
     */
    void setGPS(int32_t lat, int32_t lon, int32_t alt);

    /**
     * @brief Set battery level for SOS broadcasts
     */
    void setBattery(uint8_t level);

    /**
     * @brief Handle incoming SOS message
     * @param nodeAddress Source of SOS
     * @param msg SOS message content
     * @param rssi Signal strength
     * @param snr Signal quality
     */
    void handleReceivedSOS(uint32_t nodeAddress, const SOSMessage* msg, int16_t rssi, int8_t snr);

    /**
     * @brief Acknowledge a received SOS
     * @param nodeAddress Address to acknowledge
     */
    void acknowledgeSOS(uint32_t nodeAddress);

    /**
     * @brief Get list of received SOS messages
     */
    ReceivedSOS* getReceivedSOS(uint8_t index);

    /**
     * @brief Get count of active SOS in area
     */
    uint8_t getActiveSOSCount();

    /**
     * @brief Set broadcast callback
     */
    void setBroadcastCallback(bool (*callback)(const uint8_t*, uint16_t));

    /**
     * @brief Get statistics
     */
    EmergencyStats getStats() const { return stats; }

    /**
     * @brief Print emergency status
     */
    void printStatus();

    /**
     * @brief Clear received SOS list
     */
    void clearReceived();

    /**
     * @brief Get original TX power (to restore after SOS)
     */
    uint8_t getOriginalTxPower() const { return originalTxPower; }

    /**
     * @brief Get original SF (to restore after SOS)
     */
    uint8_t getOriginalSF() const { return originalSF; }

private:
    static const int MAX_RECEIVED_SOS = 8;

    bool sosActive;
    EmergencyType currentType;
    uint32_t sosStartTime;
    uint32_t lastBroadcast;
    SOSMessage currentMessage;

    // GPS state (updated externally)
    int32_t gpsLat;
    int32_t gpsLon;
    int32_t gpsAlt;
    bool gpsValid;
    uint8_t batteryLevel;

    // Original radio settings to restore
    uint8_t originalTxPower;
    uint8_t originalSF;

    // Received SOS tracking
    ReceivedSOS receivedSOS[MAX_RECEIVED_SOS];

    // Statistics
    EmergencyStats stats;

    // Broadcast callback
    bool (*broadcastCallback)(const uint8_t*, uint16_t);

    void broadcastSOS();
    int findReceivedSOS(uint32_t nodeAddress);
    int findEmptyReceivedSlot();
};

// Global instance
extern Emergency emergency;

#endif // EMERGENCY_H
