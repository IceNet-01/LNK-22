/**
 * @file dtn.cpp
 * @brief LNK-22 Delay-Tolerant Networking Implementation
 */

#include "dtn.h"

// Global instance
DTNManager dtnManager;

DTNManager::DTNManager() :
    nodeAddress(0),
    nextBundleId(1),
    epidemicEnabled(false),
    onDelivered(nullptr),
    onStatus(nullptr),
    sendPacket(nullptr),
    isReachable(nullptr)
{
    memset(&stats, 0, sizeof(DTNStats));
    for (int i = 0; i < DTN_MAX_BUNDLES; i++) {
        bundles[i].valid = false;
        bundles[i].status = BUNDLE_EMPTY;
    }
}

void DTNManager::begin(uint32_t nodeAddr) {
    nodeAddress = nodeAddr;

    Serial.println("[DTN] Delay-Tolerant Networking initialized");
    Serial.print("[DTN] Bundle capacity: ");
    Serial.println(DTN_MAX_BUNDLES);
}

void DTNManager::update() {
    checkExpired();
    retryPending();
}

uint32_t DTNManager::createBundle(uint32_t destination, const uint8_t* payload,
                                   uint16_t length, BundlePriority priority,
                                   uint32_t ttl, uint8_t flags) {
    if (length > DTN_MAX_BUNDLE_SIZE) {
        Serial.println("[DTN] Payload too large");
        return 0;
    }

    int slot = findEmptySlot();
    if (slot < 0) {
        cleanup();
        slot = findEmptySlot();
        if (slot < 0) {
            Serial.println("[DTN] No free bundle slots");
            return 0;
        }
    }

    Bundle* bundle = &bundles[slot];
    memset(bundle, 0, sizeof(Bundle));

    // Initialize header
    bundle->header.bundleId = nextBundleId++;
    bundle->header.source = nodeAddress;
    bundle->header.destination = destination;
    bundle->header.custodian = nodeAddress;  // We're initial custodian
    bundle->header.creationTime = millis();
    bundle->header.ttl = (ttl > 0) ? ttl : DTN_DEFAULT_TTL;
    bundle->header.payloadLength = length;
    bundle->header.flags = flags;
    bundle->header.priority = priority;
    bundle->header.fragmentOffset = 0;
    bundle->header.fragmentCount = 1;
    bundle->header.hopCount = 0;
    bundle->header.maxHops = MAX_TTL;

    // Copy payload
    memcpy(bundle->payload, payload, length);

    // Set status
    bundle->status = BUNDLE_PENDING;
    bundle->receivedAt = millis();
    bundle->valid = true;

    stats.bundlesCreated++;

    Serial.print("[DTN] Created bundle ");
    Serial.print(bundle->header.bundleId);
    Serial.print(" -> 0x");
    Serial.print(destination, HEX);
    Serial.print(" (");
    Serial.print(length);
    Serial.println(" bytes)");

    // Try immediate delivery if destination is local or reachable
    if (destination == nodeAddress) {
        processLocalDelivery(bundle);
    } else if (isReachable && isReachable(destination)) {
        forwardBundle(bundle);
    }

    return bundle->header.bundleId;
}

