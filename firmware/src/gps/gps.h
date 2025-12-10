/**
 * MeshNet GPS Module
 * GPS/GNSS support for position tracking
 */

#ifndef MESHNET_GPS_H
#define MESHNET_GPS_H

#include <Arduino.h>
#include "../config.h"

#ifdef HAS_GPS
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#endif

struct GPSPosition {
    double latitude;
    double longitude;
    double altitude;
    uint8_t satellites;
    uint8_t fixType;  // 0=no fix, 2=2D, 3=3D
    uint32_t timestamp;
    bool valid;
};

class GPS {
public:
    GPS();
    ~GPS();

    // Initialize GPS
    bool begin();

    // Update GPS (call in loop)
    void update();

    // Get current position
    bool getPosition(GPSPosition* pos);

    // Check if we have a valid fix
    bool hasFix() const { return position.valid && position.fixType >= 2; }

    // Get last position
    const GPSPosition& getLastPosition() const { return position; }

private:
    GPSPosition position;

    #ifdef HAS_GPS
    SFE_UBLOX_GNSS gnss;
    #endif

    unsigned long lastUpdate;
};

#endif // MESHNET_GPS_H
