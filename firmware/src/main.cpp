/**
 * LNK-22 Firmware
 * Professional LoRa mesh networking device
 * Main entry point for the mesh networking device
 *
 * Copyright (c) 2024-2025 LNK-22 Project
 * License: MIT
 */

#include <Arduino.h>
#include "config.h"
#include "protocol/protocol.h"
#include "radio/radio.h"
#include "mesh/mesh.h"
#include "crypto/crypto.h"

#ifdef HAS_GPS
#include "gps/gps.h"
#endif

#ifdef HAS_DISPLAY
#include "display/display.h"
#endif

#include "storage/storage.h"
#include "ble/ble_service.h"
#include "ble/ble_relay.h"
#include "naming/naming.h"

// New feature modules
#if FEATURE_STORE_FORWARD
#include "store_forward/store_forward.h"
#endif
#if FEATURE_ADR
#include "adr/adaptive_datarate.h"
#endif
#if FEATURE_EMERGENCY
#include "emergency/emergency.h"
#endif
#if FEATURE_HISTORY
#include "history/message_history.h"
#endif
#if FEATURE_LINKS
#include "link/link.h"
#endif
#if FEATURE_DTN
#include "dtn/dtn.h"
#endif
#if FEATURE_GEOROUTING
#include "georouting/georouting.h"
#endif
#if FEATURE_GROUPS
#include "groups/groups.h"
#endif
#if FEATURE_HYBRID_MAC
#include "mac/mac_hybrid.h"
#endif

// Global instances
Radio radio;
Mesh mesh;
Crypto crypto;
Storage storage;

#ifdef HAS_GPS
GPS gps;
#endif

#ifdef HAS_DISPLAY
Display display;
bool displayAvailable = false;
#endif

// Forward declarations
void handleSerialCommand();
void handleNameCommand(String& cmd);
void printStatus();
void printHelp();
void updateBLEStatus();
void onBLEMessage(uint8_t type, uint32_t destination, uint8_t channel, const uint8_t* payload, size_t length);
void onBLECommand(uint8_t command, const uint8_t* params, size_t length);
#ifdef HAS_DISPLAY
void updateDisplay();
#endif

// Device state
uint32_t nodeAddress = 0;
unsigned long lastBeacon = 0;
unsigned long lastDisplay = 0;
unsigned long lastPositionBroadcast = 0;
unsigned long lastBLEUpdate = 0;

// Button handling for display page switching
#define BUTTON_PIN 9  // PIN_BUTTON1 on RAK4631 (WisMesh Pocket v2)
volatile bool buttonPressed = false;
unsigned long lastButtonPress = 0;
#define BUTTON_DEBOUNCE_MS 200

void buttonISR() {
    buttonPressed = true;
}

void setup() {
    // Initialize LED for status indication
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // Show we're booting

    // Initialize button for display page switching
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);

    // Initialize serial for debugging
    Serial.begin(115200);
    delay(3000); // Wait longer for USB serial to be ready

    // Flush any garbage
    Serial.flush();

    Serial.println("\n\n========================================");
    Serial.print("LNK-22 Firmware v");
    Serial.println(LNK22_VERSION);
    Serial.print("Board: ");
    Serial.println(BOARD_NAME);
    Serial.println("Professional LoRa Mesh Networking");
    Serial.println("========================================\n");
    Serial.flush();

    // Initialize crypto subsystem
    Serial.println("[CRYPTO] Initializing cryptography...");
    crypto.begin();

    // Generate or load node identity
    nodeAddress = crypto.getNodeAddress();
    Serial.print("[CRYPTO] Node Address: 0x");
    Serial.println(nodeAddress, HEX);

    // Initialize naming system
    Serial.println("[NAMING] Initializing node naming...");
    nodeNaming.begin(nodeAddress);
    Serial.print("[NAMING] This node: ");
    Serial.print(nodeNaming.getLocalName());
    Serial.print(" (0x");
    Serial.print(nodeAddress, HEX);
    Serial.println(")");

    // Initialize radio with timeout protection
    Serial.println("[RADIO] Initializing LoRa radio...");
    Serial.flush();

    bool radioOk = false;
    unsigned long radioStart = millis();

    // Try to initialize radio with timeout
    radioOk = radio.begin();

    if (!radioOk || (millis() - radioStart > 5000)) {
        Serial.println("[ERROR] Radio initialization failed or timed out!");
        Serial.println("[ERROR] System will continue in degraded mode");
        Serial.flush();
        // Blink LED rapidly to show error
        for (int i = 0; i < 10; i++) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(100);
        }
        // Don't hang - continue without radio
    } else {
        Serial.println("[RADIO] Radio initialized successfully");
        Serial.flush();
    }

    // Initialize mesh network
    Serial.println("[MESH] Initializing mesh network...");
    mesh.begin(nodeAddress, &radio, &crypto);
    Serial.println("[MESH] Mesh network initialized");

    #ifdef HAS_GPS
    // Initialize GPS (optional)
    Serial.println("[GPS] Initializing GPS module...");
    Serial.flush();
    if (gps.begin()) {
        Serial.println("[GPS] GPS initialized successfully");
    } else {
        Serial.println("[GPS] GPS initialization failed (optional feature)");
    }
    Serial.flush();
    #endif

    #ifdef HAS_DISPLAY
    // Initialize display (optional)
    Serial.println("[DISPLAY] Initializing OLED display...");
    Serial.flush();
    displayAvailable = display.begin();
    if (displayAvailable) {
        Serial.println("[DISPLAY] Display initialized successfully");
    } else {
        Serial.println("[DISPLAY] Display not detected (optional feature)");
    }
    Serial.flush();
    #endif

    // Initialize BLE service for iPhone app
    Serial.println("[BLE] Initializing Bluetooth LE service...");
    Serial.flush();
    // Use node name for BLE advertising
    if (bleService.begin(nodeNaming.getLocalName())) {
        Serial.println("[BLE] BLE service initialized successfully");
        // Register BLE callbacks
        bleService.onMessage(onBLEMessage);
        bleService.onCommand(onBLECommand);
        bleService.startAdvertising();
        Serial.print("[BLE] Advertising as: ");
        Serial.println(nodeNaming.getLocalName());

        // Initialize BLE mesh relay
        Serial.println("[BLE-RELAY] Initializing BLE mesh relay...");
        if (bleRelay.begin(nodeAddress)) {
            Serial.println("[BLE-RELAY] BLE mesh relay initialized");
        } else {
            Serial.println("[BLE-RELAY] BLE mesh relay init failed");
        }
    } else {
        Serial.println("[BLE] BLE initialization failed");
    }
    Serial.flush();

    // Now that filesystem should be ready, load stored names
    Serial.println("[NAMING] Loading stored names...");
    nodeNaming.loadFromStorage();

    // Initialize new feature modules