bool DTNManager::handleIncoming(uint32_t fromAddress, const uint8_t* data, uint16_t length) {
    if (length < sizeof(BundleHeader)) {
        return false;
    }

    const BundleHeader* header = (const BundleHeader*)data;
    const uint8_t* payload = data + sizeof(BundleHeader);
    uint16_t payloadLen = length - sizeof(BundleHeader);

    Serial.print("[DTN] Received bundle ");
    Serial.print(header->bundleId);
    Serial.print(" from 0x");
    Serial.println(fromAddress, HEX);

    stats.bundlesReceived++;

    // Check if this is a fragment
    if (header->flags & BUNDLE_FLAG_FRAGMENT) {
        stats.fragmentsReceived++;
        return reassembleFragment(header, payload);
    }

    // Check for duplicate
    int existing = findBundleById(header->bundleId);
    if (existing >= 0) {
        Serial.println("[DTN] Duplicate bundle ignored");
        return true;  // Already have it
    }

    // Check TTL
    uint32_t age = millis() - header->creationTime;
    if (age > header->ttl) {
        Serial.println("[DTN] Bundle expired");
        return false;
    }

    // Check hop limit
    if (header->hopCount >= header->maxHops) {
        Serial.println("[DTN] Bundle exceeded hop limit");
        return false;
    }

    // Is this bundle for us?
    if (header->destination == nodeAddress || header->destination == 0xFFFFFFFF) {
        // Store and deliver locally
        int slot = findEmptySlot();
        if (slot < 0) {
            cleanup();
            slot = findEmptySlot();
        }

        if (slot >= 0) {
            Bundle* bundle = &bundles[slot];
            memset(bundle, 0, sizeof(Bundle));
            memcpy(&bundle->header, header, sizeof(BundleHeader));
            memcpy(bundle->payload, payload, min(payloadLen, (uint16_t)DTN_MAX_BUNDLE_SIZE));
            bundle->receivedAt = millis();
            bundle->valid = true;

            processLocalDelivery(bundle);
        }

        // For broadcasts, also forward
        if (header->destination == 0xFFFFFFFF && epidemicEnabled) {
            // Continue with forwarding below
        } else {
            return true;
        }
    }

    // Need to forward this bundle
    // Accept custody if requested
    if (header->flags & BUNDLE_FLAG_CUSTODY) {
        sendCustodySignal(fromAddress, header->bundleId, true, 0);
        stats.custodyTransfers++;
    }

    // Store for forwarding
    int slot = findEmptySlot();
    if (slot < 0) {
        cleanup();
        slot = findEmptySlot();
    }

    if (slot >= 0) {
        Bundle* bundle = &bundles[slot];
        memset(bundle, 0, sizeof(Bundle));
        memcpy(&bundle->header, header, sizeof(BundleHeader));
        bundle->header.hopCount++;  // Increment hop count
        bundle->header.custodian = nodeAddress;  // We're now custodian
        memcpy(bundle->payload, payload, min(payloadLen, (uint16_t)DTN_MAX_BUNDLE_SIZE));
        bundle->receivedAt = millis();
        bundle->status = BUNDLE_PENDING;
        bundle->valid = true;

        // Try immediate forward
        if (isReachable && isReachable(bundle->header.destination)) {
            forwardBundle(bundle);
        }
    }

    return true;
}

void DTNManager::handleCustodySignal(const CustodySignal* signal) {
    int slot = findBundleById(signal->bundleId);
    if (slot < 0) return;

    Bundle* bundle = &bundles[slot];

    if (signal->accepted) {
        Serial.print("[DTN] Custody accepted for bundle ");
        Serial.print(signal->bundleId);
        Serial.print(" by 0x");
        Serial.println(signal->custodian, HEX);

        // We can release this bundle
        bundle->status = BUNDLE_DELIVERED;
        bundle->valid = false;
        stats.bundlesDelivered++;
    } else {
        Serial.print("[DTN] Custody refused for bundle ");
        Serial.print(signal->bundleId);
        Serial.print(" reason: ");
        Serial.println(signal->reason);

        // Keep bundle, try another route
        bundle->status = BUNDLE_PENDING;
    }

    if (onStatus) {
        onStatus(signal->bundleId, bundle->status);
    }
}

void DTNManager::onPeerDiscovered(uint32_t peerAddress) {
    // Check if we have any bundles for this peer
    for (int i = 0; i < DTN_MAX_BUNDLES; i++) {
        if (bundles[i].valid && bundles[i].status == BUNDLE_PENDING) {
            if (bundles[i].header.destination == peerAddress) {
                Serial.print("[DTN] Peer discovered, forwarding bundle ");
                Serial.println(bundles[i].header.bundleId);
                forwardBundle(&bundles[i]);
            }
        }
    }
}

Bundle* DTNManager::getBundleById(uint32_t bundleId) {
    int slot = findBundleById(bundleId);
    if (slot < 0) return nullptr;
    return &bundles[slot];
}

uint8_t DTNManager::getPendingCount() {
    uint8_t count = 0;
    for (int i = 0; i < DTN_MAX_BUNDLES; i++) {
        if (bundles[i].valid && bundles[i].status == BUNDLE_PENDING) {
            count++;
        }
    }
    return count;
}

uint8_t DTNManager::getTotalBundles() {
    uint8_t count = 0;
    for (int i = 0; i < DTN_MAX_BUNDLES; i++) {
        if (bundles[i].valid) {
            count++;
        }
    }
    return count;
}

