/**
 * @file adaptive_datarate.cpp
 * @brief LNK-22 Adaptive Data Rate Implementation
 */

#include "adaptive_datarate.h"

// Global instance
AdaptiveDataRate adaptiveDataRate;

AdaptiveDataRate::AdaptiveDataRate() :
    defaultSF(LORA_SPREADING_FACTOR),
    adrEnabled(true),
    lastGlobalUpdate(0),
    scanInProgress(false),
    scanCurrentSF(SF_MIN),
    scanStartTime(0),
    scanDurationPerSF(5000),
    sfChangeCallback(nullptr)
{
    memset(&stats, 0, sizeof(ADRStats));
    for (int i = 0; i < MAX_LINKS; i++) {
        links[i].valid = false;
    }
    for (int i = 0; i < 6; i++) {
        scanResults[i].sf = SF_MIN + i;
        scanResults[i].packetsHeard = 0;
        scanResults[i].bestRssi = -140;
        scanResults[i].bestSnr = -20;
        scanResults[i].active = false;
    }
}

void AdaptiveDataRate::begin(uint8_t sf) {
    defaultSF = constrain(sf, SF_MIN, SF_MAX);
    Serial.print("[ADR] Adaptive Data Rate initialized, default SF");
    Serial.println(defaultSF);
    Serial.print("[ADR] ADR is ");
    Serial.println(adrEnabled ? "ENABLED" : "DISABLED");
}

void AdaptiveDataRate::recordRx(uint32_t peerAddress, int16_t rssi, int8_t snr, bool success) {
    int slot = findOrCreateLink(peerAddress);
    if (slot < 0) return;

    LinkADRState* link = &links[slot];

    // Add to history
    link->rssiHistory[link->historyIndex] = rssi;
    link->snrHistory[link->historyIndex] = snr;
    link->historyIndex = (link->historyIndex + 1) % ADR_HISTORY_SIZE;
    if (link->historyCount < ADR_HISTORY_SIZE) {
        link->historyCount++;
    }

    link->packetsTotal++;
    if (success) {
        link->packetsSuccess++;
    }
}

void AdaptiveDataRate::recordTx(uint32_t peerAddress, bool success) {
    int slot = findLink(peerAddress);
    if (slot < 0) return;

    LinkADRState* link = &links[slot];
    link->packetsTotal++;
    if (success) {
        link->packetsSuccess++;
    }

    // Track packets per SF
    switch (link->currentSF) {
        case 7:  stats.packetsAtSF7++; break;
        case 8:  stats.packetsAtSF8++; break;
        case 9:  stats.packetsAtSF9++; break;
        case 10: stats.packetsAtSF10++; break;
        case 11: stats.packetsAtSF11++; break;
        case 12: stats.packetsAtSF12++; break;
    }
}

uint8_t AdaptiveDataRate::getRecommendedSF(uint32_t peerAddress) {
    if (!adrEnabled) {
        return defaultSF;
    }

    int slot = findLink(peerAddress);
    if (slot < 0) {
        return defaultSF;
    }

    return links[slot].recommendedSF;
}

uint8_t AdaptiveDataRate::getCurrentSF(uint32_t peerAddress) {
    int slot = findLink(peerAddress);
    if (slot < 0) {
        return defaultSF;
    }
    return links[slot].currentSF;
}

uint8_t AdaptiveDataRate::getNegotiatedSF(uint32_t peerAddress) {
    if (!adrEnabled) {
        return defaultSF;
    }

    int slot = findLink(peerAddress);
    if (slot < 0) {
        return defaultSF;  // Unknown peer, use default
    }

    LinkADRState* link = &links[slot];

    // If we know peer's preference, negotiate
    if (link->peerSFKnown) {
        // Use the HIGHER (more robust) of the two SFs
        // This ensures both sides can communicate
        uint8_t negotiated = max(link->recommendedSF, link->peerPreferredSF);
        link->negotiatedSF = constrain(negotiated, SF_MIN, SF_MAX);
        return link->negotiatedSF;
    }

    // Peer SF unknown - use our recommendation but be conservative
    // Add 1 to our recommendation for safety margin
    uint8_t safeSF = min(link->recommendedSF + 1, (uint8_t)SF_MAX);
    return safeSF;
}

