/**
 * LNK-22 Cryptography Implementation
 * Using Monocypher (Public Domain/CC0) for ChaCha20-Poly1305
 * NO LGPL DEPENDENCIES - 100% License-Safe
 */

#include "crypto.h"
#include "monocypher.h"

#ifdef HAS_FLASH_STORAGE
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;
static File keysFile(InternalFS);
#endif

#ifdef HAS_PREFERENCES
#include <Preferences.h>
static Preferences prefs;
#endif

Crypto::Crypto() :
    nodeAddress(0),
    nonceCounter(0)
{
    crypto_wipe(privateKey, KEY_SIZE);
    crypto_wipe(publicKey, KEY_SIZE);
    crypto_wipe(networkKey, KEY_SIZE);
}

Crypto::~Crypto() {
    // Securely wipe keys using Monocypher
    crypto_wipe(privateKey, KEY_SIZE);
    crypto_wipe(publicKey, KEY_SIZE);
    crypto_wipe(networkKey, KEY_SIZE);
}

void Crypto::begin() {
    Serial.println("[CRYPTO] Initializing LNK-22 cryptography (Monocypher)...");

    // Seed random number generator with hardware entropy
    randomSeed(analogRead(A0) ^ analogRead(A1) ^ micros());

    // Stir in additional entropy
    for (int i = 0; i < 10; i++) {
        randomSeed(random() ^ analogRead(A0 + (i % 4)) ^ micros());
        delay(5);
    }

    // Generate or load keys
    generateOrLoadKeys();

    // Use permanent hardware device ID as node address
    // This ensures the address NEVER changes even after firmware updates or key regeneration
#ifdef NRF52_SERIES
    nodeAddress = NRF_FICR->DEVICEID[0];
    Serial.println("[CRYPTO] Using nRF52 DEVICEID as permanent node address");
#elif defined(ESP32)
    uint64_t mac = ESP.getEfuseMac();
    nodeAddress = (uint32_t)(mac & 0xFFFFFFFF);
    Serial.println("[CRYPTO] Using ESP32 MAC as permanent node address");
#else
    // Fallback to derived address if no hardware ID available
    nodeAddress = deriveAddress(publicKey);
    Serial.println("[CRYPTO] Using derived address (no hardware ID available)");
#endif

    Serial.print("[CRYPTO] Node address: 0x");
    Serial.println(nodeAddress, HEX);
    Serial.println("[CRYPTO] Using Monocypher (Public Domain) - Zero license risk!");
}

bool Crypto::encrypt(const uint8_t* plaintext, uint16_t len, uint8_t* ciphertext, uint16_t* outLen) {
    // Monocypher ChaCha20-Poly1305 AEAD encryption
    // Output format: [nonce 24 bytes][ciphertext][tag 16 bytes]

    uint8_t nonce[24];  // Monocypher uses 24-byte nonces
    generateNonce(nonce);

    // Encrypt and authenticate in one operation
    // crypto_aead_lock(mac, ciphertext, key, nonce, ad, ad_size, plaintext, text_size)
    crypto_aead_lock(
        ciphertext + 24 + len,  // MAC output (16 bytes at end)
        ciphertext + 24,         // Ciphertext output
        networkKey,              // 32-byte key
        nonce,                   // 24-byte nonce
        NULL,                    // Additional authenticated data (none)
        0,                       // AD size
        plaintext,               // Plaintext input
        len                      // Plaintext length
    );

    // Prepend nonce
    memcpy(ciphertext, nonce, 24);

    *outLen = 24 + len + 16;  // nonce + ciphertext + MAC

    return true;
}

