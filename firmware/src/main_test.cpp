/**
 * LNK-22 Minimal LED Test
 * Simple firmware to verify hardware initialization
 *
 * Tests:
 * - Power rail initialization (PIN_3V3_EN)
 * - LED1 and LED2 blinking
 * - Serial output
 *
 * Based on Meshtastic RAK4631 variant
 */

#include <Arduino.h>

// LED pins are defined in variant.h
// PIN_LED1 = 35
// PIN_LED2 = 36
// PIN_3V3_EN = 34

void setup() {
    // Serial for debugging
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n\n========================================");
    Serial.println("LNK-22 LED Test v1.0");
    Serial.println("RAKwireless WisMesh Pocket v2");
    Serial.println("========================================\n");

    Serial.println("[INIT] Variant initialization should have:");
    Serial.println("  - Enabled 3V3 power rail (PIN_3V3_EN=34)");
    Serial.println("  - Initialized LED pins (35, 36)");

    Serial.println("\n[TEST] Starting LED blink test...");
    Serial.println("  LED1 (pin 35) should blink every 500ms");
    Serial.println("  LED2 (pin 36) should blink every 1000ms");
    Serial.println("\nLook for LEDs on the BOTTOM of the device!\n");
}

void loop() {
    static unsigned long lastLed1 = 0;
    static unsigned long lastLed2 = 0;
    static bool led1State = false;
    static bool led2State = false;
    static int loopCount = 0;

    unsigned long now = millis();

    // Blink LED1 every 500ms
    if (now - lastLed1 > 500) {
        led1State = !led1State;
        digitalWrite(PIN_LED1, led1State ? HIGH : LOW);
        lastLed1 = now;
    }

    // Blink LED2 every 1000ms
    if (now - lastLed2 > 1000) {
        led2State = !led2State;
        digitalWrite(PIN_LED2, led2State ? HIGH : LOW);
        lastLed2 = now;

        loopCount++;
        Serial.print(".");
        if (loopCount % 60 == 0) {
            Serial.println();
            Serial.print("Uptime: ");
            Serial.print(now / 1000);
            Serial.println(" seconds");
        }
    }

    delay(10);
}
