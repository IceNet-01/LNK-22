//
//  Models.swift
//  LNK22
//
//  Data models for the LNK-22 mesh network
//

import Foundation
import SwiftUI

// MARK: - Message Types

enum MessageType: UInt8, Codable, CaseIterable {
    case text = 0x01
    case position = 0x02
    case sensor = 0x03
    case command = 0x04
    case file = 0x05

    var displayName: String {
        switch self {
        case .text: return "Text"
        case .position: return "Position"
        case .sensor: return "Sensor"
        case .command: return "Command"
        case .file: return "File"
        }
    }

    var icon: String {
        switch self {
        case .text: return "message"
        case .position: return "location"
        case .sensor: return "sensor.tag.radiowaves.forward"
        case .command: return "terminal"
        case .file: return "doc"
        }
    }
}

// MARK: - Mesh Message

struct MeshMessage: Identifiable, Codable, Equatable {
    let id: UUID
    let source: UInt32
    let destination: UInt32
    let channel: UInt8
    let type: MessageType
    let content: String
    let timestamp: Date
    let rssi: Int16?
    let snr: Int8?

    /// Friendly source name (e.g., "Node-048F")
    var sourceHex: String {
        String(format: "Node-%04X", source & 0xFFFF)
    }

    /// Friendly destination name
    var destinationHex: String {
        destination == 0xFFFFFFFF ? "Broadcast" : String(format: "Node-%04X", destination & 0xFFFF)
    }

    var isBroadcast: Bool {
        destination == 0xFFFFFFFF
    }

    var formattedTime: String {
        let formatter = DateFormatter()
        formatter.timeStyle = .short
        return formatter.string(from: timestamp)
    }

    var signalQuality: SignalQuality {
        guard let rssi = rssi else { return .unknown }
        if rssi >= -70 { return .excellent }
        if rssi >= -85 { return .good }
        if rssi >= -100 { return .fair }
        return .poor
    }
}

// MARK: - Signal Quality

enum SignalQuality: String {
    case excellent = "Excellent"
    case good = "Good"
    case fair = "Fair"
    case poor = "Poor"
    case unknown = "Unknown"

    var color: Color {
        switch self {
        case .excellent: return .green
        case .good: return .blue
        case .fair: return .yellow
        case .poor: return .red
        case .unknown: return .gray
        }
    }

    var icon: String {
        switch self {
        case .excellent: return "wifi"
        case .good: return "wifi"
        case .fair: return "wifi.exclamationmark"
        case .poor: return "wifi.slash"
        case .unknown: return "questionmark.circle"
        }
    }
}

// MARK: - Neighbor

struct Neighbor: Identifiable, Codable, Equatable {
    var id: UInt32 { address }
    let address: UInt32
    let rssi: Int16
    let snr: Int8
    let quality: UInt8
    let lastSeen: Date
    let packetsReceived: UInt32

    /// Friendly name generated from last 4 hex digits
    var addressHex: String {
        friendlyName
    }

    /// Friendly name generated from last 4 hex digits (used when no name is set)
    var friendlyName: String {
        String(format: "Node-%04X", address & 0xFFFF)
    }

    var signalQuality: SignalQuality {
        if rssi >= -70 { return .excellent }
        if rssi >= -85 { return .good }
        if rssi >= -100 { return .fair }
        return .poor
    }

    var qualityPercent: Int {
        Int(quality) * 100 / 255
    }

    var timeSinceLastSeen: String {
        let interval = Date().timeIntervalSince(lastSeen)
        if interval < 60 {
            return "\(Int(interval))s ago"
        } else if interval < 3600 {
            return "\(Int(interval / 60))m ago"
        } else {
            return "\(Int(interval / 3600))h ago"
        }
    }
}

// MARK: - Route Entry

struct RouteEntry: Identifiable, Codable, Equatable {
    var id: UInt32 { destination }
    let destination: UInt32
    let nextHop: UInt32
    let hopCount: UInt8
    let quality: UInt8
    let timestamp: Date
    let isValid: Bool

