/**
 * @file store_forward.h
 * @brief LNK-22 Store-and-Forward Messaging System
 *
 * Queues messages for offline nodes and delivers them when the node becomes
 * reachable. Key differentiator from Meshtastic.
 */

#ifndef STORE_FORWARD_H
#define STORE_FORWARD_H

#include <Arduino.h>
#include "../config.h"
#include "../protocol/protocol.h"

// Store-and-Forward Configuration
#define SF_MAX_MESSAGES 32          // Maximum queued messages
#define SF_MAX_PAYLOAD 200          // Max payload per message
#define SF_MESSAGE_TTL 3600000      // Message expires after 1 hour (ms)
#define SF_RETRY_INTERVAL 30000     // Retry delivery every 30 seconds
#define SF_MAX_RETRIES 10           // Max delivery attempts

// Message priority levels
enum MessagePriority {
    PRIORITY_LOW = 0,
    PRIORITY_NORMAL = 1,
    PRIORITY_HIGH = 2,
    PRIORITY_EMERGENCY = 3  // SOS messages
};

// Queued message structure for store-and-forward
struct QueuedMessage {
    uint32_t id;                    // Unique message ID
    uint32_t source;                // Original sender
    uint32_t destination;           // Target recipient
    uint32_t timestamp;             // When message was stored
    uint32_t lastAttempt;           // Last delivery attempt time
    uint8_t retries;                // Delivery attempt count
    uint8_t priority;               // Message priority
    uint16_t payloadLength;         // Payload size
    uint8_t payload[SF_MAX_PAYLOAD]; // Message payload
    bool valid;                     // Slot in use
    bool delivered;                 // Successfully delivered
};

// Store-and-Forward statistics
struct SFStats {
    uint32_t messagesQueued;
    uint32_t messagesDelivered;
    uint32_t messagesExpired;
    uint32_t deliveryAttempts;
};

class StoreForward {
public:
    StoreForward();

    /**
     * @brief Initialize the store-and-forward system
     */
    void begin();

    /**
     * @brief Update - call from main loop to process queued messages
     * @param isNodeReachable Function to check if destination is reachable (optional)
     */
    void update(bool (*isNodeReachable)(uint32_t) = nullptr);

    /**
     * @brief Queue a message for later delivery
     * @param dest Destination address
     * @param source Source address (usually this node)
     * @param payload Message data
     * @param length Payload length
     * @param priority Message priority
     * @return Message ID or 0 on failure
     */
    uint32_t queueMessage(uint32_t dest, uint32_t source,
                          const uint8_t* payload, uint16_t length,
                          MessagePriority priority = PRIORITY_NORMAL);

    /**
     * @brief Mark a message as delivered (called when ACK received)
     * @param messageId Message ID to mark delivered
     */
    void markDelivered(uint32_t messageId);

    /**
     * @brief Get next message ready for delivery to a specific destination
     * @param dest Destination to check
     * @param msg Output message structure
     * @return true if message found
     */
    bool getNextMessage(uint32_t dest, QueuedMessage* msg);

    /**
     * @brief Get all pending messages for a destination
     * @param dest Destination address
     * @return Number of pending messages
     */
    uint8_t getPendingCount(uint32_t dest);

    /**
     * @brief Get total queued messages
     */
    uint8_t getTotalQueued();

    /**
     * @brief Remove expired messages
     */
    void cleanup();

    /**
     * @brief Get statistics
     */
    SFStats getStats() const { return stats; }

    /**
     * @brief Clear all queued messages
     */
    void clear();

    /**
     * @brief Print queue status to serial
     */
    void printStatus();

    /**
     * @brief Set delivery callback - called when message ready for sending
     */
    void setDeliveryCallback(bool (*callback)(uint32_t dest, const uint8_t* data, uint16_t len));

private:
    QueuedMessage messages[SF_MAX_MESSAGES];
    SFStats stats;
    uint32_t nextMessageId;
    bool (*deliveryCallback)(uint32_t, const uint8_t*, uint16_t);

    int findEmptySlot();
    int findMessage(uint32_t messageId);
    void sortByPriority();
};

// Global instance
extern StoreForward storeForward;

#endif // STORE_FORWARD_H
