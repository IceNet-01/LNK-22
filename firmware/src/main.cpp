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

// Global instances
Radio radio;
Mesh mesh;
Crypto crypto;

#ifdef HAS_GPS
GPS gps;
#endif

// Device state
uint32_t nodeAddress = 0;
unsigned long lastBeacon = 0;
unsigned long lastDisplay = 0;
unsigned long lastPositionBroadcast = 0;

void setup() {
    // Initialize serial for debugging
    Serial.begin(115200);
    delay(2000); // Wait for serial to initialize

    Serial.println("\n\n========================================");
    Serial.println("MeshNet Firmware v" MESHNET_VERSION);
    Serial.println("Board: " BOARD_NAME);
    Serial.println("========================================\n");

    // Initialize crypto subsystem
    Serial.println("[CRYPTO] Initializing cryptography...");
    crypto.begin();

    // Generate or load node identity
    nodeAddress = crypto.getNodeAddress();
    Serial.print("[CRYPTO] Node Address: 0x");
    Serial.println(nodeAddress, HEX);

    // Initialize radio
    Serial.println("[RADIO] Initializing LoRa radio...");
    if (!radio.begin()) {
        Serial.println("[ERROR] Radio initialization failed!");
        while(1) {
            delay(1000);
        }
    }
    Serial.println("[RADIO] Radio initialized successfully");

    // Initialize mesh network
    Serial.println("[MESH] Initializing mesh network...");
    mesh.begin(nodeAddress, &radio, &crypto);
    Serial.println("[MESH] Mesh network initialized");

    #ifdef HAS_GPS
    // Initialize GPS
    Serial.println("[GPS] Initializing GPS module...");
    if (gps.begin()) {
        Serial.println("[GPS] GPS initialized successfully");
    } else {
        Serial.println("[GPS] GPS initialization failed (optional feature)");
    }
    #endif

    // Send initial beacon
    mesh.sendBeacon();

    Serial.println("\n[SYSTEM] Boot complete. Ready to mesh!\n");
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
    // TODO: Implement display update for devices with screens
}
#endif
