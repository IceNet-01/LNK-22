/**
 * @file ratchet.cpp
 * @brief LNK-22 Double Ratchet Implementation
 */

#include "ratchet.h"
#include "../crypto/monocypher.h"

// Global instance
DoubleRatchet ratchet;

// Message header for ratchet messages
struct __attribute__((packed)) RatchetHeader {
    uint8_t dhPublic[32];      // Sender's current DH public key
    uint32_t prevChainLen;     // Previous chain length (for skipping)
    uint32_t messageNum;       // Message number in current chain
};

DoubleRatchet::DoubleRatchet() {
}

void DoubleRatchet::initAlice(RatchetState* state, const uint8_t* sharedSecret,
                               const uint8_t* remotePublic) {
    memset(state, 0, sizeof(RatchetState));

    // Store remote's public key
    memcpy(state->dhRemote, remotePublic, 32);

    // Generate our DH keypair
    generateDH(state->dhPrivate, state->dhPublic);

    // Perform initial DH
    uint8_t dhOutput[32];
    crypto_x25519(dhOutput, state->dhPrivate, state->dhRemote);

    // Derive root key and send chain key
    // RK, CK_s = KDF(SK, DH(DH_s, DH_r))
    // where SK is the initial shared secret
    uint8_t combined[64];
    memcpy(combined, sharedSecret, 32);
    memcpy(combined + 32, dhOutput, 32);

    uint8_t output[64];
    crypto_blake2b(output, 64, combined, 64);

    memcpy(state->rootKey, output, 32);
    memcpy(state->sendChain.key, output + 32, 32);
    state->sendChain.counter = 0;

    // Recv chain will be set on first message from Bob
    state->recvChain.counter = 0;

    state->sendCounter = 0;
    state->recvCounter = 0;
    state->prevChainLength = 0;
    state->dhRatchetDone = true;  // Alice has done initial DH ratchet
    state->initialized = true;

    // Wipe sensitive intermediate values
    crypto_wipe(dhOutput, 32);
    crypto_wipe(combined, 64);
}

void DoubleRatchet::initBob(RatchetState* state, const uint8_t* sharedSecret,
                             const uint8_t* ourPrivate, const uint8_t* ourPublic) {
    memset(state, 0, sizeof(RatchetState));

    // Store our keys (Bob keeps his initial DH keys)
    memcpy(state->dhPrivate, ourPrivate, 32);
    memcpy(state->dhPublic, ourPublic, 32);

    // Set initial root key from shared secret
    memcpy(state->rootKey, sharedSecret, 32);

    // Bob's chains will be set when he receives first message from Alice
    state->sendCounter = 0;
    state->recvCounter = 0;
    state->prevChainLength = 0;
    state->dhRatchetDone = false;  // Bob hasn't done DH ratchet yet
    state->initialized = true;
}

bool DoubleRatchet::encrypt(RatchetState* state, const uint8_t* plaintext,
                             uint16_t ptLen, uint8_t* ciphertext, uint16_t* ctLen) {
    if (!state->initialized) return false;

    // Build header
    RatchetHeader* header = (RatchetHeader*)ciphertext;
    memcpy(header->dhPublic, state->dhPublic, 32);
    header->prevChainLen = state->prevChainLength;
    header->messageNum = state->sendCounter;

    // Derive message key from chain
    uint8_t messageKey[32];
    kdfChainKey(state->sendChain.key, messageKey, state->sendChain.key);

    // Encrypt with message key
    uint8_t nonce[24];
    memset(nonce, 0, 24);
    memcpy(nonce, &state->sendCounter, 4);

    uint8_t* ct = ciphertext + sizeof(RatchetHeader);
    uint8_t* tag = ct + ptLen;

    crypto_aead_lock(ct, tag, messageKey, nonce,
                     (uint8_t*)header, sizeof(RatchetHeader),
                     plaintext, ptLen);

    state->sendCounter++;
    *ctLen = sizeof(RatchetHeader) + ptLen + 16;

    // Wipe message key
    crypto_wipe(messageKey, 32);

    return true;
}

bool DoubleRatchet::decrypt(RatchetState* state, const uint8_t* ciphertext,
                             uint16_t ctLen, uint8_t* plaintext, uint16_t* ptLen) {
    if (!state->initialized) return false;
    if (ctLen < sizeof(RatchetHeader) + 16) return false;

    const RatchetHeader* header = (const RatchetHeader*)ciphertext;
    uint16_t payloadLen = ctLen - sizeof(RatchetHeader) - 16;

    // Check if we need to do a DH ratchet
    if (memcmp(header->dhPublic, state->dhRemote, 32) != 0) {
        // New DH public key from remote - do DH ratchet
        skipMessageKeys(state, header->prevChainLen);
        dhRatchet(state, header->dhPublic);
    }

    // Skip ahead if needed
    if (header->messageNum > state->recvCounter) {
        skipMessageKeys(state, header->messageNum);
    }

    // Check for skipped key first
    uint8_t messageKey[32];
    bool foundSkipped = trySkippedKeys(state, header->dhPublic,
                                        header->messageNum, messageKey);

    if (!foundSkipped) {
        // Derive message key
        if (header->messageNum != state->recvCounter) {
            // Out of order and no skipped key - can't decrypt
            return false;
        }
        kdfChainKey(state->recvChain.key, messageKey, state->recvChain.key);
        state->recvCounter++;
    }

    // Decrypt
    uint8_t nonce[24];
    memset(nonce, 0, 24);
    memcpy(nonce, &header->messageNum, 4);

    const uint8_t* ct = ciphertext + sizeof(RatchetHeader);
    const uint8_t* tag = ct + payloadLen;

    int result = crypto_aead_unlock(plaintext, tag, messageKey, nonce,
                                     (uint8_t*)header, sizeof(RatchetHeader),
                                     ct, payloadLen);

    crypto_wipe(messageKey, 32);

    if (result != 0) {
        return false;
    }

    *ptLen = payloadLen;
    return true;
}

