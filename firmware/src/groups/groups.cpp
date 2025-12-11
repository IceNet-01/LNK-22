/**
 * @file groups.cpp
 * @brief LNK-22 Encrypted Group Channels Implementation
 */

#include "groups.h"
#include "../crypto/monocypher.h"

#ifdef NRF52_SERIES
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;
#define GROUPS_FILE "/lnk22_groups.dat"
#define GROUPS_MAGIC 0x47525053  // "GRPS"
#endif

// Global instance
GroupManager groupManager;

GroupManager::GroupManager() :
    nodeAddress(0),
    onMessage(nullptr),
    onEvent(nullptr),
    sendPacket(nullptr)
{
    memset(&stats, 0, sizeof(GroupStats));
    for (int i = 0; i < MAX_GROUPS; i++) {
        groups[i].valid = false;
    }
}

void GroupManager::begin(uint32_t nodeAddr) {
    nodeAddress = nodeAddr;

    Serial.println("[GROUP] Group manager initialized");
    Serial.print("[GROUP] Max groups: ");
    Serial.println(MAX_GROUPS);

    // Try to load saved groups
    load();
}

uint32_t GroupManager::createGroup(const char* name, const uint8_t* key) {
    if (!name || strlen(name) == 0 || strlen(name) > GROUP_NAME_SIZE) {
        Serial.println("[GROUP] Invalid group name");
        return 0;
    }

    // Check if already exists
    if (findGroupByName(name) >= 0) {
        Serial.println("[GROUP] Group already exists");
        return 0;
    }

    int slot = findEmptySlot();
    if (slot < 0) {
        Serial.println("[GROUP] No free group slots");
        return 0;
    }

    Group* group = &groups[slot];
    memset(group, 0, sizeof(Group));

    // Copy name
    strncpy(group->name, name, GROUP_NAME_SIZE);
    group->name[GROUP_NAME_SIZE] = '\0';

    // Set or generate key
    if (key) {
        memcpy(group->key, key, GROUP_KEY_SIZE);
    } else {
        // Generate random key
        for (int i = 0; i < GROUP_KEY_SIZE; i++) {
            group->key[i] = random(256);
        }
    }

    // Generate group ID
    group->groupId = generateGroupId(name, group->key);

    // Set flags - we're admin since we created it
    group->flags = GROUP_FLAG_ADMIN;
    group->txSequence = 1;
    group->rxSequence = 0;
    group->lastActivity = millis();
    group->memberCount = 1;
    group->valid = true;

    Serial.print("[GROUP] Created group '");
    Serial.print(name);
    Serial.print("' (ID: 0x");
    Serial.print(group->groupId, HEX);
    Serial.println(")");

    // Auto-save
    save();

    return group->groupId;
}

uint32_t GroupManager::joinGroup(const char* name, const uint8_t* key) {
    if (!name || !key) return 0;

    // Check if already member
    int existing = findGroupByName(name);
    if (existing >= 0) {
        Serial.println("[GROUP] Already member of this group");
        return groups[existing].groupId;
    }

    int slot = findEmptySlot();
    if (slot < 0) {
        Serial.println("[GROUP] No free group slots");
        return 0;
    }

    Group* group = &groups[slot];
    memset(group, 0, sizeof(Group));

    strncpy(group->name, name, GROUP_NAME_SIZE);
    group->name[GROUP_NAME_SIZE] = '\0';
    memcpy(group->key, key, GROUP_KEY_SIZE);

    group->groupId = generateGroupId(name, key);
    group->flags = 0;  // Not admin
    group->txSequence = 1;
    group->rxSequence = 0;
    group->lastActivity = millis();
    group->memberCount = 1;
    group->valid = true;

    Serial.print("[GROUP] Joined group '");
    Serial.print(name);
    Serial.print("' (ID: 0x");
    Serial.print(group->groupId, HEX);
    Serial.println(")");

    save();

    return group->groupId;
}

void GroupManager::leaveGroup(uint32_t groupId) {
    int slot = findGroupById(groupId);
    if (slot < 0) return;

    Serial.print("[GROUP] Left group '");
    Serial.print(groups[slot].name);
    Serial.println("'");

    // Wipe key material
    crypto_wipe(groups[slot].key, GROUP_KEY_SIZE);
    groups[slot].valid = false;

    save();
}

