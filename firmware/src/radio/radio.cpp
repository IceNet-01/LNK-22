/**
 * LNK-22 Radio Driver Implementation
 * Using RadioLib (jgromes) - The Proven Solution
 */

#include "radio.h"
#include <SPI.h>

#ifdef HAS_SX1262
#include <RadioLib.h>

// SPI instance for LoRa
SPIClass SPI_LORA(NRF_SPIM2, MISO, SCK, MOSI);

// RadioLib SX1262 instance (named 'lora' to avoid conflict with Radio class)
SX1262 lora = new Module(LORA_SS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN, SPI_LORA);

// ISR flag - set when packet received
volatile bool rxFlag = false;

// Forward declaration for ISR
void radioISR(void);
#endif

// Global radio instance pointer for callbacks
static Radio* g_radioInstance = nullptr;

Radio::Radio() :
    rxCallback(nullptr),
    lastRSSI(0),
    lastSNR(0),
    txCount(0),
    rxCount(0),
    errorCount(0),
    isInitialized(false),
    isSleeping(false)
{
}

Radio::~Radio() {
}

bool Radio::begin() {
    Serial.println("[RADIO] Starting RadioLib initialization...");
    Serial.flush();

    #ifdef HAS_SX1262
    // Set global instance for ISR
    g_radioInstance = this;

    // Initialize SPI
    SPI_LORA.begin();
    Serial.println("[RADIO] SPI initialized");
    Serial.flush();

    // Initialize SX1262
    // begin(freq, bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage)
    int state = lora.begin(
        LORA_FREQUENCY / 1000000.0,  // Frequency in MHz (915.0)
        LORA_BANDWIDTH / 1000.0,      // Bandwidth in kHz (125.0)
        LORA_SPREADING_FACTOR,        // SF (10)
        LORA_CODING_RATE,             // CR (5 = 4/5)
        LORA_SYNC_WORD,               // Sync word (0x12)
        LORA_TX_POWER,                // TX power (22 dBm)
        LORA_PREAMBLE_LENGTH,         // Preamble length (8)
        1.8                           // TCXO voltage for RAK4631
    );

    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("[RADIO] RadioLib initialization failed, code: ");
        Serial.println(state);
        Serial.flush();
        return false;
    }

    Serial.println("[RADIO] SX1262 chip initialized!");
    Serial.flush();

    // Configure DIO2 as RF switch (required for RAK4631)
    state = lora.setDio2AsRfSwitch(true);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("[RADIO] setDio2AsRfSwitch failed, code: ");
        Serial.println(state);
        Serial.flush();
        return false;
    }

    Serial.println("[RADIO] DIO2 configured as RF switch");
    Serial.flush();

    // Set CRC on
    state = lora.setCRC(true);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("[RADIO] setCRC failed, code: ");
        Serial.println(state);
        Serial.flush();
        // Non-fatal, continue
    }

    // Set up interrupt handler (use setPacketReceivedAction for RX, not setDio1Action!)
    lora.setPacketReceivedAction(radioISR);
    Serial.println("[RADIO] Packet received action configured");
    Serial.flush();

    // Start receiving
    state = lora.startReceive();
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("[RADIO] startReceive failed, code: ");
        Serial.println(state);
        Serial.flush();
        return false;
    }

    Serial.println("[RADIO] RadioLib initialized successfully! 🎉");
    Serial.print("[RADIO] Freq=");
    Serial.print(LORA_FREQUENCY / 1000000.0);
    Serial.print(" MHz, BW=");
    Serial.print(LORA_BANDWIDTH / 1000.0);
    Serial.print(" kHz, SF=");
    Serial.print(LORA_SPREADING_FACTOR);
    Serial.print(", CR=4/");
    Serial.print(LORA_CODING_RATE);
    Serial.print(", Power=");
    Serial.print(LORA_TX_POWER);
    Serial.println(" dBm");
    Serial.flush();

    #else
    Serial.println("[RADIO] No supported radio hardware found!");
    return false;
    #endif

    isInitialized = true;
    return true;
}