#if FEATURE_STORE_FORWARD
    Serial.println("[SF] Initializing Store-and-Forward...");
    storeForward.begin();
#endif

#if FEATURE_ADR
    Serial.println("[ADR] Initializing Adaptive Data Rate...");
    adaptiveDataRate.begin();
#endif

#if FEATURE_EMERGENCY
    Serial.println("[SOS] Initializing Emergency SOS system...");
    emergency.begin();
#endif

#if FEATURE_HISTORY
    Serial.println("[HISTORY] Initializing Message History...");
    messageHistory.begin();
#endif

#if FEATURE_LINKS
    Serial.println("[LINK] Initializing Secure Links...");
    linkManager.begin(nodeAddress, &crypto);
#endif

#if FEATURE_DTN
    Serial.println("[DTN] Initializing Delay-Tolerant Networking...");
    dtnManager.begin(nodeAddress);
#endif

#if FEATURE_GEOROUTING
    Serial.println("[GEO] Initializing Geographic Routing...");
    geoRouting.begin(nodeAddress);
#endif

#if FEATURE_GROUPS
    Serial.println("[GROUP] Initializing Group Channels...");
    groupManager.begin(nodeAddress);
#endif

#if FEATURE_HYBRID_MAC
    Serial.println("[MAC] Initializing Hybrid TDMA/CSMA-CA...");
    hybridMAC.begin(nodeAddress);
#endif

    // Send initial beacon if radio is working
    if (radioOk) {
        mesh.sendBeacon();
    }

    digitalWrite(LED_BUILTIN, LOW); // Boot complete

    Serial.println("\n[SYSTEM] Boot complete. Ready to mesh!\n");
    Serial.println("Type 'help' for available commands");
    Serial.flush();
}

