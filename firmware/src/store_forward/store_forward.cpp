/**
 * @file store_forward.cpp
 * @brief LNK-22 Store-and-Forward Messaging Implementation
 */

#include "store_forward.h"

// Global instance
StoreForward storeForward;

StoreForward::StoreForward() :
    nextMessageId(1),
    deliveryCallback(nullptr)
{
    memset(&stats, 0, sizeof(SFStats));
    for (int i = 0; i < SF_MAX_MESSAGES; i++) {
        messages[i].valid = false;
    }
}

void StoreForward::begin() {
    Serial.println("[SF] Store-and-Forward system initialized");
    Serial.print("[SF] Queue capacity: ");
    Serial.print(SF_MAX_MESSAGES);
    Serial.println(" messages");
}

void StoreForward::update(bool (*isNodeReachable)(uint32_t)) {
    unsigned long now = millis();

    // Process each queued message
    for (int i = 0; i < SF_MAX_MESSAGES; i++) {
        if (!messages[i].valid || messages[i].delivered) {
            continue;
        }

        // Check if message has expired
        if (now - messages[i].timestamp > SF_MESSAGE_TTL) {
            Serial.print("[SF] Message ");
            Serial.print(messages[i].id);
            Serial.println(" expired");
            messages[i].valid = false;
            stats.messagesExpired++;
            continue;
        }

        // Check if it's time to retry
        if (now - messages[i].lastAttempt < SF_RETRY_INTERVAL) {
            continue;
        }

        // Check if max retries exceeded
        if (messages[i].retries >= SF_MAX_RETRIES) {
            Serial.print("[SF] Message ");
            Serial.print(messages[i].id);
            Serial.println(" max retries exceeded");
            messages[i].valid = false;
            stats.messagesExpired++;
            continue;
        }

        // Check if destination is reachable
        if (isNodeReachable && isNodeReachable(messages[i].destination)) {
            // Attempt delivery
            if (deliveryCallback) {
                Serial.print("[SF] Attempting delivery of message ");
                Serial.print(messages[i].id);
                Serial.print(" to 0x");
                Serial.println(messages[i].destination, HEX);

                if (deliveryCallback(messages[i].destination,
                                    messages[i].payload,
                                    messages[i].payloadLength)) {
                    messages[i].lastAttempt = now;
                    messages[i].retries++;
                    stats.deliveryAttempts++;
                }
            }
        }
    }
}

uint32_t StoreForward::queueMessage(uint32_t dest, uint32_t source,
                                     const uint8_t* payload, uint16_t length,
                                     MessagePriority priority) {
    if (length > SF_MAX_PAYLOAD) {
        Serial.println("[SF] Message too large to queue");
        return 0;
    }

    int slot = findEmptySlot();
    if (slot < 0) {
        // Try to make room by removing lowest priority expired message
        cleanup();
        slot = findEmptySlot();
        if (slot < 0) {
            Serial.println("[SF] Queue full, cannot store message");
            return 0;
        }
    }

    // Store the message
    messages[slot].id = nextMessageId++;
    messages[slot].source = source;
    messages[slot].destination = dest;
    messages[slot].timestamp = millis();
    messages[slot].lastAttempt = 0;
    messages[slot].retries = 0;
    messages[slot].priority = priority;
    messages[slot].payloadLength = length;
    memcpy(messages[slot].payload, payload, length);
    messages[slot].valid = true;
    messages[slot].delivered = false;

    stats.messagesQueued++;

    Serial.print("[SF] Queued message ");
    Serial.print(messages[slot].id);
    Serial.print(" for 0x");
    Serial.print(dest, HEX);
    Serial.print(" (priority ");
    Serial.print(priority);
    Serial.println(")");

    return messages[slot].id;
}

void StoreForward::markDelivered(uint32_t messageId) {
    int slot = findMessage(messageId);
    if (slot >= 0) {
        messages[slot].delivered = true;
        messages[slot].valid = false;  // Free the slot
        stats.messagesDelivered++;

        Serial.print("[SF] Message ");
        Serial.print(messageId);
        Serial.println(" delivered successfully");
    }
}

