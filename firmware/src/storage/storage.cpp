/**
 * LNK-22 Message Storage Implementation
 */

#include "storage.h"

#ifdef HAS_FLASH_STORAGE
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;
static File storageFile(InternalFS);
#endif

Storage::Storage() :
    messageCount(0),
    totalMessages(0),
    nextMessageId(1)
{
    // Initialize message array
    for (int i = 0; i < MAX_STORED_MESSAGES; i++) {
        messages[i].valid = false;
    }
}

Storage::~Storage() {
    save();
}

bool Storage::begin() {
    Serial.println("[STORAGE] Initializing message storage...");

    #ifdef HAS_FLASH_STORAGE
    // Initialize file system
    InternalFS.begin();

    // Try to load existing messages
    if (load()) {
        Serial.print("[STORAGE] Loaded ");
        Serial.print(messageCount);
        Serial.println(" messages from flash");
        return true;
    }

    Serial.println("[STORAGE] No existing messages, starting fresh");
    return true;

    #else
    Serial.println("[STORAGE] No flash storage available - messages will not persist");
    return false;
    #endif
}

bool Storage::storeMessage(uint32_t source, uint32_t dest, uint8_t channel,
                          const char* message, uint16_t len, bool outgoing,
                          uint16_t messageId, int16_t rssi, int8_t snr) {
    if (len > MESSAGE_MAX_LENGTH) {
        Serial.println("[STORAGE] Message too long to store");
        return false;
    }

    // Find slot to use
    uint8_t slot;
    if (messageCount < MAX_STORED_MESSAGES) {
        // Use next available slot
        slot = messageCount;
        messageCount++;
    } else {
        // Overwrite oldest message
        slot = findOldestSlot();
    }

    // Store message
    StoredMessage* msg = &messages[slot];
    msg->timestamp = millis() / 1000;  // Convert to seconds
    msg->source = source;
    msg->destination = dest;
    msg->channel = channel;
    msg->flags = outgoing ? MSG_FLAG_OUTGOING : MSG_FLAG_INCOMING;
    msg->message_id = messageId ? messageId : nextMessageId++;
    msg->length = len;
    memcpy(msg->content, message, len);
    msg->content[len] = '\0';  // Null terminate
    msg->rssi = rssi;
    msg->snr = snr;
    msg->valid = true;

    totalMessages++;

    #if DEBUG_MESH
    Serial.print("[STORAGE] Stored message #");
    Serial.print(msg->message_id);
    Serial.print(" in slot ");
    Serial.println(slot);
    #endif

    return true;
}

bool Storage::markDelivered(uint16_t messageId) {
    int index = findMessageById(messageId);
    if (index >= 0) {
        messages[index].flags |= MSG_FLAG_DELIVERED;
        return true;
    }
    return false;
}

bool Storage::markRead(uint16_t messageId) {
    int index = findMessageById(messageId);
    if (index >= 0) {
        messages[index].flags |= MSG_FLAG_READ;
        return true;
    }
    return false;
}

uint8_t Storage::getMessageCount() const {
    return messageCount;
}

const StoredMessage* Storage::getMessage(uint8_t index) const {
    if (index < messageCount && messages[index].valid) {
        return &messages[index];
    }
    return nullptr;
}

uint8_t Storage::getMessagesByNode(uint32_t nodeAddr, StoredMessage* results, uint8_t maxResults) const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < messageCount && count < maxResults; i++) {
        if (messages[i].valid &&
            (messages[i].source == nodeAddr || messages[i].destination == nodeAddr)) {
            memcpy(&results[count], &messages[i], sizeof(StoredMessage));
            count++;
        }
    }
    return count;
}

uint8_t Storage::getMessagesByChannel(uint8_t channel, StoredMessage* results, uint8_t maxResults) const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < messageCount && count < maxResults; i++) {
        if (messages[i].valid && messages[i].channel == channel) {
            memcpy(&results[count], &messages[i], sizeof(StoredMessage));
            count++;
        }
    }
    return count;
}

uint8_t Storage::getUnreadMessages(StoredMessage* results, uint8_t maxResults) const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < messageCount && count < maxResults; i++) {
        if (messages[i].valid &&
            (messages[i].flags & MSG_FLAG_INCOMING) &&
            !(messages[i].flags & MSG_FLAG_READ)) {
            memcpy(&results[count], &messages[i], sizeof(StoredMessage));
            count++;
        }
    }
    return count;
}

void Storage::clearAll() {
    for (int i = 0; i < MAX_STORED_MESSAGES; i++) {
        messages[i].valid = false;
    }
    messageCount = 0;
    save();
    Serial.println("[STORAGE] All messages cleared");
}

