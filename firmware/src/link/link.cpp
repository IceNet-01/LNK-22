/**
 * @file link.cpp
 * @brief LNK-22 Reticulum-style Link-Oriented Transport Implementation
 */

#include "link.h"
#include "ratchet.h"
#include "../crypto/monocypher.h"

// Global instance
LinkManager linkManager;

LinkManager::LinkManager() :
    nodeAddress(0),
    cryptoPtr(nullptr),
    autoAccept(true),
    forwardSecrecyEnabled(true),  // Enable by default for maximum security
    onEstablished(nullptr),
    onClosed(nullptr),
    onData(nullptr),
    sendPacket(nullptr)
{
    for (int i = 0; i < MAX_LINKS; i++) {
        links[i].valid = false;
        links[i].state = LINK_CLOSED;
        links[i].ratchetEnabled = false;
    }
}

void LinkManager::begin(uint32_t nodeAddr, Crypto* crypto) {
    nodeAddress = nodeAddr;
    cryptoPtr = crypto;

    Serial.println("[LINK] Link manager initialized");
    Serial.print("[LINK] Max links: ");
    Serial.println(MAX_LINKS);
    Serial.print("[LINK] Forward secrecy (Double Ratchet): ");
    Serial.println(forwardSecrecyEnabled ? "ENABLED" : "DISABLED");
}

void LinkManager::update() {
    checkTimeouts();
    sendKeepalives();
}

const uint8_t* LinkManager::requestLink(uint32_t peerAddress) {
    // Check if link already exists
    int existing = findLinkByPeer(peerAddress);
    if (existing >= 0) {
        if (links[existing].state == LINK_ACTIVE) {
            Serial.print("[LINK] Link already active to 0x");
            Serial.println(peerAddress, HEX);
            return links[existing].linkId;
        }
    }

    // Find empty slot
    int slot = findEmptySlot();
    if (slot < 0) {
        Serial.println("[LINK] No free link slots");
        return nullptr;
    }

    Link* link = &links[slot];
    memset(link, 0, sizeof(Link));

    // Generate ephemeral keypair using monocypher
    // Generate random private key
    for (int i = 0; i < 32; i++) {
        link->localPrivateKey[i] = random(256);
    }
    // Derive public key
    crypto_x25519_public_key(link->localPublicKey, link->localPrivateKey);

    // Generate link ID
    generateLinkId(link->linkId);

    // Setup link
    link->peerAddress = peerAddress;
    link->state = LINK_PENDING;
    link->createdAt = millis();
    link->lastActivity = millis();
    link->txSequence = 0;
    link->rxSequence = 0;
    link->valid = true;
    link->initiator = true;

    // Build link request
    LinkRequest req;
    memcpy(req.linkId, link->linkId, LINK_ID_SIZE);
    memcpy(req.publicKey, link->localPublicKey, 32);

    // Use node address hash as identity
    uint8_t identityData[4];
    identityData[0] = nodeAddress & 0xFF;
    identityData[1] = (nodeAddress >> 8) & 0xFF;
    identityData[2] = (nodeAddress >> 16) & 0xFF;
    identityData[3] = (nodeAddress >> 24) & 0xFF;
    crypto_blake2b(req.identity, 32, identityData, 4);

    req.timestamp = millis();
    req.flags = 0;

    // Send request
    if (sendPacket) {
        Serial.print("[LINK] Requesting link to 0x");
        Serial.println(peerAddress, HEX);
        sendPacket(peerAddress, LINK_REQUEST, (uint8_t*)&req, sizeof(LinkRequest));
    }

    return link->linkId;
}

void LinkManager::closeLink(uint32_t peerAddress) {
    int slot = findLinkByPeer(peerAddress);
    if (slot < 0) return;

    closeLinkById(links[slot].linkId);
}

