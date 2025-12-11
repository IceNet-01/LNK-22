/**
 * @file message_history.cpp
 * @brief LNK-22 Message History Implementation
 */

#include "message_history.h"
#include "../naming/naming.h"

#ifdef NRF52_SERIES
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;
#define HISTORY_FILE "/lnk22_history.dat"
#define HISTORY_MAGIC 0x48495354  // "HIST"
#endif

// Global instance
MessageHistory messageHistory;

extern NodeNaming nodeNaming;

MessageHistory::MessageHistory() :
    writeIndex(0),
    messageCount(0),
    nextMessageId(1)
{
    memset(&stats, 0, sizeof(HistoryStats));
    for (int i = 0; i < HISTORY_MAX_MESSAGES; i++) {
        messages[i].valid = false;
    }
}

void MessageHistory::begin() {
    Serial.println("[HISTORY] Message history initialized");
    Serial.print("[HISTORY] Capacity: ");
    Serial.print(HISTORY_MAX_MESSAGES);
    Serial.println(" messages");

#if HISTORY_PERSIST && defined(NRF52_SERIES)
    if (load()) {
        Serial.print("[HISTORY] Loaded ");
        Serial.print(messageCount);
        Serial.println(" messages from flash");
    }
#endif
}

void MessageHistory::addReceived(uint32_t source, uint32_t dest, const char* text,
                                  uint8_t hopCount, int16_t rssi, int8_t snr) {
    addEntry(source, dest, text, MSG_DIR_RECEIVED, hopCount, rssi, snr);
    stats.totalReceived++;
}

void MessageHistory::addSent(uint32_t dest, const char* text) {
    addEntry(0, dest, text, MSG_DIR_SENT);
    stats.totalSent++;
}

void MessageHistory::addBroadcastRx(uint32_t source, const char* text,
                                     int16_t rssi, int8_t snr) {
    addEntry(source, 0xFFFFFFFF, text, MSG_DIR_BROADCAST_RX, 0, rssi, snr);
    stats.totalBroadcastRx++;
}

void MessageHistory::addBroadcastTx(const char* text) {
    addEntry(0, 0xFFFFFFFF, text, MSG_DIR_BROADCAST_TX);
    stats.totalBroadcastTx++;
}

HistoryEntry* MessageHistory::getMessage(uint8_t index) {
    int slot = getSlotByIndex(index);
    if (slot < 0) {
        return nullptr;
    }
    return &messages[slot];
}

HistoryEntry* MessageHistory::getMessageById(uint32_t id) {
    for (int i = 0; i < HISTORY_MAX_MESSAGES; i++) {
        if (messages[i].valid && messages[i].id == id) {
            return &messages[i];
        }
    }
    return nullptr;
}

uint8_t MessageHistory::getCount() const {
    return messageCount;
}

uint8_t MessageHistory::getMessagesFrom(uint32_t nodeAddress, HistoryEntry* entries, uint8_t maxEntries) {
    uint8_t found = 0;

    // Search from newest to oldest
    for (uint8_t i = 0; i < messageCount && found < maxEntries; i++) {
        HistoryEntry* entry = getMessage(i);
        if (entry && entry->source == nodeAddress) {
            entries[found++] = *entry;
        }
    }

    return found;
}

uint8_t MessageHistory::getMessagesTo(uint32_t nodeAddress, HistoryEntry* entries, uint8_t maxEntries) {
    uint8_t found = 0;

    for (uint8_t i = 0; i < messageCount && found < maxEntries; i++) {
        HistoryEntry* entry = getMessage(i);
        if (entry && entry->destination == nodeAddress) {
            entries[found++] = *entry;
        }
    }

    return found;
}

