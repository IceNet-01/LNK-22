/**
 * LNK-22 Power Management
 * Sleep modes and duty cycling for low power operation
 */

#ifndef LNK22_POWER_H
#define LNK22_POWER_H

#include <Arduino.h>
#include "../config.h"

// Power modes
enum PowerMode {
    POWER_ACTIVE = 0,       // Full operation
    POWER_IDLE = 1,         // CPU sleep, radio RX
    POWER_LIGHT_SLEEP = 2,  // Short sleep (wake on interrupt)
    POWER_DEEP_SLEEP = 3    // Deep sleep (periodic wake)
};

class PowerManager {
public:
    PowerManager();
    ~PowerManager();

    // Initialize power management
    void begin();

    // Set power mode
    void setPowerMode(PowerMode mode);

    // Get current power mode
    PowerMode getPowerMode() const { return currentMode; }

    // Sleep for specified duration
    void sleep(uint32_t duration_ms);

    // Deep sleep with periodic wake
    void deepSleep(uint32_t sleep_ms, uint32_t wake_ms);

    // Enable/disable duty cycling
    void setDutyCycling(bool enabled, uint32_t sleep_ms, uint32_t wake_ms);

    // Update (handles duty cycling)
    void update();

    // Battery monitoring
    float getBatteryVoltage();
    uint8_t getBatteryPercent();

private:
    PowerMode currentMode;
    bool dutyCyclingEnabled;
    uint32_t dutyCycleSleep;
    uint32_t dutyCycleWake;
    unsigned long lastWakeTime;
    bool isSleeping;

    // Platform-specific sleep implementations
    void enterLightSleep(uint32_t duration_ms);
    void enterDeepSleep(uint32_t duration_ms);

    // Battery ADC reading
    void initBatteryMonitor();
    uint16_t readBatteryADC();
};

#endif // LNK22_POWER_H
