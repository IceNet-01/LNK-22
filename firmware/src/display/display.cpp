/**
 * LNK-22 Display Implementation
 */

#include "display.h"

#ifdef HAS_DISPLAY

Display::Display() :
    display(nullptr),
    isInitialized(false),
    currentPage(0),
    lastPageChange(0),
    cachedNeighborCount(0),
    cachedLatitude(0),
    cachedLongitude(0),
    cachedAltitude(0),
    cachedSatellites(0),
    cachedGPSValid(false),
    cachedMessageSource(0),
    cachedMessageTime(0),
    cachedBatteryPercent(100),
    cachedBatteryVoltage(4.2),
    cachedCharging(false)
{
    memset(cachedNeighbors, 0, sizeof(cachedNeighbors));
    memset(cachedLastMessage, 0, sizeof(cachedLastMessage));
}

Display::~Display() {
    if (display) {
        delete display;
    }
}

bool Display::begin() {
    Serial.println("[DISPLAY] Initializing display...");

    // Initialize I2C bus
    Wire.begin();
    Wire.setClock(100000); // 100kHz for OLED
    delay(100); // Give I2C time to stabilize

    // Create display object
    display = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

    // Try primary I2C address (0x3C)
    Serial.print("[DISPLAY] Trying I2C address 0x3C... ");
    if (!display->begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("failed");

        // Try alternate address (0x3D)
        Serial.print("[DISPLAY] Trying I2C address 0x3D... ");
        if (!display->begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
            Serial.println("failed");
            Serial.println("[DISPLAY] No display found on I2C bus");
            delete display;
            display = nullptr;
            return false;
        } else {
            Serial.println("success!");
        }
    } else {
        Serial.println("success!");
    }

    // Configure display
    display->clearDisplay();
    display->setTextSize(1);
    display->setTextColor(SSD1306_WHITE);
    display->setCursor(0, 0);

    // Show boot message
    display->println("LNK-22");
    display->println("Pro Mesh");
    display->println("Starting...");
    display->display();

    isInitialized = true;
    lastPageChange = millis();
    Serial.println("[DISPLAY] Display initialized");

    return true;
}

void Display::update(uint32_t nodeAddr, const char* nodeName, uint8_t neighborCount,
                     uint8_t routeCount, uint32_t txCount, uint32_t rxCount,
                     int16_t rssi, int8_t snr) {
    if (!isInitialized || !display) {
        return;
    }

    // No auto-rotate - page changes are triggered by button press
    // (see main.cpp button handler)

    display->clearDisplay();

    switch (currentPage) {
        case 0:
            drawInfoPage(nodeAddr, nodeName);
            break;
        case 1:
            drawStatusPage(neighborCount, routeCount, txCount, rxCount);
            break;
        case 2:
            drawNeighborsPage();
            break;
        case 3:
            drawSignalPage(rssi, snr);
            break;
        case 4:
            drawGPSPage();
            break;
        case 5:
            drawMessagesPage();
            break;
        case 6:
            drawBatteryPage();
            break;
    }

    display->display();
}

void Display::updateWithNeighbors(uint32_t nodeAddr, const char* nodeName,
                                   uint8_t neighborCount, uint8_t routeCount,
                                   uint32_t txCount, uint32_t rxCount,
                                   int16_t rssi, int8_t snr,
                                   DisplayNeighbor* neighbors, uint8_t numNeighbors) {
    // Cache neighbor info
    cachedNeighborCount = (numNeighbors > 4) ? 4 : numNeighbors;
    for (uint8_t i = 0; i < cachedNeighborCount; i++) {
        cachedNeighbors[i] = neighbors[i];
    }

    // Call regular update
    update(nodeAddr, nodeName, neighborCount, routeCount, txCount, rxCount, rssi, snr);
}

void Display::nextPage() {
    currentPage = (currentPage + 1) % DISPLAY_NUM_PAGES;
    lastPageChange = millis();
}

void Display::prevPage() {
    if (currentPage == 0) {
        currentPage = DISPLAY_NUM_PAGES - 1;
    } else {
        currentPage--;
    }
    lastPageChange = millis();
}

