/**
 * @file georouting.cpp
 * @brief LNK-22 Geographic Routing Implementation
 */

#include "georouting.h"
#include <math.h>

// Global instance
GeoRouting geoRouting;

// Earth radius in meters
#define EARTH_RADIUS 6371000.0

GeoRouting::GeoRouting() :
    nodeAddress(0),
    routingMode(GEO_MODE_GREEDY),
    broadcastEnabled(true),
    myLatitude(0),
    myLongitude(0),
    myAltitude(0),
    myHeading(0),
    mySpeed(0),
    mySatellites(0),
    positionValid(false),
    lastPositionUpdate(0),
    lastBroadcast(0),
    onLocation(nullptr),
    onGeocast(nullptr),
    sendPacket(nullptr),
    inPerimeterMode(false),
    perimeterStart(0)
{
    memset(&stats, 0, sizeof(GeoStats));
    for (int i = 0; i < GEO_MAX_NODES; i++) {
        locations[i].valid = false;
    }
}

void GeoRouting::begin(uint32_t nodeAddr) {
    nodeAddress = nodeAddr;

    Serial.println("[GEO] Geographic routing initialized");
    Serial.print("[GEO] Mode: ");
    switch (routingMode) {
        case GEO_MODE_DISABLED: Serial.println("DISABLED"); break;
        case GEO_MODE_GREEDY: Serial.println("GREEDY"); break;
        case GEO_MODE_GPSR: Serial.println("GPSR"); break;
        case GEO_MODE_GEOCAST: Serial.println("GEOCAST"); break;
    }
}

void GeoRouting::update() {
    cleanupExpired();

    // Broadcast our location periodically
    if (broadcastEnabled && positionValid) {
        unsigned long now = millis();
        if (now - lastBroadcast > GEO_UPDATE_INTERVAL) {
            broadcastLocation();
            lastBroadcast = now;
        }
    }
}

void GeoRouting::setPosition(int32_t lat, int32_t lon, int16_t alt,
                              uint16_t heading, uint16_t speed, uint8_t sats) {
    myLatitude = lat;
    myLongitude = lon;
    myAltitude = alt;
    myHeading = heading;
    mySpeed = speed;
    mySatellites = sats;
    positionValid = true;
    lastPositionUpdate = millis();
}

bool GeoRouting::getPosition(int32_t* lat, int32_t* lon, int16_t* alt) {
    if (!positionValid) return false;

    if (lat) *lat = myLatitude;
    if (lon) *lon = myLongitude;
    if (alt) *alt = myAltitude;
    return true;
}

void GeoRouting::handleLocationBeacon(uint32_t fromAddr, const LocationBeacon* beacon, int16_t rssi) {
    if (fromAddr == nodeAddress) return;  // Ignore our own beacons

    int slot = findLocationSlot(fromAddr);
    if (slot < 0) {
        slot = findEmptyLocationSlot();
        if (slot < 0) {
            // Find oldest entry to replace
            uint32_t oldest = 0xFFFFFFFF;
            int oldestSlot = 0;
            for (int i = 0; i < GEO_MAX_NODES; i++) {
                if (locations[i].valid && locations[i].timestamp < oldest) {
                    oldest = locations[i].timestamp;
                    oldestSlot = i;
                }
            }
            slot = oldestSlot;
        }
    }

    NodeLocation* loc = &locations[slot];
    loc->address = fromAddr;
    loc->latitude = beacon->latitude;
    loc->longitude = beacon->longitude;
    loc->altitude = beacon->altitude;
    loc->heading = beacon->heading;
    loc->speed = beacon->speed;
    loc->timestamp = millis();
    loc->rssi = rssi;
    loc->valid = true;

    stats.locationUpdates++;

    if (onLocation) {
        onLocation(fromAddr, beacon->latitude, beacon->longitude);
    }
}