uint8_t MessageHistory::search(const char* searchText, HistoryEntry* entries, uint8_t maxEntries) {
    uint8_t found = 0;

    // Convert search to lowercase for case-insensitive search
    char lowerSearch[32];
    strncpy(lowerSearch, searchText, 31);
    lowerSearch[31] = '\0';
    for (int i = 0; lowerSearch[i]; i++) {
        lowerSearch[i] = tolower(lowerSearch[i]);
    }

    for (uint8_t i = 0; i < messageCount && found < maxEntries; i++) {
        HistoryEntry* entry = getMessage(i);
        if (!entry) continue;

        // Convert message text to lowercase
        char lowerText[HISTORY_MAX_TEXT];
        strncpy(lowerText, entry->text, HISTORY_MAX_TEXT - 1);
        lowerText[HISTORY_MAX_TEXT - 1] = '\0';
        for (int j = 0; lowerText[j]; j++) {
            lowerText[j] = tolower(lowerText[j]);
        }

        // Check if search text is found
        if (strstr(lowerText, lowerSearch)) {
            entries[found++] = *entry;
        }
    }

    return found;
}

void MessageHistory::clear() {
    for (int i = 0; i < HISTORY_MAX_MESSAGES; i++) {
        messages[i].valid = false;
    }
    writeIndex = 0;
    messageCount = 0;
    Serial.println("[HISTORY] History cleared");
}

bool MessageHistory::save() {
#if HISTORY_PERSIST && defined(NRF52_SERIES)
    File file(InternalFS);
    if (!file.open(HISTORY_FILE, FILE_O_WRITE)) {
        // Try to create the file
        if (!file.open(HISTORY_FILE, FILE_O_WRITE)) {
            Serial.println("[HISTORY] Failed to open file for writing");
            return false;
        }
    }

    // Write magic number and count
    uint32_t magic = HISTORY_MAGIC;
    file.write((uint8_t*)&magic, 4);
    file.write((uint8_t*)&messageCount, 1);
    file.write((uint8_t*)&writeIndex, 1);
    file.write((uint8_t*)&nextMessageId, 4);

    // Write all messages
    for (int i = 0; i < HISTORY_MAX_MESSAGES; i++) {
        file.write((uint8_t*)&messages[i], sizeof(HistoryEntry));
    }

    file.close();
    Serial.print("[HISTORY] Saved ");
    Serial.print(messageCount);
    Serial.println(" messages to flash");
    return true;
#else
    return false;
#endif
}

bool MessageHistory::load() {
#if HISTORY_PERSIST && defined(NRF52_SERIES)
    File file(InternalFS);
    if (!file.open(HISTORY_FILE, FILE_O_READ)) {
        return false;
    }

    // Read and verify magic
    uint32_t magic;
    file.read((uint8_t*)&magic, 4);
    if (magic != HISTORY_MAGIC) {
        file.close();
        return false;
    }

    // Read header
    file.read((uint8_t*)&messageCount, 1);
    file.read((uint8_t*)&writeIndex, 1);
    file.read((uint8_t*)&nextMessageId, 4);

    // Read all messages
    for (int i = 0; i < HISTORY_MAX_MESSAGES; i++) {
        file.read((uint8_t*)&messages[i], sizeof(HistoryEntry));
    }

    file.close();
    return true;
#else
    return false;
#endif
}

