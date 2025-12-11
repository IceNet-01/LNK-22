/**
 * @file ratchet.h
 * @brief LNK-22 Double Ratchet Key Management
 *
 * Implements Signal Protocol-style double ratchet for forward secrecy:
 * - DH ratchet for new key agreement
 * - Symmetric ratchet for message keys
 * - Automatic key rotation
 * - Out-of-order message handling
 */

#ifndef RATCHET_H
#define RATCHET_H

#include <Arduino.h>
#include "../config.h"

// Ratchet configuration
#define RATCHET_MAX_SKIP 100        // Max skipped message keys to store
#define RATCHET_KEY_SIZE 32
#define RATCHET_CHAIN_KEY_SIZE 32

// Chain key structure (for symmetric ratchet)
struct ChainKey {
    uint8_t key[RATCHET_CHAIN_KEY_SIZE];
    uint32_t counter;
};

// Skipped message key (for out-of-order handling)
struct SkippedKey {
    uint8_t dhPublic[32];     // DH public key that produced this chain
    uint32_t messageNumber;    // Message number in that chain
    uint8_t messageKey[32];    // The actual message key
    uint32_t timestamp;        // When it was stored
    bool valid;
};

// Ratchet state for one peer
struct RatchetState {
    // DH Ratchet keys
    uint8_t dhPrivate[32];        // Our current DH private key
    uint8_t dhPublic[32];         // Our current DH public key
    uint8_t dhRemote[32];         // Remote's current DH public key
    uint8_t rootKey[32];          // Root key for deriving chain keys

    // Sending chain
    ChainKey sendChain;

    // Receiving chain
    ChainKey recvChain;

    // Message counters
    uint32_t sendCounter;         // Messages sent in current sending chain
    uint32_t recvCounter;         // Messages received in current receiving chain
    uint32_t prevChainLength;     // Length of previous sending chain

    // Skipped keys for out-of-order messages
    SkippedKey skippedKeys[RATCHET_MAX_SKIP];
    uint8_t skippedCount;

    // State flags
    bool initialized;
    bool dhRatchetDone;           // Has DH ratchet been performed
    uint32_t peerAddress;
};

class DoubleRatchet {
public:
    DoubleRatchet();

    /**
     * @brief Initialize as initiator (Alice)
     * @param state Ratchet state to initialize
     * @param sharedSecret Initial shared secret from X25519
     * @param remotePublic Remote's initial DH public key
     */
    void initAlice(RatchetState* state, const uint8_t* sharedSecret,
                   const uint8_t* remotePublic);

    /**
     * @brief Initialize as responder (Bob)
     * @param state Ratchet state to initialize
     * @param sharedSecret Initial shared secret from X25519
     * @param ourPrivate Our DH private key used in initial exchange
     * @param ourPublic Our DH public key
     */
    void initBob(RatchetState* state, const uint8_t* sharedSecret,
                 const uint8_t* ourPrivate, const uint8_t* ourPublic);

    /**
     * @brief Encrypt a message
     * @param state Ratchet state
     * @param plaintext Input plaintext
     * @param ptLen Plaintext length
     * @param ciphertext Output buffer (must be ptLen + 48 bytes)
     * @param ctLen Output ciphertext length
     * @return true on success
     */
    bool encrypt(RatchetState* state, const uint8_t* plaintext, uint16_t ptLen,
                 uint8_t* ciphertext, uint16_t* ctLen);

    /**
     * @brief Decrypt a message
     * @param state Ratchet state
     * @param ciphertext Input ciphertext
     * @param ctLen Ciphertext length
     * @param plaintext Output buffer
     * @param ptLen Output plaintext length
     * @return true on success
     */
    bool decrypt(RatchetState* state, const uint8_t* ciphertext, uint16_t ctLen,
                 uint8_t* plaintext, uint16_t* ptLen);

    /**
     * @brief Get current public key for header
     */
    void getCurrentPublicKey(RatchetState* state, uint8_t* pubKey);

    /**
     * @brief Clear ratchet state (wipe keys)
     */
    void clear(RatchetState* state);

private:
    // KDF functions
    void kdfRootKey(uint8_t* rootKey, uint8_t* chainKey,
                    const uint8_t* rootKeyIn, const uint8_t* dhOutput);
    void kdfChainKey(uint8_t* chainKey, uint8_t* messageKey,
                     const uint8_t* chainKeyIn);

    // DH ratchet step
    void dhRatchet(RatchetState* state, const uint8_t* remotePublic);

    // Skip message keys
    void skipMessageKeys(RatchetState* state, uint32_t until);

    // Try skipped keys
    bool trySkippedKeys(RatchetState* state, const uint8_t* dhPublic,
                        uint32_t messageNum, uint8_t* messageKey);

    // Generate new DH keypair
    void generateDH(uint8_t* privateKey, uint8_t* publicKey);
};

// Global instance
extern DoubleRatchet ratchet;

#endif // RATCHET_H
