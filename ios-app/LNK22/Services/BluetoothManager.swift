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
/// Note: The firmware constructs UUIDs with the short UUID in bytes 2-3 of the first segment
struct LNK22BLEService {
    // Default pairing PIN (matches firmware default)
    // iOS will show a system dialog for PIN entry when pairing is required
    static let defaultPairingPIN = "123456"

    // Main LNK-22 Service UUID (firmware format: 4C4E{shortUUID}-4B32-1000-8000-00805F9B34FB)
    static let serviceUUID = CBUUID(string: "4C4E0001-4B32-1000-8000-00805F9B34FB")

    // Characteristics
    static let messageRxUUID = CBUUID(string: "4C4E0002-4B32-1000-8000-00805F9B34FB")  // Write: Send messages
    static let messageTxUUID = CBUUID(string: "4C4E0003-4B32-1000-8000-00805F9B34FB")  // Notify: Receive messages
    static let commandUUID = CBUUID(string: "4C4E0004-4B32-1000-8000-00805F9B34FB")    // Write: Send commands
    static let statusUUID = CBUUID(string: "4C4E0005-4B32-1000-8000-00805F9B34FB")     // Read/Notify: Device status
    static let neighborsUUID = CBUUID(string: "4C4E0006-4B32-1000-8000-00805F9B34FB")  // Read/Notify: Neighbor list
    static let routesUUID = CBUUID(string: "4C4E0007-4B32-1000-8000-00805F9B34FB")     // Read/Notify: Routing table
    static let configUUID = CBUUID(string: "4C4E0008-4B32-1000-8000-00805F9B34FB")     // Read/Write: Configuration
    static let gpsUUID = CBUUID(string: "4C4E0009-4B32-1000-8000-00805F9B34FB")        // Read/Notify: GPS position
    static let nodeNameUUID = CBUUID(string: "4C4E000A-4B32-1000-8000-00805F9B34FB")   // Read/Write: Node name
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

    /// Get display name for an address - NEVER returns hex
    func displayName(for address: UInt32) -> String {
        // First check if we have a stored name
        if let name = nodeNames[address] {
            return name
        }
        // Generate friendly name from last 4 hex digits (e.g., "Node-048F")
        return String(format: "Node-%04X", address & 0xFFFF)
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
        print("[BLE] parseReceivedMessage: \(data.count) bytes")
        guard data.count >= 10 else {
            print("[BLE] Message too short: \(data.count) bytes")
            return
        }

        // Parse message format:
        // [type:1][source:4][channel:1][timestamp:4][payload:N]

        let type = data[0]

        // Validate message type - must be a valid MessageType
        guard type >= 0x01 && type <= 0x05 else {
            print("[BLE] Invalid message type: \(type) - ignoring")
            return
        }

        let source = data.subdata(in: 1..<5).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }

        // Validate source address - must be non-zero and not broadcast
        guard source != 0 && source != 0xFFFFFFFF else {
            print("[BLE] Invalid source address: 0x\(String(format: "%08X", source)) - ignoring")
            return
        }

        let channel = data[5]

        // Validate channel - must be 0-7
        guard channel < 8 else {
            print("[BLE] Invalid channel: \(channel) - ignoring")
            return
        }

        let timestamp = data.subdata(in: 6..<10).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        let payload = data.subdata(in: 10..<data.count)

        // For text messages, validate the content is valid UTF-8 and printable
        guard let content = String(data: payload, encoding: .utf8),
              !content.isEmpty,
              content.allSatisfy({ $0.isASCII || $0.isLetter || $0.isNumber || $0.isPunctuation || $0.isWhitespace || $0.unicodeScalars.first?.value ?? 0 > 127 }) else {
            print("[BLE] Invalid or empty message content - ignoring")
            return
        }

        // Skip messages with only control characters or garbage
        let printableContent = content.filter { !$0.isNewline && ($0.isLetter || $0.isNumber || $0.isPunctuation || $0.isWhitespace || $0 == " ") }
        guard printableContent.count > 0 else {
            print("[BLE] Message contains only non-printable characters - ignoring")
            return
        }

        print("[BLE] Parsed message: type=\(type) source=0x\(String(format: "%08X", source)) channel=\(channel) content='\(content)'")

        let message = MeshMessage(
            id: UUID(),
            source: source,
            destination: 0xFFFFFFFF, // Will be updated by firmware
            channel: channel,
            type: MessageType(rawValue: type) ?? .text,
            content: content,
            timestamp: Date(timeIntervalSince1970: TimeInterval(timestamp)),
            rssi: nil,
            snr: nil
        )

        receivedMessages.append(message)
        print("[BLE] Message added, total: \(receivedMessages.count)")
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
        // Each neighbor entry is 16 bytes:
        // [address:4][rssi:2][snr:1][quality:1][lastSeen:4][packetCount:4]
        let entrySize = 16