void AdaptiveDataRate::recordPeerSF(uint32_t peerAddress, uint8_t preferredSF) {
    int slot = findOrCreateLink(peerAddress);
    if (slot < 0) return;

    LinkADRState* link = &links[slot];
    uint8_t oldPeerSF = link->peerPreferredSF;

    link->peerPreferredSF = constrain(preferredSF, SF_MIN, SF_MAX);
    link->peerSFKnown = true;
    link->lastPeerUpdate = millis();

    // Recalculate negotiated SF
    link->negotiatedSF = max(link->recommendedSF, link->peerPreferredSF);

    if (oldPeerSF != link->peerPreferredSF && link->peerSFKnown) {
        Serial.print("[ADR] Peer 0x");
        Serial.print(peerAddress, HEX);
        Serial.print(" prefers SF");
        Serial.print(link->peerPreferredSF);
        Serial.print(" -> negotiated SF");
        Serial.println(link->negotiatedSF);
    }
}

ADRAdvertisement AdaptiveDataRate::getAdvertisement() {
    ADRAdvertisement adv;
    adv.preferredSF = defaultSF;  // Our preferred receiving SF
    adv.minSF = SF_MIN;
    adv.maxSF = SF_MAX;
    adv.txPower = LORA_TX_POWER;
    return adv;
}

void AdaptiveDataRate::setDefaultSF(uint8_t sf) {
    defaultSF = constrain(sf, SF_MIN, SF_MAX);
    Serial.print("[ADR] Default SF changed to ");
    Serial.println(defaultSF);
}

void AdaptiveDataRate::update() {
    unsigned long now = millis();

    // Only update periodically
    if (now - lastGlobalUpdate < ADR_UPDATE_INTERVAL) {
        return;
    }
    lastGlobalUpdate = now;

    if (!adrEnabled) {
        return;
    }

    // Evaluate each link
    for (int i = 0; i < MAX_LINKS; i++) {
        if (links[i].valid && links[i].historyCount > 0) {
            evaluateLink(&links[i]);
        }
    }
}

uint8_t AdaptiveDataRate::getLinkQuality(uint32_t peerAddress) {
    int slot = findLink(peerAddress);
    if (slot < 0 || links[slot].packetsTotal == 0) {
        return 0;
    }

    return (uint8_t)((links[slot].packetsSuccess * 100) / links[slot].packetsTotal);
}

int16_t AdaptiveDataRate::getAverageRSSI(uint32_t peerAddress) {
    int slot = findLink(peerAddress);
    if (slot < 0 || links[slot].historyCount == 0) {
        return -140;  // Very low default
    }

    int32_t sum = 0;
    for (int i = 0; i < links[slot].historyCount; i++) {
        sum += links[slot].rssiHistory[i];
    }
    return (int16_t)(sum / links[slot].historyCount);
}

int8_t AdaptiveDataRate::getAverageSNR(uint32_t peerAddress) {
    int slot = findLink(peerAddress);
    if (slot < 0 || links[slot].historyCount == 0) {
        return -20;  // Very low default
    }

    int32_t sum = 0;
    for (int i = 0; i < links[slot].historyCount; i++) {
        sum += links[slot].snrHistory[i];
    }
    return (int8_t)(sum / links[slot].historyCount);
}

void AdaptiveDataRate::printStatus() {
    Serial.println("\n=== Adaptive Data Rate Status ===");
    Serial.print("ADR: ");
    Serial.println(adrEnabled ? "ENABLED" : "DISABLED");
    Serial.print("Default SF: ");
    Serial.println(defaultSF);
    Serial.print("SF changes: ");
    Serial.println(stats.sfChanges);

    Serial.println("\nPackets by SF:");
    Serial.print("  SF7:  "); Serial.println(stats.packetsAtSF7);
    Serial.print("  SF8:  "); Serial.println(stats.packetsAtSF8);
    Serial.print("  SF9:  "); Serial.println(stats.packetsAtSF9);
    Serial.print("  SF10: "); Serial.println(stats.packetsAtSF10);
    Serial.print("  SF11: "); Serial.println(stats.packetsAtSF11);
    Serial.print("  SF12: "); Serial.println(stats.packetsAtSF12);

    Serial.println("\nPer-link status:");
    int count = 0;
    for (int i = 0; i < MAX_LINKS; i++) {
        if (links[i].valid) {
            Serial.print("  0x");
            Serial.print(links[i].peerAddress, HEX);
            Serial.print(": SF");
            Serial.print(links[i].currentSF);
            Serial.print(" -> SF");
            Serial.print(links[i].recommendedSF);
            Serial.print(" (RSSI:");
            Serial.print(getAverageRSSI(links[i].peerAddress));
            Serial.print(" SNR:");
            Serial.print(getAverageSNR(links[i].peerAddress));
            Serial.print(" Q:");
            Serial.print(getLinkQuality(links[i].peerAddress));
            Serial.println("%)");
            count++;
        }
    }
    if (count == 0) {
        Serial.println("  (no links tracked)");
    }
    Serial.println("=================================\n");
}

