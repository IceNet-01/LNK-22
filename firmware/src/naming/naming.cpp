/**
 * @file naming.cpp
 * @brief LNK-22 Node Naming System Implementation
 */

#include "naming.h"
#include <string.h>
#include <ctype.h>

#ifdef NRF52_SERIES
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;
#endif

#ifdef ESP32
#include <Preferences.h>
#endif

// Global instance
NodeNaming nodeNaming;

// Static buffers
char NodeNaming::_nameBuffer[MAX_NAME_LENGTH + 1];
char NodeNaming::_hexBuffer[12];

// ============================================================================
// Constructor
// ============================================================================

NodeNaming::NodeNaming()
    : _localAddress(0)
    , _nodeCount(0)
    , _initialized(false)
{
    memset(_localName, 0, sizeof(_localName));
    memset(_nodes, 0, sizeof(_nodes));
}

// ============================================================================
// Initialization
// ============================================================================

bool NodeNaming::begin(uint32_t localAddress) {
    _localAddress = localAddress;
    _initialized = true;

    // Set default local name based on permanent hardware ID
    // Use last 4 hex digits of the device's unique hardware serial
    uint32_t hwSerial = getHardwareSerial();
    char defaultName[MAX_NAME_LENGTH + 1];
    snprintf(defaultName, sizeof(defaultName), "LNK-%04X",
             (unsigned int)(hwSerial & 0xFFFF));
    strncpy(_localName, defaultName, MAX_NAME_LENGTH);
    _localName[MAX_NAME_LENGTH] = '\0';

    Serial.print("[NAMING] Hardware serial: 0x");
    Serial.println(hwSerial, HEX);
    Serial.print("[NAMING] Default name: ");
    Serial.println(_localName);

    return true;
}

uint32_t NodeNaming::getHardwareSerial() {
#ifdef NRF52_SERIES
    // Get unique device ID from nRF52 FICR (Factory Information Configuration Registers)
    // DEVICEID is a 64-bit unique identifier, we use the lower 32 bits
    return NRF_FICR->DEVICEID[0];
#elif defined(ESP32)
    // Get ESP32 MAC address as serial
    uint64_t mac = ESP.getEfuseMac();
    return (uint32_t)(mac & 0xFFFFFFFF);
#else
    // Fallback to mesh address
    return _localAddress;
#endif
}

bool NodeNaming::loadFromStorage() {
    // Call this after filesystem is ready
    if (!_initialized) return false;

    if (load()) {
        Serial.print("[NAMING] Loaded name: ");
        Serial.println(_localName);
        Serial.print("[NAMING] Known nodes: ");
        Serial.println(_nodeCount);
        return true;
    }
    return false;
}

// ============================================================================
// Local Name Management
// ============================================================================

bool NodeNaming::setLocalName(const char* name) {
    if (!_initialized || name == nullptr) return false;

    if (!isValidName(name)) {
        Serial.println("[NAMING] Invalid name");
        return false;
    }

    strncpy(_localName, name, MAX_NAME_LENGTH);
    _localName[MAX_NAME_LENGTH] = '\0';

    // Trim whitespace
    char* end = _localName + strlen(_localName) - 1;
    while (end > _localName && isspace(*end)) {
        *end-- = '\0';
    }

    save();
    return true;
}

const char* NodeNaming::getLocalName() {
    return _localName;
}

uint32_t NodeNaming::getLocalAddress() {
    return _localAddress;
}

// ============================================================================
// Remote Node Name Management
// ============================================================================

bool NodeNaming::setNodeName(uint32_t address, const char* name) {
    if (!_initialized || name == nullptr) return false;
    if (address == 0 || address == 0xFFFFFFFF) return false;

    // Don't allow setting name for local address this way
    if (address == _localAddress) {
        return setLocalName(name);
    }

    if (!isValidName(name)) {
        Serial.println("[NAMING] Invalid name");
        return false;
    }

    // Check if name already exists for different address
    int existingName = findByName(name);
    if (existingName >= 0 && _nodes[existingName].address != address) {
        Serial.println("[NAMING] Name already in use");
        return false;
    }

    // Find existing entry or create new one
    int idx = findByAddress(address);
    if (idx < 0) {
        // Find empty slot
        for (int i = 0; i < MAX_NODE_NAMES; i++) {
            if (!_nodes[i].valid) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            Serial.println("[NAMING] Name table full");
            return false;
        }
        _nodeCount++;
    }

    _nodes[idx].address = address;
    strncpy(_nodes[idx].name, name, MAX_NAME_LENGTH);
    _nodes[idx].name[MAX_NAME_LENGTH] = '\0';
    _nodes[idx].lastSeen = millis() / 1000;
    _nodes[idx].valid = true;

    save();
    return true;
}

