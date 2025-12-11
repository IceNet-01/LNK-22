/**
 * @file message_history.h
 * @brief LNK-22 Message History System
 *
 * Stores recent messages for viewing on any node. Useful when:
 * - Joining a network and wanting to see recent activity
 * - Scrolling back through message history on OLED
 * - Debugging message flow
 */

#ifndef MESSAGE_HISTORY_H
#define MESSAGE_HISTORY_H

#include <Arduino.h>
#include "../config.h"

// History Configuration
#define HISTORY_MAX_MESSAGES 32      // Maximum messages to store
#define HISTORY_MAX_TEXT 128         // Max text per message
#define HISTORY_PERSIST true         // Save to flash on power-off

// Message direction
enum MessageDirection {
    MSG_DIR_RECEIVED = 0,
    MSG_DIR_SENT = 1,
    MSG_DIR_BROADCAST_RX = 2,
    MSG_DIR_BROADCAST_TX = 3
};

// Stored message entry
struct HistoryEntry {
    uint32_t id;                     // Unique message ID
    uint32_t timestamp;              // When stored (millis or RTC)
    uint32_t source;                 // Message sender
    uint32_t destination;            // Message recipient
    uint8_t direction;               // MessageDirection
    uint8_t hopCount;                // How many hops (for received)
    int16_t rssi;                    // RSSI when received
    int8_t snr;                      // SNR when received
    uint8_t textLength;              // Length of text
    char text[HISTORY_MAX_TEXT];     // Message content
    bool valid;
};

// History statistics
struct HistoryStats {
    uint32_t totalReceived;
    uint32_t totalSent;
    uint32_t totalBroadcastRx;
    uint32_t totalBroadcastTx;
};

class MessageHistory {
public:
    MessageHistory();

    /**
     * @brief Initialize message history
     */
    void begin();

    /**
     * @brief Add a received message to history
     */
    void addReceived(uint32_t source, uint32_t dest, const char* text,
                     uint8_t hopCount = 0, int16_t rssi = 0, int8_t snr = 0);

    /**
     * @brief Add a sent message to history
     */
    void addSent(uint32_t dest, const char* text);

    /**
     * @brief Add a received broadcast to history
     */
    void addBroadcastRx(uint32_t source, const char* text,
                        int16_t rssi = 0, int8_t snr = 0);

    /**
     * @brief Add a sent broadcast to history
     */
    void addBroadcastTx(const char* text);

    /**
     * @brief Get message by index (0 = newest)
     * @return Pointer to entry or nullptr
     */
    HistoryEntry* getMessage(uint8_t index);

    /**
     * @brief Get message by ID
     */
    HistoryEntry* getMessageById(uint32_t id);

    /**
     * @brief Get total message count
     */
    uint8_t getCount() const;

    /**
     * @brief Get messages from a specific node
     * @param nodeAddress Node to filter by
     * @param entries Output array
     * @param maxEntries Maximum to return
     * @return Number of entries found
     */
    uint8_t getMessagesFrom(uint32_t nodeAddress, HistoryEntry* entries, uint8_t maxEntries);

    /**
     * @brief Get messages to a specific node
     */
    uint8_t getMessagesTo(uint32_t nodeAddress, HistoryEntry* entries, uint8_t maxEntries);

    /**
     * @brief Search messages for text
     * @param searchText Text to search for (case-insensitive)
     * @param entries Output array
     * @param maxEntries Maximum to return
     * @return Number of matches
     */
    uint8_t search(const char* searchText, HistoryEntry* entries, uint8_t maxEntries);

    /**
     * @brief Clear all history
     */
    void clear();

    /**
     * @brief Save history to flash
     */
    bool save();

    /**
     * @brief Load history from flash
     */
    bool load();

    /**
     * @brief Get statistics
     */
    HistoryStats getStats() const { return stats; }

    /**
     * @brief Print history to serial
     * @param count Number of messages to print (0 = all)
     */
    void print(uint8_t count = 10);

    /**
     * @brief Print messages from specific node
     */
    void printFrom(uint32_t nodeAddress, uint8_t count = 5);

    /**
     * @brief Get newest message timestamp
     */
    uint32_t getNewestTimestamp();

    /**
     * @brief Get oldest message timestamp
     */
    uint32_t getOldestTimestamp();

private:
    HistoryEntry messages[HISTORY_MAX_MESSAGES];
    uint8_t writeIndex;              // Next slot to write
    uint8_t messageCount;            // Total valid messages
    uint32_t nextMessageId;
    HistoryStats stats;

    void addEntry(uint32_t source, uint32_t dest, const char* text,
                  MessageDirection direction, uint8_t hopCount = 0,
                  int16_t rssi = 0, int8_t snr = 0);
    int getSlotByIndex(uint8_t index);
};

// Global instance
extern MessageHistory messageHistory;

#endif // MESSAGE_HISTORY_H