void DTNManager::cleanup() {
    unsigned long now = millis();

    for (int i = 0; i < DTN_MAX_BUNDLES; i++) {
        if (bundles[i].valid) {
            uint32_t age = now - bundles[i].header.creationTime;
            if (age > bundles[i].header.ttl) {
                Serial.print("[DTN] Bundle ");
                Serial.print(bundles[i].header.bundleId);
                Serial.println(" expired");
                bundles[i].valid = false;
                bundles[i].status = BUNDLE_EXPIRED;
                stats.bundlesExpired++;
            }
        }
    }
}

void DTNManager::clear() {
    for (int i = 0; i < DTN_MAX_BUNDLES; i++) {
        bundles[i].valid = false;
        bundles[i].status = BUNDLE_EMPTY;
    }
    Serial.println("[DTN] All bundles cleared");
}

void DTNManager::printStatus() {
    Serial.println("\n=== DTN Status ===");
    Serial.print("Bundles: ");
    Serial.print(getTotalBundles());
    Serial.print("/");
    Serial.print(DTN_MAX_BUNDLES);
    Serial.print(" (");
    Serial.print(getPendingCount());
    Serial.println(" pending)");

    Serial.print("Epidemic mode: ");
    Serial.println(epidemicEnabled ? "ON" : "OFF");

    Serial.println("\nStatistics:");
    Serial.print("  Created: ");
    Serial.println(stats.bundlesCreated);
    Serial.print("  Received: ");
    Serial.println(stats.bundlesReceived);
    Serial.print("  Forwarded: ");
    Serial.println(stats.bundlesForwarded);
    Serial.print("  Delivered: ");
    Serial.println(stats.bundlesDelivered);
    Serial.print("  Expired: ");
    Serial.println(stats.bundlesExpired);
    Serial.print("  Custody transfers: ");
    Serial.println(stats.custodyTransfers);

    Serial.println("\nPending bundles:");
    for (int i = 0; i < DTN_MAX_BUNDLES; i++) {
        if (bundles[i].valid) {
            Serial.print("  [");
            Serial.print(bundles[i].header.bundleId);
            Serial.print("] -> 0x");
            Serial.print(bundles[i].header.destination, HEX);
            Serial.print(" (");
            Serial.print(bundles[i].header.payloadLength);
            Serial.print("B, hop ");
            Serial.print(bundles[i].header.hopCount);
            Serial.print("/");
            Serial.print(bundles[i].header.maxHops);
            Serial.print(", ");

            switch (bundles[i].status) {
                case BUNDLE_PENDING: Serial.print("PENDING"); break;
                case BUNDLE_IN_TRANSIT: Serial.print("IN_TRANSIT"); break;
                case BUNDLE_CUSTODY_WAIT: Serial.print("CUSTODY_WAIT"); break;
                case BUNDLE_DELIVERED: Serial.print("DELIVERED"); break;
                case BUNDLE_EXPIRED: Serial.print("EXPIRED"); break;
                case BUNDLE_FAILED: Serial.print("FAILED"); break;
                default: Serial.print("UNKNOWN"); break;
            }
            Serial.println(")");
        }
    }
    Serial.println("==================\n");
}

void DTNManager::setSendFunction(bool (*sendFn)(uint32_t, const uint8_t*, uint16_t)) {
    sendPacket = sendFn;
}

void DTNManager::setReachableFunction(bool (*reachFn)(uint32_t)) {
    isReachable = reachFn;
}

// Private methods

int DTNManager::findEmptySlot() {
    for (int i = 0; i < DTN_MAX_BUNDLES; i++) {
        if (!bundles[i].valid) {
            return i;
        }
    }
    return -1;
}

int DTNManager::findBundleById(uint32_t bundleId) {
    for (int i = 0; i < DTN_MAX_BUNDLES; i++) {
        if (bundles[i].valid && bundles[i].header.bundleId == bundleId) {
            return i;
        }
    }
    return -1;
}

int DTNManager::findBundleForDest(uint32_t destination) {
    for (int i = 0; i < DTN_MAX_BUNDLES; i++) {
        if (bundles[i].valid && bundles[i].status == BUNDLE_PENDING &&
            bundles[i].header.destination == destination) {
            return i;
        }
    }
    return -1;
}

