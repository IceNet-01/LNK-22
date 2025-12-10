/**
 * LNK-22 Message Storage
 * Persistent message storage in Flash memory
 */

#ifndef LNK22_STORAGE_H
#define LNK22_STORAGE_H

#include <Arduino.h>
#include "../config.h"

// Storage configuration
#define MAX_STORED_MESSAGES 50      // Maximum messages to store
#define MESSAGE_MAX_LENGTH 240      // Max message content length
#define STORAGE_FILE "/lnk22_messages.dat"

// Message flags
#define MSG_FLAG_OUTGOING   0x01    // Message was sent by us
#define MSG_FLAG_INCOMING   0x02    // Message received
#define MSG_FLAG_DELIVERED  0x04    // Delivery confirmed (ACK received)
#define MSG_FLAG_READ       0x08    // Message has been read

// Stored message structure
struct StoredMessage {
    uint32_t timestamp;             // Unix timestamp or millis()
    uint32_t source;                // Source node address
    uint32_t destination;           // Destination node address
    uint8_t channel;                // Channel ID
    uint8_t flags;                  // Message flags
    uint16_t message_id;            // Unique message ID
    uint16_t length;                // Message length
    char content[MESSAGE_MAX_LENGTH]; // Message content
    int16_t rssi;                   // RSSI (for received messages)
    int8_t snr;                     // SNR (for received messages)
    bool valid;                     // Entry is valid
};

class Storage {
public:
    Storage();
    ~Storage();

    // Initialize storage subsystem
    bool begin();

    // Store a message
    bool storeMessage(uint32_t source, uint32_t dest, uint8_t channel,
                     const char* message, uint16_t len, bool outgoing,
                     uint16_t messageId = 0, int16_t rssi = 0, int8_t snr = 0);

    // Mark message as delivered (ACK received)
    bool markDelivered(uint16_t messageId);

    // Mark message as read
    bool markRead(uint16_t messageId);

    // Get message history
    uint8_t getMessageCount() const;
    const StoredMessage* getMessage(uint8_t index) const;

    // Get messages by filter
    uint8_t getMessagesByNode(uint32_t nodeAddr, StoredMessage* results, uint8_t maxResults) const;
    uint8_t getMessagesByChannel(uint8_t channel, StoredMessage* results, uint8_t maxResults) const;
    uint8_t getUnreadMessages(StoredMessage* results, uint8_t maxResults) const;

    // Clear all messages
    void clearAll();

    // Clear old messages (keep last N)
    void clearOld(uint8_t keepCount);

    // Save to flash
    bool save();

    // Load from flash
    bool load();

    // Get storage statistics
    uint32_t getTotalMessages() const { return totalMessages; }
    uint32_t getStorageUsed() const;
    uint32_t getStorageAvailable() const;

private:
    StoredMessage messages[MAX_STORED_MESSAGES];
    uint8_t messageCount;
    uint32_t totalMessages;  // Total messages ever stored (wraps)
    uint16_t nextMessageId;

    // Find oldest message slot
    uint8_t findOldestSlot() const;

    // Find message by ID
    int findMessageById(uint16_t messageId) const;
};

#endif // LNK22_STORAGE_H