        // Don't update if no valid data or data is not properly aligned
        guard data.count >= entrySize && data.count % entrySize == 0 else {
            print("[BLE] Neighbors data invalid size: \(data.count) bytes (not multiple of \(entrySize))")
            return
        }

        // Build new array first, then replace all at once to avoid UI flashing
        var newNeighbors: [Neighbor] = []
        var offset = 0

        while offset + entrySize <= data.count {
            let entryData = data.subdata(in: offset..<(offset + entrySize))

            let address = entryData.subdata(in: 0..<4).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }

            // Skip invalid entries (address 0 or 0xFFFFFFFF)
            guard address != 0 && address != 0xFFFFFFFF else {
                offset += entrySize
                continue
            }

            let rssi = entryData.subdata(in: 4..<6).withUnsafeBytes { $0.load(as: Int16.self).littleEndian }

            // Validate RSSI is in reasonable range (-120 to 0 dBm)
            guard rssi >= -120 && rssi <= 0 else {
                print("[BLE] Skipping neighbor with invalid RSSI: \(rssi)")
                offset += entrySize
                continue
            }

            let snr = Int8(bitPattern: entryData[6])

            // Validate SNR is in reasonable range (-20 to 20 dB)
            guard snr >= -20 && snr <= 20 else {
                print("[BLE] Skipping neighbor with invalid SNR: \(snr)")
                offset += entrySize
                continue
            }

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

            newNeighbors.append(neighbor)
            offset += entrySize
        }

        // Only update if we got valid data
        guard !newNeighbors.isEmpty else {
            print("[BLE] No valid neighbors in data")
            return
        }

        // IMPORTANT: Merge new neighbors into existing list instead of replacing
        // This prevents flashing when firmware sends partial updates
        for newNeighbor in newNeighbors {
            if let index = neighbors.firstIndex(where: { $0.address == newNeighbor.address }) {
                // Update existing neighbor only if RSSI/SNR changed significantly
                if abs(neighbors[index].rssi - newNeighbor.rssi) > 3 ||
                   abs(Int16(neighbors[index].snr) - Int16(newNeighbor.snr)) > 1 {
                    neighbors[index] = newNeighbor
                }
            } else {
                // Add new neighbor
                neighbors.append(newNeighbor)
                print("[BLE] Added new neighbor: \(String(format: "0x%08X", newNeighbor.address))")
            }
        }

        // Prune neighbors not seen in last 5 minutes
        let fiveMinutesAgo = Date().addingTimeInterval(-300)
        let beforeCount = neighbors.count
        neighbors.removeAll { $0.lastSeen < fiveMinutesAgo }
        if neighbors.count != beforeCount {
            print("[BLE] Pruned \(beforeCount - neighbors.count) stale neighbors")
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

            // Filter to only LNK-22 devices
            // Check if device advertises LNK-22 service UUID or has LNK-22 in name
            let serviceUUIDs = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
            let hasLNK22Service = serviceUUIDs.contains(LNK22BLEService.serviceUUID)
            let hasLNK22Name = name.lowercased().contains("lnk") || name.lowercased().contains("mesh")

            // Only show LNK-22 compatible devices
            guard hasLNK22Service || hasLNK22Name else { return }

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
            print("[BLE] Connected to peripheral: \(peripheral.name ?? "unknown")")
            connectedPeripheral = peripheral
            peripheral.delegate = self
            connectionState = .connected

            // Find matching device
            if let device = discoveredDevices.first(where: { $0.peripheral.identifier == peripheral.identifier }) {
                connectedDevice = device
            }

            // Discover services
            print("[BLE] Starting service discovery...")
            discoverServices()
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        Task { @MainActor in
            let errorMsg = error?.localizedDescription ?? "Failed to connect"
            print("[BLE] ❌ Failed to connect: \(errorMsg)")
            lastError = errorMsg
            connectionState = .error
            cleanup()
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        Task { @MainActor in
            if let error = error {
                print("[BLE] ❌ Disconnected with error: \(error.localizedDescription)")
                lastError = error.localizedDescription
            } else {
                print("[BLE] Disconnected normally")
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
                print("[BLE] ❌ Service discovery error: \(error.localizedDescription)")
                lastError = error.localizedDescription
                connectionState = .error
                return
            }

            guard let services = peripheral.services else {
                print("[BLE] ❌ No services found on device")
                lastError = "No services found"
                connectionState = .error
                return
            }

            print("[BLE] ✅ Found \(services.count) services")
            var foundLNK22Service = false

            for service in services {
                print("[BLE]   - Service: \(service.uuid)")
                if service.uuid == LNK22BLEService.serviceUUID {
                    foundLNK22Service = true
                    print("[BLE] ✅ Found LNK-22 service! Discovering characteristics...")
                    // Discover all characteristics
                    peripheral.discoverCharacteristics(nil, for: service)
                }
            }

            if !foundLNK22Service {
                print("[BLE] ❌ LNK-22 service NOT found. Expected UUID: \(LNK22BLEService.serviceUUID)")
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