bool NodeNaming::getNodeName(uint32_t address, char* buffer) {
    if (buffer == nullptr) return false;

    // Check local address
    if (address == _localAddress) {
        strcpy(buffer, _localName);
        return true;
    }

    // Search remote nodes
    int idx = findByAddress(address);
    if (idx >= 0) {
        strcpy(buffer, _nodes[idx].name);
        return true;
    }

    // Not found, return hex format
    formatAddress(address, buffer);
    return false;
}

const char* NodeNaming::getNodeName(uint32_t address) {
    // Check local address
    if (address == _localAddress) {
        return _localName;
    }

    // Search remote nodes
    int idx = findByAddress(address);
    if (idx >= 0) {
        return _nodes[idx].name;
    }

    // Not found, return hex format
    formatAddress(address, _hexBuffer);
    return _hexBuffer;
}

uint32_t NodeNaming::resolveAddress(const char* name) {
    if (name == nullptr || strlen(name) == 0) return 0;

    // Check if it's a hex address (0x prefix)
    if (strlen(name) > 2 && name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
        return strtoul(name, nullptr, 16);
    }

    // Check local name (case-insensitive)
    if (strcasecmp(name, _localName) == 0) {
        return _localAddress;
    }

    // Search remote nodes
    int idx = findByName(name);
    if (idx >= 0) {
        return _nodes[idx].address;
    }

    return 0;
}

bool NodeNaming::removeNodeName(uint32_t address) {
    int idx = findByAddress(address);
    if (idx < 0) return false;

    _nodes[idx].valid = false;
    memset(&_nodes[idx], 0, sizeof(NodeNameEntry));
    _nodeCount--;

    save();
    return true;
}

int NodeNaming::getNodeCount() {
    return _nodeCount;
}

bool NodeNaming::getNodeByIndex(int index, NodeNameEntry* entry) {
    if (entry == nullptr || index < 0) return false;

    int count = 0;
    for (int i = 0; i < MAX_NODE_NAMES; i++) {
        if (_nodes[i].valid) {
            if (count == index) {
                *entry = _nodes[i];
                return true;
            }
            count++;
        }
    }
    return false;
}

void NodeNaming::updateLastSeen(uint32_t address) {
    int idx = findByAddress(address);
    if (idx >= 0) {
        _nodes[idx].lastSeen = millis() / 1000;
    }
}

// ============================================================================
// Storage
// ============================================================================

bool NodeNaming::save() {
#ifdef NRF52_SERIES
    // Ensure filesystem is initialized
    if (!InternalFS.begin()) {
        Serial.println("[NAMING] Filesystem not ready");
        return false;
    }

    File file(InternalFS);

    if (!file.open(NAMES_FILE, FILE_O_WRITE)) {
        // Try to create
        file.open(NAMES_FILE, FILE_O_WRITE);
    }

    if (!file) {
        Serial.println("[NAMING] Failed to open file for writing");
        return false;
    }

    // Write header
    uint32_t magic = NAMES_MAGIC;
    uint32_t version = NAMES_VERSION;
    file.write((uint8_t*)&magic, 4);
    file.write((uint8_t*)&version, 4);

    // Write local info
    file.write((uint8_t*)&_localAddress, 4);
    file.write((uint8_t*)_localName, MAX_NAME_LENGTH + 1);

    // Write node count
    file.write((uint8_t*)&_nodeCount, 4);

    // Write each valid node
    for (int i = 0; i < MAX_NODE_NAMES; i++) {
        if (_nodes[i].valid) {
            file.write((uint8_t*)&_nodes[i].address, 4);
            file.write((uint8_t*)_nodes[i].name, MAX_NAME_LENGTH + 1);
            file.write((uint8_t*)&_nodes[i].lastSeen, 4);
        }
    }

    file.close();
    Serial.println("[NAMING] Saved to flash");
    return true;

#elif defined(ESP32)
    Preferences prefs;
    prefs.begin("lnk22names", false);

    prefs.putUInt("magic", NAMES_MAGIC);
    prefs.putUInt("version", NAMES_VERSION);
    prefs.putUInt("localAddr", _localAddress);
    prefs.putString("localName", _localName);
    prefs.putInt("nodeCount", _nodeCount);

    // Save each valid node
    int savedCount = 0;
    for (int i = 0; i < MAX_NODE_NAMES && savedCount < _nodeCount; i++) {
        if (_nodes[i].valid) {
            char key[16];
            snprintf(key, sizeof(key), "addr%d", savedCount);
            prefs.putUInt(key, _nodes[i].address);
            snprintf(key, sizeof(key), "name%d", savedCount);
            prefs.putString(key, _nodes[i].name);
            snprintf(key, sizeof(key), "seen%d", savedCount);
            prefs.putUInt(key, _nodes[i].lastSeen);
            savedCount++;
        }
    }

    prefs.end();
    Serial.println("[NAMING] Saved to NVS");
    return true;

#else
    Serial.println("[NAMING] No storage backend");
    return false;
#endif
}