void MessageHistory::print(uint8_t count) {
    Serial.println("\n=== Message History ===");
    Serial.print("Total: ");
    Serial.print(messageCount);
    Serial.println(" messages\n");

    uint8_t toPrint = (count == 0 || count > messageCount) ? messageCount : count;

    for (uint8_t i = 0; i < toPrint; i++) {
        HistoryEntry* entry = getMessage(i);
        if (!entry) continue;

        // Direction indicator
        switch (entry->direction) {
            case MSG_DIR_RECEIVED:
                Serial.print("<< RX ");
                break;
            case MSG_DIR_SENT:
                Serial.print(">> TX ");
                break;
            case MSG_DIR_BROADCAST_RX:
                Serial.print("<< BC ");
                break;
            case MSG_DIR_BROADCAST_TX:
                Serial.print(">> BC ");
                break;
        }

        // Source/destination
        if (entry->direction == MSG_DIR_RECEIVED || entry->direction == MSG_DIR_BROADCAST_RX) {
            Serial.print("from ");
            Serial.print(nodeNaming.getNodeName(entry->source));
        } else {
            Serial.print("to ");
            if (entry->destination == 0xFFFFFFFF) {
                Serial.print("ALL");
            } else {
                Serial.print(nodeNaming.getNodeName(entry->destination));
            }
        }

        // Signal quality for received
        if (entry->direction == MSG_DIR_RECEIVED || entry->direction == MSG_DIR_BROADCAST_RX) {
            Serial.print(" (RSSI:");
            Serial.print(entry->rssi);
            Serial.print(")");
        }

        Serial.println(":");
        Serial.print("   \"");
        Serial.print(entry->text);
        Serial.println("\"");
    }

    Serial.println("\n=======================\n");
}

void MessageHistory::printFrom(uint32_t nodeAddress, uint8_t count) {
    Serial.print("\n=== Messages from ");
    Serial.print(nodeNaming.getNodeName(nodeAddress));
    Serial.println(" ===");

    HistoryEntry entries[10];
    uint8_t found = getMessagesFrom(nodeAddress, entries, min(count, (uint8_t)10));

    for (uint8_t i = 0; i < found; i++) {
        Serial.print("  \"");
        Serial.print(entries[i].text);
        Serial.print("\" (RSSI:");
        Serial.print(entries[i].rssi);
        Serial.println(")");
    }

    if (found == 0) {
        Serial.println("  (no messages)");
    }

    Serial.println("========================\n");
}

uint32_t MessageHistory::getNewestTimestamp() {
    if (messageCount == 0) return 0;
    HistoryEntry* entry = getMessage(0);
    return entry ? entry->timestamp : 0;
}

uint32_t MessageHistory::getOldestTimestamp() {
    if (messageCount == 0) return 0;
    HistoryEntry* entry = getMessage(messageCount - 1);
    return entry ? entry->timestamp : 0;
}

void MessageHistory::addEntry(uint32_t source, uint32_t dest, const char* text,
                               MessageDirection direction, uint8_t hopCount,
                               int16_t rssi, int8_t snr) {
    if (!text || strlen(text) == 0) {
        return;
    }

    // Write to current slot
    HistoryEntry* entry = &messages[writeIndex];

    entry->id = nextMessageId++;
    entry->timestamp = millis();
    entry->source = source;
    entry->destination = dest;
    entry->direction = direction;
    entry->hopCount = hopCount;
    entry->rssi = rssi;
    entry->snr = snr;
    entry->textLength = min(strlen(text), (size_t)(HISTORY_MAX_TEXT - 1));
    strncpy(entry->text, text, HISTORY_MAX_TEXT - 1);
    entry->text[HISTORY_MAX_TEXT - 1] = '\0';
    entry->valid = true;

    // Advance write index (circular buffer)
    writeIndex = (writeIndex + 1) % HISTORY_MAX_MESSAGES;

    // Update count
    if (messageCount < HISTORY_MAX_MESSAGES) {
        messageCount++;
    }
}

int MessageHistory::getSlotByIndex(uint8_t index) {
    if (index >= messageCount) {
        return -1;
    }

    // Index 0 = most recent
    // writeIndex points to next slot to write (oldest if full)
    // So most recent is writeIndex - 1

    int slot;
    if (messageCount < HISTORY_MAX_MESSAGES) {
        // Buffer not full yet
        slot = (messageCount - 1 - index);
    } else {
        // Buffer is full, wraparound
        slot = (writeIndex - 1 - index + HISTORY_MAX_MESSAGES) % HISTORY_MAX_MESSAGES;
    }

    if (slot >= 0 && slot < HISTORY_MAX_MESSAGES && messages[slot].valid) {
        return slot;
    }

    return -1;
}