    /// Friendly destination name
    var destinationHex: String {
        String(format: "Node-%04X", destination & 0xFFFF)
    }

    /// Friendly next hop name
    var nextHopHex: String {
        String(format: "Node-%04X", nextHop & 0xFFFF)
    }

    var qualityPercent: Int {
        Int(quality) * 100 / 255
    }
}

// MARK: - Device Status

struct DeviceStatus: Codable, Equatable {
    let nodeAddress: UInt32
    let firmwareVersion: String
    let txCount: UInt32
    let rxCount: UInt32
    let neighborCount: Int
    let routeCount: Int
    let currentChannel: UInt8
    let txPower: Int8
    let batteryPercent: UInt8
    let isEncryptionEnabled: Bool
    let isGPSEnabled: Bool
    let isDisplayEnabled: Bool
    let uptime: UInt32

    // v1.8.0 feature flags (optional, for extended status)
    var isEmergencyActive: Bool = false
    var isADREnabled: Bool = false
    var isStoreForwardEnabled: Bool = false

    /// Friendly node name
    var nodeAddressHex: String {
        String(format: "Node-%04X", nodeAddress & 0xFFFF)
    }

    var uptimeFormatted: String {
        let hours = uptime / 3600
        let minutes = (uptime % 3600) / 60
        let seconds = uptime % 60

        if hours > 0 {
            return "\(hours)h \(minutes)m \(seconds)s"
        } else if minutes > 0 {
            return "\(minutes)m \(seconds)s"
        } else {
            return "\(seconds)s"
        }
    }

    var batteryIcon: String {
        if batteryPercent >= 80 {
            return "battery.100"
        } else if batteryPercent >= 50 {
            return "battery.75"
        } else if batteryPercent >= 25 {
            return "battery.50"
        } else if batteryPercent > 10 {
            return "battery.25"
        }
        return "battery.0"
    }

    var batteryColor: Color {
        if batteryPercent >= 50 {
            return .green
        } else if batteryPercent >= 25 {
            return .yellow
        }
        return .red
    }
}

// MARK: - Device Configuration

struct DeviceConfig: Codable {
    var channel: UInt8
    var txPower: Int8
    var spreadingFactor: UInt8
    var bandwidth: UInt32
    var encryptionEnabled: Bool
    var gpsEnabled: Bool
    var beaconInterval: UInt16
    var displayEnabled: Bool

    static var `default`: DeviceConfig {
        DeviceConfig(
            channel: 0,
            txPower: 22,
            spreadingFactor: 10,
            bandwidth: 125000,
            encryptionEnabled: true,
            gpsEnabled: true,
            beaconInterval: 60,
            displayEnabled: true
        )
    }

    func toData() -> Data {
        var data = Data()
        data.append(channel)
        data.append(UInt8(bitPattern: txPower))
        data.append(spreadingFactor)

        var bw = bandwidth.littleEndian
        data.append(Data(bytes: &bw, count: 4))

        var flags: UInt8 = 0
        if encryptionEnabled { flags |= 0x01 }
        if gpsEnabled { flags |= 0x02 }
        if displayEnabled { flags |= 0x04 }
        data.append(flags)

        var beacon = beaconInterval.littleEndian
        data.append(Data(bytes: &beacon, count: 2))

        return data
    }
}

// MARK: - Device Commands

enum DeviceCommand {
    case sendBeacon
    case switchChannel(UInt8)
    case setTxPower(Int8)
    case requestStatus
    case requestNeighbors
    case requestRoutes
    case clearRoutes
    case triggerEmergency(EmergencyType)
    case cancelEmergency
    case reboot
    case factoryReset