bool NodeNaming::load() {
#ifdef NRF52_SERIES
    // Ensure filesystem is initialized
    if (!InternalFS.begin()) {
        Serial.println("[NAMING] Filesystem not ready for load");
        return false;
    }

    File file(InternalFS);

    if (!file.open(NAMES_FILE, FILE_O_READ)) {
        Serial.println("[NAMING] No saved names found");
        return false;
    }

    // Read and verify header
    uint32_t magic, version;
    file.read((uint8_t*)&magic, 4);
    file.read((uint8_t*)&version, 4);

    if (magic != NAMES_MAGIC) {
        Serial.println("[NAMING] Invalid file magic");
        file.close();
        return false;
    }

    if (version != NAMES_VERSION) {
        Serial.println("[NAMING] Version mismatch");
        file.close();
        return false;
    }

    // Read local info
    uint32_t savedAddr;
    file.read((uint8_t*)&savedAddr, 4);
    file.read((uint8_t*)_localName, MAX_NAME_LENGTH + 1);
    _localName[MAX_NAME_LENGTH] = '\0';

    // Read node count
    file.read((uint8_t*)&_nodeCount, 4);
    if (_nodeCount > MAX_NODE_NAMES) {
        _nodeCount = MAX_NODE_NAMES;
    }

    // Read each node
    for (int i = 0; i < _nodeCount; i++) {
        file.read((uint8_t*)&_nodes[i].address, 4);
        file.read((uint8_t*)_nodes[i].name, MAX_NAME_LENGTH + 1);
        _nodes[i].name[MAX_NAME_LENGTH] = '\0';
        file.read((uint8_t*)&_nodes[i].lastSeen, 4);
        _nodes[i].valid = true;
    }

    file.close();
    return true;

#elif defined(ESP32)
    Preferences prefs;
    prefs.begin("lnk22names", true);  // Read-only

    uint32_t magic = prefs.getUInt("magic", 0);
    if (magic != NAMES_MAGIC) {
        prefs.end();
        return false;
    }

    String localName = prefs.getString("localName", "");
    if (localName.length() > 0) {
        strncpy(_localName, localName.c_str(), MAX_NAME_LENGTH);
        _localName[MAX_NAME_LENGTH] = '\0';
    }

    _nodeCount = prefs.getInt("nodeCount", 0);
    if (_nodeCount > MAX_NODE_NAMES) {
        _nodeCount = MAX_NODE_NAMES;
    }

    for (int i = 0; i < _nodeCount; i++) {
        char key[16];
        snprintf(key, sizeof(key), "addr%d", i);
        _nodes[i].address = prefs.getUInt(key, 0);
        snprintf(key, sizeof(key), "name%d", i);
        String name = prefs.getString(key, "");
        strncpy(_nodes[i].name, name.c_str(), MAX_NAME_LENGTH);
        _nodes[i].name[MAX_NAME_LENGTH] = '\0';
        snprintf(key, sizeof(key), "seen%d", i);
        _nodes[i].lastSeen = prefs.getUInt(key, 0);
        _nodes[i].valid = (_nodes[i].address != 0);
    }

    prefs.end();
    return true;

#else
    return false;
#endif
}

// ============================================================================
// Utility Functions
// ============================================================================

bool NodeNaming::isValidName(const char* name) {
    if (name == nullptr) return false;

    size_t len = strlen(name);
    if (len == 0 || len > MAX_NAME_LENGTH) return false;

    // No leading/trailing spaces
    if (isspace(name[0]) || isspace(name[len - 1])) return false;

    // Must contain at least one alphanumeric character
    bool hasAlnum = false;
    for (size_t i = 0; i < len; i++) {
        if (isalnum(name[i])) {
            hasAlnum = true;
        }
        // Allow alphanumeric, spaces, hyphens, underscores
        if (!isalnum(name[i]) && name[i] != ' ' && name[i] != '-' && name[i] != '_') {
            return false;
        }
    }

    return hasAlnum;
}

void NodeNaming::formatAddress(uint32_t address, char* buffer) {
    snprintf(buffer, 12, "0x%08X", (unsigned int)address);
}

int NodeNaming::findByAddress(uint32_t address) {
    for (int i = 0; i < MAX_NODE_NAMES; i++) {
        if (_nodes[i].valid && _nodes[i].address == address) {
            return i;
        }
    }
    return -1;
}

int NodeNaming::findByName(const char* name) {
    for (int i = 0; i < MAX_NODE_NAMES; i++) {
        if (_nodes[i].valid && strcasecmp(_nodes[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}