bool GeoRouting::getNodeLocation(uint32_t nodeAddr, int32_t* lat, int32_t* lon) {
    int slot = findLocationSlot(nodeAddr);
    if (slot < 0) return false;

    if (lat) *lat = locations[slot].latitude;
    if (lon) *lon = locations[slot].longitude;
    return true;
}

uint32_t GeoRouting::findNextHop(int32_t destLat, int32_t destLon) {
    if (routingMode == GEO_MODE_DISABLED) return 0;
    if (!positionValid) return 0;

    uint32_t myDist = calculateDistance(myLatitude, myLongitude, destLat, destLon);

    // Find neighbor closest to destination (greedy forwarding)
    uint32_t bestHop = 0;
    uint32_t bestDist = myDist;

    for (int i = 0; i < GEO_MAX_NODES; i++) {
        if (!locations[i].valid) continue;

        // Check if location is recent enough
        if (millis() - locations[i].timestamp > GEO_LOCATION_TIMEOUT) continue;

        uint32_t dist = calculateDistance(locations[i].latitude, locations[i].longitude,
                                          destLat, destLon);

        if (dist < bestDist) {
            bestDist = dist;
            bestHop = locations[i].address;
        }
    }

    if (bestHop != 0) {
        stats.greeedyForwards++;
        inPerimeterMode = false;
    } else if (routingMode == GEO_MODE_GPSR) {
        // Enter perimeter mode (simplified)
        // In full GPSR, this would use planarized graph and right-hand rule
        inPerimeterMode = true;
        stats.perimeterForwards++;

        // For now, just pick neighbor with best signal as fallback
        int16_t bestRssi = -200;
        for (int i = 0; i < GEO_MAX_NODES; i++) {
            if (!locations[i].valid) continue;
            if (millis() - locations[i].timestamp > GEO_LOCATION_TIMEOUT) continue;

            if (locations[i].rssi > bestRssi) {
                bestRssi = locations[i].rssi;
                bestHop = locations[i].address;
            }
        }
    }

    if (bestHop == 0) {
        stats.noRouteDrops++;
    }

    return bestHop;
}

uint32_t GeoRouting::findNextHopToNode(uint32_t destAddr) {
    int32_t destLat, destLon;

    if (!getNodeLocation(destAddr, &destLat, &destLon)) {
        return 0;  // Don't know where destination is
    }

    return findNextHop(destLat, destLon);
}

bool GeoRouting::sendGeocast(const GeocastRegion* region, const uint8_t* data, uint16_t length) {
    if (!sendPacket) return false;
    if (!positionValid) return false;

    // Build geocast packet
    uint8_t packet[sizeof(GeocastHeader) + length];
    GeocastHeader* header = (GeocastHeader*)packet;

    header->destLat = region->centerLat;
    header->destLon = region->centerLon;
    header->radius = region->radius;
    header->sourceAddr = nodeAddress;
    header->hopCount = 0;
    header->maxHops = MAX_TTL;
    header->payloadLen = length;

    memcpy(packet + sizeof(GeocastHeader), data, length);

    // Send to all neighbors (flood within region)
    bool sent = false;
    for (int i = 0; i < GEO_MAX_NODES; i++) {
        if (!locations[i].valid) continue;
        if (millis() - locations[i].timestamp > GEO_LOCATION_TIMEOUT) continue;

        // Check if neighbor is closer to or within region
        uint32_t neighborDist = calculateDistance(locations[i].latitude, locations[i].longitude,
                                                   region->centerLat, region->centerLon);

        if (neighborDist < region->radius * 2) {  // Within 2x radius
            sendPacket(locations[i].address, 0x20, packet, sizeof(GeocastHeader) + length);
            sent = true;
        }
    }

    if (sent) {
        stats.geocastSent++;
    }

    return sent;
}