void LinkManager::closeLinkById(const uint8_t* linkId) {
    int slot = findLinkById(linkId);
    if (slot < 0) return;

    Link* link = &links[slot];

    // Send close notification
    if (sendPacket && link->state == LINK_ACTIVE) {
        sendPacket(link->peerAddress, LINK_CLOSE, link->linkId, LINK_ID_SIZE);
    }

    // Callback
    if (onClosed) {
        onClosed(link->peerAddress, link->linkId);
    }

    Serial.print("[LINK] Closed link to 0x");
    Serial.println(link->peerAddress, HEX);

    // Clear sensitive data
    crypto_wipe(link->localPrivateKey, 32);
    crypto_wipe(link->sharedSecret, 32);
    crypto_wipe(link->txKey, 32);
    crypto_wipe(link->rxKey, 32);

    // Clear ratchet state if enabled
    if (link->ratchetEnabled) {
        ratchet.clear(&link->ratchetState);
        link->ratchetEnabled = false;
    }

    link->valid = false;
    link->state = LINK_CLOSED;
}

bool LinkManager::sendData(uint32_t peerAddress, const uint8_t* data, uint16_t length) {
    int slot = findLinkByPeer(peerAddress);
    if (slot < 0) return false;

    return sendDataById(links[slot].linkId, data, length);
}

bool LinkManager::sendDataById(const uint8_t* linkId, const uint8_t* data, uint16_t length) {
    int slot = findLinkById(linkId);
    if (slot < 0) return false;

    Link* link = &links[slot];
    if (link->state != LINK_ACTIVE) {
        Serial.println("[LINK] Cannot send - link not active");
        return false;
    }

    // Use Double Ratchet if enabled for this link
    if (link->ratchetEnabled) {
        // Ratchet-encrypted packet: header + ratchet_ciphertext
        // Ratchet adds its own header (40 bytes) + 16 byte tag
        uint8_t ratchetCt[length + 56];  // ratchet overhead
        uint16_t ratchetCtLen;

        if (!ratchet.encrypt(&link->ratchetState, data, length, ratchetCt, &ratchetCtLen)) {
            Serial.println("[LINK] Ratchet encryption failed");
            return false;
        }

        // Build packet with ratchet ciphertext
        uint8_t packet[sizeof(LinkDataHeader) + ratchetCtLen];
        LinkDataHeader* header = (LinkDataHeader*)packet;
        memcpy(header->linkId, linkId, LINK_ID_SIZE);
        header->sequence = link->txSequence++;
        header->length = ratchetCtLen;
        header->flags = 0x80;  // Flag bit to indicate ratchet encryption

        memcpy(packet + sizeof(LinkDataHeader), ratchetCt, ratchetCtLen);

        if (sendPacket) {
            sendPacket(link->peerAddress, LINK_DATA, packet, sizeof(LinkDataHeader) + ratchetCtLen);
            link->stats.packetsOut++;
            link->stats.bytesOut += length;
            link->lastActivity = millis();
            return true;
        }
        return false;
    }

    // Standard encryption (non-ratchet)
    // Build packet: header + encrypted(data) + tag
    uint8_t packet[sizeof(LinkDataHeader) + length + 16];  // 16 byte auth tag

    LinkDataHeader* header = (LinkDataHeader*)packet;
    memcpy(header->linkId, linkId, LINK_ID_SIZE);
    header->sequence = link->txSequence++;
    header->length = length;
    header->flags = 0;

    // Generate nonce from sequence number
    uint8_t nonce[24];
    memset(nonce, 0, 24);
    memcpy(nonce, &header->sequence, 4);
    nonce[4] = 0x01; // TX direction marker

    // Encrypt data with authentication
    uint8_t* ciphertext = packet + sizeof(LinkDataHeader);
    uint8_t* tag = ciphertext + length;

    crypto_aead_lock(ciphertext, tag, link->txKey, nonce,
                     (uint8_t*)header, sizeof(LinkDataHeader),
                     data, length);

    // Send
    if (sendPacket) {
        sendPacket(link->peerAddress, LINK_DATA, packet, sizeof(LinkDataHeader) + length + 16);
        link->stats.packetsOut++;
        link->stats.bytesOut += length;
        link->lastActivity = millis();
        return true;
    }

    return false;
}

void LinkManager::handlePacket(uint32_t fromAddress, uint8_t type,
                                const uint8_t* data, uint16_t length,
                                int16_t rssi, int8_t snr) {
    switch (type) {
        case LINK_REQUEST:
            handleLinkRequest(fromAddress, data, length, rssi, snr);
            break;
        case LINK_ACCEPT:
            handleLinkAccept(fromAddress, data, length, rssi, snr);
            break;
        case LINK_REJECT:
            handleLinkReject(fromAddress, data, length);
            break;
        case LINK_DATA:
            handleLinkData(fromAddress, data, length, rssi, snr);
            break;
        case LINK_KEEPALIVE:
            handleLinkKeepalive(fromAddress, data, length);
            break;
        case LINK_CLOSE:
            handleLinkClose(fromAddress, data, length);
            break;
    }
}

