/**
 * @file naming.h
 * @brief LNK-22 Node Naming System
 *
 * Human-friendly naming system for mesh network nodes.
 * Maps simple names (up to 16 characters) to unique 32-bit node addresses.
 */

#ifndef NAMING_H
#define NAMING_H

#include <Arduino.h>

// Configuration
#define MAX_NODE_NAMES 64
#define MAX_NAME_LENGTH 16

// Storage file
#define NAMES_FILE "/lnk22_names.dat"
#define NAMES_MAGIC 0x324D4E00  // "NM2\0"
#define NAMES_VERSION 1

/**
 * @brief Node name entry structure
 */
struct NodeNameEntry {
    uint32_t address;           // 32-bit node address
    char name[MAX_NAME_LENGTH + 1];  // Name (16 chars + null)
    uint32_t lastSeen;          // Last seen timestamp (for remote nodes)
    bool valid;                 // Entry is valid
};

/**
 * @brief Node naming system class
 */
class NodeNaming {
public:
    NodeNaming();

    /**
     * @brief Initialize the naming system
     * @param localAddress This node's address
     * @return true if successful
     */
    bool begin(uint32_t localAddress);

    /**
     * @brief Load names from storage (call after filesystem ready)
     * @return true if names were loaded
     */
    bool loadFromStorage();

    /**
     * @brief Set this node's name
     * @param name Name to set (max 16 chars)
     * @return true if successful
     */
    bool setLocalName(const char* name);

    /**
     * @brief Get this node's name
     * @return Name string (never null)
     */
    const char* getLocalName();

    /**
     * @brief Get this node's address
     * @return 32-bit address
     */
    uint32_t getLocalAddress();

    /**
     * @brief Set/update name for a remote node
     * @param address Node address
     * @param name Name to assign
     * @return true if successful
     */
    bool setNodeName(uint32_t address, const char* name);

    /**
     * @brief Get name for an address
     * @param address Node address
     * @param buffer Buffer to store name (must be at least 17 bytes)
     * @return true if name found, false if returning hex format
     */
    bool getNodeName(uint32_t address, char* buffer);

    /**
     * @brief Get name for an address (static buffer version)
     * @param address Node address
     * @return Name string or hex format "0xXXXXXXXX"
     */
    const char* getNodeName(uint32_t address);

    /**
     * @brief Resolve a name to an address
     * @param name Name to look up
     * @return Address or 0 if not found
     */
    uint32_t resolveAddress(const char* name);

    /**
     * @brief Remove a node name
     * @param address Node address
     * @return true if removed
     */
    bool removeNodeName(uint32_t address);

    /**
     * @brief Get count of named nodes (excluding local)
     * @return Number of remote nodes with names
     */
    int getNodeCount();

    /**
     * @brief Get node entry by index
     * @param index Index (0 to getNodeCount()-1)
     * @param entry Output entry
     * @return true if valid index
     */
    bool getNodeByIndex(int index, NodeNameEntry* entry);

    /**
     * @brief Update last seen timestamp for a node
     * @param address Node address
     */
    void updateLastSeen(uint32_t address);

    /**
     * @brief Save names to flash storage
     * @return true if successful
     */
    bool save();

    /**
     * @brief Load names from flash storage
     * @return true if successful
     */
    bool load();

    /**
     * @brief Check if a name is valid (alphanumeric, no spaces at start/end)
     * @param name Name to validate
     * @return true if valid
     */
    static bool isValidName(const char* name);

    /**
     * @brief Format address as hex string
     * @param address Address to format
     * @param buffer Output buffer (must be at least 11 bytes)
     */
    static void formatAddress(uint32_t address, char* buffer);

    /**
     * @brief Get unique hardware serial number
     * @return Permanent device ID (from FICR on nRF52, MAC on ESP32)
     */
    static uint32_t getHardwareSerial();

private:
    uint32_t _localAddress;
    char _localName[MAX_NAME_LENGTH + 1];
    NodeNameEntry _nodes[MAX_NODE_NAMES];
    int _nodeCount;
    bool _initialized;

    // Static buffer for getNodeName(address) convenience method
    static char _nameBuffer[MAX_NAME_LENGTH + 1];
    static char _hexBuffer[12];  // "0xXXXXXXXX\0"

    /**
     * @brief Find entry by address
     * @param address Address to find
     * @return Index or -1 if not found
     */
    int findByAddress(uint32_t address);

    /**
     * @brief Find entry by name (case-insensitive)
     * @param name Name to find
     * @return Index or -1 if not found
     */
    int findByName(const char* name);
};

// Global instance
extern NodeNaming nodeNaming;

#endif // NAMING_H