void GeoRouting::handleGeocast(uint32_t fromAddr, const GeocastHeader* header,
                                const uint8_t* payload, int16_t rssi) {
    stats.geocastReceived++;

    // Check if we're in the target region
    GeocastRegion checkRegion = {header->destLat, header->destLon, header->radius};
    if (positionValid && isInRegion(myLatitude, myLongitude, &checkRegion)) {
        // Deliver locally
        stats.geocastDelivered++;

        if (onGeocast) {
            GeocastRegion region = {header->destLat, header->destLon, header->radius};
            onGeocast(&region, payload, header->payloadLen);
        }
    }

    // Forward if we haven't exceeded hop limit
    if (header->hopCount < header->maxHops) {
        // Build forwarded packet
        uint8_t packet[sizeof(GeocastHeader) + header->payloadLen];
        GeocastHeader* fwdHeader = (GeocastHeader*)packet;
        memcpy(fwdHeader, header, sizeof(GeocastHeader));
        fwdHeader->hopCount++;
        memcpy(packet + sizeof(GeocastHeader), payload, header->payloadLen);

        // Forward to neighbors closer to/within region
        GeocastRegion region = {header->destLat, header->destLon, header->radius};

        for (int i = 0; i < GEO_MAX_NODES; i++) {
            if (!locations[i].valid) continue;
            if (locations[i].address == fromAddr) continue;  // Don't send back
            if (millis() - locations[i].timestamp > GEO_LOCATION_TIMEOUT) continue;

            // Check if neighbor is relevant
            uint32_t neighborDist = calculateDistance(locations[i].latitude, locations[i].longitude,
                                                       region.centerLat, region.centerLon);

            if (neighborDist < region.radius * 2 && sendPacket) {
                sendPacket(locations[i].address, 0x20, packet, sizeof(GeocastHeader) + header->payloadLen);
            }
        }
    }
}

bool GeoRouting::isInRegion(int32_t lat, int32_t lon, const GeocastRegion* region) {
    uint32_t dist = calculateDistance(lat, lon, region->centerLat, region->centerLon);
    return dist <= region->radius;
}

uint32_t GeoRouting::calculateDistance(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2) {
    // Haversine formula
    double lat1Rad = toRadians(lat1);
    double lat2Rad = toRadians(lat2);
    double deltaLat = toRadians(lat2 - lat1);
    double deltaLon = toRadians(lon2 - lon1);

    double a = sin(deltaLat / 2) * sin(deltaLat / 2) +
               cos(lat1Rad) * cos(lat2Rad) *
               sin(deltaLon / 2) * sin(deltaLon / 2);

    double c = 2 * atan2(sqrt(a), sqrt(1 - a));

    return (uint32_t)(EARTH_RADIUS * c);
}

uint8_t GeoRouting::getKnownLocationCount() {
    uint8_t count = 0;
    for (int i = 0; i < GEO_MAX_NODES; i++) {
        if (locations[i].valid) count++;
    }
    return count;
}

NodeLocation* GeoRouting::getLocationByIndex(uint8_t index) {
    uint8_t current = 0;
    for (int i = 0; i < GEO_MAX_NODES; i++) {
        if (locations[i].valid) {
            if (current == index) {
                return &locations[i];
            }
            current++;
        }
    }
    return nullptr;
}

void GeoRouting::setSendFunction(bool (*sendFn)(uint32_t, uint8_t, const uint8_t*, uint16_t)) {
    sendPacket = sendFn;
}

