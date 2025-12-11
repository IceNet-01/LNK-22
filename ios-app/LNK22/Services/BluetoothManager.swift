//
//  BluetoothManager.swift
//  LNK22
//
//  Core Bluetooth Low Energy manager for LNK-22 radio communication
//  Handles device discovery, connection, and data transfer
//

import Foundation
import CoreBluetooth
import Combine

// MARK: - BLE Service and Characteristic UUIDs

/// LNK-22 BLE Service UUIDs
/// These match the GATT service defined in the firmware
struct LNK22BLEService {
    // Default pairing PIN (matches firmware default)
    // iOS will show a system dialog for PIN entry when pairing is required
    static let defaultPairingPIN = "123456"

    // Main LNK-22 Service UUID
    static let serviceUUID = CBUUID(string: "4C4E4B32-0001-1000-8000-00805F9B34FB")

    // Characteristics
    static let messageRxUUID = CBUUID(string: "4C4E4B32-0002-1000-8000-00805F9B34FB")  // Write: Send messages
    static let messageTxUUID = CBUUID(string: "4C4E4B32-0003-1000-8000-00805F9B34FB")  // Notify: Receive messages
    static let commandUUID = CBUUID(string: "4C4E4B32-0004-1000-8000-00805F9B34FB")    // Write: Send commands
    static let statusUUID = CBUUID(string: "4C4E4B32-0005-1000-8000-00805F9B34FB")     // Read/Notify: Device status
    static let neighborsUUID = CBUUID(string: "4C4E4B32-0006-1000-8000-00805F9B34FB")  // Read/Notify: Neighbor list
    static let routesUUID = CBUUID(string: "4C4E4B32-0007-1000-8000-00805F9B34FB")     // Read/Notify: Routing table
    static let configUUID = CBUUID(string: "4C4E4B32-0008-1000-8000-00805F9B34FB")     // Read/Write: Configuration
    static let gpsUUID = CBUUID(string: "4C4E4B32-0009-1000-8000-00805F9B34FB")        // Read/Notify: GPS position
    static let nodeNameUUID = CBUUID(string: "4C4E4B32-000A-1000-8000-00805F9B34FB")   // Read/Write: Node name
}

// MARK: - Connection State

enum ConnectionState: String {
    case disconnected = "Disconnected"
    case scanning = "Scanning..."
    case connecting = "Connecting..."
    case connected = "Connected"
    case ready = "Ready"
    case error = "Error"
}

// MARK: - Discovered Device

struct DiscoveredDevice: Identifiable, Hashable {
    let id: UUID
    let peripheral: CBPeripheral
    let name: String
    let rssi: Int
    var lastSeen: Date

    func hash(into hasher: inout Hasher) {
        hasher.combine(id)
    }

    static func == (lhs: DiscoveredDevice, rhs: DiscoveredDevice) -> Bool {
        lhs.id == rhs.id
    }
}

// MARK: - Bluetooth Manager

@MainActor
class BluetoothManager: NSObject, ObservableObject {
    // MARK: - Published Properties

    @Published var connectionState: ConnectionState = .disconnected
    @Published var discoveredDevices: [DiscoveredDevice] = []
    @Published var connectedDevice: DiscoveredDevice?
    @Published var deviceStatus: DeviceStatus?
    @Published var neighbors: [Neighbor] = []
    @Published var routes: [RouteEntry] = []
    @Published var lastError: String?
    @Published var isBluetoothEnabled = false
    @Published var isPaired = false  // True after successful pairing
    @Published var isPairingRequired = false  // True when PIN entry is needed

    // Node naming
    @Published var nodeName: String?
    @Published var nodeNames: [UInt32: String] = [:]  // Address -> Name mapping

    // New v1.8.0 features
    @Published var secureLinks: [SecureLink] = []
    @Published var groups: [MeshGroup] = []
    @Published var emergencyActive: Bool = false
    @Published var storeForwardStatus: StoreForwardStatus?
    @Published var adrStatus: ADRStatus?

