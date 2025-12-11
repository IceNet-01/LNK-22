/**
 * @file link.h
 * @brief LNK-22 Reticulum-style Link-Oriented Transport
 *
 * Implements secure, bidirectional links between nodes with:
 * - Ephemeral key exchange (X25519)
 * - Per-link encryption (ChaCha20-Poly1305)
 * - Link state management
 * - Automatic reconnection
 *
 * Inspired by Reticulum Network Stack (reticulum.network)
 */

#ifndef LINK_H
#define LINK_H

#include <Arduino.h>
#include "../config.h"
#include "../crypto/crypto.h"
#include "ratchet.h"

// Link Configuration
#define MAX_LINKS 8                       // Maximum concurrent links
#define LINK_TIMEOUT_MS 300000            // Link expires after 5 minutes of inactivity
#define LINK_KEEPALIVE_INTERVAL 60000     // Send keepalive every 60 seconds
#define LINK_HANDSHAKE_TIMEOUT 10000      // Handshake must complete in 10 seconds
#define LINK_MAX_RETRIES 3                // Max handshake retries
#define LINK_ID_SIZE 16                   // Link identifier size

// Link states
enum LinkState {
    LINK_CLOSED = 0,
    LINK_PENDING,      // Waiting for handshake response
    LINK_HANDSHAKE,    // Handshake in progress
    LINK_ACTIVE,       // Link established and active
    LINK_STALE         // Link timed out, needs refresh
};

// Link packet types
enum LinkPacketType {
    LINK_REQUEST = 0x10,     // Request to establish link
    LINK_ACCEPT = 0x11,      // Accept link request
    LINK_REJECT = 0x12,      // Reject link request
    LINK_DATA = 0x13,        // Encrypted data over link
    LINK_ACK = 0x14,         // Acknowledge data
    LINK_KEEPALIVE = 0x15,   // Keep link alive
    LINK_CLOSE = 0x16,       // Close link gracefully
    LINK_IDENTIFY = 0x17     // Identity announcement
};

// Link request packet (initiator -> responder)
struct __attribute__((packed)) LinkRequest {
    uint8_t linkId[LINK_ID_SIZE];    // Proposed link ID
    uint8_t publicKey[32];            // Initiator's ephemeral public key
    uint8_t identity[32];             // Initiator's identity hash
    uint32_t timestamp;               // Request timestamp
    uint8_t flags;                    // Link flags
};

// Link accept packet (responder -> initiator)
struct __attribute__((packed)) LinkAccept {
    uint8_t linkId[LINK_ID_SIZE];    // Confirmed link ID
    uint8_t publicKey[32];            // Responder's ephemeral public key
    uint8_t proof[16];                // Proof of key derivation
};

// Link data packet
struct __attribute__((packed)) LinkDataHeader {
    uint8_t linkId[LINK_ID_SIZE];    // Link identifier
    uint32_t sequence;                // Sequence number
    uint16_t length;                  // Payload length
    uint8_t flags;                    // Data flags
    // Followed by encrypted payload + auth tag
};

// Link statistics
struct LinkStats {
    uint32_t packetsIn;
    uint32_t packetsOut;
    uint32_t bytesIn;
    uint32_t bytesOut;
    uint32_t retransmits;
    int16_t lastRssi;
    int8_t lastSnr;
};

// Individual link structure
struct Link {
    uint8_t linkId[LINK_ID_SIZE];     // Unique link identifier
    uint32_t peerAddress;              // Remote node address
    LinkState state;                   // Current state

    // Cryptographic material
    uint8_t localPrivateKey[32];       // Our ephemeral private key
    uint8_t localPublicKey[32];        // Our ephemeral public key
    uint8_t peerPublicKey[32];         // Peer's ephemeral public key
    uint8_t sharedSecret[32];          // Derived shared secret
    uint8_t txKey[32];                 // TX encryption key
    uint8_t rxKey[32];                 // RX encryption key

    // Double Ratchet state for forward secrecy
    RatchetState ratchetState;
    bool ratchetEnabled;               // true if using double ratchet

    // Sequence numbers
    uint32_t txSequence;
    uint32_t rxSequence;

    // Timing
    uint32_t createdAt;
    uint32_t lastActivity;
    uint32_t lastKeepalive;
    uint8_t handshakeRetries;

    // Statistics
    LinkStats stats;

    // Valid flag
    bool valid;
    bool initiator;                    // true if we initiated this link
};

// Link event callback types
typedef void (*LinkEstablishedCallback)(uint32_t peerAddress, const uint8_t* linkId);
typedef void (*LinkClosedCallback)(uint32_t peerAddress, const uint8_t* linkId);
typedef void (*LinkDataCallback)(uint32_t peerAddress, const uint8_t* linkId,
                                  const uint8_t* data, uint16_t length);

class LinkManager {
public:
    LinkManager();

    /**
     * @brief Initialize the link manager
     * @param nodeAddr This node's address
     * @param crypto Pointer to crypto subsystem
     */
    void begin(uint32_t nodeAddr, Crypto* crypto);