void GeoRouting::printStatus() {
    Serial.println("\n=== Geographic Routing ===");

    Serial.print("Mode: ");
    switch (routingMode) {
        case GEO_MODE_DISABLED: Serial.println("DISABLED"); break;
        case GEO_MODE_GREEDY: Serial.println("GREEDY"); break;
        case GEO_MODE_GPSR: Serial.println("GPSR"); break;
        case GEO_MODE_GEOCAST: Serial.println("GEOCAST"); break;
    }

    Serial.print("Our position: ");
    if (positionValid) {
        Serial.print((double)myLatitude / 10000000.0, 6);
        Serial.print(", ");
        Serial.print((double)myLongitude / 10000000.0, 6);
        Serial.print(" (");
        Serial.print(mySatellites);
        Serial.println(" sats)");
    } else {
        Serial.println("UNKNOWN");
    }

    Serial.print("Known locations: ");
    Serial.println(getKnownLocationCount());

    Serial.println("\nNode locations:");
    for (int i = 0; i < GEO_MAX_NODES; i++) {
        if (locations[i].valid) {
            Serial.print("  0x");
            Serial.print(locations[i].address, HEX);
            Serial.print(": ");
            Serial.print((double)locations[i].latitude / 10000000.0, 5);
            Serial.print(", ");
            Serial.print((double)locations[i].longitude / 10000000.0, 5);

            // Calculate distance from us
            if (positionValid) {
                uint32_t dist = calculateDistance(myLatitude, myLongitude,
                                                   locations[i].latitude, locations[i].longitude);
                Serial.print(" (");
                if (dist > 1000) {
                    Serial.print(dist / 1000.0, 1);
                    Serial.print(" km");
                } else {
                    Serial.print(dist);
                    Serial.print(" m");
                }
                Serial.print(")");
            }

            Serial.print(" RSSI: ");
            Serial.print(locations[i].rssi);
            Serial.println(" dBm");
        }
    }

    Serial.println("\nStatistics:");
    Serial.print("  Location updates: ");
    Serial.println(stats.locationUpdates);
    Serial.print("  Greedy forwards: ");
    Serial.println(stats.greeedyForwards);
    Serial.print("  Perimeter forwards: ");
    Serial.println(stats.perimeterForwards);
    Serial.print("  Geocasts sent: ");
    Serial.println(stats.geocastSent);
    Serial.print("  Geocasts received: ");
    Serial.println(stats.geocastReceived);
    Serial.print("  Geocasts delivered: ");
    Serial.println(stats.geocastDelivered);
    Serial.print("  No-route drops: ");
    Serial.println(stats.noRouteDrops);

    Serial.println("=========================\n");
}

void GeoRouting::clearLocations() {
    for (int i = 0; i < GEO_MAX_NODES; i++) {
        locations[i].valid = false;
    }
    Serial.println("[GEO] Locations cleared");
}

// Private methods

int GeoRouting::findLocationSlot(uint32_t nodeAddr) {
    for (int i = 0; i < GEO_MAX_NODES; i++) {
        if (locations[i].valid && locations[i].address == nodeAddr) {
            return i;
        }
    }
    return -1;
}

int GeoRouting::findEmptyLocationSlot() {
    for (int i = 0; i < GEO_MAX_NODES; i++) {
        if (!locations[i].valid) {
            return i;
        }
    }
    return -1;
}

void GeoRouting::cleanupExpired() {
    unsigned long now = millis();
    for (int i = 0; i < GEO_MAX_NODES; i++) {
        if (locations[i].valid) {
            if (now - locations[i].timestamp > GEO_LOCATION_TIMEOUT) {
                locations[i].valid = false;
            }
        }
    }
}

void GeoRouting::broadcastLocation() {
    if (!sendPacket) return;
    if (!positionValid) return;

    LocationBeacon beacon;
    beacon.nodeAddress = nodeAddress;
    beacon.latitude = myLatitude;
    beacon.longitude = myLongitude;
    beacon.altitude = myAltitude;
    beacon.heading = myHeading;
    beacon.speed = mySpeed;
    beacon.satellites = mySatellites;
    beacon.fixType = 3;  // 3D fix

    // Broadcast to all (0xFFFFFFFF)
    sendPacket(0xFFFFFFFF, 0x21, (uint8_t*)&beacon, sizeof(LocationBeacon));
}

double GeoRouting::toRadians(int32_t coord) {
    // Convert from * 10^7 to degrees, then to radians
    return ((double)coord / 10000000.0) * M_PI / 180.0;
}

int32_t GeoRouting::fromRadians(double rad) {
    return (int32_t)((rad * 180.0 / M_PI) * 10000000.0);
}