void loop() {
    unsigned long now = millis();

    // Handle button press for display page switching
    #ifdef HAS_DISPLAY
    if (buttonPressed && (now - lastButtonPress > BUTTON_DEBOUNCE_MS)) {
        buttonPressed = false;
        lastButtonPress = now;
        if (displayAvailable) {
            display.nextPage();
            Serial.print("[DISPLAY] Page: ");
            Serial.print(display.getCurrentPage() + 1);
            Serial.println("/7");
        }
    }
    #endif

    // Process incoming packets
    radio.update();

    // Process mesh network
    mesh.update();

    #ifdef HAS_GPS
    // Update GPS
    gps.update();

    // Broadcast position every 60 seconds (if we have a fix)
    if (now - lastPositionBroadcast > 60000 && gps.hasFix()) {
        GPSPosition pos;
        if (gps.getPosition(&pos)) {
            // Convert to protocol format
            PositionMessage posMsg;
            posMsg.latitude = (int32_t)(pos.latitude * 10000000.0);
            posMsg.longitude = (int32_t)(pos.longitude * 10000000.0);
            posMsg.altitude = (int32_t)(pos.altitude * 100.0);
            posMsg.satellites = pos.satellites;
            posMsg.fix_type = pos.fixType;
            posMsg.heading = 0;  // Not implemented yet
            posMsg.speed = 0;    // Not implemented yet
            posMsg.timestamp = pos.timestamp;

            // Broadcast position to all nodes
            mesh.sendPosition(0xFFFFFFFF, &posMsg, false);

            lastPositionBroadcast = now;
        }
    }
    #endif

    // Handle serial commands
    if (Serial.available()) {
        handleSerialCommand();
    }

    // Send periodic beacon
    if (now - lastBeacon > BEACON_INTERVAL) {
        mesh.sendBeacon();
        lastBeacon = now;
    }

    // Update display (if available)
    #ifdef HAS_DISPLAY
    if (now - lastDisplay > DISPLAY_UPDATE_INTERVAL) {
        updateDisplay();
        lastDisplay = now;
    }
    #endif

    // Update BLE service and send status to connected app
    bleService.update();
    bleRelay.update();
    if (now - lastBLEUpdate > 2000) {  // Update BLE status every 2 seconds
        updateBLEStatus();
        lastBLEUpdate = now;
    }

    // Update new feature modules
#if FEATURE_STORE_FORWARD
    storeForward.update();
#endif

#if FEATURE_ADR
    adaptiveDataRate.updateScan();
#endif

#if FEATURE_EMERGENCY
    emergency.update();
#endif

#if FEATURE_LINKS
    linkManager.update();
#endif

#if FEATURE_DTN
    dtnManager.update();
#endif

#if FEATURE_GEOROUTING
    geoRouting.update();
    // Update georouting with GPS position
    #ifdef HAS_GPS
    if (gps.hasFix()) {
        GPSPosition pos;
        if (gps.getPosition(&pos)) {
            geoRouting.setPosition(
                (int32_t)(pos.latitude * 10000000.0),
                (int32_t)(pos.longitude * 10000000.0),
                (int16_t)pos.altitude,
                0, 0, pos.satellites
            );
        }
    }
    #endif
#endif

#if FEATURE_HYBRID_MAC
    hybridMAC.update();
    // Feed GPS time to MAC layer for TDMA sync
    #ifdef HAS_GPS
    if (gps.hasFix()) {
        GPSPosition pos;
        if (gps.getPosition(&pos)) {
            // Use GPS timestamp for time sync
            hybridMAC.setTimeSource(TIME_SOURCE_GPS, pos.timestamp, 0);
        }
    }
    #endif
#endif

    // Yield to system tasks
    delay(10);
}

void handleSerialCommand() {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.startsWith("send ")) {
        // Format: send <dest_name_or_addr> <message>
        int spaceIdx = cmd.indexOf(' ', 5);
        if (spaceIdx > 0) {
            String destStr = cmd.substring(5, spaceIdx);
            String message = cmd.substring(spaceIdx + 1);

            // Resolve name or hex address
            uint32_t dest = nodeNaming.resolveAddress(destStr.c_str());
            if (dest == 0) {
                Serial.print("Unknown destination: ");
                Serial.println(destStr);
                return;
            }

            Serial.print("Sending to ");
            Serial.print(nodeNaming.getNodeName(dest));
            Serial.print(" (0x");
            Serial.print(dest, HEX);
            Serial.print("): ");
            Serial.println(message);

            mesh.sendMessage(dest, (uint8_t*)message.c_str(), message.length());
        }
    }
    else if (cmd.startsWith("name")) {
        handleNameCommand(cmd);
    }
    else if (cmd == "status") {
        printStatus();
    }
    else if (cmd == "routes") {
        mesh.printRoutes();
    }
    else if (cmd == "neighbors") {
        mesh.printNeighbors();
    }
    else if (cmd == "beacon") {
        Serial.println("[CMD] Sending beacon now...");
        mesh.sendBeacon();
        Serial.println("[CMD] Beacon sent!");
    }
    else if (cmd.startsWith("channel ")) {
        uint8_t ch = cmd.substring(8).toInt();
        mesh.setChannel(ch);
    }
    else if (cmd == "radio") {
        Serial.println("\n=== Radio Status ===");
        Serial.print("Frequency: 915 MHz, SF");
        Serial.print(LORA_SPREADING_FACTOR);
        Serial.print(", BW");
        Serial.print(LORA_BANDWIDTH / 1000);
        Serial.println(" kHz");
        Serial.print("TX Power: ");
        Serial.print(LORA_TX_POWER);
        Serial.println(" dBm");
        Serial.print("Sync Word: 0x");
        Serial.println(LORA_SYNC_WORD, HEX);
        Serial.println("===================\n");
    }
    else if (cmd == "help") {
        printHelp();
    }
#if FEATURE_STORE_FORWARD
    else if (cmd == "queue") {
        storeForward.printStatus();
    }
    else if (cmd == "queue clear") {
        storeForward.clear();
    }