void LinkManager::handleLinkRequest(uint32_t from, const uint8_t* data, uint16_t len,
                                     int16_t rssi, int8_t snr) {
    if (len < sizeof(LinkRequest)) return;

    const LinkRequest* req = (const LinkRequest*)data;

    Serial.print("[LINK] Link request from 0x");
    Serial.println(from, HEX);

    if (!autoAccept) {
        // Send reject
        if (sendPacket) {
            sendPacket(from, LINK_REJECT, req->linkId, LINK_ID_SIZE);
        }
        return;
    }

    // Find or create slot
    int slot = findLinkByPeer(from);
    if (slot < 0) {
        slot = findEmptySlot();
    }

    if (slot < 0) {
        Serial.println("[LINK] No free slots - rejecting");
        if (sendPacket) {
            sendPacket(from, LINK_REJECT, req->linkId, LINK_ID_SIZE);
        }
        return;
    }

    Link* link = &links[slot];
    memset(link, 0, sizeof(Link));

    // Copy link ID and peer's public key
    memcpy(link->linkId, req->linkId, LINK_ID_SIZE);
    memcpy(link->peerPublicKey, req->publicKey, 32);

    // Generate our ephemeral keypair
    for (int i = 0; i < 32; i++) {
        link->localPrivateKey[i] = random(256);
    }
    crypto_x25519_public_key(link->localPublicKey, link->localPrivateKey);

    // Setup link
    link->peerAddress = from;
    link->createdAt = millis();
    link->lastActivity = millis();
    link->valid = true;
    link->initiator = false;
    link->stats.lastRssi = rssi;
    link->stats.lastSnr = snr;

    // Derive shared secret and keys
    deriveKeys(link);

    // Build accept response
    LinkAccept accept;
    memcpy(accept.linkId, link->linkId, LINK_ID_SIZE);
    memcpy(accept.publicKey, link->localPublicKey, 32);

    // Generate proof (hash of shared secret for verification)
    crypto_blake2b(accept.proof, 16, link->sharedSecret, 32);

    // Send accept
    if (sendPacket) {
        sendPacket(from, LINK_ACCEPT, (uint8_t*)&accept, sizeof(LinkAccept));
    }

    // Initialize Double Ratchet if forward secrecy is enabled
    // Responder (Bob) initializes with our keypair
    if (forwardSecrecyEnabled) {
        ratchet.initBob(&link->ratchetState, link->sharedSecret,
                        link->localPrivateKey, link->localPublicKey);
        link->ratchetEnabled = true;
        Serial.println("[LINK] Forward secrecy enabled (Double Ratchet)");
    }

    link->state = LINK_ACTIVE;

    Serial.print("[LINK] Accepted link from 0x");
    Serial.println(from, HEX);

    if (onEstablished) {
        onEstablished(from, link->linkId);
    }
}

void LinkManager::handleLinkAccept(uint32_t from, const uint8_t* data, uint16_t len,
                                    int16_t rssi, int8_t snr) {
    if (len < sizeof(LinkAccept)) return;

    const LinkAccept* accept = (const LinkAccept*)data;

    // Find our pending link
    int slot = findLinkById(accept->linkId);
    if (slot < 0) {
        Serial.println("[LINK] Accept for unknown link");
        return;
    }

    Link* link = &links[slot];
    if (link->state != LINK_PENDING) {
        Serial.println("[LINK] Accept for non-pending link");
        return;
    }

    // Store peer's public key
    memcpy(link->peerPublicKey, accept->publicKey, 32);

    // Derive shared secret and keys
    deriveKeys(link);

    // Verify proof
    uint8_t expectedProof[16];
    crypto_blake2b(expectedProof, 16, link->sharedSecret, 32);

    if (memcmp(expectedProof, accept->proof, 16) != 0) {
        Serial.println("[LINK] Proof verification failed!");
        link->valid = false;
        link->state = LINK_CLOSED;
        return;
    }

    // Initialize Double Ratchet if forward secrecy is enabled
    // Initiator (Alice) initializes with peer's public key
    if (forwardSecrecyEnabled) {
        ratchet.initAlice(&link->ratchetState, link->sharedSecret, link->peerPublicKey);
        link->ratchetEnabled = true;
        Serial.println("[LINK] Forward secrecy enabled (Double Ratchet)");
    }

    link->state = LINK_ACTIVE;
    link->lastActivity = millis();
    link->stats.lastRssi = rssi;
    link->stats.lastSnr = snr;

    Serial.print("[LINK] Link established to 0x");
    Serial.println(from, HEX);

    if (onEstablished) {
        onEstablished(from, link->linkId);
    }
}

