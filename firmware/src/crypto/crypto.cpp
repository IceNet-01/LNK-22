/**
 * MeshNet Cryptography Implementation
 * Using Arduino Crypto library for ChaCha20-Poly1305
 */

#include "crypto.h"

#ifdef USE_CRYPTO_LIB
#include <ChaCha.h>
#include <Poly1305.h>
#include <SHA256.h>
#include <RNG.h>
#endif

Crypto::Crypto() :
    nodeAddress(0),
    nonceCounter(0)
{
    memset(privateKey, 0, KEY_SIZE);
    memset(publicKey, 0, KEY_SIZE);
    memset(networkKey, 0, KEY_SIZE);
}

Crypto::~Crypto() {
    // Securely wipe keys
    memset(privateKey, 0, KEY_SIZE);
    memset(publicKey, 0, KEY_SIZE);
    memset(networkKey, 0, KEY_SIZE);
}

void Crypto::begin() {
    Serial.println("[CRYPTO] Initializing cryptography...");

    #ifdef USE_CRYPTO_LIB
    // Initialize RNG with hardware sources
    RNG.begin("MeshNet " MESHNET_VERSION);

    // Stir in some entropy from analog pins
    for (int i = 0; i < 10; i++) {
        RNG.stir((uint8_t*)&i, sizeof(i), random(256));
        delay(10);
    }
    #endif

    // Generate or load keys
    generateOrLoadKeys();

    // Derive node address from public key
    nodeAddress = deriveAddress(publicKey);

    Serial.print("[CRYPTO] Node address: 0x");
    Serial.println(nodeAddress, HEX);
}

bool Crypto::encrypt(const uint8_t* plaintext, uint16_t len, uint8_t* ciphertext, uint16_t* outLen) {
    #ifdef USE_CRYPTO_LIB
    // ChaCha20-Poly1305 AEAD encryption
    // Output format: [nonce 12 bytes][ciphertext][tag 16 bytes]

    uint8_t nonce[NONCE_SIZE];
    generateNonce(nonce);

    // Setup ChaCha20
    ChaCha chacha;
    chacha.setKey(networkKey, KEY_SIZE);
    chacha.setIV(nonce, NONCE_SIZE);
    chacha.setCounter(0);

    // Encrypt
    chacha.encrypt(ciphertext + NONCE_SIZE, plaintext, len);

    // Compute Poly1305 MAC
    Poly1305 poly;
    poly.reset(networkKey);
    poly.update(ciphertext + NONCE_SIZE, len);
    poly.finalize(ciphertext + NONCE_SIZE + len, TAG_SIZE);

    // Prepend nonce
    memcpy(ciphertext, nonce, NONCE_SIZE);

    *outLen = NONCE_SIZE + len + TAG_SIZE;

    chacha.clear();
    poly.clear();

    return true;
    #else
    // Fallback: no encryption
    memcpy(ciphertext, plaintext, len);
    *outLen = len;
    return true;
    #endif
}

bool Crypto::decrypt(const uint8_t* ciphertext, uint16_t len, uint8_t* plaintext, uint16_t* outLen) {
    #ifdef USE_CRYPTO_LIB
    // ChaCha20-Poly1305 AEAD decryption
    // Input format: [nonce 12 bytes][ciphertext][tag 16 bytes]

    if (len < NONCE_SIZE + TAG_SIZE) {
        Serial.println("[CRYPTO] Ciphertext too short!");
        return false;
    }

    uint8_t nonce[NONCE_SIZE];
    memcpy(nonce, ciphertext, NONCE_SIZE);

    uint16_t msgLen = len - NONCE_SIZE - TAG_SIZE;
    const uint8_t* encrypted = ciphertext + NONCE_SIZE;
    const uint8_t* tag = ciphertext + NONCE_SIZE + msgLen;

    // Verify Poly1305 MAC
    uint8_t computedTag[TAG_SIZE];
    Poly1305 poly;
    poly.reset(networkKey);
    poly.update(encrypted, msgLen);
    poly.finalize(computedTag, TAG_SIZE);

    // Constant-time comparison
    uint8_t diff = 0;
    for (int i = 0; i < TAG_SIZE; i++) {
        diff |= computedTag[i] ^ tag[i];
    }

    if (diff != 0) {
        Serial.println("[CRYPTO] Authentication failed!");
        poly.clear();
        return false;
    }

    // Decrypt
    ChaCha chacha;
    chacha.setKey(networkKey, KEY_SIZE);
    chacha.setIV(nonce, NONCE_SIZE);
    chacha.setCounter(0);
    chacha.decrypt(plaintext, encrypted, msgLen);

    *outLen = msgLen;

    chacha.clear();
    poly.clear();

    return true;
    #else
    // Fallback: no decryption
    memcpy(plaintext, ciphertext, len);
    *outLen = len;
    return true;
    #endif
}