#endif
#if FEATURE_ADR
    else if (cmd == "adr") {
        adaptiveDataRate.printStatus();
    }
    else if (cmd == "adr on") {
        adaptiveDataRate.setEnabled(true);
        Serial.println("[ADR] Adaptive Data Rate ENABLED");
    }
    else if (cmd == "adr off") {
        adaptiveDataRate.setEnabled(false);
        Serial.println("[ADR] Adaptive Data Rate DISABLED");
    }
    else if (cmd == "adr scan") {
        Serial.println("[ADR] Starting network SF scan...");
        adaptiveDataRate.startSFScan(5000);
    }
#endif
#if FEATURE_EMERGENCY
    else if (cmd == "sos") {
        emergency.activateSOS(EMERGENCY_GENERAL);
    }
    else if (cmd.startsWith("sos ")) {
        String type = cmd.substring(4);
        type.trim();
        EmergencyType eType = EMERGENCY_GENERAL;
        if (type == "medical") eType = EMERGENCY_MEDICAL;
        else if (type == "fire") eType = EMERGENCY_FIRE;
        else if (type == "rescue") eType = EMERGENCY_RESCUE;
        else if (type == "test") eType = EMERGENCY_TEST;
        else if (type == "cancel") {
            emergency.cancelSOS();
            Serial.println("[SOS] Emergency cancelled");
        }
        if (type != "cancel") {
            emergency.activateSOS(eType);
        }
    }
    else if (cmd == "sos status") {
        emergency.printStatus();
    }
#endif
#if FEATURE_HISTORY
    else if (cmd == "history") {
        messageHistory.print(10);
    }
    else if (cmd.startsWith("history ")) {
        int count = cmd.substring(8).toInt();
        if (count > 0) {
            messageHistory.print(count);
        }
    }
    else if (cmd == "history clear") {
        messageHistory.clear();
    }
    else if (cmd == "history save") {
        if (messageHistory.save()) {
            Serial.println("[HISTORY] Saved to flash");
        } else {
            Serial.println("[HISTORY] Save failed");
        }
    }
#endif
#if FEATURE_LINKS
    else if (cmd == "link") {
        linkManager.printStatus();
    }
    else if (cmd.startsWith("link ")) {
        String target = cmd.substring(5);
        target.trim();
        uint32_t addr = nodeNaming.resolveAddress(target.c_str());
        if (addr != 0) {
            Serial.print("[LINK] Requesting link to ");
            Serial.println(target);
            linkManager.requestLink(addr);
        } else {
            Serial.println("Unknown node. Use address or name.");
        }
    }
    else if (cmd.startsWith("link close ")) {
        String target = cmd.substring(11);
        target.trim();
        uint32_t addr = nodeNaming.resolveAddress(target.c_str());
        if (addr != 0) {
            linkManager.closeLink(addr);
        }
    }
#endif
#if FEATURE_DTN
    else if (cmd == "dtn") {
        dtnManager.printStatus();
    }
    else if (cmd == "dtn clear") {
        dtnManager.clear();
    }
    else if (cmd.startsWith("dtn send ")) {
        // dtn send <dest> <message>
        int spaceIdx = cmd.indexOf(' ', 9);
        if (spaceIdx > 0) {
            String destStr = cmd.substring(9, spaceIdx);
            String message = cmd.substring(spaceIdx + 1);
            uint32_t dest = nodeNaming.resolveAddress(destStr.c_str());
            if (dest != 0) {
                uint32_t id = dtnManager.createBundle(dest, (uint8_t*)message.c_str(),
                                                       message.length(), BUNDLE_NORMAL);
                Serial.print("[DTN] Created bundle ");
                Serial.println(id);
            }
        }
    }
#endif
#if FEATURE_GEOROUTING
    else if (cmd == "geo") {
        geoRouting.printStatus();
    }
    else if (cmd == "geo on") {
        geoRouting.setMode(GEO_MODE_GREEDY);
        Serial.println("[GEO] Geographic routing ENABLED (greedy)");
    }
    else if (cmd == "geo off") {
        geoRouting.setMode(GEO_MODE_DISABLED);
        Serial.println("[GEO] Geographic routing DISABLED");
    }
    else if (cmd == "geo gpsr") {
        geoRouting.setMode(GEO_MODE_GPSR);
        Serial.println("[GEO] GPSR mode enabled");
    }
