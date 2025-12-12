/**
 * LNK-22 Cryptography Layer
 * Using Monocypher (Public Domain/CC0) - Zero LGPL Risk
 * Handles ChaCha20-Poly1305 encryption, BLAKE2b signing, and key management
 */

#ifndef LNK22_CRYPTO_H
#define LNK22_CRYPTO_H

#include <Arduino.h>
#include "../config.h"

// Crypto statistics
struct CryptoStats {
    uint32_t encryptSuccess;
    uint32_t encryptFail;
    uint32_t decryptSuccess;
    uint32_t decryptFail;
    uint32_t signCount;
    uint32_t verifySuccess;
    uint32_t verifyFail;
};

class Crypto {
public:
    Crypto();
    ~Crypto();

    // Initialize crypto subsystem
    void begin();

    // Get node address (derived from identity)
    uint32_t getNodeAddress() const { return nodeAddress; }

    // Get network ID (derived from PSK hash - first 4 bytes)
    uint32_t getNetworkId() const { return networkId; }

    // Encrypt payload
    bool encrypt(const uint8_t* plaintext, uint16_t len, uint8_t* ciphertext, uint16_t* outLen);

    // Decrypt payload
    bool decrypt(const uint8_t* ciphertext, uint16_t len, uint8_t* plaintext, uint16_t* outLen);

    // Sign data
    bool sign(const uint8_t* data, uint16_t len, uint8_t* signature);

    // Verify signature
    bool verify(const uint8_t* data, uint16_t len, const uint8_t* signature, uint32_t signer);

    // ========================================
    // PSK (Pre-Shared Key) Management - Phase 1.1
    // ========================================

    // Set network PSK from raw bytes (32 bytes)
    bool setPSK(const uint8_t* psk);

    // Set network PSK from passphrase (derives key using BLAKE2b)
    bool setPSKFromPassphrase(const char* passphrase);

    // Generate new random PSK
    bool generateRandomPSK();

    // Get PSK hash (safe to display - first 8 bytes of BLAKE2b hash)
    void getPSKHash(uint8_t* hash8);

    // Export PSK (for network setup) - returns pointer to internal key
    const uint8_t* getPSK() const { return networkKey; }

    // Check if using default/weak PSK
    bool isDefaultPSK() const { return usingDefaultPSK; }

    // Get crypto statistics
    const CryptoStats& getStats() const { return stats; }

    // Print crypto status to serial
    void printStatus();

private:
    uint32_t nodeAddress;
    uint32_t networkId;          // Derived from PSK hash
    uint8_t privateKey[KEY_SIZE];
    uint8_t publicKey[KEY_SIZE];
    uint8_t networkKey[KEY_SIZE];
    uint32_t nonceCounter;
    bool usingDefaultPSK;        // True if using weak/default key
    CryptoStats stats;

    // Key generation and storage
    void generateOrLoadKeys();
    bool saveKeys();
    bool loadKeys();

    // Utility functions
    void generateNonce(uint8_t* nonce);
    uint32_t deriveAddress(const uint8_t* pubKey);
    void deriveNetworkId();      // Calculate network ID from PSK
};

#endif // LNK22_CRYPTO_H