void Storage::clearOld(uint8_t keepCount) {
    if (messageCount <= keepCount) {
        return;  // Nothing to clear
    }

    // Find oldest messages to remove
    uint8_t toRemove = messageCount - keepCount;

    for (uint8_t removed = 0; removed < toRemove; removed++) {
        uint8_t oldestIdx = findOldestSlot();
        messages[oldestIdx].valid = false;
    }

    // Compact array
    uint8_t writeIdx = 0;
    for (uint8_t readIdx = 0; readIdx < MAX_STORED_MESSAGES; readIdx++) {
        if (messages[readIdx].valid) {
            if (writeIdx != readIdx) {
                memcpy(&messages[writeIdx], &messages[readIdx], sizeof(StoredMessage));
                messages[readIdx].valid = false;
            }
            writeIdx++;
        }
    }

    messageCount = writeIdx;
    save();

    Serial.print("[STORAGE] Cleared old messages, kept ");
    Serial.println(keepCount);
}

bool Storage::save() {
    #ifdef HAS_FLASH_STORAGE
    Serial.println("[STORAGE] Saving messages to flash...");

    // Open file for writing
    storageFile.open(STORAGE_FILE, FILE_O_WRITE);
    if (!storageFile) {
        Serial.println("[STORAGE] Failed to open storage file for writing");
        return false;
    }

    // Write header
    storageFile.write((uint8_t*)&messageCount, sizeof(messageCount));
    storageFile.write((uint8_t*)&totalMessages, sizeof(totalMessages));
    storageFile.write((uint8_t*)&nextMessageId, sizeof(nextMessageId));

    // Write messages
    for (uint8_t i = 0; i < messageCount; i++) {
        if (messages[i].valid) {
            storageFile.write((uint8_t*)&messages[i], sizeof(StoredMessage));
        }
    }

    storageFile.close();
    Serial.println("[STORAGE] Messages saved successfully");
    return true;

    #else
    return false;
    #endif
}

bool Storage::load() {
    #ifdef HAS_FLASH_STORAGE
    // Check if file exists
    if (!InternalFS.exists(STORAGE_FILE)) {
        return false;
    }

    // Open file for reading
    storageFile.open(STORAGE_FILE, FILE_O_READ);
    if (!storageFile) {
        Serial.println("[STORAGE] Failed to open storage file for reading");
        return false;
    }

    // Read header
    size_t bytesRead = 0;
    bytesRead += storageFile.read((uint8_t*)&messageCount, sizeof(messageCount));
    bytesRead += storageFile.read((uint8_t*)&totalMessages, sizeof(totalMessages));
    bytesRead += storageFile.read((uint8_t*)&nextMessageId, sizeof(nextMessageId));

    if (bytesRead != sizeof(messageCount) + sizeof(totalMessages) + sizeof(nextMessageId)) {
        Serial.println("[STORAGE] Corrupt storage header");
        storageFile.close();
        return false;
    }

    // Validate message count
    if (messageCount > MAX_STORED_MESSAGES) {
        Serial.println("[STORAGE] Invalid message count in storage");
        storageFile.close();
        return false;
    }

    // Read messages
    for (uint8_t i = 0; i < messageCount; i++) {
        size_t read = storageFile.read((uint8_t*)&messages[i], sizeof(StoredMessage));
        if (read != sizeof(StoredMessage)) {
            Serial.println("[STORAGE] Incomplete message data");
            messageCount = i;  // Truncate to valid messages
            break;
        }
    }

    storageFile.close();
    return true;

    #else
    return false;
    #endif
}

uint32_t Storage::getStorageUsed() const {
    return messageCount * sizeof(StoredMessage) +
           sizeof(messageCount) + sizeof(totalMessages) + sizeof(nextMessageId);
}

uint32_t Storage::getStorageAvailable() const {
    return (MAX_STORED_MESSAGES - messageCount) * sizeof(StoredMessage);
}

uint8_t Storage::findOldestSlot() const {
    uint8_t oldestIdx = 0;
    uint32_t oldestTime = 0xFFFFFFFF;

    for (uint8_t i = 0; i < messageCount; i++) {
        if (messages[i].valid && messages[i].timestamp < oldestTime) {
            oldestTime = messages[i].timestamp;
            oldestIdx = i;
        }
    }

    return oldestIdx;
}

int Storage::findMessageById(uint16_t messageId) const {
    for (uint8_t i = 0; i < messageCount; i++) {
        if (messages[i].valid && messages[i].message_id == messageId) {
            return i;
        }
    }
    return -1;
}