#endif
#if FEATURE_GROUPS
    else if (cmd == "group" || cmd == "groups") {
        groupManager.printStatus();
    }
    else if (cmd.startsWith("group create ")) {
        String name = cmd.substring(13);
        name.trim();
        uint32_t gid = groupManager.createGroup(name.c_str());
        if (gid) {
            Serial.print("[GROUP] Created group '");
            Serial.print(name);
            Serial.println("'");
            // Show key for sharing
            uint8_t key[32];
            if (groupManager.exportKey(gid, key)) {
                Serial.print("[GROUP] Key: ");
                for (int i = 0; i < 32; i++) {
                    if (key[i] < 16) Serial.print("0");
                    Serial.print(key[i], HEX);
                }
                Serial.println();
            }
        }
    }
    else if (cmd.startsWith("group join ")) {
        // Format: group join <name> <hex_key>
        String rest = cmd.substring(11);
        int spaceIdx = rest.indexOf(' ');
        if (spaceIdx > 0) {
            String name = rest.substring(0, spaceIdx);
            String keyStr = rest.substring(spaceIdx + 1);
            keyStr.trim();
            // Parse hex key
            uint8_t key[32];
            if (keyStr.length() >= 64) {
                for (int i = 0; i < 32; i++) {
                    char hex[3] = {keyStr[i*2], keyStr[i*2+1], 0};
                    key[i] = strtoul(hex, NULL, 16);
                }
                groupManager.joinGroup(name.c_str(), key);
            } else {
                Serial.println("Key must be 64 hex characters");
            }
        } else {
            Serial.println("Usage: group join <name> <64-char-hex-key>");
        }
    }
    else if (cmd.startsWith("group leave ")) {
        String name = cmd.substring(12);
        name.trim();
        Group* g = groupManager.getGroupByName(name.c_str());
        if (g) {
            groupManager.leaveGroup(g->groupId);
        } else {
            Serial.println("Group not found");
        }
    }
    else if (cmd.startsWith("group send ")) {
        // Format: group send <name> <message>
        int spaceIdx = cmd.indexOf(' ', 11);
        if (spaceIdx > 0) {
            String name = cmd.substring(11, spaceIdx);
            String message = cmd.substring(spaceIdx + 1);
            if (groupManager.sendMessageByName(name.c_str(), (uint8_t*)message.c_str(), message.length())) {
                Serial.print("[GROUP] Sent to '");
                Serial.print(name);
                Serial.println("'");
            } else {
                Serial.println("Failed to send - not member of group");
            }
        }
    }
#endif
    // BLE Relay commands (always available)
    else if (cmd == "relay") {
        bleRelay.printStatus();
    }
    else if (cmd == "relay on") {
        bleRelay.setBLERelayEnabled(true);
        Serial.println("[BLE-RELAY] BLE-to-BLE relay ENABLED");
    }
    else if (cmd == "relay off") {
        bleRelay.setBLERelayEnabled(false);
        Serial.println("[BLE-RELAY] BLE-to-BLE relay DISABLED");
    }
#if FEATURE_HYBRID_MAC
    // Hybrid MAC commands
    else if (cmd == "mac") {
        hybridMAC.printStatus();
    }
    else if (cmd == "mac on") {
        hybridMAC.setTDMAEnabled(true);
        Serial.println("[MAC] TDMA mode ENABLED");
    }
    else if (cmd == "mac off") {
        hybridMAC.setTDMAEnabled(false);
        Serial.println("[MAC] TDMA mode DISABLED (CSMA-only)");
    }
    else if (cmd == "mac sync") {
        Serial.println("[MAC] Broadcasting time sync...");
        hybridMAC.broadcastTimeSync();
    }
    // Time sync from serial - format: time <unix_timestamp>
    else if (cmd.startsWith("time ")) {
        String timeStr = cmd.substring(5);
        timeStr.trim();
        uint32_t timestamp = strtoul(timeStr.c_str(), NULL, 10);
        if (timestamp > 1700000000) {  // Sanity check: after 2023
            hybridMAC.setTimeSource(TIME_SOURCE_SERIAL, timestamp, 0);
            Serial.print("[TIME] Set from serial: ");
            Serial.println(timestamp);
            // Automatically broadcast to mesh
            hybridMAC.broadcastTimeSync();
        } else {
            Serial.println("[TIME] Invalid timestamp. Use Unix epoch (seconds since 1970)");
        }
    }
    else if (cmd == "time") {
        // Show current time info
        Serial.println("\n=== Time Status ===");
        Serial.print("Source: ");
        switch (hybridMAC.getTimeSource()) {
            case TIME_SOURCE_GPS:     Serial.println("GPS"); break;
            case TIME_SOURCE_NTP:     Serial.println("NTP"); break;
            case TIME_SOURCE_SERIAL:  Serial.println("SERIAL (host)"); break;
            case TIME_SOURCE_SYNCED:  Serial.println("SYNCED (from mesh)"); break;
            case TIME_SOURCE_CRYSTAL: Serial.println("CRYSTAL (local)"); break;
            default:                  Serial.println("UNKNOWN"); break;
        }
        Serial.print("Quality: ");
        Serial.print(hybridMAC.getTimeQuality());
        Serial.println("%");
        Serial.println("To set: time <unix_timestamp>");
        Serial.println("===================\n");
    }
