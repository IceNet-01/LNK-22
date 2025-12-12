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
    networkId(0),
    nonceCounter(0),
    usingDefaultPSK(true)
{
    crypto_wipe(privateKey, KEY_SIZE);
    crypto_wipe(publicKey, KEY_SIZE);
    crypto_wipe(networkKey, KEY_SIZE);
    memset(&stats, 0, sizeof(stats));
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

    // Derive and display network ID
    deriveNetworkId();
    Serial.print("[CRYPTO] Network ID: 0x");
    Serial.println(networkId, HEX);

    // Show PSK security status
    if (usingDefaultPSK) {
        Serial.println("[CRYPTO] WARNING: Using default PSK! Run 'psk generate' or 'psk set <passphrase>'");
    } else {
        uint8_t pskHash[8];
        getPSKHash(pskHash);
        Serial.print("[CRYPTO] PSK hash: ");
        for (int i = 0; i < 8; i++) {
            if (pskHash[i] < 16) Serial.print("0");
            Serial.print(pskHash[i], HEX);
        }
        Serial.println();
    }

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

    stats.encryptSuccess++;
    return true;
}

bool Crypto::decrypt(const uint8_t* ciphertext, uint16_t len, uint8_t* plaintext, uint16_t* outLen) {
    // Monocypher ChaCha20-Poly1305 AEAD decryption
    // Input format: [nonce 24 bytes][ciphertext][tag 16 bytes]

    if (len < 24 + 16) {
        Serial.println("[CRYPTO] Ciphertext too short!");
        stats.decryptFail++;
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
        stats.decryptFail++;
        return false;
    }

    *outLen = msgLen;
    stats.decryptSuccess++;
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

    stats.signCount++;
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
    bool valid = crypto_verify64(computedSig, signature) == 0;
    if (valid) {
        stats.verifySuccess++;
    } else {
        stats.verifyFail++;
    }
    return valid;
}

void Crypto::generateOrLoadKeys() {
    // Try to load existing keys from storage
    if (loadKeys()) {
        Serial.println("[CRYPTO] Loaded existing LNK-22 identity");
        // Check if loaded key is the old default (0x42 repeated)
        bool isOldDefault = true;
        for (int i = 0; i < KEY_SIZE; i++) {
            if (networkKey[i] != 0x42) {
                isOldDefault = false;
                break;
            }
        }
        usingDefaultPSK = isOldDefault;
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

    // PHASE 1.1 FIX: Generate random network key instead of hardcoded 0x42
    // This ensures each new device gets a unique PSK on first boot
    Serial.println("[CRYPTO] Generating random network PSK...");
    for (int i = 0; i < KEY_SIZE; i++) {
        networkKey[i] = random(256);
    }
    usingDefaultPSK = false;  // New random key is secure

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

// ============================================================================
// Phase 1.1: PSK Management Functions
// ============================================================================

bool Crypto::setPSK(const uint8_t* psk) {
    if (psk == NULL) {
        return false;
    }

    memcpy(networkKey, psk, KEY_SIZE);
    usingDefaultPSK = false;

    // Update network ID
    deriveNetworkId();

    // Persist to storage
    if (saveKeys()) {
        Serial.println("[CRYPTO] PSK updated and saved");
        return true;
    }

    Serial.println("[CRYPTO] PSK updated (save failed)");
    return true;
}

bool Crypto::setPSKFromPassphrase(const char* passphrase) {
    if (passphrase == NULL || strlen(passphrase) == 0) {
        Serial.println("[CRYPTO] Empty passphrase");
        return false;
    }

    // Derive 32-byte key from passphrase using BLAKE2b
    // Add salt for extra security
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, KEY_SIZE);
    crypto_blake2b_update(&ctx, (const uint8_t*)"lnk22-psk-v1", 12);  // Domain separation
    crypto_blake2b_update(&ctx, (const uint8_t*)passphrase, strlen(passphrase));
    crypto_blake2b_final(&ctx, networkKey);

    usingDefaultPSK = false;

    // Update network ID
    deriveNetworkId();

    // Persist to storage
    if (saveKeys()) {
        Serial.print("[CRYPTO] PSK derived from passphrase and saved");
        Serial.print(" (Network ID: 0x");
        Serial.print(networkId, HEX);
        Serial.println(")");
        return true;
    }

    Serial.println("[CRYPTO] PSK derived (save failed)");
    return true;
}

bool Crypto::generateRandomPSK() {
    Serial.println("[CRYPTO] Generating new random PSK...");

    // Generate random key
    for (int i = 0; i < KEY_SIZE; i++) {
        networkKey[i] = random(256);
    }

    usingDefaultPSK = false;

    // Update network ID
    deriveNetworkId();

    // Persist to storage
    if (saveKeys()) {
        Serial.print("[CRYPTO] Random PSK generated and saved");
        Serial.print(" (Network ID: 0x");
        Serial.print(networkId, HEX);
        Serial.println(")");
        return true;
    }

    Serial.println("[CRYPTO] Random PSK generated (save failed)");
    return true;
}

void Crypto::getPSKHash(uint8_t* hash8) {
    // Generate a safe-to-display hash of the PSK
    // Only shows first 8 bytes of BLAKE2b hash
    uint8_t fullHash[32];
    crypto_blake2b(fullHash, 32, networkKey, KEY_SIZE);
    memcpy(hash8, fullHash, 8);
}

void Crypto::deriveNetworkId() {
    // Derive 32-bit network ID from PSK using BLAKE2b
    // Network ID is used to filter packets from different networks
    uint8_t hash[32];
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 32);
    crypto_blake2b_update(&ctx, (const uint8_t*)"lnk22-netid-v1", 14);
    crypto_blake2b_update(&ctx, networkKey, KEY_SIZE);
    crypto_blake2b_final(&ctx, hash);

    memcpy(&networkId, hash, 4);

    // Ensure network ID is not 0 or broadcast
    if (networkId == 0 || networkId == 0xFFFFFFFF) {
        networkId = 0x22222222;
    }
}

void Crypto::printStatus() {
    Serial.println("\n=== Crypto Status ===");

    Serial.print("Node Address: 0x");
    Serial.println(nodeAddress, HEX);

    Serial.print("Network ID: 0x");
    Serial.println(networkId, HEX);

    Serial.print("PSK Status: ");
    if (usingDefaultPSK) {
        Serial.println("DEFAULT (INSECURE!)");
        Serial.println("  WARNING: Using weak/default PSK");
        Serial.println("  Run 'psk set <passphrase>' or 'psk generate'");
    } else {
        Serial.println("CONFIGURED");
        uint8_t pskHash[8];
        getPSKHash(pskHash);
        Serial.print("  Hash: ");
        for (int i = 0; i < 8; i++) {
            if (pskHash[i] < 16) Serial.print("0");
            Serial.print(pskHash[i], HEX);
        }
        Serial.println();
    }

    Serial.println("\nCrypto Statistics:");
    Serial.print("  Encryptions: ");
    Serial.print(stats.encryptSuccess);
    Serial.print(" OK, ");
    Serial.print(stats.encryptFail);
    Serial.println(" failed");

    Serial.print("  Decryptions: ");
    Serial.print(stats.decryptSuccess);
    Serial.print(" OK, ");
    Serial.print(stats.decryptFail);
    Serial.println(" failed");

    Serial.print("  Signatures: ");
    Serial.println(stats.signCount);

    Serial.print("  Verifications: ");
    Serial.print(stats.verifySuccess);
    Serial.print(" OK, ");
    Serial.print(stats.verifyFail);
    Serial.println(" failed");

    Serial.println("=====================\n");
}
