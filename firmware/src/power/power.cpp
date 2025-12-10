/**
 * LNK-22 Power Management Implementation
 */

#include "power.h"

#ifdef ESP32
#include <esp_sleep.h>
#include <driver/adc.h>
#endif

PowerManager::PowerManager() :
    currentMode(POWER_ACTIVE),
    dutyCyclingEnabled(false),
    dutyCycleSleep(30000),  // 30s sleep
    dutyCycleWake(1000),    // 1s wake
    lastWakeTime(0),
    isSleeping(false)
{
}

PowerManager::~PowerManager() {
}

void PowerManager::begin() {
    Serial.println("[POWER] Initializing power management...");

    initBatteryMonitor();

    #ifdef NRF52840
    // nRF52840 power optimization
    // Configure low-power UART
    #endif

    #ifdef ESP32
    // ESP32 power optimization
    esp_sleep_enable_timer_wakeup(1000000);  // 1s default
    #endif

    Serial.println("[POWER] Power management initialized");
}

void PowerManager::setPowerMode(PowerMode mode) {
    currentMode = mode;

    switch (mode) {
        case POWER_ACTIVE:
            Serial.println("[POWER] Mode: ACTIVE");
            break;

        case POWER_IDLE:
            Serial.println("[POWER] Mode: IDLE");
            // CPU can sleep, peripherals active
            break;

        case POWER_LIGHT_SLEEP:
            Serial.println("[POWER] Mode: LIGHT SLEEP");
            break;

        case POWER_DEEP_SLEEP:
            Serial.println("[POWER] Mode: DEEP SLEEP");
            break;
    }
}

void PowerManager::sleep(uint32_t duration_ms) {
    if (duration_ms < 10) {
        delay(duration_ms);
        return;
    }

    if (currentMode == POWER_DEEP_SLEEP) {
        enterDeepSleep(duration_ms);
    } else {
        enterLightSleep(duration_ms);
    }
}

void PowerManager::deepSleep(uint32_t sleep_ms, uint32_t wake_ms) {
    Serial.println("[POWER] Entering deep sleep...");
    Serial.flush();

    enterDeepSleep(sleep_ms);

    // After wake
    Serial.println("[POWER] Woke from deep sleep");
}

void PowerManager::setDutyCycling(bool enabled, uint32_t sleep_ms, uint32_t wake_ms) {
    dutyCyclingEnabled = enabled;
    dutyCycleSleep = sleep_ms;
    dutyCycleWake = wake_ms;

    if (enabled) {
        Serial.print("[POWER] Duty cycling enabled: ");
        Serial.print(sleep_ms);
        Serial.print("ms sleep, ");
        Serial.print(wake_ms);
        Serial.println("ms wake");
    } else {
        Serial.println("[POWER] Duty cycling disabled");
    }
}

void PowerManager::update() {
    if (!dutyCyclingEnabled) {
        return;
    }

    unsigned long now = millis();

    if (!isSleeping) {
        // Check if it's time to sleep
        if (now - lastWakeTime > dutyCycleWake) {
            Serial.println("[POWER] Duty cycle: entering sleep");
            Serial.flush();
            isSleeping = true;
            sleep(dutyCycleSleep);
        }
    } else {
        // Just woke up
        isSleeping = false;
        lastWakeTime = now;
        Serial.println("[POWER] Duty cycle: woke up");
    }
}

float PowerManager::getBatteryVoltage() {
    #if defined(NRF52840) || defined(ESP32)
    uint16_t adc = readBatteryADC();

    // Convert ADC to voltage (platform-specific)
    #ifdef NRF52840
    // nRF52840: 3.6V reference, 12-bit ADC, 1/6 divider
    return (adc / 4096.0) * 3.6 * 6.0;
    #endif

    #ifdef ESP32
    // ESP32: 1.1V reference, 12-bit ADC, voltage divider
    return (adc / 4096.0) * 1.1 * 2.0;
    #endif
    #endif

    return 0.0;
}

uint8_t PowerManager::getBatteryPercent() {
    float voltage = getBatteryVoltage();

    // LiPo battery voltage curve
    if (voltage >= 4.2) return 100;
    if (voltage >= 4.0) return 80 + (voltage - 4.0) * 100;
    if (voltage >= 3.7) return 50 + (voltage - 3.7) * 100;
    if (voltage >= 3.4) return 20 + (voltage - 3.4) * 100;
    if (voltage >= 3.0) return (voltage - 3.0) * 50;
    return 0;
}

void PowerManager::enterLightSleep(uint32_t duration_ms) {
    #ifdef NRF52840
    // nRF52: Use SD_POWER_MODE_LOWPWR
    delay(duration_ms);  // Simplified for now
    #endif

    #ifdef ESP32
    esp_sleep_enable_timer_wakeup(duration_ms * 1000ULL);
    esp_light_sleep_start();
    #endif
}

void PowerManager::enterDeepSleep(uint32_t duration_ms) {
    #ifdef NRF52840
    // nRF52: System OFF mode
    // TODO: Implement with wake sources
    delay(duration_ms);
    #endif

    #ifdef ESP32
    esp_sleep_enable_timer_wakeup(duration_ms * 1000ULL);
    esp_deep_sleep_start();
    #endif
}

void PowerManager::initBatteryMonitor() {
    #ifdef NRF52840
    // Configure ADC for battery monitoring
    // TODO: Set up ADC channel for VBAT
    #endif

    #ifdef ESP32
    // Configure ADC for battery monitoring
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
    #endif
}

uint16_t PowerManager::readBatteryADC() {
    #ifdef NRF52840
    // Read nRF52 ADC
    // TODO: Implement proper ADC reading
    return 0;
    #endif

    #ifdef ESP32
    return adc1_get_raw(ADC1_CHANNEL_0);
    #endif

    return 0;
}
