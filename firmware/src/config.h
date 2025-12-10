/**
 * LNK-22 Configuration
 * Professional LoRa mesh networking
 * Hardware and protocol configuration parameters
 */

#ifndef LNK22_CONFIG_H
#define LNK22_CONFIG_H

// Protocol version
#define PROTOCOL_VERSION 2  // v2 adds channel support

// Channel configuration (8 channels: 0-7)
#define NUM_CHANNELS 8
#define DEFAULT_CHANNEL 0
#define ADMIN_CHANNEL 7  // Reserved for admin/config

// Timing constants (milliseconds)
#define BEACON_INTERVAL 30000        // Send beacon every 30 seconds
#define ROUTE_TIMEOUT 300000         // Routes expire after 5 minutes
#define ACK_TIMEOUT 5000             // Wait 5 seconds for ACK
#define DISPLAY_UPDATE_INTERVAL 1000 // Update display every second

// Network parameters
#define MAX_ROUTES 32                // Maximum routing table entries
#define MAX_NEIGHBORS 16             // Maximum neighbor table entries
#define MAX_RETRIES 3                // Maximum packet retransmission attempts
#define MAX_TTL 15                   // Maximum time-to-live (hops)
#define MAX_PAYLOAD_SIZE 255         // Maximum payload size in bytes

// Radio configuration (from Meshtastic RAK4631 variant)
#ifdef RAK4631
    #define LORA_SS_PIN 42       // P1.10 NSS
    #define LORA_RST_PIN 38      // P1.06 NRESET (was 43 - WRONG!)
    #define LORA_DIO1_PIN 47     // P1.15 DIO1
    #define LORA_BUSY_PIN 46     // P1.14 BUSY
    #define LORA_TXEN_PIN -1     // Not used - DIO2 controls antenna switch
    #define LORA_RXEN_PIN -1     // Not used - DIO2 controls antenna switch
#elif defined(RAK11200)
    #define LORA_SS_PIN 32
    #define LORA_RST_PIN 33
    #define LORA_DIO1_PIN 38
    #define LORA_BUSY_PIN 36
    #define LORA_TXEN_PIN -1
    #define LORA_RXEN_PIN -1
#elif defined(HELTEC_V3)
    #define LORA_SS_PIN 8
    #define LORA_RST_PIN 12
    #define LORA_DIO1_PIN 14
    #define LORA_BUSY_PIN 13
    #define LORA_TXEN_PIN -1
    #define LORA_RXEN_PIN -1
#endif

// LoRa radio parameters - Optimized for maximum range
// SF10 provides 5-10x better range than SF7 with acceptable data rate (~1.76 kbps)
// Range: 10-15 km line-of-sight, 2-5 km urban
#define LORA_FREQUENCY 915000000     // 915 MHz (US ISM band)
#define LORA_BANDWIDTH 125000        // 125 kHz (optimal for range)
#define LORA_SPREADING_FACTOR 10     // SF10 (balanced range/speed - UPDATED from SF7)
#define LORA_CODING_RATE 5           // 4/5 (error correction)
#define LORA_TX_POWER 22             // 22 dBm (max for SX1262)
#define LORA_PREAMBLE_LENGTH 8       // 8 symbols
#define LORA_SYNC_WORD 0x12          // Private network

// Adaptive data rate thresholds (future feature)
#define ADR_RSSI_THRESHOLD_SF7  -80  // Switch to SF7 when RSSI > -80 dBm
#define ADR_RSSI_THRESHOLD_SF9  -110 // Switch to SF9 when RSSI -80 to -110
#define ADR_RSSI_THRESHOLD_SF10 -120 // Default SF10 when RSSI -110 to -120
// SF12 when RSSI < -120 dBm (extreme range mode)

// Cryptography
#define KEY_SIZE 32                  // 256-bit keys
#define NONCE_SIZE 12                // 96-bit nonce for ChaCha20
#define TAG_SIZE 16                  // 128-bit authentication tag

// Serial configuration
#define SERIAL_BAUD 115200

// Debug flags
#define DEBUG_RADIO 1
#define DEBUG_MESH 1
#define DEBUG_CRYPTO 0
#define DEBUG_GPS 1

#endif // LNK22_CONFIG_H