bool Crypto::sign(const uint8_t* data, uint16_t len, uint8_t* signature) {
    #ifdef USE_CRYPTO_LIB
    // HMAC-SHA256 for message authentication
    // (Ed25519 would be better but requires external library)

    SHA256 sha256;
    sha256.resetHMAC(privateKey, KEY_SIZE);
    sha256.update(data, len);
    sha256.finalizeHMAC(privateKey, KEY_SIZE, signature, 32);

    // Pad to 64 bytes for compatibility
    memset(signature + 32, 0, 32);

    return true;
    #else
    memset(signature, 0, 64);
    return true;
    #endif
}

bool Crypto::verify(const uint8_t* data, uint16_t len, const uint8_t* signature, uint32_t signer) {
    #ifdef USE_CRYPTO_LIB
    // Verify HMAC-SHA256
    // In production, we'd look up the signer's public key
    // For now, we verify with our own key (testing only)

    uint8_t computedSig[32];
    SHA256 sha256;
    sha256.resetHMAC(privateKey, KEY_SIZE);
    sha256.update(data, len);
    sha256.finalizeHMAC(privateKey, KEY_SIZE, computedSig, 32);

    // Constant-time comparison
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) {
        diff |= computedSig[i] ^ signature[i];
    }

    return diff == 0;
    #else
    return true;
    #endif
}

void Crypto::generateOrLoadKeys() {
    // Try to load existing keys from storage
    if (loadKeys()) {
        Serial.println("[CRYPTO] Loaded existing identity");
        return;
    }

    Serial.println("[CRYPTO] Generating new identity...");

    #ifdef USE_CRYPTO_LIB
    // Generate cryptographically secure random keys
    RNG.rand(privateKey, KEY_SIZE);

    // Derive public key from private key using SHA256
    // (simplified - in production use proper key derivation)
    SHA256 sha256;
    sha256.reset();
    sha256.update(privateKey, KEY_SIZE);
    sha256.update((const uint8_t*)"meshnet-pubkey", 14);
    sha256.finalize(publicKey, KEY_SIZE);

    // Generate or use default network key
    // In production, this would be pre-shared or derived from a passphrase
    memset(networkKey, 0x42, KEY_SIZE);

    #else
    // Fallback: pseudo-random keys
    for (int i = 0; i < KEY_SIZE; i++) {
        privateKey[i] = random(256);
        publicKey[i] = random(256);
        networkKey[i] = 0x42;
    }
    #endif

    // Save keys to persistent storage
    saveKeys();
}

bool Crypto::saveKeys() {
    #if defined(NRF52840) || defined(ESP32)
    // TODO: Implement flash storage
    // For nRF52: Use InternalFS
    // For ESP32: Use Preferences or SPIFFS

    Serial.println("[CRYPTO] Key storage not yet implemented");
    return false;
    #else
    return false;
    #endif
}

bool Crypto::loadKeys() {
    #if defined(NRF52840) || defined(ESP32)
    // TODO: Implement flash storage
    // For nRF52: Use InternalFS
    // For ESP32: Use Preferences or SPIFFS

    return false;
    #else
    return false;
    #endif
}

void Crypto::generateNonce(uint8_t* nonce) {
    // Generate unique nonce for ChaCha20
    // Format: [node address 4 bytes][counter 8 bytes]

    memcpy(nonce, &nodeAddress, 4);
    memcpy(nonce + 4, &nonceCounter, 8);

    nonceCounter++;

    // If counter wraps, we should rekey (not implemented)
    if (nonceCounter == 0) {
        Serial.println("[CRYPTO] WARNING: Nonce counter wrapped!");
    }
}

uint32_t Crypto::deriveAddress(const uint8_t* pubKey) {
    #ifdef USE_CRYPTO_LIB
    // Derive 32-bit address from SHA256(publicKey)
    uint8_t hash[32];
    SHA256 sha256;
    sha256.reset();
    sha256.update(pubKey, KEY_SIZE);
    sha256.finalize(hash, 32);

    // Use first 4 bytes of hash
    uint32_t addr;
    memcpy(&addr, hash, 4);

    // Ensure address is not 0 or broadcast
    if (addr == 0 || addr == 0xFFFFFFFF) {
        addr = 0x12345678;
    }

    return addr;
    #else
    // Fallback: use first 4 bytes
    uint32_t addr = 0;
    for (int i = 0; i < 4; i++) {
        addr |= ((uint32_t)pubKey[i]) << (i * 8);
    }

    if (addr == 0 || addr == 0xFFFFFFFF) {
        addr = 0x12345678;
    }

    return addr;
    #endif
}