#endif
    else {
        Serial.println("Unknown command. Type 'help' for available commands.");
    }
}

void handleNameCommand(String& cmd) {
    if (cmd == "name") {
        // Show this node's name
        Serial.print("Node name: ");
        Serial.print(nodeNaming.getLocalName());
        Serial.print(" (0x");
        Serial.print(nodeAddress, HEX);
        Serial.println(")");
    }
    else if (cmd.startsWith("name set ")) {
        // Set this node's name
        String newName = cmd.substring(9);
        newName.trim();
        if (nodeNaming.setLocalName(newName.c_str())) {
            Serial.print("Name set to: ");
            Serial.println(nodeNaming.getLocalName());
        } else {
            Serial.println("Invalid name. Use 1-16 alphanumeric chars.");
        }
    }
    else if (cmd == "name list") {
        // List all known nodes
        Serial.println("\n=== Known Nodes ===");
        Serial.print("  * ");
        Serial.print(nodeNaming.getLocalName());
        Serial.print(" (0x");
        Serial.print(nodeAddress, HEX);
        Serial.println(") [local]");

        int count = nodeNaming.getNodeCount();
        for (int i = 0; i < count; i++) {
            NodeNameEntry entry;
            if (nodeNaming.getNodeByIndex(i, &entry)) {
                Serial.print("    ");
                Serial.print(entry.name);
                Serial.print(" (0x");
                Serial.print(entry.address, HEX);
                Serial.println(")");
            }
        }
        Serial.print("Total: ");
        Serial.print(count + 1);
        Serial.println(" nodes");
        Serial.println("===================\n");
    }
    else if (cmd.startsWith("name add ")) {
        // Format: name add <addr> <name>
        String rest = cmd.substring(9);
        int spaceIdx = rest.indexOf(' ');
        if (spaceIdx > 0) {
            String addrStr = rest.substring(0, spaceIdx);
            String name = rest.substring(spaceIdx + 1);
            name.trim();

            uint32_t addr = strtoul(addrStr.c_str(), NULL, 16);
            if (addr == 0 || addr == 0xFFFFFFFF) {
                Serial.println("Invalid address");
                return;
            }

            if (nodeNaming.setNodeName(addr, name.c_str())) {
                Serial.print("Added name '");
                Serial.print(name);
                Serial.print("' for 0x");
                Serial.println(addr, HEX);
            } else {
                Serial.println("Failed to add name. Check name validity or table full.");
            }
        } else {
            Serial.println("Usage: name add <hex_addr> <name>");
        }
    }
    else if (cmd.startsWith("name remove ")) {
        String addrStr = cmd.substring(12);
        addrStr.trim();
        uint32_t addr = strtoul(addrStr.c_str(), NULL, 16);
        if (nodeNaming.removeNodeName(addr)) {
            Serial.print("Removed name for 0x");
            Serial.println(addr, HEX);
        } else {
            Serial.println("Address not found in name table");
        }
    }
    else {
        Serial.println("Name commands:");
        Serial.println("  name              - Show this node's name");
        Serial.println("  name set <name>   - Set this node's name");
        Serial.println("  name list         - List all known nodes");
        Serial.println("  name add <addr> <name> - Add name for address");
        Serial.println("  name remove <addr>     - Remove name");
    }
}

void printStatus() {
    Serial.println("\n=== LNK-22 Status ===");
    Serial.print("Node: ");
    Serial.print(nodeNaming.getLocalName());
    Serial.print(" (0x");
    Serial.print(nodeAddress, HEX);
    Serial.println(")");
    Serial.print("Uptime: ");
    Serial.print(millis() / 1000);
    Serial.println(" seconds");
    Serial.print("Radio RSSI: ");
    Serial.print(radio.getLastRSSI());
    Serial.println(" dBm");
    Serial.print("Radio SNR: ");
    Serial.print(radio.getLastSNR());
    Serial.println(" dB");
    Serial.print("Packets Sent: ");
    Serial.println(mesh.getPacketsSent());
    Serial.print("Packets Received: ");
    Serial.println(mesh.getPacketsReceived());
    Serial.print("Neighbors: ");
    Serial.println(mesh.getNeighborCount());
    Serial.print("Routes: ");
    Serial.println(mesh.getRouteCount());
    Serial.print("Channel: ");
    Serial.println(mesh.getChannel());
    Serial.println("===================\n");
}