    func toData() -> Data {
        var data = Data()

        switch self {
        case .sendBeacon:
            data.append(0x01)

        case .switchChannel(let channel):
            data.append(0x02)
            data.append(channel)

        case .setTxPower(let power):
            data.append(0x03)
            data.append(UInt8(bitPattern: power))

        case .requestStatus:
            data.append(0x10)

        case .requestNeighbors:
            data.append(0x11)

        case .requestRoutes:
            data.append(0x12)

        case .clearRoutes:
            data.append(0x20)

        case .triggerEmergency(let emergencyType):
            data.append(0x30)  // Emergency command
            data.append(emergencyType.rawValue)

        case .cancelEmergency:
            data.append(0x31)  // Cancel emergency

        case .reboot:
            data.append(0xFE)

        case .factoryReset:
            data.append(0xFF)
        }

        return data
    }
}

// MARK: - Emergency Type

enum EmergencyType: UInt8, Codable, CaseIterable {
    case sos = 0x01
    case medical = 0x02
    case fire = 0x03
    case police = 0x04
    case custom = 0x05

    var displayName: String {
        switch self {
        case .sos: return "SOS"
        case .medical: return "Medical"
        case .fire: return "Fire"
        case .police: return "Police"
        case .custom: return "Custom"
        }
    }

    var icon: String {
        switch self {
        case .sos: return "sos"
        case .medical: return "cross.case.fill"
        case .fire: return "flame.fill"
        case .police: return "shield.fill"
        case .custom: return "exclamationmark.triangle.fill"
        }
    }

    var color: Color {
        switch self {
        case .sos: return .red
        case .medical: return .blue
        case .fire: return .orange
        case .police: return .purple
        case .custom: return .yellow
        }
    }
}

// MARK: - Secure Link

struct SecureLink: Identifiable, Codable, Equatable {
    var id: UInt32 { peerAddress }
    let peerAddress: UInt32
    let peerName: String?
    let isEstablished: Bool
    let lastHandshake: Date?
    let messagesExchanged: UInt32

    /// Friendly peer name
    var peerAddressHex: String {
        String(format: "Node-%04X", peerAddress & 0xFFFF)
    }

    var displayName: String {
        peerName ?? peerAddressHex
    }

    var statusText: String {
        isEstablished ? "Secure" : "Pending"
    }

    var statusColor: Color {
        isEstablished ? .green : .orange
    }
}

// MARK: - Mesh Group

struct MeshGroup: Identifiable, Codable, Equatable {
    let id: UUID
    let groupId: UInt8
    let name: String
    let memberCount: Int
    let isEncrypted: Bool
    let createdAt: Date

    var displayName: String {
        "\(name) (\(memberCount) members)"
    }
}

// MARK: - Store and Forward Status

struct StoreForwardStatus: Codable, Equatable {
    let isEnabled: Bool
    let storedMessageCount: Int
    let maxCapacity: Int
    let oldestMessageAge: TimeInterval?

    var capacityPercent: Int {
        guard maxCapacity > 0 else { return 0 }
        return (storedMessageCount * 100) / maxCapacity
    }

    var statusText: String {
        if !isEnabled {
            return "Disabled"
        }
        return "\(storedMessageCount)/\(maxCapacity) messages"
    }
}

// MARK: - ADR Status (Adaptive Data Rate)

struct ADRStatus: Codable, Equatable {
    let isEnabled: Bool
    let currentSpreadingFactor: UInt8
    let currentBandwidth: UInt32
    let currentTxPower: Int8
    let linkBudget: Float
    let packetLossRate: Float

    var dataRateText: String {
        "SF\(currentSpreadingFactor) / \(currentBandwidth / 1000)kHz"
    }

    var qualityText: String {
        let lossPercent = Int(packetLossRate * 100)
        if lossPercent < 5 {
            return "Excellent"
        } else if lossPercent < 15 {
            return "Good"
        } else if lossPercent < 30 {
            return "Fair"
        }
        return "Poor"
    }