bool Radio::send(const Packet* packet) {
    if (!isInitialized || isSleeping) {
        return false;
    }

    uint16_t size = getPacketSize(packet);

    #if DEBUG_RADIO
    Serial.print("[RADIO] TX: ");
    Serial.print(size);
    Serial.print(" bytes to 0x");
    Serial.println(packet->header.destination, HEX);
    #endif

    #ifdef HAS_SX1262
    // Transmit using RadioLib
    int state = lora.transmit((uint8_t*)packet, size);

    if (state == RADIOLIB_ERR_NONE) {
        txCount++;

        // Return to RX mode
        lora.startReceive();

        #if DEBUG_RADIO
        Serial.println("[RADIO] TX complete");
        #endif

        return true;
    } else {
        Serial.print("[RADIO] TX failed, code: ");
        Serial.println(state);
        errorCount++;

        // Try to recover by restarting RX
        lora.startReceive();

        return false;
    }
    #else
    return false;
    #endif
}

void Radio::update() {
    if (!isInitialized || isSleeping) {
        return;
    }

    #ifdef HAS_SX1262
    // Check if we received a packet
    if (rxFlag) {
        rxFlag = false;

        // Get the packet
        uint8_t buffer[256];
        int state = lora.readData(buffer, sizeof(buffer));

        if (state == RADIOLIB_ERR_NONE) {
            // Packet received successfully!
            int16_t rssi = lora.getRSSI();
            int8_t snr = lora.getSNR();
            size_t length = lora.getPacketLength();

            Serial.print("[ISR] OnRxDone! sz=");
            Serial.print(length);
            Serial.print(" rssi=");
            Serial.print(rssi);
            Serial.print(" snr=");
            Serial.println(snr);
            Serial.flush();

            // Handle the packet
            handleRxDone(buffer, length, rssi, snr);
        } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
            Serial.println("[RADIO] CRC error!");
            errorCount++;
        } else {
            Serial.print("[RADIO] RX error, code: ");
            Serial.println(state);
            errorCount++;
        }

        // Return to RX mode
        lora.startReceive();
    }
    #endif
}

void Radio::setRxCallback(RadioRxCallback callback) {
    rxCallback = callback;
}

void Radio::setFrequency(uint32_t freq) {
    #ifdef HAS_SX1262
    int state = lora.setFrequency(freq / 1000000.0);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("[RADIO] setFrequency failed, code: ");
        Serial.println(state);
    }
    #endif
}

void Radio::setSpreadingFactor(uint8_t sf) {
    #ifdef HAS_SX1262
    int state = lora.setSpreadingFactor(sf);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("[RADIO] setSpreadingFactor failed, code: ");
        Serial.println(state);
    }
    #endif
}

void Radio::setBandwidth(uint32_t bw) {
    #ifdef HAS_SX1262
    int state = lora.setBandwidth(bw / 1000.0);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("[RADIO] setBandwidth failed, code: ");
        Serial.println(state);
    }
    #endif
}

void Radio::setTxPower(uint8_t power) {
    #ifdef HAS_SX1262
    int state = lora.setOutputPower(power);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("[RADIO] setOutputPower failed, code: ");
        Serial.println(state);
    }
    #endif
}

void Radio::sleep() {
    #ifdef HAS_SX1262
    lora.sleep();
    isSleeping = true;
    #endif
}

void Radio::wake() {
    #ifdef HAS_SX1262
    lora.standby();
    lora.startReceive();
    isSleeping = false;
    #endif
}

// Handle received packet (called from ISR)
void Radio::handleRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
    // Update statistics
    lastRSSI = rssi;
    lastSNR = snr;
    rxCount++;

    #if DEBUG_RADIO
    Serial.print("[RADIO] RX: ");
    Serial.print(size);
    Serial.print(" bytes, RSSI=");
    Serial.print(rssi);
    Serial.print(" dBm, SNR=");
    Serial.print(snr);
    Serial.println(" dB");
    #endif

    // Forward to callback if registered
    if (rxCallback) {
        Packet* packet = (Packet*)payload;
        rxCallback(packet, rssi, snr);
    }
}

#ifdef HAS_SX1262
// ISR for DIO1 interrupt - MUST be very simple!
void radioISR(void) {
    // Set flag to process in main loop
    rxFlag = true;
}
#endif