bool DTNManager::processLocalDelivery(Bundle* bundle) {
    Serial.print("[DTN] Delivering bundle ");
    Serial.print(bundle->header.bundleId);
    Serial.println(" locally");

    bundle->status = BUNDLE_DELIVERED;
    stats.bundlesDelivered++;

    if (onDelivered) {
        onDelivered(bundle->header.bundleId, bundle->payload, bundle->header.payloadLength);
    }

    if (onStatus) {
        onStatus(bundle->header.bundleId, BUNDLE_DELIVERED);
    }

    // Send delivery report if requested
    if (bundle->header.flags & BUNDLE_FLAG_REPORT_DELIV) {
        // TODO: Send delivery status report to source
    }

    bundle->valid = false;  // Free slot
    return true;
}

bool DTNManager::forwardBundle(Bundle* bundle) {
    if (!sendPacket) return false;

    // Check if we need to fragment
    uint16_t totalSize = sizeof(BundleHeader) + bundle->header.payloadLength;
    if (totalSize > DTN_FRAGMENT_SIZE + sizeof(BundleHeader)) {
        return sendFragmented(bundle);
    }

    // Send complete bundle
    uint8_t packet[sizeof(BundleHeader) + DTN_MAX_BUNDLE_SIZE];
    memcpy(packet, &bundle->header, sizeof(BundleHeader));
    memcpy(packet + sizeof(BundleHeader), bundle->payload, bundle->header.payloadLength);

    bundle->status = BUNDLE_IN_TRANSIT;
    bundle->lastForwardAttempt = millis();

    bool sent = sendPacket(bundle->header.destination, packet,
                          sizeof(BundleHeader) + bundle->header.payloadLength);

    if (sent) {
        stats.bundlesForwarded++;
        bundle->forwardCount++;

        // If custody requested, wait for ACK
        if (bundle->header.flags & BUNDLE_FLAG_CUSTODY) {
            bundle->status = BUNDLE_CUSTODY_WAIT;
        } else {
            // No custody, assume delivered
            bundle->status = BUNDLE_DELIVERED;
            bundle->valid = false;
        }
    } else {
        bundle->status = BUNDLE_PENDING;  // Retry later
    }

    return sent;
}

bool DTNManager::sendCustodySignal(uint32_t to, uint32_t bundleId, bool accepted, uint8_t reason) {
    if (!sendPacket) return false;

    CustodySignal signal;
    signal.bundleId = bundleId;
    signal.custodian = nodeAddress;
    signal.accepted = accepted ? 1 : 0;
    signal.reason = reason;

    // Send as a special bundle type (would need protocol extension)
    // For now, just log
    Serial.print("[DTN] Custody signal: bundle ");
    Serial.print(bundleId);
    Serial.print(accepted ? " ACCEPTED" : " REFUSED");
    Serial.println();

    return true;
}

bool DTNManager::sendFragmented(Bundle* bundle) {
    if (!sendPacket) return false;

    uint16_t remaining = bundle->header.payloadLength;
    uint8_t offset = 0;
    uint8_t fragmentCount = (remaining + DTN_FRAGMENT_SIZE - 1) / DTN_FRAGMENT_SIZE;

    Serial.print("[DTN] Fragmenting bundle ");
    Serial.print(bundle->header.bundleId);
    Serial.print(" into ");
    Serial.print(fragmentCount);
    Serial.println(" fragments");

    while (remaining > 0) {
        uint16_t fragSize = min(remaining, (uint16_t)DTN_FRAGMENT_SIZE);

        // Build fragment header
        BundleHeader fragHeader = bundle->header;
        fragHeader.flags |= BUNDLE_FLAG_FRAGMENT;
        fragHeader.fragmentOffset = offset;
        fragHeader.fragmentCount = fragmentCount;
        fragHeader.payloadLength = fragSize;

        // Build packet
        uint8_t packet[sizeof(BundleHeader) + DTN_FRAGMENT_SIZE];
        memcpy(packet, &fragHeader, sizeof(BundleHeader));
        memcpy(packet + sizeof(BundleHeader), bundle->payload + (offset * DTN_FRAGMENT_SIZE), fragSize);

        sendPacket(bundle->header.destination, packet, sizeof(BundleHeader) + fragSize);
        stats.fragmentsSent++;

        remaining -= fragSize;
        offset++;
    }

    bundle->status = BUNDLE_IN_TRANSIT;
    bundle->lastForwardAttempt = millis();
    stats.bundlesForwarded++;

    return true;
}