bool GroupManager::sendMessage(uint32_t groupId, const uint8_t* data, uint16_t length) {
    int slot = findGroupById(groupId);
    if (slot < 0) {
        Serial.println("[GROUP] Not member of group");
        return false;
    }

    Group* group = &groups[slot];

    // Build message
    // Header + encrypted(payload) + auth tag (16 bytes)
    uint8_t packet[sizeof(GroupMessageHeader) + length + 16];
    GroupMessageHeader* header = (GroupMessageHeader*)packet;

    header->groupId = groupId;
    header->sequence = group->txSequence++;
    header->sender = nodeAddress;
    header->payloadLen = length;
    header->msgType = 0;  // Normal message
    header->reserved = 0;

    // Encrypt payload
    uint8_t nonce[GROUP_NONCE_SIZE];
    memset(nonce, 0, GROUP_NONCE_SIZE);
    memcpy(nonce, &header->sequence, 4);
    nonce[4] = (groupId >> 24) & 0xFF;
    nonce[5] = (groupId >> 16) & 0xFF;
    nonce[6] = (groupId >> 8) & 0xFF;
    nonce[7] = groupId & 0xFF;

    uint8_t* ciphertext = packet + sizeof(GroupMessageHeader);
    uint8_t* tag = ciphertext + length;

    crypto_aead_lock(ciphertext, tag, group->key, nonce,
                     (uint8_t*)header, sizeof(GroupMessageHeader),
                     data, length);

    // Broadcast to all (group messages are broadcast)
    if (sendPacket) {
        sendPacket(0xFFFFFFFF, GROUP_MSG, packet, sizeof(GroupMessageHeader) + length + 16);
        stats.messagesSent++;
        group->lastActivity = millis();
        return true;
    }

    return false;
}

bool GroupManager::sendMessageByName(const char* groupName, const uint8_t* data, uint16_t length) {
    int slot = findGroupByName(groupName);
    if (slot < 0) return false;
    return sendMessage(groups[slot].groupId, data, length);
}

void GroupManager::handlePacket(uint32_t fromAddr, uint8_t type,
                                 const uint8_t* data, uint16_t length) {
    if (type != GROUP_MSG) return;  // Only handle messages for now

    if (length < sizeof(GroupMessageHeader) + 16) {
        return;  // Too short
    }

    const GroupMessageHeader* header = (const GroupMessageHeader*)data;

    // Check if we're member of this group
    int slot = findGroupById(header->groupId);
    if (slot < 0) {
        return;  // Not our group
    }

    // Ignore our own messages
    if (header->sender == nodeAddress) {
        return;
    }

    Group* group = &groups[slot];
    stats.messagesReceived++;

    // Check sequence (replay protection)
    if (header->sequence <= group->rxSequence && group->rxSequence > 0) {
        Serial.println("[GROUP] Replay attack detected");
        stats.replayRejected++;
        return;
    }

    // Decrypt
    uint8_t nonce[GROUP_NONCE_SIZE];
    memset(nonce, 0, GROUP_NONCE_SIZE);
    memcpy(nonce, &header->sequence, 4);
    nonce[4] = (header->groupId >> 24) & 0xFF;
    nonce[5] = (header->groupId >> 16) & 0xFF;
    nonce[6] = (header->groupId >> 8) & 0xFF;
    nonce[7] = header->groupId & 0xFF;

    const uint8_t* ciphertext = data + sizeof(GroupMessageHeader);
    const uint8_t* tag = ciphertext + header->payloadLen;
    uint8_t plaintext[header->payloadLen];

    int result = crypto_aead_unlock(plaintext, tag, group->key, nonce,
                                     (uint8_t*)header, sizeof(GroupMessageHeader),
                                     ciphertext, header->payloadLen);

    if (result != 0) {
        Serial.println("[GROUP] Decryption failed");
        stats.decryptionFailed++;
        return;
    }

    // Update sequence
    group->rxSequence = header->sequence;
    group->lastActivity = millis();
    stats.messagesDecrypted++;

    Serial.print("[GROUP] Message in '");
    Serial.print(group->name);
    Serial.print("' from 0x");
    Serial.print(header->sender, HEX);
    Serial.print(": ");
    // Print as text if printable
    for (uint16_t i = 0; i < header->payloadLen && i < 64; i++) {
        if (plaintext[i] >= 32 && plaintext[i] < 127) {
            Serial.print((char)plaintext[i]);
        }
    }
    Serial.println();

    // Callback
    if (onMessage) {
        onMessage(header->groupId, header->sender, plaintext, header->payloadLen);
    }
}

Group* GroupManager::getGroupById(uint32_t groupId) {
    int slot = findGroupById(groupId);
    if (slot < 0) return nullptr;
    return &groups[slot];
}

Group* GroupManager::getGroupByName(const char* name) {
    int slot = findGroupByName(name);
    if (slot < 0) return nullptr;
    return &groups[slot];
}

uint8_t GroupManager::getGroupCount() {
    uint8_t count = 0;
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].valid) count++;
    }
    return count;
}

Group* GroupManager::getGroupByIndex(uint8_t index) {
    uint8_t current = 0;
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].valid) {
            if (current == index) return &groups[i];
            current++;
        }
    }
    return nullptr;
}