void printHelp() {
    Serial.println("\n=== LNK-22 Commands ===");
    Serial.println("send <name|addr> <msg> - Send message");
    Serial.println("name              - Show/set node names");
    Serial.println("status            - Show device status");
    Serial.println("routes            - Show routing table");
    Serial.println("neighbors         - Show neighbor list");
    Serial.println("beacon            - Send beacon now");
    Serial.println("channel <0-7>     - Switch to channel");
    Serial.println("radio             - Show radio config");
#if FEATURE_STORE_FORWARD
    Serial.println("queue             - Show message queue");
    Serial.println("queue clear       - Clear message queue");
#endif
#if FEATURE_ADR
    Serial.println("adr               - Show ADR status");
    Serial.println("adr on/off        - Enable/disable ADR");
    Serial.println("adr scan          - Scan for network SF");
#endif
#if FEATURE_EMERGENCY
    Serial.println("sos               - Send SOS (general)");
    Serial.println("sos <type>        - SOS: medical/fire/rescue/test");
    Serial.println("sos cancel        - Cancel active SOS");
    Serial.println("sos status        - Show SOS status");
#endif
#if FEATURE_HISTORY
    Serial.println("history [n]       - Show last n messages");
    Serial.println("history clear     - Clear history");
    Serial.println("history save      - Save to flash");
#endif
#if FEATURE_LINKS
    Serial.println("link              - Show secure links");
    Serial.println("link <node>       - Request link to node");
    Serial.println("link close <node> - Close link");
#endif
#if FEATURE_DTN
    Serial.println("dtn               - Show DTN status");
    Serial.println("dtn send <dest> <msg> - Send DTN bundle");
    Serial.println("dtn clear         - Clear bundles");
#endif
#if FEATURE_GEOROUTING
    Serial.println("geo               - Show geo routing status");
    Serial.println("geo on/off        - Enable/disable");
    Serial.println("geo gpsr          - Enable GPSR mode");
#endif
#if FEATURE_GROUPS
    Serial.println("group             - Show group channels");
    Serial.println("group create <name> - Create encrypted group");
    Serial.println("group join <name> <key> - Join with key");
    Serial.println("group send <name> <msg> - Send to group");
    Serial.println("group leave <name> - Leave group");
#endif
    Serial.println("relay             - Show BLE relay status");
    Serial.println("relay on/off      - Enable/disable BLE-to-BLE relay");
#if FEATURE_HYBRID_MAC
    Serial.println("mac               - Show MAC layer status");
    Serial.println("mac on/off        - Enable/disable TDMA mode");
    Serial.println("mac sync          - Broadcast time sync");
    Serial.println("time              - Show time sync status");
    Serial.println("time <unix_ts>    - Set time from host");
#endif
    Serial.println("help              - Show this help");
    Serial.println("=======================\n");
}

#ifdef HAS_DISPLAY
void updateDisplay() {
    if (displayAvailable) {
        // Build neighbor list with names for display
        DisplayNeighbor neighbors[4];
        uint8_t numNeighbors = 0;
        uint8_t totalNeighbors = mesh.getNeighborCount();

        for (uint8_t i = 0; i < totalNeighbors && numNeighbors < 4; i++) {
            uint32_t addr;
            int16_t rssi;
            int8_t snr;
            if (mesh.getNeighbor(i, &addr, &rssi, &snr)) {
                neighbors[numNeighbors].address = addr;
                neighbors[numNeighbors].rssi = rssi;
                // Get name or hex address
                const char* name = nodeNaming.getNodeName(addr);
                strncpy(neighbors[numNeighbors].name, name, 16);
                neighbors[numNeighbors].name[16] = '\0';
                numNeighbors++;
            }
        }

        // Update GPS data for display
        #ifdef HAS_GPS
        if (gps.hasFix()) {
            GPSPosition pos;
            if (gps.getPosition(&pos)) {
                display.updateGPS(pos.latitude, pos.longitude, pos.altitude, pos.satellites, true);
            }
        } else {
            display.updateGPS(0, 0, 0, 0, false);
        }
        #endif

        // Update battery data for display (RAK4631 battery sensing)
        // RAK4631 uses voltage divider on VBAT: AIN3 = VBAT * (1.5/2.5) = VBAT * 0.6
        // With 12-bit ADC (4096) and 3.0V internal reference
        analogReference(AR_INTERNAL_3_0);
        analogReadResolution(12);
        uint32_t adcValue = analogRead(PIN_A0);
        float batteryVoltage = (adcValue * 3.0 / 4096.0) * 1.66;  // Convert back from divider
        uint8_t batteryPercent = 0;
        if (batteryVoltage >= 4.1) {
            batteryPercent = 100;
        } else if (batteryVoltage <= 3.3) {
            batteryPercent = 0;
        } else {
            batteryPercent = (uint8_t)((batteryVoltage - 3.3) / 0.8 * 100);
        }
        // Note: Can't easily detect charging state without additional hardware
        bool charging = false;
        display.updateBattery(batteryPercent, batteryVoltage, charging);

        // Update MAC/TDMA status for display
        const MACStats& macStats = hybridMAC.getStats();
        bool isTDMA = hybridMAC.isTimeSynced();
        uint8_t timeSource = static_cast<uint8_t>(hybridMAC.getTimeSource());
        display.updateMAC(isTDMA, timeSource, 15,  // stratum from MAC
                         hybridMAC.getCurrentFrame(), hybridMAC.getCurrentSlot(),
                         macStats.tdmaTransmissions, macStats.csmaTransmissions);

        display.updateWithNeighbors(
            nodeAddress,
            nodeNaming.getLocalName(),
            mesh.getNeighborCount(),
            mesh.getRouteCount(),
            mesh.getPacketsSent(),
            mesh.getPacketsReceived(),
            radio.getLastRSSI(),
            radio.getLastSNR(),
            neighbors,
            numNeighbors
        );
    }
}
#endif