void DoubleRatchet::getCurrentPublicKey(RatchetState* state, uint8_t* pubKey) {
    memcpy(pubKey, state->dhPublic, 32);
}

void DoubleRatchet::clear(RatchetState* state) {
    crypto_wipe(state->dhPrivate, 32);
    crypto_wipe(state->rootKey, 32);
    crypto_wipe(state->sendChain.key, 32);
    crypto_wipe(state->recvChain.key, 32);

    for (int i = 0; i < RATCHET_MAX_SKIP; i++) {
        crypto_wipe(state->skippedKeys[i].messageKey, 32);
    }

    memset(state, 0, sizeof(RatchetState));
}

// Private methods

void DoubleRatchet::kdfRootKey(uint8_t* rootKeyOut, uint8_t* chainKeyOut,
                                const uint8_t* rootKeyIn, const uint8_t* dhOutput) {
    // KDF(rk, dh_out) -> (rk', ck)
    uint8_t input[64];
    memcpy(input, rootKeyIn, 32);
    memcpy(input + 32, dhOutput, 32);

    uint8_t output[64];
    crypto_blake2b(output, 64, input, 64);

    memcpy(rootKeyOut, output, 32);
    memcpy(chainKeyOut, output + 32, 32);

    crypto_wipe(input, 64);
    crypto_wipe(output, 64);
}

void DoubleRatchet::kdfChainKey(uint8_t* chainKeyOut, uint8_t* messageKeyOut,
                                 const uint8_t* chainKeyIn) {
    // CK, MK = KDF(CK)
    // We use different info bytes to derive chain vs message key

    uint8_t input[33];
    memcpy(input, chainKeyIn, 32);

    // Message key: append 0x01
    input[32] = 0x01;
    crypto_blake2b(messageKeyOut, 32, input, 33);

    // Chain key: append 0x02
    input[32] = 0x02;
    crypto_blake2b(chainKeyOut, 32, input, 33);

    crypto_wipe(input, 33);
}

void DoubleRatchet::dhRatchet(RatchetState* state, const uint8_t* remotePublic) {
    // Save previous chain length
    state->prevChainLength = state->sendCounter;

    // Update remote's public key
    memcpy(state->dhRemote, remotePublic, 32);

    // Derive new receiving chain
    uint8_t dhOutput[32];
    crypto_x25519(dhOutput, state->dhPrivate, state->dhRemote);
    kdfRootKey(state->rootKey, state->recvChain.key, state->rootKey, dhOutput);
    state->recvCounter = 0;

    // Generate new DH keypair
    generateDH(state->dhPrivate, state->dhPublic);

    // Derive new sending chain
    crypto_x25519(dhOutput, state->dhPrivate, state->dhRemote);
    kdfRootKey(state->rootKey, state->sendChain.key, state->rootKey, dhOutput);
    state->sendCounter = 0;

    state->dhRatchetDone = true;

    crypto_wipe(dhOutput, 32);
}

void DoubleRatchet::skipMessageKeys(RatchetState* state, uint32_t until) {
    if (state->recvCounter + RATCHET_MAX_SKIP < until) {
        // Too many skipped messages - security concern
        Serial.println("[RATCHET] Too many skipped messages");
        return;
    }

    while (state->recvCounter < until) {
        // Store skipped key
        if (state->skippedCount < RATCHET_MAX_SKIP) {
            SkippedKey* sk = &state->skippedKeys[state->skippedCount];
            memcpy(sk->dhPublic, state->dhRemote, 32);
            sk->messageNumber = state->recvCounter;
            kdfChainKey(state->recvChain.key, sk->messageKey, state->recvChain.key);
            sk->timestamp = millis();
            sk->valid = true;
            state->skippedCount++;
        } else {
            // Discard oldest
            kdfChainKey(state->recvChain.key, state->recvChain.key, state->recvChain.key);
        }
        state->recvCounter++;
    }
}

bool DoubleRatchet::trySkippedKeys(RatchetState* state, const uint8_t* dhPublic,
                                    uint32_t messageNum, uint8_t* messageKey) {
    for (int i = 0; i < state->skippedCount; i++) {
        SkippedKey* sk = &state->skippedKeys[i];
        if (sk->valid &&
            memcmp(sk->dhPublic, dhPublic, 32) == 0 &&
            sk->messageNumber == messageNum) {

            memcpy(messageKey, sk->messageKey, 32);

            // Remove from list
            crypto_wipe(sk->messageKey, 32);
            sk->valid = false;

            return true;
        }
    }
    return false;
}

void DoubleRatchet::generateDH(uint8_t* privateKey, uint8_t* publicKey) {
    // Generate random private key
    for (int i = 0; i < 32; i++) {
        privateKey[i] = random(256);
    }
    // Derive public key
    crypto_x25519_public_key(publicKey, privateKey);
}
