/**
 * @file georouting.h
 * @brief LNK-22 Geographic Routing with GPS
 *
 * Implements location-based routing algorithms:
 * - GPSR (Greedy Perimeter Stateless Routing)
 * - Geocast (location-based multicast)
 * - Location service for mobile nodes
 * - Dead reckoning for GPS-denied environments
 */

#ifndef GEOROUTING_H
#define GEOROUTING_H

#include <Arduino.h>
#include "../config.h"

// Georouting Configuration
#define GEO_MAX_NODES 32             // Maximum nodes with known locations
#define GEO_LOCATION_TIMEOUT 600000  // Location expires after 10 minutes
#define GEO_UPDATE_INTERVAL 60000    // Broadcast location every 60 seconds
#define GEO_GEOCAST_RADIUS 1000      // Default geocast radius (meters)

// Routing modes
enum GeoRoutingMode {
    GEO_MODE_DISABLED = 0,
    GEO_MODE_GREEDY,        // Greedy forwarding only
    GEO_MODE_GPSR,          // GPSR with perimeter mode
    GEO_MODE_GEOCAST        // Location-based multicast
};

// Node location entry
struct NodeLocation {
    uint32_t address;        // Node address
    int32_t latitude;        // Latitude * 10^7
    int32_t longitude;       // Longitude * 10^7
    int16_t altitude;        // Altitude in meters
    uint16_t heading;        // Heading in degrees (0-359)
    uint16_t speed;          // Speed in cm/s
    uint32_t timestamp;      // When location was received
    int16_t rssi;            // Signal strength
    bool valid;
};

// Geocast region (circular)
struct GeocastRegion {
    int32_t centerLat;       // Center latitude * 10^7
    int32_t centerLon;       // Center longitude * 10^7
    uint32_t radius;         // Radius in meters
};

// Location beacon
struct __attribute__((packed)) LocationBeacon {
    uint32_t nodeAddress;
    int32_t latitude;
    int32_t longitude;
    int16_t altitude;
    uint16_t heading;
    uint16_t speed;
    uint8_t satellites;
    uint8_t fixType;
};

// Geocast packet header
struct __attribute__((packed)) GeocastHeader {
    int32_t destLat;         // Destination center latitude
    int32_t destLon;         // Destination center longitude
    uint32_t radius;         // Delivery radius
    uint32_t sourceAddr;     // Original sender
    uint8_t hopCount;
    uint8_t maxHops;
    uint16_t payloadLen;
    // Followed by payload
};

// Georouting statistics
struct GeoStats {
    uint32_t locationUpdates;
    uint32_t greeedyForwards;
    uint32_t perimeterForwards;
    uint32_t geocastSent;
    uint32_t geocastReceived;
    uint32_t geocastDelivered;
    uint32_t noRouteDrops;
};

// Callbacks
typedef void (*LocationCallback)(uint32_t nodeAddr, int32_t lat, int32_t lon);
typedef void (*GeocastCallback)(const GeocastRegion* region, const uint8_t* data, uint16_t len);

class GeoRouting {
public:
    GeoRouting();

    /**
     * @brief Initialize geographic routing
     * @param nodeAddr This node's address
     */
    void begin(uint32_t nodeAddr);

    /**
     * @brief Update - call from main loop
     */
    void update();

    /**
     * @brief Set our current GPS position
     * @param lat Latitude * 10^7
     * @param lon Longitude * 10^7
     * @param alt Altitude in meters
     * @param heading Heading in degrees
     * @param speed Speed in cm/s
     * @param sats Number of satellites
     */
    void setPosition(int32_t lat, int32_t lon, int16_t alt,
                     uint16_t heading = 0, uint16_t speed = 0, uint8_t sats = 0);

    /**
     * @brief Get our current position
     */
    bool getPosition(int32_t* lat, int32_t* lon, int16_t* alt = nullptr);

    /**
     * @brief Check if we have valid GPS position
     */
    bool hasValidPosition() const { return positionValid; }

    /**
     * @brief Handle incoming location beacon
     * @param fromAddr Source address
     * @param beacon Location beacon data
     * @param rssi Signal strength
     */
    void handleLocationBeacon(uint32_t fromAddr, const LocationBeacon* beacon, int16_t rssi);