void LinkManager::handleLinkReject(uint32_t from, const uint8_t* data, uint16_t len) {
    if (len < LINK_ID_SIZE) return;

    int slot = findLinkById(data);
    if (slot < 0) return;

    Serial.print("[LINK] Link rejected by 0x");
    Serial.println(from, HEX);

    links[slot].valid = false;
    links[slot].state = LINK_CLOSED;

    if (onClosed) {
        onClosed(from, data);
    }
}

void LinkManager::handleLinkData(uint32_t from, const uint8_t* data, uint16_t len,
                                  int16_t rssi, int8_t snr) {
    if (len < sizeof(LinkDataHeader) + 16) return;  // Need header + auth tag

    const LinkDataHeader* header = (const LinkDataHeader*)data;

    int slot = findLinkById(header->linkId);
    if (slot < 0) {
        Serial.println("[LINK] Data for unknown link");
        return;
    }

    Link* link = &links[slot];
    if (link->state != LINK_ACTIVE) {
        Serial.println("[LINK] Data for inactive link");
        return;
    }

    // Check if this packet uses ratchet encryption (flag bit 0x80)
    bool isRatchetPacket = (header->flags & 0x80) != 0;

    if (isRatchetPacket && link->ratchetEnabled) {
        // Decrypt using Double Ratchet
        const uint8_t* ratchetCt = data + sizeof(LinkDataHeader);
        uint16_t ratchetCtLen = header->length;
        uint8_t plaintext[256];  // Max payload
        uint16_t ptLen;

        if (!ratchet.decrypt(&link->ratchetState, ratchetCt, ratchetCtLen, plaintext, &ptLen)) {
            Serial.println("[LINK] Ratchet decryption failed!");
            return;
        }

        // Update state
        link->rxSequence = header->sequence;
        link->lastActivity = millis();
        link->stats.packetsIn++;
        link->stats.bytesIn += ptLen;
        link->stats.lastRssi = rssi;
        link->stats.lastSnr = snr;

        // Deliver to application
        if (onData) {
            onData(from, link->linkId, plaintext, ptLen);
        }
        return;
    }

    // Standard decryption (non-ratchet)
    // Check sequence number (prevent replay)
    if (header->sequence <= link->rxSequence && link->rxSequence > 0) {
        Serial.println("[LINK] Duplicate or old packet");
        return;
    }

    // Generate nonce from sequence
    uint8_t nonce[24];
    memset(nonce, 0, 24);
    memcpy(nonce, &header->sequence, 4);
    nonce[4] = 0x01; // TX direction (from sender's perspective)

    // Decrypt and verify
    const uint8_t* ciphertext = data + sizeof(LinkDataHeader);
    const uint8_t* tag = ciphertext + header->length;
    uint8_t plaintext[header->length];

    int result = crypto_aead_unlock(plaintext, tag, link->rxKey, nonce,
                                     (uint8_t*)header, sizeof(LinkDataHeader),
                                     ciphertext, header->length);

    if (result != 0) {
        Serial.println("[LINK] Decryption failed!");
        return;
    }

    // Update state
    link->rxSequence = header->sequence;
    link->lastActivity = millis();
    link->stats.packetsIn++;
    link->stats.bytesIn += header->length;
    link->stats.lastRssi = rssi;
    link->stats.lastSnr = snr;

    // Deliver to application
    if (onData) {
        onData(from, link->linkId, plaintext, header->length);
    }
}

void LinkManager::handleLinkKeepalive(uint32_t from, const uint8_t* data, uint16_t len) {
    if (len < LINK_ID_SIZE) return;

    int slot = findLinkById(data);
    if (slot < 0) return;

    links[slot].lastActivity = millis();
}

