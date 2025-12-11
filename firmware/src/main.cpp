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

void setup() {
    // Initialize LED for status indication
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // Show we're booting

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
    if (bleService.begin("LNK-22")) {
        Serial.println("[BLE] BLE service initialized successfully");
        // Register BLE callbacks
        bleService.onMessage(onBLEMessage);
        bleService.onCommand(onBLECommand);
        bleService.startAdvertising();
        Serial.println("[BLE] Advertising started - connect with iPhone app");
    } else {
        Serial.println("[BLE] BLE initialization failed");
    }
    Serial.flush();

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
    if (now - lastBLEUpdate > 2000) {  // Update BLE status every 2 seconds
        updateBLEStatus();
        lastBLEUpdate = now;
    }

    // Yield to system tasks
    delay(10);
}

void handleSerialCommand() {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.startsWith("send ")) {
        // Format: send <dest_addr> <message>
        int spaceIdx = cmd.indexOf(' ', 5);
        if (spaceIdx > 0) {
            String destStr = cmd.substring(5, spaceIdx);
            String message = cmd.substring(spaceIdx + 1);
            uint32_t dest = strtoul(destStr.c_str(), NULL, 16);

            Serial.print("Sending to 0x");
            Serial.print(dest, HEX);
            Serial.print(": ");
            Serial.println(message);

            mesh.sendMessage(dest, (uint8_t*)message.c_str(), message.length());
        }
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
    else {
        Serial.println("Unknown command. Type 'help' for available commands.");
    }
}

void printStatus() {
    Serial.println("\n=== LNK-22 Status ===");
    Serial.print("Node Address: 0x");
    Serial.println(nodeAddress, HEX);
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
    Serial.println("send <addr> <msg> - Send message to address (hex)");
    Serial.println("status            - Show device status");
    Serial.println("routes            - Show routing table");
    Serial.println("neighbors         - Show neighbor list");
    Serial.println("beacon            - Send beacon now");
    Serial.println("channel <0-7>     - Switch to channel");
    Serial.println("radio             - Show radio config");
    Serial.println("debug <module> <0|1> - Toggle debugging");
    Serial.println("help              - Show this help");
    Serial.println("=======================\n");
}

#ifdef HAS_DISPLAY
void updateDisplay() {
    if (displayAvailable) {
        display.update(
            nodeAddress,
            mesh.getNeighborCount(),
            mesh.getRouteCount(),
            mesh.getPacketsSent(),
            mesh.getPacketsReceived(),
            radio.getLastRSSI(),
            radio.getLastSNR()
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
            Serial.println("[BLE] Clearing routing table...");
            mesh.clearRoutes();
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