void Display::drawInfoPage(uint32_t nodeAddr, const char* nodeName) {
    display->setTextSize(1);
    display->setCursor(0, 0);

    // Title with page indicator
    display->println("== LNK-22 [1/7] ==");
    display->print("FW: ");
    display->println(LNK22_VERSION);
    display->println();

    // Node name (large text)
    display->setTextSize(2);
    if (nodeName != nullptr && strlen(nodeName) > 0) {
        // Show up to 10 chars of name
        char buf[11];
        strncpy(buf, nodeName, 10);
        buf[10] = '\0';
        display->println(buf);
    } else {
        display->println("Node");
    }

    // Node address (small text)
    display->setTextSize(1);
    display->println();
    display->print("0x");
    display->println(nodeAddr, HEX);

    // Uptime in simple format
    unsigned long secs = millis() / 1000;
    display->print("Up: ");
    display->print(secs);
    display->println("s");
}

void Display::drawStatusPage(uint8_t neighborCount, uint8_t routeCount,
                              uint32_t txCount, uint32_t rxCount) {
    display->setTextSize(1);
    display->setCursor(0, 0);

    // Title with page indicator
    display->println("= Network [2/7] =");
    display->println();

    // Network stats (larger for emphasis)
    display->setTextSize(2);
    display->print("N:");
    display->print(neighborCount);
    display->print(" R:");
    display->println(routeCount);

    display->setTextSize(1);
    display->println();

    // Traffic stats
    display->print("TX: ");
    display->print(txCount);
    display->print("  RX: ");
    display->println(rxCount);

    // Channel info
    display->println();
    display->print("Channel: ");
    display->println(DEFAULT_CHANNEL);
}

void Display::drawSignalPage(int16_t rssi, int8_t snr) {
    display->setTextSize(1);
    display->setCursor(0, 0);

    // Title with page indicator
    display->println("= Signal [4/7] =");
    display->println();

    // RSSI
    display->setTextSize(2);
    display->print(rssi);
    display->println(" dBm");

    // SNR
    display->setTextSize(1);
    display->print("SNR: ");
    display->print(snr);
    display->println(" dB");

    // Signal quality indicator
    display->println();
    if (rssi > -80) {
        display->println("Quality: EXCELLENT");
    } else if (rssi > -100) {
        display->println("Quality: GOOD");
    } else if (rssi > -120) {
        display->println("Quality: FAIR");
    } else {
        display->println("Quality: POOR");
    }
}

void Display::drawNeighborsPage() {
    display->setTextSize(1);
    display->setCursor(0, 0);

    // Title with page indicator
    display->println("Neighbors [3/7]");

    if (cachedNeighborCount == 0) {
        display->println();
        display->println("No neighbors");
        display->println("discovered yet");
        return;
    }

    // Show up to 4 neighbors (fits on screen)
    for (uint8_t i = 0; i < cachedNeighborCount && i < 4; i++) {
        // Truncate name to fit (10 chars max for name + signal)
        char nameBuf[11];
        strncpy(nameBuf, cachedNeighbors[i].name, 10);
        nameBuf[10] = '\0';

        display->print(nameBuf);

        // Right-align RSSI
        int nameLen = strlen(nameBuf);
        int spaces = 10 - nameLen;
        for (int s = 0; s < spaces; s++) {
            display->print(' ');
        }

        // Signal bar indicator
        display->print(' ');
        if (cachedNeighbors[i].rssi > -70) {
            display->println("[####]");
        } else if (cachedNeighbors[i].rssi > -90) {
            display->println("[### ]");
        } else if (cachedNeighbors[i].rssi > -110) {
            display->println("[##  ]");
        } else {
            display->println("[#   ]");
        }
    }

    // If more neighbors exist, show count
    if (cachedNeighborCount > 4) {
        display->print("+");
        display->print(cachedNeighborCount - 4);
        display->println(" more");
    }
}

void Display::showGPS(double latitude, double longitude, uint8_t satellites) {
    if (!isInitialized || !display) {
        return;
    }

    display->clearDisplay();
    display->setTextSize(1);
    display->setCursor(0, 0);

    display->println("=== GPS ===");
    display->println();

    display->print("Lat: ");
    display->println(latitude, 6);

    display->print("Lon: ");
    display->println(longitude, 6);

    display->print("Sats: ");
    display->println(satellites);

    display->display();
}

void Display::showMessage(const char* message) {
    if (!isInitialized || !display) {
        return;
    }

    display->clearDisplay();
    display->setTextSize(1);
    display->setCursor(0, 0);

    display->println("=== Message ===");
    display->println();
    display->println(message);

    display->display();
}

