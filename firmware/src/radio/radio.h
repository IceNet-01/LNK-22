/**
 * MeshNet Radio Driver
 * Abstraction layer for LoRa radio hardware
 */

#ifndef MESHNET_RADIO_H
#define MESHNET_RADIO_H

#include <Arduino.h>
#include "../config.h"
#include "../protocol/protocol.h"

// Radio events callback
typedef void (*RadioRxCallback)(Packet* packet, int16_t rssi, int8_t snr);

class Radio {
public:
    Radio();
    ~Radio();

    // Initialize radio hardware
    bool begin();

    // Transmit packet
    bool send(const Packet* packet);

    // Check for received packets
    void update();

    // Set receive callback
    void setRxCallback(RadioRxCallback callback);

    // Radio statistics
    int16_t getLastRSSI() const { return lastRSSI; }
    int8_t getLastSNR() const { return lastSNR; }
    uint32_t getTxCount() const { return txCount; }
    uint32_t getRxCount() const { return rxCount; }
    uint32_t getErrorCount() const { return errorCount; }

    // Configuration
    void setFrequency(uint32_t freq);
    void setSpreadingFactor(uint8_t sf);
    void setBandwidth(uint32_t bw);
    void setTxPower(uint8_t power);

    // Power management
    void sleep();
    void wake();

private:
    RadioRxCallback rxCallback;

    // Statistics
    int16_t lastRSSI;
    int8_t lastSNR;
    uint32_t txCount;
    uint32_t rxCount;
    uint32_t errorCount;

    // Internal state
    bool isInitialized;
    bool isSleeping;

    // Hardware-specific initialization
    bool initHardware();

    // Internal handlers
    void handleRxDone();
    void handleTxDone();
    void handleError();
};

#endif // MESHNET_RADIO_H