    // Message handling
    @Published var receivedMessages: [MeshMessage] = []

    // MARK: - Private Properties

    private var centralManager: CBCentralManager!
    private var connectedPeripheral: CBPeripheral?
    private var characteristics: [CBUUID: CBCharacteristic] = [:]

    // Callbacks
    var onMessageReceived: ((MeshMessage) -> Void)?
    var onStatusUpdate: ((DeviceStatus) -> Void)?

    // MARK: - Initialization

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: nil, queue: .main)
        centralManager.delegate = self
    }

    // MARK: - Public Methods

    /// Start scanning for LNK-22 devices
    func startScanning() {
        guard centralManager.state == .poweredOn else {
            lastError = "Bluetooth is not available"
            return
        }

        connectionState = .scanning
        discoveredDevices.removeAll()

        // Scan for devices advertising the LNK-22 service
        centralManager.scanForPeripherals(
            withServices: [LNK22BLEService.serviceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )

        // Also scan for devices with the name "LNK-22" (fallback)
        centralManager.scanForPeripherals(
            withServices: nil,
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )

        // Stop scanning after 10 seconds
        DispatchQueue.main.asyncAfter(deadline: .now() + 10) { [weak self] in
            self?.stopScanning()
        }
    }

    /// Stop scanning for devices
    func stopScanning() {
        centralManager.stopScan()
        if connectionState == .scanning {
            connectionState = .disconnected
        }
    }

    /// Connect to a discovered device
    func connect(to device: DiscoveredDevice) {
        stopScanning()
        connectionState = .connecting
        centralManager.connect(device.peripheral, options: nil)
    }

    /// Disconnect from current device
    func disconnect() {
        if let peripheral = connectedPeripheral {
            centralManager.cancelPeripheralConnection(peripheral)
        }
        cleanup()
    }

    /// Send a message to a destination address
    func sendMessage(to destination: UInt32, text: String, channel: UInt8 = 0) {
        guard connectionState == .ready,
              let characteristic = characteristics[LNK22BLEService.messageRxUUID] else {
            lastError = "Not connected to device"
            return
        }

        // Build message packet
        var data = Data()

        // Message type (1 byte) - 0x01 = text message
        data.append(0x01)

        // Destination address (4 bytes, little-endian)
        var dest = destination.littleEndian
        data.append(Data(bytes: &dest, count: 4))

        // Channel (1 byte)
        data.append(channel)

        // Message text (UTF-8)
        if let textData = text.data(using: .utf8) {
            data.append(textData)
        }

        // Write to characteristic
        connectedPeripheral?.writeValue(data, for: characteristic, type: .withResponse)
    }

    /// Send a broadcast message
    func sendBroadcast(text: String, channel: UInt8 = 0) {
        sendMessage(to: 0xFFFFFFFF, text: text, channel: channel)
    }

    /// Send a command to the device
    func sendCommand(_ command: DeviceCommand) {
        guard connectionState == .ready,
              let characteristic = characteristics[LNK22BLEService.commandUUID] else {
            lastError = "Not connected to device"
            return
        }

        let data = command.toData()
        connectedPeripheral?.writeValue(data, for: characteristic, type: .withResponse)
    }

    /// Request device status update
    func requestStatus() {
        guard let characteristic = characteristics[LNK22BLEService.statusUUID] else { return }
        connectedPeripheral?.readValue(for: characteristic)
    }

    /// Request neighbor list
    func requestNeighbors() {
        guard let characteristic = characteristics[LNK22BLEService.neighborsUUID] else { return }
        connectedPeripheral?.readValue(for: characteristic)
    }

    /// Request routing table
    func requestRoutes() {
        guard let characteristic = characteristics[LNK22BLEService.routesUUID] else { return }
        connectedPeripheral?.readValue(for: characteristic)
    }

    /// Update device configuration
    func updateConfig(_ config: DeviceConfig) {
        guard connectionState == .ready,
              let characteristic = characteristics[LNK22BLEService.configUUID] else {
            lastError = "Not connected to device"
            return
        }

        let data = config.toData()
        connectedPeripheral?.writeValue(data, for: characteristic, type: .withResponse)
    }

    /// Switch channel
    func switchChannel(_ channel: UInt8) {
        let command = DeviceCommand.switchChannel(channel)
        sendCommand(command)
    }

    /// Send beacon
    func sendBeacon() {
        let command = DeviceCommand.sendBeacon
        sendCommand(command)
    }

    /// Request node name
    func requestNodeName() {
        guard let characteristic = characteristics[LNK22BLEService.nodeNameUUID] else { return }
        connectedPeripheral?.readValue(for: characteristic)
    }

    /// Set node name
    func setNodeName(_ name: String) {
        guard connectionState == .ready,
              let characteristic = characteristics[LNK22BLEService.nodeNameUUID] else {
            lastError = "Not connected to device"
            return
        }

        // Node name is max 16 characters + null terminator
        let trimmedName = String(name.prefix(16))
        if let data = trimmedName.data(using: .utf8) {
            connectedPeripheral?.writeValue(data, for: characteristic, type: .withResponse)
            nodeName = trimmedName
        }
    }

    /// Trigger emergency SOS
    func triggerEmergency(type: EmergencyType = .sos) {
        let command = DeviceCommand.triggerEmergency(type)
        sendCommand(command)
        emergencyActive = true
    }

    /// Cancel emergency
    func cancelEmergency() {
        let command = DeviceCommand.cancelEmergency
        sendCommand(command)
        emergencyActive = false
    }

    /// Get display name for an address
    func displayName(for address: UInt32) -> String {
        if let name = nodeNames[address] {
            return name
        }
        return String(format: "0x%08X", address)
    }

    // MARK: - Private Methods

    private func cleanup() {
        connectedPeripheral = nil
        connectedDevice = nil
        characteristics.removeAll()
        deviceStatus = nil
        neighbors.removeAll()
        routes.removeAll()
        connectionState = .disconnected
        isPaired = false
        isPairingRequired = false
        nodeName = nil
    }

    private func discoverServices() {
        // Discover all services to help with debugging
        // Once connected, we'll filter for the LNK-22 service
        connectedPeripheral?.discoverServices(nil)
    }

    private func subscribeToNotifications() {
        // Subscribe to all notification characteristics
        let notifyCharacteristics = [
            LNK22BLEService.messageTxUUID,
            LNK22BLEService.statusUUID,
            LNK22BLEService.neighborsUUID,
            LNK22BLEService.routesUUID,
            LNK22BLEService.gpsUUID,
            LNK22BLEService.nodeNameUUID
        ]

        for uuid in notifyCharacteristics {
            if let characteristic = characteristics[uuid] {
                connectedPeripheral?.setNotifyValue(true, for: characteristic)
            }
        }
    }

    private func parseNodeName(_ data: Data) {
        if let name = String(data: data, encoding: .utf8)?.trimmingCharacters(in: .controlCharacters) {
            nodeName = name
        }
    }

    private func parseReceivedMessage(_ data: Data) {
        guard data.count >= 10 else { return }

        // Parse message format:
        // [type:1][source:4][channel:1][timestamp:4][payload:N]

        let type = data[0]
        let source = data.subdata(in: 1..<5).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        let channel = data[5]
        let timestamp = data.subdata(in: 6..<10).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        let payload = data.subdata(in: 10..<data.count)

        let message = MeshMessage(
            id: UUID(),
            source: source,
            destination: 0xFFFFFFFF, // Will be updated by firmware
            channel: channel,
            type: MessageType(rawValue: type) ?? .text,
            content: String(data: payload, encoding: .utf8) ?? "",
            timestamp: Date(timeIntervalSince1970: TimeInterval(timestamp)),
            rssi: nil,
            snr: nil
        )

        receivedMessages.append(message)
        onMessageReceived?(message)
    }

    private func parseStatus(_ data: Data) {
        guard data.count >= 24 else { return }

        // Parse status format:
        // [nodeAddr:4][txCount:4][rxCount:4][neighborCount:2][routeCount:2]
        // [channel:1][txPower:1][battery:1][flags:1][uptime:4]

        let nodeAddress = data.subdata(in: 0..<4).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        let txCount = data.subdata(in: 4..<8).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        let rxCount = data.subdata(in: 8..<12).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        let neighborCount = data.subdata(in: 12..<14).withUnsafeBytes { $0.load(as: UInt16.self).littleEndian }
        let routeCount = data.subdata(in: 14..<16).withUnsafeBytes { $0.load(as: UInt16.self).littleEndian }
        let channel = data[16]
        let txPower = Int8(bitPattern: data[17])
        let battery = data[18]
        let flags = data[19]
        let uptime = data.subdata(in: 20..<24).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }

        deviceStatus = DeviceStatus(
            nodeAddress: nodeAddress,
            firmwareVersion: "1.8.0",
            txCount: txCount,
            rxCount: rxCount,
            neighborCount: Int(neighborCount),
            routeCount: Int(routeCount),
            currentChannel: channel,
            txPower: txPower,
            batteryPercent: battery,
            isEncryptionEnabled: (flags & 0x01) != 0,
            isGPSEnabled: (flags & 0x02) != 0,
            isDisplayEnabled: (flags & 0x04) != 0,
            uptime: uptime
        )

        onStatusUpdate?(deviceStatus!)
    }

    private func parseNeighbors(_ data: Data) {
        neighbors.removeAll()

        // Each neighbor entry is 16 bytes:
        // [address:4][rssi:2][snr:1][quality:1][lastSeen:4][packetCount:4]
        let entrySize = 16
        var offset = 0

        while offset + entrySize <= data.count {
            let entryData = data.subdata(in: offset..<(offset + entrySize))

            let address = entryData.subdata(in: 0..<4).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
            let rssi = entryData.subdata(in: 4..<6).withUnsafeBytes { $0.load(as: Int16.self).littleEndian }
            let snr = Int8(bitPattern: entryData[6])
            let quality = entryData[7]
            let lastSeen = entryData.subdata(in: 8..<12).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
            let packetCount = entryData.subdata(in: 12..<16).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }

            let neighbor = Neighbor(
                address: address,
                rssi: rssi,
                snr: snr,
                quality: quality,
                lastSeen: Date(timeIntervalSince1970: TimeInterval(lastSeen)),
                packetsReceived: packetCount
            )

            neighbors.append(neighbor)
            offset += entrySize
        }
    }

    private func parseRoutes(_ data: Data) {
        routes.removeAll()

        // Each route entry is 14 bytes:
        // [destination:4][nextHop:4][hopCount:1][quality:1][timestamp:4]
        let entrySize = 14
        var offset = 0

        while offset + entrySize <= data.count {
            let entryData = data.subdata(in: offset..<(offset + entrySize))

            let destination = entryData.subdata(in: 0..<4).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
            let nextHop = entryData.subdata(in: 4..<8).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
            let hopCount = entryData[8]
            let quality = entryData[9]
            let timestamp = entryData.subdata(in: 10..<14).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }

            let route = RouteEntry(
                destination: destination,
                nextHop: nextHop,
                hopCount: hopCount,
                quality: quality,
                timestamp: Date(timeIntervalSince1970: TimeInterval(timestamp)),
                isValid: true
            )

            routes.append(route)
            offset += entrySize
        }
    }
}

