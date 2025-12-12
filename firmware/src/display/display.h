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

// Number of display pages (manually scrolled via button)
#define DISPLAY_NUM_PAGES 8  // Info, Network, Neighbors, Signal, GPS, Messages, Battery, MAC

// Forward declarations
class Mesh;
class NodeNaming;

// Neighbor info for display (passed from main)
struct DisplayNeighbor {
    uint32_t address;
    char name[17];
    int16_t rssi;
};

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

    // Extended update with neighbor list for names display
    void updateWithNeighbors(uint32_t nodeAddr, const char* nodeName,
                             uint8_t neighborCount, uint8_t routeCount,
                             uint32_t txCount, uint32_t rxCount,
                             int16_t rssi, int8_t snr,
                             DisplayNeighbor* neighbors, uint8_t numNeighbors);

    // Show GPS coordinates (legacy one-shot method)
    void showGPS(double latitude, double longitude, uint8_t satellites);

    // Show a message (legacy one-shot method)
    void showMessage(const char* message);

    // Clear display
    void clear();

    // Manual page control
    void nextPage();
    void prevPage();
    uint8_t getCurrentPage() const { return currentPage; }

    // Update cached data for new pages
    void updateGPS(double latitude, double longitude, float altitude, uint8_t satellites, bool valid);
    void updateLastMessage(const char* message, uint32_t source);
    void updateBattery(uint8_t percent, float voltage, bool charging);
    void updateMAC(bool isTDMA, uint8_t timeSource, uint8_t stratum,
                   uint32_t frame, uint8_t slot, uint32_t tdmaTx, uint32_t csmaTx);

private:
    Adafruit_SSD1306* display;
    bool isInitialized;
    uint8_t currentPage;
    unsigned long lastPageChange;

    // Cached neighbor info for display
    DisplayNeighbor cachedNeighbors[4];  // Show up to 4 neighbors
    uint8_t cachedNeighborCount;

    // Cached GPS info
    double cachedLatitude;
    double cachedLongitude;
    float cachedAltitude;
    uint8_t cachedSatellites;
    bool cachedGPSValid;

    // Cached message info
    char cachedLastMessage[64];
    uint32_t cachedMessageSource;
    unsigned long cachedMessageTime;

    // Cached battery info
    uint8_t cachedBatteryPercent;
    float cachedBatteryVoltage;
    bool cachedCharging;

    // Cached MAC info
    bool cachedMACisTDMA;
    uint8_t cachedMACTimeSource;
    uint8_t cachedMACStratum;
    uint32_t cachedMACFrame;
    uint8_t cachedMACSlot;
    uint32_t cachedMACTdmaTx;
    uint32_t cachedMACCsmaTx;

    void drawInfoPage(uint32_t nodeAddr, const char* nodeName);
    void drawStatusPage(uint8_t neighborCount, uint8_t routeCount,
                        uint32_t txCount, uint32_t rxCount);
    void drawSignalPage(int16_t rssi, int8_t snr);
    void drawNeighborsPage();
    void drawGPSPage();
    void drawMessagesPage();
    void drawBatteryPage();
    void drawMACPage();
};

#endif // HAS_DISPLAY
#endif // LNK22_DISPLAY_H