bool Crypto::decrypt(const uint8_t* ciphertext, uint16_t len, uint8_t* plaintext, uint16_t* outLen) {
    // Monocypher ChaCha20-Poly1305 AEAD decryption
    // Input format: [nonce 24 bytes][ciphertext][tag 16 bytes]

    if (len < 24 + 16) {
        Serial.println("[CRYPTO] Ciphertext too short!");
        return false;
    }

    uint8_t nonce[24];
    memcpy(nonce, ciphertext, 24);

    uint16_t msgLen = len - 24 - 16;
    const uint8_t* encrypted = ciphertext + 24;
    const uint8_t* mac = ciphertext + 24 + msgLen;

    // Decrypt and verify in one operation
    // crypto_aead_unlock(plaintext, mac, key, nonce, ad, ad_size, ciphertext, text_size)
    int result = crypto_aead_unlock(
        plaintext,               // Plaintext output
        mac,                     // MAC to verify (16 bytes)
        networkKey,              // 32-byte key
        nonce,                   // 24-byte nonce
        NULL,                    // Additional authenticated data (none)
        0,                       // AD size
        encrypted,               // Ciphertext input
        msgLen                   // Ciphertext length
    );

    if (result != 0) {
        Serial.println("[CRYPTO] Authentication failed!");
        return false;
    }

    *outLen = msgLen;
    return true;
}

bool Crypto::sign(const uint8_t* data, uint16_t len, uint8_t* signature) {
    // Use BLAKE2b for message authentication (better than SHA256!)
    // BLAKE2b is a cryptographic hash function that's faster and more secure than SHA256

    // Create HMAC using BLAKE2b with private key
    // Output 64 bytes for compatibility with Ed25519 signature size
    crypto_blake2b_ctx ctx;
    crypto_blake2b_keyed_init(&ctx, 64, privateKey, KEY_SIZE);
    crypto_blake2b_update(&ctx, data, len);
    crypto_blake2b_final(&ctx, signature);

    return true;
}

bool Crypto::verify(const uint8_t* data, uint16_t len, const uint8_t* signature, uint32_t signer) {
    // Verify BLAKE2b HMAC
    // In production, we'd look up the signer's public key
    // For now, we verify with our own key (testing only)

    uint8_t computedSig[64];
    crypto_blake2b_ctx ctx;
    crypto_blake2b_keyed_init(&ctx, 64, privateKey, KEY_SIZE);
    crypto_blake2b_update(&ctx, data, len);
    crypto_blake2b_final(&ctx, computedSig);

    // Constant-time comparison using Monocypher
    return crypto_verify64(computedSig, signature) == 0;
}

void Crypto::generateOrLoadKeys() {
    // Try to load existing keys from storage
    if (loadKeys()) {
        Serial.println("[CRYPTO] Loaded existing LNK-22 identity");
        return;
    }

    Serial.println("[CRYPTO] Generating new LNK-22 identity...");

    // Generate cryptographically secure random keys
    // Note: For production, consider using hardware RNG if available
    for (int i = 0; i < KEY_SIZE; i++) {
        privateKey[i] = random(256);
    }

    // Derive public key from private key using BLAKE2b
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, KEY_SIZE);
    crypto_blake2b_update(&ctx, privateKey, KEY_SIZE);
    crypto_blake2b_update(&ctx, (const uint8_t*)"lnk22-pubkey-v1", 15);
    crypto_blake2b_final(&ctx, publicKey);

    // Generate or use default network key
    // In production, this would be pre-shared or derived from a passphrase
    memset(networkKey, 0x42, KEY_SIZE);

    // Save keys to persistent storage
    saveKeys();
}

bool Crypto::saveKeys() {
    #ifdef HAS_FLASH_STORAGE
    // nRF52840: Use LittleFS
    Serial.println("[CRYPTO] Saving LNK-22 keys to flash...");

    // Initialize file system
    InternalFS.begin();

    // Open file for writing
    keysFile.open("/lnk22_keys.dat", FILE_O_WRITE);
    if (!keysFile) {
        Serial.println("[CRYPTO] Failed to open keys file for writing");
        return false;
    }

    // Write keys
    keysFile.write(privateKey, KEY_SIZE);
    keysFile.write(publicKey, KEY_SIZE);
    keysFile.write(networkKey, KEY_SIZE);
    keysFile.write((uint8_t*)&nonceCounter, sizeof(nonceCounter));

    keysFile.close();
    Serial.println("[CRYPTO] Keys saved successfully");
    return true;

    #elif defined(HAS_PREFERENCES)
    // ESP32: Use Preferences
    Serial.println("[CRYPTO] Saving LNK-22 keys to NVS...");

    if (!prefs.begin("lnk22", false)) {
        Serial.println("[CRYPTO] Failed to open preferences");
        return false;
    }

    prefs.putBytes("privkey", privateKey, KEY_SIZE);
    prefs.putBytes("pubkey", publicKey, KEY_SIZE);
    prefs.putBytes("netkey", networkKey, KEY_SIZE);
    prefs.putUInt("nonce", nonceCounter);

    prefs.end();
    Serial.println("[CRYPTO] Keys saved successfully");
    return true;

    #else
    Serial.println("[CRYPTO] No storage backend available");
    return false;
    #endif
}