// ============================================================================
// BLE Callback Functions
// ============================================================================

void updateBLEStatus() {
    if (!bleService.isConnected()) return;

    // Build status structure
    BLEDeviceStatus status;
    status.nodeAddress = nodeAddress;
    status.txCount = mesh.getPacketsSent();
    status.rxCount = mesh.getPacketsReceived();
    status.neighborCount = mesh.getNeighborCount();
    status.routeCount = mesh.getRouteCount();
    status.channel = mesh.getChannel();
    status.txPower = LORA_TX_POWER;
    status.battery = 100;  // TODO: Read actual battery level
    status.flags = 0;
    #ifdef HAS_GPS
    status.flags |= STATUS_FLAG_GPS;
    #endif
    #ifdef HAS_DISPLAY
    status.flags |= STATUS_FLAG_DISPLAY;
    #endif
    status.uptime = millis() / 1000;

    bleService.notifyStatus(status);

    // Send neighbor data to app
    uint8_t neighborCount = mesh.getNeighborCount();
    if (neighborCount > 0) {
        BLENeighborEntry bleNeighbors[MAX_NEIGHBORS];
        uint8_t validCount = 0;

        for (uint8_t i = 0; i < MAX_NEIGHBORS && validCount < neighborCount; i++) {
            uint32_t addr;
            int16_t rssi;
            int8_t snr;
            if (mesh.getNeighbor(i, &addr, &rssi, &snr)) {
                bleNeighbors[validCount].address = addr;
                bleNeighbors[validCount].rssi = rssi;
                bleNeighbors[validCount].snr = snr;
                bleNeighbors[validCount].quality = 100;  // TODO: Calculate actual quality
                bleNeighbors[validCount].lastSeen = millis() / 1000;
                bleNeighbors[validCount].packetCount = 0;  // TODO: Get actual packet count
                validCount++;
            }
        }

        if (validCount > 0) {
            bleService.notifyNeighbors(bleNeighbors, validCount);
        }
    }

    #ifdef HAS_GPS
    // Send GPS position if available
    if (gps.hasFix()) {
        GPSPosition pos;
        if (gps.getPosition(&pos)) {
            BLEGPSPosition blePos;
            blePos.latitude = pos.latitude;
            blePos.longitude = pos.longitude;
            blePos.altitude = pos.altitude;
            blePos.satellites = pos.satellites;
            blePos.valid = 1;
            bleService.notifyGPS(blePos);
        }
    }
    #endif
}

void onBLEMessage(uint8_t type, uint32_t destination, uint8_t channel, const uint8_t* payload, size_t length) {
    Serial.print("[BLE] Message from app -> dest: 0x");
    Serial.print(destination, HEX);
    Serial.print(", channel: ");
    Serial.print(channel);
    Serial.print(", len: ");
    Serial.println(length);

    // Send message via mesh network
    mesh.sendMessage(destination, (uint8_t*)payload, length);
}

void onBLECommand(uint8_t command, const uint8_t* params, size_t length) {
    Serial.print("[BLE] Command from app: 0x");
    Serial.println(command, HEX);

    switch (command) {
        case CMD_SEND_BEACON:
            Serial.println("[BLE] Sending beacon...");
            mesh.sendBeacon();
            break;

        case CMD_SWITCH_CHANNEL:
            if (length >= 1) {
                uint8_t newChannel = params[0];
                Serial.print("[BLE] Switching to channel ");
                Serial.println(newChannel);
                mesh.setChannel(newChannel);
            }
            break;

        case CMD_SET_TX_POWER:
            if (length >= 1) {
                int8_t power = (int8_t)params[0];
                Serial.print("[BLE] Setting TX power to ");
                Serial.print(power);
                Serial.println(" dBm");
                radio.setTxPower(power);
            }
            break;

        case CMD_REQUEST_STATUS:
            updateBLEStatus();
            break;

        case CMD_CLEAR_ROUTES:
            Serial.println("[BLE] Route clearing not supported via BLE");
            // Routes auto-expire after ROUTE_TIMEOUT (5 minutes)
            break;

        case CMD_REBOOT:
            Serial.println("[BLE] Rebooting device...");
            delay(100);
            NVIC_SystemReset();
            break;

        default:
            Serial.print("[BLE] Unknown command: 0x");
            Serial.println(command, HEX);
            break;
    }
}
