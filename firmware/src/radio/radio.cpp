/**
 * LNK-22 Radio Driver Implementation
 */

#include "radio.h"
#include <SPI.h>

#ifdef HAS_SX1262
#include <SX126x-RAK4630.h>

// SX126x radio instance
hw_config hwConfig;

// SPI instance for LoRa (required by SX126x library)
SPIClass SPI_LORA(NRF_SPIM2, MISO, SCK, MOSI);

// Forward declarations for radio callbacks
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
void OnTxDone(void);
void OnRxTimeout(void);
void OnTxTimeout(void);
void OnRxError(void);
#endif

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
    Serial.println("[RADIO] Starting radio initialization...");
    Serial.flush();

    #ifdef HAS_SX1262
    Serial.println("[RADIO] Configuring hardware pins...");
    Serial.flush();

    // Configure hardware pins
    hwConfig.CHIP_TYPE = SX1262_CHIP;
    hwConfig.PIN_LORA_RESET = LORA_RST_PIN;
    hwConfig.PIN_LORA_NSS = LORA_SS_PIN;
    hwConfig.PIN_LORA_SCLK = SCK;
    hwConfig.PIN_LORA_MISO = MISO;
    hwConfig.PIN_LORA_DIO_1 = LORA_DIO1_PIN;
    hwConfig.PIN_LORA_BUSY = LORA_BUSY_PIN;
    hwConfig.PIN_LORA_MOSI = MOSI;
    hwConfig.RADIO_TXEN = LORA_TXEN_PIN;
    hwConfig.RADIO_RXEN = LORA_RXEN_PIN;
    hwConfig.USE_DIO2_ANT_SWITCH = false;
    hwConfig.USE_DIO3_TCXO = true;
    hwConfig.USE_DIO3_ANT_SWITCH = false;

    Serial.println("[RADIO] Pin configuration complete");
    Serial.print("[RADIO] Reset Pin: "); Serial.println(hwConfig.PIN_LORA_RESET);
    Serial.print("[RADIO] NSS Pin: "); Serial.println(hwConfig.PIN_LORA_NSS);
    Serial.print("[RADIO] DIO1 Pin: "); Serial.println(hwConfig.PIN_LORA_DIO_1);
    Serial.print("[RADIO] BUSY Pin: "); Serial.println(hwConfig.PIN_LORA_BUSY);
    Serial.flush();

    // Initialize SX126x
    Serial.println("[RADIO] Calling lora_hardware_init()...");
    Serial.flush();

    int initResult = lora_hardware_init(hwConfig);

    Serial.print("[RADIO] lora_hardware_init() returned: ");
    Serial.println(initResult);
    Serial.flush();

    if (initResult != 0) {
        Serial.println("[RADIO] Hardware initialization failed!");
        Serial.flush();
        return false;
    }

    Serial.println("[RADIO] Hardware initialized successfully!");
    Serial.flush();

    // Setup radio callbacks
    RadioEvents_t radioEvents;
    radioEvents.TxDone = OnTxDone;
    radioEvents.RxDone = OnRxDone;
    radioEvents.TxTimeout = OnTxTimeout;
    radioEvents.RxTimeout = OnRxTimeout;
    radioEvents.RxError = OnRxError;
    radioEvents.CadDone = NULL;
    radioEvents.FhssChangeChannel = NULL;
    radioEvents.PreAmpDetect = NULL;

    ::Radio.Init(&radioEvents);

    // Set radio configuration
    ::Radio.SetModem(MODEM_LORA);
    ::Radio.SetChannel(LORA_FREQUENCY);

    ::Radio.SetTxConfig(
        MODEM_LORA,
        LORA_TX_POWER,
        0,  // Frequency deviation (FSK only)
        LORA_BANDWIDTH,
        LORA_SPREADING_FACTOR,
        LORA_CODING_RATE,
        LORA_PREAMBLE_LENGTH,
        false,  // Fixed length
        true,   // CRC enabled
        false,  // Frequency hopping off
        0,      // Hop period
        false,  // IQ inversion off
        3000    // TX timeout
    );

    ::Radio.SetRxConfig(
        MODEM_LORA,
        LORA_BANDWIDTH,
        LORA_SPREADING_FACTOR,
        LORA_CODING_RATE,
        0,  // AFC bandwidth (FSK only)
        LORA_PREAMBLE_LENGTH,
        10, // Symbol timeout
        false,  // Fixed length
        0,  // Payload length (unused in variable mode)
        true,   // CRC enabled
        false,  // Frequency hopping off
        0,      // Hop period
        false,  // IQ inversion off
        true    // Continuous RX
    );

    // Set sync word
    ::Radio.SetPublicNetwork(false);
    uint8_t syncWord = LORA_SYNC_WORD;
    SX126xSetSyncWord(&syncWord);

    // Start receiving
    ::Radio.Rx(0);  // 0 = continuous RX

    Serial.println("[RADIO] SX1262 initialized successfully");
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
    ::Radio.Send((uint8_t*)packet, size);
    txCount++;
    return true;
    #else
    return false;
    #endif
}

void Radio::update() {
    if (!isInitialized || isSleeping) {
        return;
    }

    #ifdef HAS_SX1262
    // Process radio events
    ::Radio.IrqProcess();
    #endif
}

void Radio::setRxCallback(RadioRxCallback callback) {
    rxCallback = callback;
}

void Radio::setFrequency(uint32_t freq) {
    #ifdef HAS_SX1262
    ::Radio.SetChannel(freq);
    #endif
}

void Radio::setSpreadingFactor(uint8_t sf) {
    // TODO: Reconfigure radio with new SF
}

void Radio::setBandwidth(uint32_t bw) {
    // TODO: Reconfigure radio with new BW
}

void Radio::setTxPower(uint8_t power) {
    // TODO: Reconfigure radio with new power
}

void Radio::sleep() {
    #ifdef HAS_SX1262
    ::Radio.Sleep();
    isSleeping = true;
    #endif
}

void Radio::wake() {
    #ifdef HAS_SX1262
    ::Radio.Standby();
    ::Radio.Rx(0);
    isSleeping = false;
    #endif
}

// Global RX callback for SX126x library
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
    // This will be called by the radio library
    // We need to forward it to our Radio instance
    // TODO: Implement proper callback forwarding
}

void OnTxDone(void) {
    // TX complete
    #if DEBUG_RADIO
    Serial.println("[RADIO] TX complete");
    #endif

    // Return to RX mode
    #ifdef HAS_SX1262
    ::Radio.Rx(0);
    #endif
}

void OnRxTimeout(void) {
    #if DEBUG_RADIO
    Serial.println("[RADIO] RX timeout");
    #endif
}

void OnTxTimeout(void) {
    Serial.println("[RADIO] TX timeout!");
}

void OnRxError(void) {
    Serial.println("[RADIO] RX error!");
}