    /**
     * @brief Get location of a node
     * @param nodeAddr Node to find
     * @param lat Output latitude
     * @param lon Output longitude
     * @return true if location known
     */
    bool getNodeLocation(uint32_t nodeAddr, int32_t* lat, int32_t* lon);

    /**
     * @brief Find next hop using geographic routing
     * @param destLat Destination latitude
     * @param destLon Destination longitude
     * @return Address of next hop, or 0 if no route
     */
    uint32_t findNextHop(int32_t destLat, int32_t destLon);

    /**
     * @brief Find next hop to reach a node
     * @param destAddr Destination node address
     * @return Address of next hop, or 0 if no route
     */
    uint32_t findNextHopToNode(uint32_t destAddr);

    /**
     * @brief Send geocast message
     * @param region Target region
     * @param data Message data
     * @param length Data length
     * @return true if sent
     */
    bool sendGeocast(const GeocastRegion* region, const uint8_t* data, uint16_t length);

    /**
     * @brief Handle incoming geocast packet
     * @param fromAddr Source of packet
     * @param header Geocast header
     * @param payload Message payload
     * @param rssi Signal strength
     */
    void handleGeocast(uint32_t fromAddr, const GeocastHeader* header,
                       const uint8_t* payload, int16_t rssi);

    /**
     * @brief Check if a point is within a region
     */
    bool isInRegion(int32_t lat, int32_t lon, const GeocastRegion* region);

    /**
     * @brief Calculate distance between two points (Haversine)
     * @param lat1, lon1 First point (* 10^7)
     * @param lat2, lon2 Second point (* 10^7)
     * @return Distance in meters
     */
    uint32_t calculateDistance(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2);

    /**
     * @brief Get number of nodes with known locations
     */
    uint8_t getKnownLocationCount();

    /**
     * @brief Get node location by index
     */
    NodeLocation* getLocationByIndex(uint8_t index);

    /**
     * @brief Set routing mode
     */
    void setMode(GeoRoutingMode mode) { routingMode = mode; }
    GeoRoutingMode getMode() const { return routingMode; }

    /**
     * @brief Enable/disable location broadcasting
     */
    void setBroadcastEnabled(bool enabled) { broadcastEnabled = enabled; }

    /**
     * @brief Set callbacks
     */
    void setLocationCallback(LocationCallback cb) { onLocation = cb; }
    void setGeocastCallback(GeocastCallback cb) { onGeocast = cb; }

    /**
     * @brief Set send function
     */
    void setSendFunction(bool (*sendFn)(uint32_t dest, uint8_t type,
                                         const uint8_t* data, uint16_t len));

    /**
     * @brief Get statistics
     */
    GeoStats getStats() const { return stats; }

    /**
     * @brief Print status
     */
    void printStatus();

    /**
     * @brief Clear all known locations
     */
    void clearLocations();

private:
    uint32_t nodeAddress;
    GeoRoutingMode routingMode;
    bool broadcastEnabled;

    // Our position
    int32_t myLatitude;
    int32_t myLongitude;
    int16_t myAltitude;
    uint16_t myHeading;
    uint16_t mySpeed;
    uint8_t mySatellites;
    bool positionValid;
    uint32_t lastPositionUpdate;

    // Known node locations
    NodeLocation locations[GEO_MAX_NODES];

    // Timing
    uint32_t lastBroadcast;

    // Statistics
    GeoStats stats;

    // Callbacks
    LocationCallback onLocation;
    GeocastCallback onGeocast;
    bool (*sendPacket)(uint32_t, uint8_t, const uint8_t*, uint16_t);

    // GPSR state (for perimeter mode)
    bool inPerimeterMode;
    uint32_t perimeterStart;
    int32_t perimeterFaceLat;
    int32_t perimeterFaceLon;

    // Helper methods
    int findLocationSlot(uint32_t nodeAddr);
    int findEmptyLocationSlot();
    void cleanupExpired();
    void broadcastLocation();

    // Math helpers
    double toRadians(int32_t coord);  // Convert * 10^7 to radians
    int32_t fromRadians(double rad);  // Convert radians to * 10^7
};

// Global instance
extern GeoRouting geoRouting;

#endif // GEOROUTING_H