void AdaptiveDataRate::clear() {
    for (int i = 0; i < MAX_LINKS; i++) {
        links[i].valid = false;
    }
    Serial.println("[ADR] Link states cleared");
}

int AdaptiveDataRate::findLink(uint32_t peerAddress) {
    for (int i = 0; i < MAX_LINKS; i++) {
        if (links[i].valid && links[i].peerAddress == peerAddress) {
            return i;
        }
    }
    return -1;
}

int AdaptiveDataRate::findOrCreateLink(uint32_t peerAddress) {
    // First try to find existing
    int slot = findLink(peerAddress);
    if (slot >= 0) {
        return slot;
    }

    // Find empty slot
    for (int i = 0; i < MAX_LINKS; i++) {
        if (!links[i].valid) {
            links[i].peerAddress = peerAddress;
            links[i].historyIndex = 0;
            links[i].historyCount = 0;
            links[i].currentSF = defaultSF;
            links[i].recommendedSF = defaultSF;
            links[i].peerPreferredSF = defaultSF;
            links[i].negotiatedSF = defaultSF;
            links[i].lastUpdate = millis();
            links[i].lastPeerUpdate = 0;
            links[i].packetsSuccess = 0;
            links[i].packetsTotal = 0;
            links[i].peerSFKnown = false;
            links[i].valid = true;
            return i;
        }
    }

    // Table full - find oldest link
    uint32_t oldest = millis();
    int oldestSlot = 0;
    for (int i = 0; i < MAX_LINKS; i++) {
        if (links[i].lastUpdate < oldest) {
            oldest = links[i].lastUpdate;
            oldestSlot = i;
        }
    }

    // Reuse oldest slot
    links[oldestSlot].peerAddress = peerAddress;
    links[oldestSlot].historyIndex = 0;
    links[oldestSlot].historyCount = 0;
    links[oldestSlot].currentSF = defaultSF;
    links[oldestSlot].recommendedSF = defaultSF;
    links[oldestSlot].peerPreferredSF = defaultSF;
    links[oldestSlot].negotiatedSF = defaultSF;
    links[oldestSlot].lastUpdate = millis();
    links[oldestSlot].lastPeerUpdate = 0;
    links[oldestSlot].packetsSuccess = 0;
    links[oldestSlot].packetsTotal = 0;
    links[oldestSlot].peerSFKnown = false;
    links[oldestSlot].valid = true;

    return oldestSlot;
}

uint8_t AdaptiveDataRate::calculateSF(int16_t avgRssi, int8_t avgSnr) {
    // Find the lowest SF that meets both RSSI and SNR thresholds
    for (int i = 0; i < ADR_THRESHOLD_COUNT; i++) {
        if (avgRssi >= ADR_THRESHOLDS[i].rssiThreshold &&
            avgSnr >= ADR_THRESHOLDS[i].snrThreshold) {
            return ADR_THRESHOLDS[i].spreadingFactor;
        }
    }

    // Default to maximum SF if no threshold met
    return SF_MAX;
}

void AdaptiveDataRate::evaluateLink(LinkADRState* link) {
    if (link->historyCount < 2) {
        // Not enough data yet
        return;
    }

    // Calculate averages
    int32_t rssiSum = 0;
    int32_t snrSum = 0;
    for (int i = 0; i < link->historyCount; i++) {
        rssiSum += link->rssiHistory[i];
        snrSum += link->snrHistory[i];
    }
    int16_t avgRssi = (int16_t)(rssiSum / link->historyCount);
    int8_t avgSnr = (int8_t)(snrSum / link->historyCount);

    // Calculate recommended SF
    uint8_t newSF = calculateSF(avgRssi, avgSnr);
    link->recommendedSF = newSF;

    // Apply hysteresis before changing
    bool shouldChange = false;

    if (newSF < link->currentSF) {
        // Considering going to faster (lower) SF
        // Be more conservative - require significantly better signal
        if (avgRssi >= ADR_THRESHOLDS[newSF - SF_MIN].rssiThreshold + ADR_HYSTERESIS_DB) {
            shouldChange = true;
        }
    } else if (newSF > link->currentSF) {
        // Considering going to slower (higher) SF
        // Be less conservative - switch sooner to maintain link
        shouldChange = true;
    }

    if (shouldChange && newSF != link->currentSF) {
        Serial.print("[ADR] Link 0x");
        Serial.print(link->peerAddress, HEX);
        Serial.print(" SF");
        Serial.print(link->currentSF);
        Serial.print(" -> SF");
        Serial.print(newSF);
        Serial.print(" (RSSI:");
        Serial.print(avgRssi);
        Serial.print(" SNR:");
        Serial.print(avgSnr);
        Serial.println(")");

        link->currentSF = newSF;
        stats.sfChanges++;
    }

    link->lastUpdate = millis();
}

