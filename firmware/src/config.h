/**
 * MeshNet Configuration
 * Hardware and protocol configuration parameters
 */

#ifndef MESHNET_CONFIG_H
#define MESHNET_CONFIG_H

// Protocol version
#define PROTOCOL_VERSION 1

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

// Radio configuration
#ifdef RAK4631
    #define LORA_SS_PIN 42
    #define LORA_RST_PIN 43
    #define LORA_DIO1_PIN 47
    #define LORA_BUSY_PIN 46
    #define LORA_TXEN_PIN 14
    #define LORA_RXEN_PIN 15
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

// LoRa radio parameters
#define LORA_FREQUENCY 915000000     // 915 MHz (US)
#define LORA_BANDWIDTH 125000        // 125 kHz
#define LORA_SPREADING_FACTOR 7      // SF7 (fastest, shortest range)
#define LORA_CODING_RATE 5           // 4/5
#define LORA_TX_POWER 20             // 20 dBm (max)
#define LORA_PREAMBLE_LENGTH 8       // 8 symbols
#define LORA_SYNC_WORD 0x12          // Private network

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

#endif // MESHNET_CONFIG_H
