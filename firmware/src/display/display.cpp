/**
 * LNK-22 Display Implementation
 */

#include "display.h"

#ifdef HAS_DISPLAY

Display::Display() :
    display(nullptr),
    isInitialized(false),
    currentPage(0),
    lastPageChange(0)
{
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

    unsigned long now = millis();

    // Auto-rotate pages every 5 seconds
    if (now - lastPageChange > 5000) {
        currentPage = (currentPage + 1) % 3;
        lastPageChange = now;
    }

    display->clearDisplay();

    switch (currentPage) {
        case 0:
            drawInfoPage(nodeAddr, nodeName);
            break;
        case 1:
            drawStatusPage(neighborCount, routeCount, txCount, rxCount);
            break;
        case 2:
            drawSignalPage(rssi, snr);
            break;
    }

    display->display();
}

void Display::drawInfoPage(uint32_t nodeAddr, const char* nodeName) {
    display->setTextSize(1);
    display->setCursor(0, 0);

    // Title with version
    display->println("=== LNK-22 ===");
    display->println("FW: 1.3.0");
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

    // Title
    display->println("=== Network ===");
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

    // Title
    display->println("=== Signal ===");
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

#endif // HAS_DISPLAY
