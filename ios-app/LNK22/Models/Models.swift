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

    var sourceHex: String {
        String(format: "0x%08X", source)
    }

    var destinationHex: String {
        destination == 0xFFFFFFFF ? "Broadcast" : String(format: "0x%08X", destination)
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

    var addressHex: String {
        String(format: "0x%08X", address)
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

    var destinationHex: String {
        String(format: "0x%08X", destination)
    }

    var nextHopHex: String {
        String(format: "0x%08X", nextHop)
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
    let uptime: UInt32

    var nodeAddressHex: String {
        String(format: "0x%08X", nodeAddress)
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

        case .reboot:
            data.append(0xFE)

        case .factoryReset:
            data.append(0xFF)
        }

        return data
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

// MARK: - Node

struct MeshNode: Identifiable, Codable, Equatable {
    var id: UInt32 { address }
    let address: UInt32
    var name: String?
    var position: GPSPosition?
    var lastStatus: DeviceStatus?
    var lastSeen: Date
    var isOnline: Bool

    var addressHex: String {
        String(format: "0x%08X", address)
    }

    var displayName: String {
        name ?? addressHex
    }
}