// MARK: - CBCentralManagerDelegate

extension BluetoothManager: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        Task { @MainActor in
            switch central.state {
            case .poweredOn:
                isBluetoothEnabled = true
            case .poweredOff:
                isBluetoothEnabled = false
                lastError = "Bluetooth is turned off"
                cleanup()
            case .unauthorized:
                isBluetoothEnabled = false
                lastError = "Bluetooth access not authorized"
            case .unsupported:
                isBluetoothEnabled = false
                lastError = "Bluetooth is not supported on this device"
            default:
                isBluetoothEnabled = false
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        Task { @MainActor in
            let name = peripheral.name ?? advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? "Unknown"

            // Skip devices with no name (truly unknown devices)
            guard name != "Unknown" else { return }

            // Check if already discovered
            if let index = discoveredDevices.firstIndex(where: { $0.id == peripheral.identifier }) {
                discoveredDevices[index].lastSeen = Date()
            } else {
                let device = DiscoveredDevice(
                    id: peripheral.identifier,
                    peripheral: peripheral,
                    name: name,
                    rssi: RSSI.intValue,
                    lastSeen: Date()
                )
                discoveredDevices.append(device)
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        Task { @MainActor in
            connectedPeripheral = peripheral
            peripheral.delegate = self
            connectionState = .connected

            // Find matching device
            if let device = discoveredDevices.first(where: { $0.peripheral.identifier == peripheral.identifier }) {
                connectedDevice = device
            }

            // Discover services
            discoverServices()
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        Task { @MainActor in
            lastError = error?.localizedDescription ?? "Failed to connect"
            connectionState = .error
            cleanup()
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        Task { @MainActor in
            if let error = error {
                lastError = error.localizedDescription
            }
            cleanup()
        }
    }
}

// MARK: - CBPeripheralDelegate

extension BluetoothManager: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        Task { @MainActor in
            if let error = error {
                lastError = error.localizedDescription
                connectionState = .error
                return
            }

            guard let services = peripheral.services else {
                lastError = "No services found"
                connectionState = .error
                return
            }

            print("[BLE] Found \(services.count) services")
            var foundLNK22Service = false

            for service in services {
                print("[BLE] Service: \(service.uuid)")
                if service.uuid == LNK22BLEService.serviceUUID {
                    foundLNK22Service = true
                    // Discover all characteristics
                    peripheral.discoverCharacteristics(nil, for: service)
                }
            }

            if !foundLNK22Service {
                lastError = "LNK-22 service not found. Is this an LNK-22 device?"
                connectionState = .error
            }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        Task { @MainActor in
            if let error = error {
                lastError = error.localizedDescription
                print("[BLE] Characteristic discovery error: \(error.localizedDescription)")
                return
            }

            guard let chars = service.characteristics else {
                print("[BLE] No characteristics found in service")
                return
            }

            print("[BLE] Found \(chars.count) characteristics")
            for characteristic in chars {
                print("[BLE] Characteristic: \(characteristic.uuid)")
                characteristics[characteristic.uuid] = characteristic
            }

            // All characteristics discovered, ready to communicate
            connectionState = .ready
            isPaired = true  // Successfully connected and discovered services (pairing succeeded if required)
            print("[BLE] Connection ready - \(characteristics.count) characteristics stored")

            // Subscribe to notifications
            subscribeToNotifications()

            // Request initial status
            requestStatus()
            requestNeighbors()
            requestRoutes()
            requestNodeName()
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        Task { @MainActor in
            if let error = error {
                print("[BLE] Read error for \(characteristic.uuid): \(error.localizedDescription)")
                return
            }
            guard let data = characteristic.value else {
                print("[BLE] No data for \(characteristic.uuid)")
                return
            }
            print("[BLE] Received \(data.count) bytes from \(characteristic.uuid)")

            switch characteristic.uuid {
            case LNK22BLEService.messageTxUUID:
                parseReceivedMessage(data)

            case LNK22BLEService.statusUUID:
                parseStatus(data)

            case LNK22BLEService.neighborsUUID:
                parseNeighbors(data)

            case LNK22BLEService.routesUUID:
                parseRoutes(data)

            case LNK22BLEService.gpsUUID:
                // Parse GPS data if needed
                break

            case LNK22BLEService.nodeNameUUID:
                parseNodeName(data)

            default:
                break
            }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        Task { @MainActor in
            if let error = error {
                lastError = "Write failed: \(error.localizedDescription)"
            }
        }
    }
}
