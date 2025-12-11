/**
 * @file groups.h
 * @brief LNK-22 Encrypted Group Channels
 *
 * Implements secure group messaging with:
 * - Pre-shared key (PSK) encryption per group
 * - Group membership management
 * - Message authentication
 * - Replay protection via sequence numbers
 */

#ifndef GROUPS_H
#define GROUPS_H

#include <Arduino.h>
#include "../config.h"

// Group Configuration
#define MAX_GROUPS 8                  // Maximum groups we can be member of
#define MAX_GROUP_MEMBERS 16          // Max members per group (for display)
#define GROUP_KEY_SIZE 32             // 256-bit group keys
#define GROUP_NAME_SIZE 16            // Max group name length
#define GROUP_NONCE_SIZE 12           // Nonce size for encryption

// Group flags
#define GROUP_FLAG_ADMIN    0x01      // We are admin of this group
#define GROUP_FLAG_READONLY 0x02      // Read-only membership
#define GROUP_FLAG_HIDDEN   0x04      // Don't advertise membership

// Group packet types
enum GroupPacketType {
    GROUP_MSG = 0x30,         // Encrypted group message
    GROUP_ACK = 0x31,         // Message acknowledgment
    GROUP_ANNOUNCE = 0x32,    // Group announcement/discovery
    GROUP_KEY_UPDATE = 0x33,  // Key rotation (admin only)
    GROUP_INVITE = 0x34       // Invite to group
};

// Group definition
struct Group {
    uint32_t groupId;                  // Unique group identifier (hash of name+key)
    char name[GROUP_NAME_SIZE + 1];    // Human-readable name
    uint8_t key[GROUP_KEY_SIZE];       // Shared encryption key
    uint8_t flags;                     // Group flags
    uint32_t txSequence;               // Our TX sequence number
    uint32_t rxSequence;               // Highest RX sequence seen
    uint32_t lastActivity;             // Last message timestamp
    uint8_t memberCount;               // Known members (for display)
    bool valid;
};

// Group message header (encrypted portion follows)
struct __attribute__((packed)) GroupMessageHeader {
    uint32_t groupId;          // Target group
    uint32_t sequence;         // Sequence number (replay protection)
    uint32_t sender;           // Sender node address
    uint16_t payloadLen;       // Encrypted payload length
    uint8_t msgType;           // Message type within group
    uint8_t reserved;
    // Followed by: encrypted payload + 16-byte auth tag
};

// Group announcement (for discovery)
struct __attribute__((packed)) GroupAnnounce {
    uint32_t groupId;
    uint8_t nameHash[8];       // First 8 bytes of name hash (privacy)
    uint8_t memberCount;
    uint8_t flags;
};

// Group statistics
struct GroupStats {
    uint32_t messagesSent;
    uint32_t messagesReceived;
    uint32_t messagesDecrypted;
    uint32_t decryptionFailed;
    uint32_t replayRejected;
};

// Callbacks
typedef void (*GroupMessageCallback)(uint32_t groupId, uint32_t sender,
                                      const uint8_t* data, uint16_t length);
typedef void (*GroupEventCallback)(uint32_t groupId, uint8_t event);

class GroupManager {
public:
    GroupManager();

    /**
     * @brief Initialize group manager
     * @param nodeAddr This node's address
     */
    void begin(uint32_t nodeAddr);

    /**
     * @brief Create a new group
     * @param name Group name
     * @param key Pre-shared key (32 bytes), or nullptr to generate
     * @return Group ID or 0 on failure
     */
    uint32_t createGroup(const char* name, const uint8_t* key = nullptr);

    /**
     * @brief Join an existing group with known key
     * @param name Group name
     * @param key Pre-shared key (32 bytes)
     * @return Group ID or 0 on failure
     */
    uint32_t joinGroup(const char* name, const uint8_t* key);

    /**
     * @brief Leave a group
     * @param groupId Group to leave
     */
    void leaveGroup(uint32_t groupId);

    /**
     * @brief Send message to group
     * @param groupId Target group
     * @param data Message data
     * @param length Data length
     * @return true if sent
     */
    bool sendMessage(uint32_t groupId, const uint8_t* data, uint16_t length);

    /**
     * @brief Send message to group by name
     * @param groupName Group name
     * @param data Message data
     * @param length Data length
     * @return true if sent
     */
    bool sendMessageByName(const char* groupName, const uint8_t* data, uint16_t length);

    /**
     * @brief Handle incoming group packet
     * @param fromAddr Source address
     * @param type Packet type
     * @param data Packet data
     * @param length Data length
     */
    void handlePacket(uint32_t fromAddr, uint8_t type,
                      const uint8_t* data, uint16_t length);

    /**
     * @brief Get group by ID
     */
    Group* getGroupById(uint32_t groupId);

    /**
     * @brief Get group by name
     */
    Group* getGroupByName(const char* name);

    /**
     * @brief Get number of groups
     */
    uint8_t getGroupCount();

    /**
     * @brief Get group by index
     */
    Group* getGroupByIndex(uint8_t index);

    /**
     * @brief Generate group ID from name and key
     */
    static uint32_t generateGroupId(const char* name, const uint8_t* key);

    /**
     * @brief Export group key (for sharing)
     * @param groupId Group ID
     * @param keyOut Output buffer (32 bytes)
     * @return true if exported
     */
    bool exportKey(uint32_t groupId, uint8_t* keyOut);

    /**
     * @brief Set callbacks
     */
    void setMessageCallback(GroupMessageCallback cb) { onMessage = cb; }
    void setEventCallback(GroupEventCallback cb) { onEvent = cb; }

    /**
     * @brief Set send function
     */
    void setSendFunction(bool (*sendFn)(uint32_t dest, uint8_t type,
                                         const uint8_t* data, uint16_t len));

    /**
     * @brief Get statistics
     */
    GroupStats getStats() const { return stats; }

    /**
     * @brief Print status
     */
    void printStatus();

    /**
     * @brief Save groups to flash
     */
    bool save();

    /**
     * @brief Load groups from flash
     */
    bool load();

private:
    Group groups[MAX_GROUPS];
    uint32_t nodeAddress;
    GroupStats stats;

    GroupMessageCallback onMessage;
    GroupEventCallback onEvent;
    bool (*sendPacket)(uint32_t, uint8_t, const uint8_t*, uint16_t);

    int findEmptySlot();
    int findGroupById(uint32_t groupId);
    int findGroupByName(const char* name);

    bool encryptMessage(Group* group, const uint8_t* plaintext, uint16_t len,
                        uint8_t* output, uint16_t* outLen);
    bool decryptMessage(Group* group, const uint8_t* ciphertext, uint16_t len,
                        uint8_t* output, uint16_t* outLen, uint32_t sequence);
};

// Global instance
extern GroupManager groupManager;

#endif // GROUPS_H
