/**
 * @file emergency.cpp
 * @brief LNK-22 Emergency SOS System Implementation
 */

#include "emergency.h"

// Global instance
Emergency emergency;

Emergency::Emergency() :
    sosActive(false),
    currentType(EMERGENCY_NONE),
    sosStartTime(0),
    lastBroadcast(0),
    gpsLat(0),
    gpsLon(0),
    gpsAlt(0),
    gpsValid(false),
    batteryLevel(100),
    originalTxPower(LORA_TX_POWER),
    originalSF(LORA_SPREADING_FACTOR),
    broadcastCallback(nullptr)
{
    memset(&currentMessage, 0, sizeof(SOSMessage));
    memset(&stats, 0, sizeof(EmergencyStats));
    for (int i = 0; i < MAX_RECEIVED_SOS; i++) {
        receivedSOS[i].valid = false;
    }
}

void Emergency::begin() {
    Serial.println("[SOS] Emergency system initialized");
}

void Emergency::update() {
    if (!sosActive) {
        // Check for received SOS expiry (older than 30 minutes)
        unsigned long now = millis();
        for (int i = 0; i < MAX_RECEIVED_SOS; i++) {
            if (receivedSOS[i].valid) {
                if (now - receivedSOS[i].receivedAt > 1800000) {
                    receivedSOS[i].valid = false;
                }
            }
        }
        return;
    }

    unsigned long now = millis();

    // Check if SOS should auto-cancel
    if (now - sosStartTime > SOS_MAX_DURATION) {
        Serial.println("[SOS] Auto-cancelling SOS after maximum duration");
        cancelSOS();
        return;
    }

    // Broadcast SOS periodically
    if (now - lastBroadcast >= SOS_BROADCAST_INTERVAL) {
        broadcastSOS();
        lastBroadcast = now;
    }
}