    /**
     * @brief Update - call from main loop
     */
    void update();

    /**
     * @brief Request a new link to a peer
     * @param peerAddress Target node address
     * @return Link ID or nullptr on failure
     */
    const uint8_t* requestLink(uint32_t peerAddress);

    /**
     * @brief Close an active link
     * @param peerAddress Peer to disconnect
     */
    void closeLink(uint32_t peerAddress);

    /**
     * @brief Close link by ID
     * @param linkId Link identifier
     */
    void closeLinkById(const uint8_t* linkId);

    /**
     * @brief Send data over an established link
     * @param peerAddress Destination
     * @param data Data to send
     * @param length Data length
     * @return true if sent successfully
     */
    bool sendData(uint32_t peerAddress, const uint8_t* data, uint16_t length);

    /**
     * @brief Send data over link by ID
     * @param linkId Link identifier
     * @param data Data to send
     * @param length Data length
     * @return true if sent successfully
     */
    bool sendDataById(const uint8_t* linkId, const uint8_t* data, uint16_t length);

    /**
     * @brief Handle incoming link packet
     * @param fromAddress Source address
     * @param type Packet type
     * @param data Packet data
     * @param length Data length
     * @param rssi Signal strength
     * @param snr Signal quality
     */
    void handlePacket(uint32_t fromAddress, uint8_t type,
                      const uint8_t* data, uint16_t length,
                      int16_t rssi, int8_t snr);

    /**
     * @brief Check if we have an active link to a peer
     * @param peerAddress Address to check
     * @return true if active link exists
     */
    bool hasActiveLink(uint32_t peerAddress);

    /**
     * @brief Get link state
     * @param peerAddress Peer address
     * @return Link state
     */
    LinkState getLinkState(uint32_t peerAddress);

    /**
     * @brief Get link by peer address
     * @param peerAddress Peer to find
     * @return Pointer to link or nullptr
     */
    Link* getLinkByPeer(uint32_t peerAddress);

    /**
     * @brief Get link by ID
     * @param linkId Link identifier
     * @return Pointer to link or nullptr
     */
    Link* getLinkById(const uint8_t* linkId);

    /**
     * @brief Get number of active links
     */
    uint8_t getActiveLinkCount();

    /**
     * @brief Get link by index
     */
    Link* getLinkByIndex(uint8_t index);

    /**
     * @brief Set callbacks
     */
    void setEstablishedCallback(LinkEstablishedCallback cb) { onEstablished = cb; }
    void setClosedCallback(LinkClosedCallback cb) { onClosed = cb; }
    void setDataCallback(LinkDataCallback cb) { onData = cb; }

    /**
     * @brief Set packet send function
     */
    void setSendFunction(bool (*sendFn)(uint32_t dest, uint8_t type,
                                         const uint8_t* data, uint16_t len));

    /**
     * @brief Print link status
     */
    void printStatus();

    /**
     * @brief Accept all incoming link requests (default: true)
     */
    void setAutoAccept(bool accept) { autoAccept = accept; }

    /**
     * @brief Enable forward secrecy mode (default: true)
     * When enabled, new links use Double Ratchet for forward secrecy
     */
    void setForwardSecrecy(bool enable) { forwardSecrecyEnabled = enable; }

    /**
     * @brief Check if forward secrecy is enabled for a link
     */
    bool hasForwardSecrecy(uint32_t peerAddress);

private:
    Link links[MAX_LINKS];
    uint32_t nodeAddress;
    Crypto* cryptoPtr;
    bool autoAccept;
    bool forwardSecrecyEnabled;

    // Callbacks
    LinkEstablishedCallback onEstablished;
    LinkClosedCallback onClosed;
    LinkDataCallback onData;
    bool (*sendPacket)(uint32_t, uint8_t, const uint8_t*, uint16_t);

    // Link management
    int findEmptySlot();
    int findLinkByPeer(uint32_t peerAddress);
    int findLinkById(const uint8_t* linkId);
    void generateLinkId(uint8_t* linkId);
    void deriveKeys(Link* link);

    // Packet handlers
    void handleLinkRequest(uint32_t from, const uint8_t* data, uint16_t len, int16_t rssi, int8_t snr);
    void handleLinkAccept(uint32_t from, const uint8_t* data, uint16_t len, int16_t rssi, int8_t snr);
    void handleLinkReject(uint32_t from, const uint8_t* data, uint16_t len);
    void handleLinkData(uint32_t from, const uint8_t* data, uint16_t len, int16_t rssi, int8_t snr);
    void handleLinkKeepalive(uint32_t from, const uint8_t* data, uint16_t len);
    void handleLinkClose(uint32_t from, const uint8_t* data, uint16_t len);

    // Maintenance
    void checkTimeouts();
    void sendKeepalives();
};

// Global instance
extern LinkManager linkManager;

#endif // LINK_H