    var qualityColor: Color {
        let lossPercent = Int(packetLossRate * 100)
        if lossPercent < 5 {
            return .green
        } else if lossPercent < 15 {
            return .blue
        } else if lossPercent < 30 {
            return .yellow
        }
        return .red
    }
}

// MARK: - GPS Position

struct GPSPosition: Codable, Equatable {
    let latitude: Double
    let longitude: Double
    let altitude: Float
    let satellites: UInt8
    let timestamp: Date

    var isValid: Bool {
        latitude != 0 && longitude != 0
    }

    var coordinateString: String {
        String(format: "%.6f, %.6f", latitude, longitude)
    }
}

// MARK: - MAC Layer Status (TDMA/CSMA)

/// Time source priority (higher value = better source)
enum TimeSource: UInt8, Codable, CaseIterable {
    case crystal = 0   // Free-running crystal (fallback)
    case synced = 1    // Synced from network peer
    case serial = 2    // Synced from host computer via serial
    case ntp = 3       // NTP server (via WiFi)
    case gps = 4       // GPS time (most accurate)

    var displayName: String {
        switch self {
        case .crystal: return "Crystal"
        case .synced: return "Network Synced"
        case .serial: return "Serial"
        case .ntp: return "NTP"
        case .gps: return "GPS"
        }
    }

    var icon: String {
        switch self {
        case .crystal: return "stopwatch"
        case .synced: return "network"
        case .serial: return "cable.connector"
        case .ntp: return "wifi"
        case .gps: return "location.fill"
        }
    }

    var color: Color {
        switch self {
        case .crystal: return .gray
        case .synced: return .green
        case .serial: return .orange
        case .ntp: return .blue
        case .gps: return .purple
        }
    }
}

/// MAC layer status including TDMA slot information
struct MACStatus: Codable, Equatable {
    let isTDMAEnabled: Bool
    let currentFrame: UInt32
    let currentSlot: UInt8
    let timeSource: TimeSource
    let stratum: UInt8
    let tdmaTxCount: UInt32
    let csmaTxCount: UInt32
    let collisionCount: UInt32
    let ccaBusyCount: UInt32
    let timeSyncCount: UInt32

    var macModeText: String {
        isTDMAEnabled && stratum < 15 ? "TDMA" : "CSMA-CA"
    }

    var macModeDescription: String {
        if isTDMAEnabled && stratum < 15 {
            return "Time Division Multiple Access - synchronized slots"
        }
        return "Carrier Sense Multiple Access with Collision Avoidance"
    }

    var isTimeSynced: Bool {
        stratum < 15
    }

    var stratumText: String {
        "Stratum \(stratum)"
    }
}

// MARK: - Radio Status

struct RadioStatus: Codable, Equatable {
    let frequency: Float      // MHz
    let txPower: Int8         // dBm
    let spreadingFactor: UInt8
    let bandwidth: UInt32     // Hz
    let channel: UInt8
    let lastRSSI: Int16
    let lastSNR: Int8

    var frequencyText: String {
        String(format: "%.1f MHz", frequency)
    }

    var sfText: String {
        "SF\(spreadingFactor)"
    }

    var bandwidthText: String {
        "\(bandwidth / 1000) kHz"
    }

    var signalQuality: SignalQuality {
        if lastRSSI >= -70 { return .excellent }
        if lastRSSI >= -85 { return .good }
        if lastRSSI >= -100 { return .fair }
        return .poor
    }
}

// MARK: - Node

struct MeshNode: Identifiable, Codable, Equatable {
    var id: UInt32 { address }
    let address: UInt32
    var name: String?
    var position: GPSPosition?
    var lastStatus: DeviceStatus?
    var lastSeen: Date
    var isOnline: Bool

    /// Friendly name from last 4 hex digits
    var addressHex: String {
        friendlyName
    }

    /// Friendly name generated from last 4 hex digits (used when no name is set)
    var friendlyName: String {
        String(format: "Node-%04X", address & 0xFFFF)
    }

    var displayName: String {
        name ?? friendlyName
    }
}
