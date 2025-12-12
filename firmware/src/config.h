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
#define ROUTE_REFRESH_TIME 240000    // Refresh routes at 4 minutes (Phase 3.1)
#define NEIGHBOR_TIMEOUT 60000       // Neighbors expire after 60 seconds (Phase 3.2)
#define ACK_TIMEOUT 5000             // Base timeout for ACK (5 seconds)
#define ACK_TIMEOUT_MAX 60000        // Maximum timeout (60 seconds)
#define ACK_JITTER_MAX 500           // Maximum random jitter (0-500ms)
#define DISPLAY_UPDATE_INTERVAL 1000 // Update display every second

// Network parameters
#define MAX_ROUTES 32                // Maximum routing table entries
#define MAX_NEIGHBORS 16             // Maximum neighbor table entries
#define MAX_ROUTES_PER_DEST 3        // Maximum routes per destination (Phase 3.3)
#define MAX_RETRIES 3                // Maximum packet retransmission attempts
#define MAX_TTL 15                   // Maximum time-to-live (hops)
#define MAX_PAYLOAD_SIZE 255         // Maximum payload size in bytes
#define TX_WINDOW_SIZE 4             // Max outstanding packets (flow control)

// Node naming
#define MAX_NODE_NAMES 64            // Maximum named nodes
#define MAX_NAME_LENGTH 16           // Maximum name length (chars)

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

// Feature flags
#define FEATURE_STORE_FORWARD 1    // Store-and-forward messaging
#define FEATURE_ADR 1              // Adaptive Data Rate
#define FEATURE_EMERGENCY 1        // Emergency SOS mode
#define FEATURE_HISTORY 1          // Message history
#define FEATURE_LINKS 1            // Reticulum-style secure links
#define FEATURE_DTN 1              // Delay-tolerant networking
#define FEATURE_GEOROUTING 1       // Geographic routing
#define FEATURE_GROUPS 1           // Encrypted group channels
#define FEATURE_HYBRID_MAC 1       // Hybrid TDMA/CSMA-CA MAC layer

// Store-and-Forward settings
#define SF_MAX_MESSAGES 32
#define SF_MESSAGE_TTL_MS 3600000  // 1 hour

// Message history settings
#define HISTORY_MAX_MSGS 32

#endif // LNK22_CONFIG_H