// === SF Scan Implementation ===

void AdaptiveDataRate::startSFScan(uint32_t durationPerSF) {
    Serial.println("\n[ADR] Starting SF scan to discover active networks...");
    Serial.print("[ADR] Scanning SF7-SF12, ");
    Serial.print(durationPerSF / 1000);
    Serial.println(" seconds per SF\n");

    // Reset scan results
    for (int i = 0; i < 6; i++) {
        scanResults[i].sf = SF_MIN + i;
        scanResults[i].packetsHeard = 0;
        scanResults[i].bestRssi = -140;
        scanResults[i].bestSnr = -20;
        scanResults[i].active = false;
    }

    scanDurationPerSF = durationPerSF;
    scanCurrentSF = SF_MIN;
    scanStartTime = millis();
    scanInProgress = true;

    // Switch to first SF
    if (sfChangeCallback) {
        Serial.print("[ADR] Scanning SF");
        Serial.println(scanCurrentSF);
        sfChangeCallback(scanCurrentSF);
    }
}

bool AdaptiveDataRate::updateScan() {
    if (!scanInProgress) {
        return true;  // Scan complete (or not started)
    }

    unsigned long now = millis();

    // Check if current SF scan duration has elapsed
    if (now - scanStartTime >= scanDurationPerSF) {
        // Report results for this SF
        int idx = scanCurrentSF - SF_MIN;
        Serial.print("[ADR] SF");
        Serial.print(scanCurrentSF);
        Serial.print(": ");
        if (scanResults[idx].packetsHeard > 0) {
            Serial.print(scanResults[idx].packetsHeard);
            Serial.print(" packets, best RSSI ");
            Serial.print(scanResults[idx].bestRssi);
            Serial.println(" dBm");
            scanResults[idx].active = true;
        } else {
            Serial.println("no activity");
        }

        // Move to next SF
        scanCurrentSF++;

        if (scanCurrentSF > SF_MAX) {
            // Scan complete
            scanInProgress = false;
            Serial.println("\n[ADR] SF Scan complete!");

            uint8_t bestSF = getBestSFFromScan();
            Serial.print("[ADR] Best SF found: SF");
            Serial.println(bestSF);

            // Switch to best SF
            if (sfChangeCallback) {
                sfChangeCallback(bestSF);
            }
            defaultSF = bestSF;

            return true;
        }

        // Switch to next SF
        scanStartTime = millis();
        if (sfChangeCallback) {
            Serial.print("[ADR] Scanning SF");
            Serial.println(scanCurrentSF);
            sfChangeCallback(scanCurrentSF);
        }
    }

    return false;  // Scan still in progress
}

AdaptiveDataRate::ScanResult AdaptiveDataRate::getScanResult(uint8_t sf) {
    if (sf < SF_MIN || sf > SF_MAX) {
        ScanResult empty = {0, 0, -140, -20, false};
        return empty;
    }
    return scanResults[sf - SF_MIN];
}

uint8_t AdaptiveDataRate::getBestSFFromScan() {
    uint8_t bestSF = defaultSF;
    uint8_t maxPackets = 0;
    int16_t bestRssi = -140;

    for (int i = 0; i < 6; i++) {
        if (scanResults[i].packetsHeard > maxPackets) {
            maxPackets = scanResults[i].packetsHeard;
            bestSF = scanResults[i].sf;
            bestRssi = scanResults[i].bestRssi;
        } else if (scanResults[i].packetsHeard == maxPackets &&
                   scanResults[i].packetsHeard > 0 &&
                   scanResults[i].bestRssi > bestRssi) {
            // Same packet count but better signal - prefer this SF
            bestSF = scanResults[i].sf;
            bestRssi = scanResults[i].bestRssi;
        }
    }

    // If no activity found, return default
    if (maxPackets == 0) {
        Serial.println("[ADR] No network activity detected, using default SF");
        return defaultSF;
    }

    return bestSF;
}

void AdaptiveDataRate::onScanPacketReceived(uint8_t sf, int16_t rssi, int8_t snr) {
    if (!scanInProgress) return;
    if (sf < SF_MIN || sf > SF_MAX) return;

    int idx = sf - SF_MIN;
    scanResults[idx].packetsHeard++;

    if (rssi > scanResults[idx].bestRssi) {
        scanResults[idx].bestRssi = rssi;
    }
    if (snr > scanResults[idx].bestSnr) {
        scanResults[idx].bestSnr = snr;
    }
}