void LinkManager::handleLinkClose(uint32_t from, const uint8_t* data, uint16_t len) {
    if (len < LINK_ID_SIZE) return;

    int slot = findLinkById(data);
    if (slot < 0) return;

    Link* link = &links[slot];

    Serial.print("[LINK] Remote closed link from 0x");
    Serial.println(from, HEX);

    if (onClosed) {
        onClosed(from, link->linkId);
    }

    // Clear sensitive data
    crypto_wipe(link->localPrivateKey, 32);
    crypto_wipe(link->sharedSecret, 32);
    crypto_wipe(link->txKey, 32);
    crypto_wipe(link->rxKey, 32);

    link->valid = false;
    link->state = LINK_CLOSED;
}

bool LinkManager::hasActiveLink(uint32_t peerAddress) {
    int slot = findLinkByPeer(peerAddress);
    return slot >= 0 && links[slot].state == LINK_ACTIVE;
}

LinkState LinkManager::getLinkState(uint32_t peerAddress) {
    int slot = findLinkByPeer(peerAddress);
    if (slot < 0) return LINK_CLOSED;
    return links[slot].state;
}

Link* LinkManager::getLinkByPeer(uint32_t peerAddress) {
    int slot = findLinkByPeer(peerAddress);
    if (slot < 0) return nullptr;
    return &links[slot];
}

Link* LinkManager::getLinkById(const uint8_t* linkId) {
    int slot = findLinkById(linkId);
    if (slot < 0) return nullptr;
    return &links[slot];
}

uint8_t LinkManager::getActiveLinkCount() {
    uint8_t count = 0;
    for (int i = 0; i < MAX_LINKS; i++) {
        if (links[i].valid && links[i].state == LINK_ACTIVE) {
            count++;
        }
    }
    return count;
}

Link* LinkManager::getLinkByIndex(uint8_t index) {
    uint8_t current = 0;
    for (int i = 0; i < MAX_LINKS; i++) {
        if (links[i].valid) {
            if (current == index) {
                return &links[i];
            }
            current++;
        }
    }
    return nullptr;
}

void LinkManager::setSendFunction(bool (*sendFn)(uint32_t, uint8_t, const uint8_t*, uint16_t)) {
    sendPacket = sendFn;
}

void LinkManager::printStatus() {
    Serial.println("\n=== Link Status ===");
    Serial.print("Active links: ");
    Serial.print(getActiveLinkCount());
    Serial.print("/");
    Serial.println(MAX_LINKS);

    for (int i = 0; i < MAX_LINKS; i++) {
        if (links[i].valid) {
            Serial.print("\n[");
            Serial.print(i);
            Serial.print("] 0x");
            Serial.print(links[i].peerAddress, HEX);

            Serial.print(" State: ");
            switch (links[i].state) {
                case LINK_CLOSED: Serial.print("CLOSED"); break;
                case LINK_PENDING: Serial.print("PENDING"); break;
                case LINK_HANDSHAKE: Serial.print("HANDSHAKE"); break;
                case LINK_ACTIVE: Serial.print("ACTIVE"); break;
                case LINK_STALE: Serial.print("STALE"); break;
            }

            Serial.print(" (");
            Serial.print(links[i].initiator ? "initiator" : "responder");
            Serial.print(links[i].ratchetEnabled ? ", FS" : "");
            Serial.println(")");

            Serial.print("    TX: ");
            Serial.print(links[i].stats.packetsOut);
            Serial.print(" pkts, ");
            Serial.print(links[i].stats.bytesOut);
            Serial.println(" bytes");

            Serial.print("    RX: ");
            Serial.print(links[i].stats.packetsIn);
            Serial.print(" pkts, ");
            Serial.print(links[i].stats.bytesIn);
            Serial.println(" bytes");

            Serial.print("    RSSI: ");
            Serial.print(links[i].stats.lastRssi);
            Serial.print(" dBm, SNR: ");
            Serial.print(links[i].stats.lastSnr);
            Serial.println(" dB");

            unsigned long age = (millis() - links[i].lastActivity) / 1000;
            Serial.print("    Last activity: ");
            Serial.print(age);
            Serial.println("s ago");
        }
    }
    Serial.println("==================\n");
}

// Private methods