bool StoreForward::getNextMessage(uint32_t dest, QueuedMessage* msg) {
    // Find highest priority message for this destination
    int bestSlot = -1;
    uint8_t highestPriority = 0;

    for (int i = 0; i < SF_MAX_MESSAGES; i++) {
        if (messages[i].valid && !messages[i].delivered &&
            messages[i].destination == dest) {
            if (bestSlot < 0 || messages[i].priority > highestPriority) {
                bestSlot = i;
                highestPriority = messages[i].priority;
            }
        }
    }

    if (bestSlot >= 0 && msg) {
        *msg = messages[bestSlot];
        return true;
    }

    return false;
}

uint8_t StoreForward::getPendingCount(uint32_t dest) {
    uint8_t count = 0;
    for (int i = 0; i < SF_MAX_MESSAGES; i++) {
        if (messages[i].valid && !messages[i].delivered &&
            messages[i].destination == dest) {
            count++;
        }
    }
    return count;
}

uint8_t StoreForward::getTotalQueued() {
    uint8_t count = 0;
    for (int i = 0; i < SF_MAX_MESSAGES; i++) {
        if (messages[i].valid && !messages[i].delivered) {
            count++;
        }
    }
    return count;
}

void StoreForward::cleanup() {
    unsigned long now = millis();

    for (int i = 0; i < SF_MAX_MESSAGES; i++) {
        if (messages[i].valid) {
            // Remove expired messages
            if (now - messages[i].timestamp > SF_MESSAGE_TTL) {
                messages[i].valid = false;
                stats.messagesExpired++;
            }
            // Remove delivered messages
            else if (messages[i].delivered) {
                messages[i].valid = false;
            }
        }
    }
}

void StoreForward::clear() {
    for (int i = 0; i < SF_MAX_MESSAGES; i++) {
        messages[i].valid = false;
    }
    Serial.println("[SF] Queue cleared");
}

void StoreForward::printStatus() {
    Serial.println("\n=== Store-and-Forward Status ===");
    Serial.print("Queued: ");
    Serial.print(getTotalQueued());
    Serial.print("/");
    Serial.println(SF_MAX_MESSAGES);

    Serial.print("Total queued: ");
    Serial.println(stats.messagesQueued);
    Serial.print("Delivered: ");
    Serial.println(stats.messagesDelivered);
    Serial.print("Expired: ");
    Serial.println(stats.messagesExpired);
    Serial.print("Attempts: ");
    Serial.println(stats.deliveryAttempts);

    // List pending messages
    Serial.println("\nPending messages:");
    unsigned long now = millis();
    int count = 0;

    for (int i = 0; i < SF_MAX_MESSAGES; i++) {
        if (messages[i].valid && !messages[i].delivered) {
            Serial.print("  [");
            Serial.print(messages[i].id);
            Serial.print("] -> 0x");
            Serial.print(messages[i].destination, HEX);
            Serial.print(" P");
            Serial.print(messages[i].priority);
            Serial.print(" (");
            Serial.print(messages[i].retries);
            Serial.print(" tries, ");
            Serial.print((now - messages[i].timestamp) / 1000);
            Serial.println("s old)");
            count++;
        }
    }

    if (count == 0) {
        Serial.println("  (none)");
    }
    Serial.println("================================\n");
}

void StoreForward::setDeliveryCallback(bool (*callback)(uint32_t, const uint8_t*, uint16_t)) {
    deliveryCallback = callback;
}

int StoreForward::findEmptySlot() {
    for (int i = 0; i < SF_MAX_MESSAGES; i++) {
        if (!messages[i].valid) {
            return i;
        }
    }
    return -1;
}

int StoreForward::findMessage(uint32_t messageId) {
    for (int i = 0; i < SF_MAX_MESSAGES; i++) {
        if (messages[i].valid && messages[i].id == messageId) {
            return i;
        }
    }
    return -1;
}