uint32_t GroupManager::generateGroupId(const char* name, const uint8_t* key) {
    // Hash name + key to create group ID
    uint8_t input[GROUP_NAME_SIZE + GROUP_KEY_SIZE];
    memset(input, 0, sizeof(input));
    strncpy((char*)input, name, GROUP_NAME_SIZE);
    memcpy(input + GROUP_NAME_SIZE, key, GROUP_KEY_SIZE);

    uint8_t hash[32];
    crypto_blake2b(hash, 32, input, sizeof(input));

    // Use first 4 bytes as ID
    return (hash[0] << 24) | (hash[1] << 16) | (hash[2] << 8) | hash[3];
}

bool GroupManager::exportKey(uint32_t groupId, uint8_t* keyOut) {
    int slot = findGroupById(groupId);
    if (slot < 0) return false;

    memcpy(keyOut, groups[slot].key, GROUP_KEY_SIZE);
    return true;
}

void GroupManager::setSendFunction(bool (*sendFn)(uint32_t, uint8_t, const uint8_t*, uint16_t)) {
    sendPacket = sendFn;
}

void GroupManager::printStatus() {
    Serial.println("\n=== Group Channels ===");
    Serial.print("Groups: ");
    Serial.print(getGroupCount());
    Serial.print("/");
    Serial.println(MAX_GROUPS);

    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].valid) {
            Serial.print("\n  [");
            Serial.print(groups[i].name);
            Serial.print("] ID: 0x");
            Serial.println(groups[i].groupId, HEX);

            Serial.print("    Flags: ");
            if (groups[i].flags & GROUP_FLAG_ADMIN) Serial.print("ADMIN ");
            if (groups[i].flags & GROUP_FLAG_READONLY) Serial.print("RO ");
            if (groups[i].flags & GROUP_FLAG_HIDDEN) Serial.print("HIDDEN ");
            if (groups[i].flags == 0) Serial.print("member");
            Serial.println();

            Serial.print("    TX seq: ");
            Serial.print(groups[i].txSequence);
            Serial.print(", RX seq: ");
            Serial.println(groups[i].rxSequence);

            unsigned long age = (millis() - groups[i].lastActivity) / 1000;
            Serial.print("    Last activity: ");
            Serial.print(age);
            Serial.println("s ago");
        }
    }

    Serial.println("\nStatistics:");
    Serial.print("  Sent: ");
    Serial.println(stats.messagesSent);
    Serial.print("  Received: ");
    Serial.println(stats.messagesReceived);
    Serial.print("  Decrypted: ");
    Serial.println(stats.messagesDecrypted);
    Serial.print("  Decrypt failed: ");
    Serial.println(stats.decryptionFailed);
    Serial.print("  Replay rejected: ");
    Serial.println(stats.replayRejected);

    Serial.println("=====================\n");
}

bool GroupManager::save() {
#ifdef NRF52_SERIES
    File file(InternalFS);
    if (!file.open(GROUPS_FILE, FILE_O_WRITE)) {
        return false;
    }

    uint32_t magic = GROUPS_MAGIC;
    file.write((uint8_t*)&magic, 4);

    uint8_t count = getGroupCount();
    file.write(&count, 1);

    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].valid) {
            file.write((uint8_t*)&groups[i], sizeof(Group));
        }
    }

    file.close();
    Serial.print("[GROUP] Saved ");
    Serial.print(count);
    Serial.println(" groups");
    return true;
#else
    return false;
#endif
}

bool GroupManager::load() {
#ifdef NRF52_SERIES
    File file(InternalFS);
    if (!file.open(GROUPS_FILE, FILE_O_READ)) {
        return false;
    }

    uint32_t magic;
    file.read((uint8_t*)&magic, 4);
    if (magic != GROUPS_MAGIC) {
        file.close();
        return false;
    }

    uint8_t count;
    file.read(&count, 1);

    for (uint8_t i = 0; i < count && i < MAX_GROUPS; i++) {
        file.read((uint8_t*)&groups[i], sizeof(Group));
    }

    file.close();
    Serial.print("[GROUP] Loaded ");
    Serial.print(count);
    Serial.println(" groups");
    return true;
#else
    return false;
#endif
}

// Private methods

int GroupManager::findEmptySlot() {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (!groups[i].valid) return i;
    }
    return -1;
}

int GroupManager::findGroupById(uint32_t groupId) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].valid && groups[i].groupId == groupId) {
            return i;
        }
    }
    return -1;
}

int GroupManager::findGroupByName(const char* name) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].valid && strcmp(groups[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

bool GroupManager::encryptMessage(Group* group, const uint8_t* plaintext, uint16_t len,
                                   uint8_t* output, uint16_t* outLen) {
    // Not used - encryption inline in sendMessage
    return false;
}

bool GroupManager::decryptMessage(Group* group, const uint8_t* ciphertext, uint16_t len,
                                   uint8_t* output, uint16_t* outLen, uint32_t sequence) {
    // Not used - decryption inline in handlePacket
    return false;
}
