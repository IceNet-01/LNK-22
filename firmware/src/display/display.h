/**
 * LNK-22 Display Driver
 * OLED display support for showing mesh network status
 */

#ifndef LNK22_DISPLAY_H
#define LNK22_DISPLAY_H

#include <Arduino.h>
#include "../config.h"

#ifdef HAS_DISPLAY
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Reset pin (-1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C  // Common I2C address for 128x64 OLED

class Display {
public:
    Display();
    ~Display();

    // Initialize display
    bool begin();

    // Update display with current status (auto-rotates through screens)
    void update(uint32_t nodeAddr, const char* nodeName, uint8_t neighborCount,
                uint8_t routeCount, uint32_t txCount, uint32_t rxCount,
                int16_t rssi, int8_t snr);

    // Show GPS coordinates
    void showGPS(double latitude, double longitude, uint8_t satellites);

    // Show a message
    void showMessage(const char* message);

    // Clear display
    void clear();

private:
    Adafruit_SSD1306* display;
    bool isInitialized;
    uint8_t currentPage;
    unsigned long lastPageChange;

    void drawInfoPage(uint32_t nodeAddr, const char* nodeName);
    void drawStatusPage(uint8_t neighborCount, uint8_t routeCount,
                        uint32_t txCount, uint32_t rxCount);
    void drawSignalPage(int16_t rssi, int8_t snr);
};

#endif // HAS_DISPLAY
#endif // LNK22_DISPLAY_H
