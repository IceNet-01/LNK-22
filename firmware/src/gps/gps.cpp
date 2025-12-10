/**
 * MeshNet GPS Implementation
 */

#include "gps.h"

GPS::GPS() :
    lastUpdate(0)
{
    memset(&position, 0, sizeof(position));
}

GPS::~GPS() {
}

bool GPS::begin() {
    #ifdef HAS_GPS
    Serial.println("[GPS] Initializing GPS...");

    // Try I2C first (most common for WisBlock)
    Wire.begin();
    if (gnss.begin() == true) {
        Serial.println("[GPS] GPS initialized via I2C");

        // Configure GPS
        gnss.setI2COutput(COM_TYPE_UBX);  // UBX protocol
        gnss.saveConfigSelective(VAL_CFG_SUBSEC_IOPORT);  // Save config

        // Set measurement rate (1 Hz)
        gnss.setNavigationFrequency(1);

        // Enable automatic NMEA messages (for debugging)
        gnss.setAutoPVT(true);

        return true;
    }

    Serial.println("[GPS] GPS not found!");
    return false;

    #else
    Serial.println("[GPS] GPS support not enabled");
    return false;
    #endif
}

void GPS::update() {
    #ifdef HAS_GPS
    unsigned long now = millis();

    // Update every second
    if (now - lastUpdate > 1000) {
        lastUpdate = now;

        // Read GPS data
        if (gnss.getPVT()) {
            position.latitude = gnss.getLatitude() / 10000000.0;
            position.longitude = gnss.getLongitude() / 10000000.0;
            position.altitude = gnss.getAltitudeMSL() / 1000.0;
            position.satellites = gnss.getSIV();
            position.fixType = gnss.getFixType();
            position.timestamp = now / 1000;
            position.valid = (position.fixType >= 2);

            #if DEBUG_GPS
            if (position.valid) {
                Serial.print("[GPS] Fix: ");
                Serial.print(position.latitude, 6);
                Serial.print(", ");
                Serial.print(position.longitude, 6);
                Serial.print(" Alt: ");
                Serial.print(position.altitude);
                Serial.print("m Sats: ");
                Serial.println(position.satellites);
            } else {
                Serial.print("[GPS] No fix (");
                Serial.print(position.satellites);
                Serial.println(" sats)");
            }
            #endif
        }
    }
    #endif
}

bool GPS::getPosition(GPSPosition* pos) {
    if (position.valid) {
        memcpy(pos, &position, sizeof(GPSPosition));
        return true;
    }
    return false;
}