int LinkManager::findEmptySlot() {
    for (int i = 0; i < MAX_LINKS; i++) {
        if (!links[i].valid) {
            return i;
        }
    }
    return -1;
}

int LinkManager::findLinkByPeer(uint32_t peerAddress) {
    for (int i = 0; i < MAX_LINKS; i++) {
        if (links[i].valid && links[i].peerAddress == peerAddress) {
            return i;
        }
    }
    return -1;
}

int LinkManager::findLinkById(const uint8_t* linkId) {
    for (int i = 0; i < MAX_LINKS; i++) {
        if (links[i].valid && memcmp(links[i].linkId, linkId, LINK_ID_SIZE) == 0) {
            return i;
        }
    }
    return -1;
}

void LinkManager::generateLinkId(uint8_t* linkId) {
    // Generate random link ID
    for (int i = 0; i < LINK_ID_SIZE; i++) {
        linkId[i] = random(256);
    }
}

void LinkManager::deriveKeys(Link* link) {
    // X25519 key exchange
    crypto_x25519(link->sharedSecret, link->localPrivateKey, link->peerPublicKey);

    // Derive TX and RX keys from shared secret
    // Use HKDF-like construction with Blake2b
    uint8_t keyMaterial[64];
    uint8_t context[LINK_ID_SIZE + 1];

    // TX key: shared_secret + link_id + 0x01
    memcpy(context, link->linkId, LINK_ID_SIZE);
    context[LINK_ID_SIZE] = link->initiator ? 0x01 : 0x02;

    uint8_t txInput[32 + LINK_ID_SIZE + 1];
    memcpy(txInput, link->sharedSecret, 32);
    memcpy(txInput + 32, context, LINK_ID_SIZE + 1);
    crypto_blake2b(link->txKey, 32, txInput, sizeof(txInput));

    // RX key: shared_secret + link_id + 0x02
    context[LINK_ID_SIZE] = link->initiator ? 0x02 : 0x01;
    memcpy(txInput + 32, context, LINK_ID_SIZE + 1);
    crypto_blake2b(link->rxKey, 32, txInput, sizeof(txInput));
}

void LinkManager::checkTimeouts() {
    unsigned long now = millis();

    for (int i = 0; i < MAX_LINKS; i++) {
        if (!links[i].valid) continue;

        // Check handshake timeout
        if (links[i].state == LINK_PENDING) {
            if (now - links[i].createdAt > LINK_HANDSHAKE_TIMEOUT) {
                links[i].handshakeRetries++;
                if (links[i].handshakeRetries >= LINK_MAX_RETRIES) {
                    Serial.print("[LINK] Handshake timeout to 0x");
                    Serial.println(links[i].peerAddress, HEX);
                    links[i].valid = false;
                    links[i].state = LINK_CLOSED;
                } else {
                    // Retry
                    links[i].createdAt = now;
                    // Re-send request would go here
                }
            }
        }

        // Check link timeout
        if (links[i].state == LINK_ACTIVE) {
            if (now - links[i].lastActivity > LINK_TIMEOUT_MS) {
                Serial.print("[LINK] Link timeout to 0x");
                Serial.println(links[i].peerAddress, HEX);

                if (onClosed) {
                    onClosed(links[i].peerAddress, links[i].linkId);
                }

                crypto_wipe(links[i].localPrivateKey, 32);
                crypto_wipe(links[i].sharedSecret, 32);
                crypto_wipe(links[i].txKey, 32);
                crypto_wipe(links[i].rxKey, 32);

                links[i].valid = false;
                links[i].state = LINK_CLOSED;
            }
        }
    }
}

void LinkManager::sendKeepalives() {
    unsigned long now = millis();

    for (int i = 0; i < MAX_LINKS; i++) {
        if (links[i].valid && links[i].state == LINK_ACTIVE) {
            if (now - links[i].lastKeepalive > LINK_KEEPALIVE_INTERVAL) {
                if (sendPacket) {
                    sendPacket(links[i].peerAddress, LINK_KEEPALIVE,
                              links[i].linkId, LINK_ID_SIZE);
                }
                links[i].lastKeepalive = now;
            }
        }
    }
}

bool LinkManager::hasForwardSecrecy(uint32_t peerAddress) {
    int slot = findLinkByPeer(peerAddress);
    if (slot < 0) return false;
    return links[slot].ratchetEnabled;
}