void Display::clear() {
    if (!isInitialized || !display) {
        return;
    }

    display->clearDisplay();
    display->display();
}

void Display::updateGPS(double latitude, double longitude, float altitude, uint8_t satellites, bool valid) {
    cachedLatitude = latitude;
    cachedLongitude = longitude;
    cachedAltitude = altitude;
    cachedSatellites = satellites;
    cachedGPSValid = valid;
}

void Display::updateLastMessage(const char* message, uint32_t source) {
    strncpy(cachedLastMessage, message, sizeof(cachedLastMessage) - 1);
    cachedLastMessage[sizeof(cachedLastMessage) - 1] = '\0';
    cachedMessageSource = source;
    cachedMessageTime = millis();
}

void Display::updateBattery(uint8_t percent, float voltage, bool charging) {
    cachedBatteryPercent = percent;
    cachedBatteryVoltage = voltage;
    cachedCharging = charging;
}

void Display::drawGPSPage() {
    display->setTextSize(1);
    display->setCursor(0, 0);

    // Title with page indicator
    display->println("=== GPS [5/7] ===");
    display->println();

    if (!cachedGPSValid || cachedSatellites == 0) {
        display->setTextSize(2);
        display->println("No Fix");
        display->setTextSize(1);
        display->println();
        display->println("Waiting for GPS...");
        display->print("Satellites: ");
        display->println(cachedSatellites);
    } else {
        // Latitude
        display->print("Lat:  ");
        display->println(cachedLatitude, 6);

        // Longitude
        display->print("Lon:  ");
        display->println(cachedLongitude, 6);

        // Altitude
        display->print("Alt:  ");
        display->print(cachedAltitude, 1);
        display->println(" m");

        display->println();

        // Satellites with visual bar
        display->print("Sats: ");
        display->print(cachedSatellites);
        display->print(" ");
        // Draw satellite strength bar
        int bars = cachedSatellites > 12 ? 6 : cachedSatellites / 2;
        display->print("[");
        for (int i = 0; i < 6; i++) {
            display->print(i < bars ? '#' : ' ');
        }
        display->println("]");
    }
}

void Display::drawMessagesPage() {
    display->setTextSize(1);
    display->setCursor(0, 0);

    // Title with page indicator
    display->println("=== Msgs [6/7] ===");
    display->println();

    if (cachedMessageTime == 0) {
        display->println("No messages yet");
        display->println();
        display->println("Send or receive a");
        display->println("message to see it");
        display->println("displayed here.");
    } else {
        // Time since message
        unsigned long ago = (millis() - cachedMessageTime) / 1000;
        display->print("Last msg: ");
        if (ago < 60) {
            display->print(ago);
            display->println("s ago");
        } else if (ago < 3600) {
            display->print(ago / 60);
            display->println("m ago");
        } else {
            display->print(ago / 3600);
            display->println("h ago");
        }

        // Source
        display->print("From: 0x");
        display->println(cachedMessageSource, HEX);
        display->println();

        // Message content (wrap to screen)
        display->println("Message:");
        // Show up to 48 chars across 4 lines of 12 chars each
        int len = strlen(cachedLastMessage);
        for (int i = 0; i < len && i < 48; i += 16) {
            char line[17];
            strncpy(line, cachedLastMessage + i, 16);
            line[16] = '\0';
            display->println(line);
        }
    }
}

void Display::drawBatteryPage() {
    display->setTextSize(1);
    display->setCursor(0, 0);

    // Title with page indicator
    display->println("=== Battery [7/7]");
    display->println();

    // Large percentage
    display->setTextSize(2);
    display->print(cachedBatteryPercent);
    display->println("%");

    display->setTextSize(1);
    display->println();

    // Voltage
    display->print("Voltage: ");
    display->print(cachedBatteryVoltage, 2);
    display->println("V");

    // Status
    display->print("Status:  ");
    if (cachedCharging) {
        display->println("Charging");
    } else if (cachedBatteryPercent > 80) {
        display->println("Full");
    } else if (cachedBatteryPercent > 20) {
        display->println("Good");
    } else {
        display->println("Low!");
    }

    // Visual battery bar
    display->println();
    int bars = cachedBatteryPercent / 10;
    display->print("[");
    for (int i = 0; i < 10; i++) {
        display->print(i < bars ? '#' : ' ');
    }
    display->println("]");

    // Hint
    display->println();
    display->println("Press button: next");
}

#endif // HAS_DISPLAY
