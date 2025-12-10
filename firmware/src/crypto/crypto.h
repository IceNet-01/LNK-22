/**
 * MeshNet Cryptography Layer
 * Handles encryption, signing, and key management
 */

#ifndef MESHNET_CRYPTO_H
#define MESHNET_CRYPTO_H

#include <Arduino.h>
#include "../config.h"

class Crypto {
public:
    Crypto();
    ~Crypto();

    // Initialize crypto subsystem
    void begin();

    // Get node address (derived from identity)
    uint32_t getNodeAddress() const { return nodeAddress; }

    // Encrypt payload
    bool encrypt(const uint8_t* plaintext, uint16_t len, uint8_t* ciphertext, uint16_t* outLen);

    // Decrypt payload
    bool decrypt(const uint8_t* ciphertext, uint16_t len, uint8_t* plaintext, uint16_t* outLen);

    // Sign data
    bool sign(const uint8_t* data, uint16_t len, uint8_t* signature);

    // Verify signature
    bool verify(const uint8_t* data, uint16_t len, const uint8_t* signature, uint32_t signer);

private:
    uint32_t nodeAddress;
    uint8_t privateKey[KEY_SIZE];
    uint8_t publicKey[KEY_SIZE];
    uint8_t networkKey[KEY_SIZE];
    uint32_t nonceCounter;

    // Key generation and storage
    void generateOrLoadKeys();
    bool saveKeys();
    bool loadKeys();

    // Utility functions
    void generateNonce(uint8_t* nonce);
    uint32_t deriveAddress(const uint8_t* pubKey);
};

#endif // MESHNET_CRYPTO_H