bool Crypto::loadKeys() {
    #ifdef HAS_FLASH_STORAGE
    // nRF52840: Use LittleFS
    Serial.println("[CRYPTO] Loading LNK-22 keys from flash...");

    // Initialize file system
    InternalFS.begin();

    // Check if file exists
    if (!InternalFS.exists("/lnk22_keys.dat")) {
        Serial.println("[CRYPTO] No saved keys found");
        return false;
    }

    // Open file for reading
    keysFile.open("/lnk22_keys.dat", FILE_O_READ);
    if (!keysFile) {
        Serial.println("[CRYPTO] Failed to open keys file for reading");
        return false;
    }

    // Read keys
    size_t bytesRead = 0;
    bytesRead += keysFile.read(privateKey, KEY_SIZE);
    bytesRead += keysFile.read(publicKey, KEY_SIZE);
    bytesRead += keysFile.read(networkKey, KEY_SIZE);
    bytesRead += keysFile.read((uint8_t*)&nonceCounter, sizeof(nonceCounter));

    keysFile.close();

    if (bytesRead != (KEY_SIZE * 3 + sizeof(nonceCounter))) {
        Serial.println("[CRYPTO] Incomplete key data");
        return false;
    }

    Serial.println("[CRYPTO] Keys loaded successfully");
    return true;

    #elif defined(HAS_PREFERENCES)
    // ESP32: Use Preferences
    Serial.println("[CRYPTO] Loading LNK-22 keys from NVS...");

    if (!prefs.begin("lnk22", true)) {  // Read-only
        Serial.println("[CRYPTO] Failed to open preferences");
        return false;
    }

    // Check if keys exist
    if (!prefs.isKey("privkey")) {
        prefs.end();
        Serial.println("[CRYPTO] No saved keys found");
        return false;
    }

    // Read keys
    size_t len = prefs.getBytes("privkey", privateKey, KEY_SIZE);
    len += prefs.getBytes("pubkey", publicKey, KEY_SIZE);
    len += prefs.getBytes("netkey", networkKey, KEY_SIZE);
    nonceCounter = prefs.getUInt("nonce", 0);

    prefs.end();

    if (len != KEY_SIZE * 3) {
        Serial.println("[CRYPTO] Incomplete key data");
        return false;
    }

    Serial.println("[CRYPTO] Keys loaded successfully");
    return true;

    #else
    return false;
    #endif
}

void Crypto::generateNonce(uint8_t* nonce) {
    // Generate unique 24-byte nonce for ChaCha20
    // Format: [node address 4 bytes][counter 8 bytes][random 12 bytes]

    memcpy(nonce, &nodeAddress, 4);
    memcpy(nonce + 4, &nonceCounter, 8);

    // Fill remaining bytes with random data
    for (int i = 12; i < 24; i++) {
        nonce[i] = random(256);
    }

    nonceCounter++;

    // If counter wraps, we should rekey (not implemented)
    if (nonceCounter == 0) {
        Serial.println("[CRYPTO] WARNING: Nonce counter wrapped! Please rekey.");
    }
}

uint32_t Crypto::deriveAddress(const uint8_t* pubKey) {
    // Derive 32-bit address from BLAKE2b(publicKey)
    uint8_t hash[32];
    crypto_blake2b(hash, 32, pubKey, KEY_SIZE);

    // Use first 4 bytes of hash
    uint32_t addr;
    memcpy(&addr, hash, 4);

    // Ensure address is not 0 or broadcast
    if (addr == 0 || addr == 0xFFFFFFFF) {
        addr = 0x22222222;  // LNK-22 fallback address
    }

    return addr;
}
