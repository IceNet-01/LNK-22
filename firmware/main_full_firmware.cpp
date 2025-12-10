/**
 * MeshNet Firmware
 * Main entry point for the mesh networking device
 *
 * Copyright (c) 2024 MeshNet Project
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

// Global instances
Radio radio;
Mesh mesh;
Crypto crypto;

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
#ifdef HAS_DISPLAY
void updateDisplay();
#endif

// Device state
uint32_t nodeAddress = 0;
unsigned long lastBeacon = 0;
unsigned long lastDisplay = 0;
unsigned long lastPositionBroadcast = 0;

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
    Serial.print("MeshNet Firmware v");
    Serial.println(MESHNET_VERSION);
    Serial.print("Board: ");
    Serial.println(BOARD_NAME);
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
    else if (cmd == "help") {
        printHelp();
    }
    else {
        Serial.println("Unknown command. Type 'help' for available commands.");
    }
}

void printStatus() {
    Serial.println("\n=== MeshNet Status ===");
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
    Serial.println("===================\n");
}

void printHelp() {
    Serial.println("\n=== MeshNet Commands ===");
    Serial.println("send <addr> <msg> - Send message to address (hex)");
    Serial.println("status            - Show device status");
    Serial.println("routes            - Show routing table");
    Serial.println("neighbors         - Show neighbor list");
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