bool DTNManager::reassembleFragment(const BundleHeader* header, const uint8_t* payload) {
    // Find or create reassembly buffer
    int slot = -1;

    // Look for existing reassembly in progress
    for (int i = 0; i < DTN_MAX_BUNDLES; i++) {
        if (bundles[i].valid &&
            bundles[i].header.bundleId == header->bundleId &&
            bundles[i].header.source == header->source) {
            slot = i;
            break;
        }
    }

    // Create new reassembly buffer
    if (slot < 0) {
        slot = findEmptySlot();
        if (slot < 0) {
            cleanup();
            slot = findEmptySlot();
        }

        if (slot < 0) {
            Serial.println("[DTN] No slot for fragment reassembly");
            return false;
        }

        // Initialize with first fragment's header
        Bundle* bundle = &bundles[slot];
        memset(bundle, 0, sizeof(Bundle));
        memcpy(&bundle->header, header, sizeof(BundleHeader));
        bundle->header.flags &= ~BUNDLE_FLAG_FRAGMENT;  // Clear fragment flag
        bundle->receivedAt = millis();
        bundle->status = BUNDLE_PENDING;
        bundle->valid = true;
        bundle->fragmentMask = 0;
    }

    Bundle* bundle = &bundles[slot];

    // Copy fragment payload to correct position
    uint16_t offset = header->fragmentOffset * DTN_FRAGMENT_SIZE;
    if (offset + header->payloadLength <= DTN_MAX_BUNDLE_SIZE) {
        memcpy(bundle->payload + offset, payload, header->payloadLength);
        bundle->fragmentMask |= (1 << header->fragmentOffset);
        bundle->fragmentsReceived++;
    }

    // Check if complete
    uint8_t expectedMask = (1 << header->fragmentCount) - 1;
    if (bundle->fragmentMask == expectedMask) {
        Serial.print("[DTN] Bundle ");
        Serial.print(bundle->header.bundleId);
        Serial.println(" reassembly complete");

        // Calculate total payload length
        bundle->header.payloadLength = (header->fragmentCount - 1) * DTN_FRAGMENT_SIZE +
                                       header->payloadLength;

        // Process as complete bundle
        if (bundle->header.destination == nodeAddress) {
            processLocalDelivery(bundle);
        } else {
            // Forward
            if (isReachable && isReachable(bundle->header.destination)) {
                forwardBundle(bundle);
            }
        }
    }

    return true;
}

void DTNManager::checkExpired() {
    unsigned long now = millis();

    for (int i = 0; i < DTN_MAX_BUNDLES; i++) {
        if (bundles[i].valid) {
            // Check TTL
            uint32_t age = now - bundles[i].header.creationTime;
            if (age > bundles[i].header.ttl) {
                bundles[i].status = BUNDLE_EXPIRED;
                bundles[i].valid = false;
                stats.bundlesExpired++;

                if (onStatus) {
                    onStatus(bundles[i].header.bundleId, BUNDLE_EXPIRED);
                }
            }

            // Check custody timeout
            if (bundles[i].status == BUNDLE_CUSTODY_WAIT) {
                if (now - bundles[i].lastForwardAttempt > DTN_CUSTODY_TIMEOUT) {
                    Serial.print("[DTN] Custody timeout for bundle ");
                    Serial.println(bundles[i].header.bundleId);
                    bundles[i].status = BUNDLE_PENDING;  // Retry
                }
            }
        }
    }
}

void DTNManager::retryPending() {
    unsigned long now = millis();

    for (int i = 0; i < DTN_MAX_BUNDLES; i++) {
        if (bundles[i].valid && bundles[i].status == BUNDLE_PENDING) {
            // Only retry every 30 seconds
            if (now - bundles[i].lastForwardAttempt > 30000) {
                if (isReachable && isReachable(bundles[i].header.destination)) {
                    forwardBundle(&bundles[i]);
                }
                // For epidemic mode, also try sending to any neighbor
                else if (epidemicEnabled && bundles[i].forwardCount < DTN_MAX_COPIES) {
                    // Would need neighbor list to send copies
                    bundles[i].lastForwardAttempt = now;
                }
            }
        }
    }
}