bool Emergency::activateSOS(EmergencyType type, const char* message) {
    if (sosActive && type != EMERGENCY_TEST) {
        Serial.println("[SOS] SOS already active!");
        return false;
    }

    sosActive = true;
    currentType = type;
    sosStartTime = millis();
    lastBroadcast = 0;  // Force immediate broadcast
    stats.sosActivations++;

    // Build SOS message
    memset(&currentMessage, 0, sizeof(SOSMessage));
    currentMessage.sosType = type;
    currentMessage.flags = 0;

    if (gpsValid) {
        currentMessage.latitude = gpsLat;
        currentMessage.longitude = gpsLon;
        currentMessage.altitude = gpsAlt;
        currentMessage.flags |= SOS_FLAG_GPS_VALID;
    }

    currentMessage.batteryLevel = batteryLevel;
    if (batteryLevel < 20) {
        currentMessage.flags |= SOS_FLAG_BATTERY_LOW;
    }

    currentMessage.timestamp = millis() / 1000;

    // Set emergency type flags
    switch (type) {
        case EMERGENCY_MEDICAL:
            currentMessage.flags |= SOS_FLAG_MEDICAL;
            break;
        case EMERGENCY_FIRE:
            currentMessage.flags |= SOS_FLAG_FIRE;
            break;
        case EMERGENCY_RESCUE:
            currentMessage.flags |= SOS_FLAG_RESCUE;
            break;
        default:
            break;
    }

    // Copy custom message if provided
    if (message && strlen(message) > 0) {
        strncpy(currentMessage.message, message, sizeof(currentMessage.message) - 1);
        currentMessage.message[sizeof(currentMessage.message) - 1] = '\0';
    } else {
        // Default messages
        switch (type) {
            case EMERGENCY_GENERAL:
                strcpy(currentMessage.message, "EMERGENCY - NEED HELP");
                break;
            case EMERGENCY_MEDICAL:
                strcpy(currentMessage.message, "MEDICAL EMERGENCY");
                break;
            case EMERGENCY_FIRE:
                strcpy(currentMessage.message, "FIRE EMERGENCY");
                break;
            case EMERGENCY_RESCUE:
                strcpy(currentMessage.message, "NEED RESCUE");
                break;
            case EMERGENCY_SECURITY:
                strcpy(currentMessage.message, "SECURITY THREAT");
                break;
            case EMERGENCY_TEST:
                strcpy(currentMessage.message, "TEST - NOT A REAL EMERGENCY");
                break;
            default:
                strcpy(currentMessage.message, "SOS");
                break;
        }
    }

    // Alert on serial
    Serial.println("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("!!! SOS ACTIVATED !!!");
    Serial.print("!!! Type: ");
    Serial.println(currentMessage.message);
    if (gpsValid) {
        Serial.print("!!! Location: ");
        Serial.print(gpsLat / 10000000.0, 6);
        Serial.print(", ");
        Serial.println(gpsLon / 10000000.0, 6);
    }
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

    // Immediate first broadcast
    broadcastSOS();

    return true;
}

void Emergency::cancelSOS() {
    if (!sosActive) {
        return;
    }

    sosActive = false;
    currentType = EMERGENCY_NONE;

    Serial.println("\n[SOS] Emergency cancelled");
    Serial.println("[SOS] Restoring normal radio parameters\n");
}

void Emergency::setGPS(int32_t lat, int32_t lon, int32_t alt) {
    gpsLat = lat;
    gpsLon = lon;
    gpsAlt = alt;
    gpsValid = true;

    // Update current message if SOS active
    if (sosActive) {
        currentMessage.latitude = lat;
        currentMessage.longitude = lon;
        currentMessage.altitude = alt;
        currentMessage.flags |= SOS_FLAG_GPS_VALID;
    }
}

void Emergency::setBattery(uint8_t level) {
    batteryLevel = level;
    if (sosActive) {
        currentMessage.batteryLevel = level;
        if (level < 20) {
            currentMessage.flags |= SOS_FLAG_BATTERY_LOW;
        }
    }
}

void Emergency::handleReceivedSOS(uint32_t nodeAddress, const SOSMessage* msg, int16_t rssi, int8_t snr) {
    // Check if we've already received from this node
    int slot = findReceivedSOS(nodeAddress);
    if (slot < 0) {
        slot = findEmptyReceivedSlot();
        if (slot < 0) {
            // Replace oldest
            uint32_t oldest = millis();
            for (int i = 0; i < MAX_RECEIVED_SOS; i++) {
                if (receivedSOS[i].receivedAt < oldest) {
                    oldest = receivedSOS[i].receivedAt;
                    slot = i;
                }
            }
        }
    }

    if (slot >= 0) {
        receivedSOS[slot].nodeAddress = nodeAddress;
        memcpy(&receivedSOS[slot].message, msg, sizeof(SOSMessage));
        receivedSOS[slot].rssi = rssi;
        receivedSOS[slot].snr = snr;
        receivedSOS[slot].receivedAt = millis();
        receivedSOS[slot].acknowledged = false;
        receivedSOS[slot].valid = true;
        stats.sosReceived++;
    }

    // Alert on serial
    Serial.println("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("!!! RECEIVED SOS !!!");
    Serial.print("!!! From: 0x");
    Serial.println(nodeAddress, HEX);
    Serial.print("!!! Message: ");
    Serial.println(msg->message);
    if (msg->flags & SOS_FLAG_GPS_VALID) {
        Serial.print("!!! Location: ");
        Serial.print(msg->latitude / 10000000.0, 6);
        Serial.print(", ");
        Serial.println(msg->longitude / 10000000.0, 6);
    }
    Serial.print("!!! RSSI: ");
    Serial.print(rssi);
    Serial.print(" SNR: ");
    Serial.println(snr);
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
}

void Emergency::acknowledgeSOS(uint32_t nodeAddress) {
    int slot = findReceivedSOS(nodeAddress);
    if (slot >= 0) {
        receivedSOS[slot].acknowledged = true;
        stats.sosAcknowledged++;
        Serial.print("[SOS] Acknowledged SOS from 0x");
        Serial.println(nodeAddress, HEX);
    }
}

ReceivedSOS* Emergency::getReceivedSOS(uint8_t index) {
    if (index >= MAX_RECEIVED_SOS) {
        return nullptr;
    }
    if (!receivedSOS[index].valid) {
        return nullptr;
    }
    return &receivedSOS[index];
}

uint8_t Emergency::getActiveSOSCount() {
    uint8_t count = 0;
    for (int i = 0; i < MAX_RECEIVED_SOS; i++) {
        if (receivedSOS[i].valid) {
            count++;
        }
    }
    return count;
}

void Emergency::setBroadcastCallback(bool (*callback)(const uint8_t*, uint16_t)) {
    broadcastCallback = callback;
}

void Emergency::printStatus() {
    Serial.println("\n=== Emergency System Status ===");

    if (sosActive) {
        Serial.println("*** SOS ACTIVE ***");
        Serial.print("Type: ");
        switch (currentType) {
            case EMERGENCY_GENERAL: Serial.println("General"); break;
            case EMERGENCY_MEDICAL: Serial.println("Medical"); break;
            case EMERGENCY_FIRE: Serial.println("Fire"); break;
            case EMERGENCY_RESCUE: Serial.println("Rescue"); break;
            case EMERGENCY_SECURITY: Serial.println("Security"); break;
            case EMERGENCY_TEST: Serial.println("TEST"); break;
            default: Serial.println("Unknown"); break;
        }
        Serial.print("Message: ");
        Serial.println(currentMessage.message);
        Serial.print("Duration: ");
        Serial.print((millis() - sosStartTime) / 1000);
        Serial.println(" seconds");
        Serial.print("Broadcasts: ");
        Serial.println(stats.sosBroadcasts);
    } else {
        Serial.println("SOS: Not active");
    }

    Serial.println("\nReceived SOS alerts:");
    int count = 0;
    for (int i = 0; i < MAX_RECEIVED_SOS; i++) {
        if (receivedSOS[i].valid) {
            Serial.print("  0x");
            Serial.print(receivedSOS[i].nodeAddress, HEX);
            Serial.print(": ");
            Serial.print(receivedSOS[i].message.message);
            if (receivedSOS[i].acknowledged) {
                Serial.print(" [ACK'd]");
            }
            Serial.print(" (");
            Serial.print((millis() - receivedSOS[i].receivedAt) / 1000);
            Serial.println("s ago)");
            count++;
        }
    }
    if (count == 0) {
        Serial.println("  (none)");
    }

    Serial.println("\nStatistics:");
    Serial.print("  Activations: ");
    Serial.println(stats.sosActivations);
    Serial.print("  Broadcasts: ");
    Serial.println(stats.sosBroadcasts);
    Serial.print("  Received: ");
    Serial.println(stats.sosReceived);
    Serial.print("  Acknowledged: ");
    Serial.println(stats.sosAcknowledged);

    Serial.println("===============================\n");
}

void Emergency::clearReceived() {
    for (int i = 0; i < MAX_RECEIVED_SOS; i++) {
        receivedSOS[i].valid = false;
    }
    Serial.println("[SOS] Received SOS list cleared");
}

void Emergency::broadcastSOS() {
    if (!broadcastCallback) {
        Serial.println("[SOS] No broadcast callback set!");
        return;
    }

    // Build broadcast packet
    // Format: [MSG_SOS marker] + SOSMessage
    uint8_t packet[sizeof(SOSMessage) + 2];
    packet[0] = 0x53;  // SOS marker byte 1 ('S')
    packet[1] = 0x4F;  // SOS marker byte 2 ('O')
    memcpy(&packet[2], &currentMessage, sizeof(SOSMessage));

    if (broadcastCallback(packet, sizeof(packet))) {
        stats.sosBroadcasts++;
        Serial.print("[SOS] Broadcast #");
        Serial.println(stats.sosBroadcasts);
    }
}

int Emergency::findReceivedSOS(uint32_t nodeAddress) {
    for (int i = 0; i < MAX_RECEIVED_SOS; i++) {
        if (receivedSOS[i].valid && receivedSOS[i].nodeAddress == nodeAddress) {
            return i;
        }
    }
    return -1;
}

int Emergency::findEmptyReceivedSlot() {
    for (int i = 0; i < MAX_RECEIVED_SOS; i++) {
        if (!receivedSOS[i].valid) {
            return i;
        }
    }
    return -1;
}
