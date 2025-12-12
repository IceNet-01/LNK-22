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
#if canImport(UIKit)
import UIKit
#endif

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

// MARK: - Standalone Mesh Service UUID
/// Used for phone-to-phone BLE mesh when no radio is connected
struct StandaloneMeshService {
    // Service UUID for phone-to-phone mesh
    static let serviceUUID = CBUUID(string: "4C4E1001-4B32-1000-8000-00805F9B34FB")

    // Characteristics for standalone mesh
    static let meshDataUUID = CBUUID(string: "4C4E1002-4B32-1000-8000-00805F9B34FB")   // Read/Write/Notify: Mesh data
    static let nodeInfoUUID = CBUUID(string: "4C4E1003-4B32-1000-8000-00805F9B34FB")   // Read: Node info (address, name)
    static let beaconUUID = CBUUID(string: "4C4E1004-4B32-1000-8000-00805F9B34FB")     // Notify: Beacon/heartbeat
}

// MARK: - Connection State

enum ConnectionState: String {
    case disconnected = "Disconnected"
    case scanning = "Scanning..."
    case connecting = "Connecting..."
    case connected = "Connected"
    case ready = "Ready"
    case error = "Error"
    case standaloneMode = "Standalone Mesh"  // New: phone acting as mesh node
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

// MARK: - Standalone Mesh Peer

/// Represents another phone running in standalone mesh mode
struct StandaloneMeshPeer: Identifiable, Hashable {
    let id: UUID
    let peripheral: CBPeripheral
    var nodeAddress: UInt32
    var nodeName: String?
    var rssi: Int
    var lastSeen: Date
    var isConnected: Bool

    var displayName: String {
        nodeName ?? String(format: "Node-%04X", nodeAddress & 0xFFFF)
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

    func hash(into hasher: inout Hasher) {
        hasher.combine(id)
    }

    static func == (lhs: StandaloneMeshPeer, rhs: StandaloneMeshPeer) -> Bool {
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

    // MAC/Radio status (v1.9.0)
    @Published var macStatus: MACStatus?
    @Published var radioStatus: RadioStatus?

    // Message handling
    @Published var receivedMessages: [MeshMessage] = []

    // Message deduplication - track recent message hashes to prevent duplicates
    private var recentMessageHashes: Set<Int> = []
    private var recentRawDataHashes: Set<Int> = []  // Track raw BLE data to catch exact duplicates
    private var recentlySentContent: Set<String> = []  // Track content we sent to ignore echo-back
    private var messageHashCleanupTimer: Timer?

    // MARK: - Standalone Mesh Mode Properties

    /// Whether the app is running in standalone mesh mode (no radio, phone-to-phone only)
    @Published var isStandaloneMeshMode = false

    /// Virtual node address for standalone mode (generated from device UUID)
    @Published var virtualNodeAddress: UInt32 = 0

    /// Discovered standalone mesh peers (other phones in mesh mode)
    @Published var standaloneMeshPeers: [StandaloneMeshPeer] = []

    // MARK: - Private Properties

    private var centralManager: CBCentralManager!
    private var connectedPeripheral: CBPeripheral?
    private var characteristics: [CBUUID: CBCharacteristic] = [:]

    // Standalone mesh mode peripherals
    private var peripheralManager: CBPeripheralManager?
    private var meshDataCharacteristic: CBMutableCharacteristic?
    private var nodeInfoCharacteristic: CBMutableCharacteristic?
    private var beaconCharacteristic: CBMutableCharacteristic?
    private var connectedStandalonePeers: [CBCentral] = []
    private var standalonePeerPeripherals: [UUID: CBPeripheral] = [:]
    private var standaloneScanTimer: Timer?

    // Callbacks
    var onMessageReceived: ((MeshMessage) -> Void)?
    var onStatusUpdate: ((DeviceStatus) -> Void)?

    // MARK: - Initialization

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: nil, queue: .main)
        centralManager.delegate = self

        // Generate virtual node address from device UUID
        generateVirtualNodeAddress()

        // Default to standalone mesh mode (auto-enabled when BT is ready)
        isStandaloneMeshMode = true
        connectionState = .standaloneMode
    }

    /// Called when Bluetooth becomes available - auto-start standalone mesh
    private func autoStartStandaloneMesh() {
        guard centralManager.state == .poweredOn else { return }

        // Initialize peripheral manager for advertising if needed
        if peripheralManager == nil {
            peripheralManager = CBPeripheralManager(delegate: self, queue: .main)
        }

        // Start scanning for mesh peers and radios
        startStandaloneMeshScan()
        print("[BLE-MESH] Auto-started standalone mesh mode")
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
        // Exit standalone mesh mode when connecting to a radio
        if isStandaloneMeshMode {
            isStandaloneMeshMode = false
            peripheralManager?.stopAdvertising()
            standaloneScanTimer?.invalidate()
        }
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

    // MARK: - Standalone Mesh Mode Methods

    /// Generate a virtual node address from the device UUID
    private func generateVirtualNodeAddress() {
        // Use device identifier to create a deterministic but unique address
        // This ensures the same device always gets the same virtual address
        #if canImport(UIKit) && !os(macOS)
        if let deviceUUID = UIDevice.current.identifierForVendor {
            let uuidString = deviceUUID.uuidString
            let hash = uuidString.hashValue
            // Use lower 32 bits, ensure it's in the valid range (not broadcast)
            virtualNodeAddress = UInt32(truncatingIfNeeded: abs(hash)) & 0x7FFFFFFF
            if virtualNodeAddress == 0 {
                virtualNodeAddress = 0x10000001  // Fallback if hash is 0
            }
            print("[BLE-MESH] Virtual node address: 0x\(String(format: "%08X", virtualNodeAddress))")
            return
        }
        #endif
        // Fallback for macOS or if UIDevice is not available
        let randomAddr = UInt32.random(in: 0x10000000...0x7FFFFFFF)
        virtualNodeAddress = randomAddr
        print("[BLE-MESH] Virtual node address (random): 0x\(String(format: "%08X", virtualNodeAddress))")
    }

    /// Enter standalone mesh mode (phone acts as a mesh node without connecting to a radio)
    func enterStandaloneMeshMode() {
        guard centralManager.state == .poweredOn else {
            lastError = "Bluetooth is not available"
            return
        }

        // Disconnect from any radio first
        disconnect()

        print("[BLE-MESH] Entering standalone mesh mode")
        isStandaloneMeshMode = true
        connectionState = .standaloneMode

        // Initialize peripheral manager for advertising
        if peripheralManager == nil {
            peripheralManager = CBPeripheralManager(delegate: nil, queue: .main)
        }

        // Start scanning for other phones in mesh mode
        startStandaloneMeshScan()
    }

    /// Exit standalone mesh mode
    func exitStandaloneMeshMode() {
        print("[BLE-MESH] Exiting standalone mesh mode")
        isStandaloneMeshMode = false

        // Stop advertising
        peripheralManager?.stopAdvertising()

        // Stop scanning
        standaloneScanTimer?.invalidate()
        standaloneScanTimer = nil
        centralManager.stopScan()

        // Disconnect from all peers
        for (_, peripheral) in standalonePeerPeripherals {
            centralManager.cancelPeripheralConnection(peripheral)
        }
        standalonePeerPeripherals.removeAll()
        standaloneMeshPeers.removeAll()
        connectedStandalonePeers.removeAll()

        connectionState = .disconnected
    }

    /// Start scanning for other phones AND LNK-22 radios in standalone mesh mode
    private func startStandaloneMeshScan() {
        guard isStandaloneMeshMode else { return }

        print("[BLE-MESH] Scanning for mesh peers and radios...")

        // Scan for BOTH standalone mesh peers (phones) AND LNK-22 radios
        // Using nil services to discover all nearby BLE devices, then filter
        centralManager.scanForPeripherals(
            withServices: nil,  // Scan for all devices
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: true]
        )

        // Periodically refresh scan and prune stale peers
        standaloneScanTimer?.invalidate()
        standaloneScanTimer = Timer.scheduledTimer(withTimeInterval: 10, repeats: true) { [weak self] _ in
            Task { @MainActor in
                self?.pruneStaleStandalonePeers()
                // Re-scan to keep discovering devices
                if let self = self, self.isStandaloneMeshMode {
                    self.centralManager.scanForPeripherals(
                        withServices: nil,
                        options: [CBCentralManagerScanOptionAllowDuplicatesKey: true]
                    )
                }
            }
        }
    }

    /// Setup the BLE peripheral service for advertising in standalone mode
    private func setupStandaloneMeshService() {
        guard let peripheralManager = peripheralManager,
              peripheralManager.state == .poweredOn else {
            print("[BLE-MESH] Peripheral manager not ready")
            return
        }

        print("[BLE-MESH] Setting up mesh service...")

        // Create mesh data characteristic (read/write/notify)
        meshDataCharacteristic = CBMutableCharacteristic(
            type: StandaloneMeshService.meshDataUUID,
            properties: [.read, .write, .writeWithoutResponse, .notify],
            value: nil,
            permissions: [.readable, .writeable]
        )

        // Create node info characteristic (read)
        let nodeInfoData = buildNodeInfoData()
        nodeInfoCharacteristic = CBMutableCharacteristic(
            type: StandaloneMeshService.nodeInfoUUID,
            properties: [.read],
            value: nodeInfoData,
            permissions: [.readable]
        )

        // Create beacon characteristic (notify)
        beaconCharacteristic = CBMutableCharacteristic(
            type: StandaloneMeshService.beaconUUID,
            properties: [.notify],
            value: nil,
            permissions: [.readable]
        )

        // Create the service
        let meshService = CBMutableService(
            type: StandaloneMeshService.serviceUUID,
            primary: true
        )
        meshService.characteristics = [meshDataCharacteristic!, nodeInfoCharacteristic!, beaconCharacteristic!]

        // Add service and start advertising
        peripheralManager.add(meshService)
    }

    /// Build node info data for the characteristic
    private func buildNodeInfoData() -> Data {
        var data = Data()

        // Node address (4 bytes)
        var addr = virtualNodeAddress.littleEndian
        data.append(Data(bytes: &addr, count: 4))

        // Node name (up to 16 bytes)
        let name = nodeName ?? "iOS-\(String(format: "%04X", virtualNodeAddress & 0xFFFF))"
        if let nameData = name.data(using: .utf8) {
            let trimmedData = nameData.prefix(16)
            data.append(UInt8(trimmedData.count))
            data.append(trimmedData)
        } else {
            data.append(0)  // Empty name
        }

        return data
    }

    /// Start advertising in standalone mesh mode
    private func startStandaloneMeshAdvertising() {
        guard let peripheralManager = peripheralManager,
              peripheralManager.state == .poweredOn else {
            return
        }

        let advertisementData: [String: Any] = [
            CBAdvertisementDataServiceUUIDsKey: [StandaloneMeshService.serviceUUID],
            CBAdvertisementDataLocalNameKey: "LNK-22-\(String(format: "%04X", virtualNodeAddress & 0xFFFF))"
        ]

        peripheralManager.startAdvertising(advertisementData)
        print("[BLE-MESH] Started advertising as mesh node")
    }

    /// Prune peers not seen recently
    private func pruneStaleStandalonePeers() {
        let cutoffTime = Date().addingTimeInterval(-120)  // 2 minutes
        standaloneMeshPeers.removeAll { $0.lastSeen < cutoffTime && !$0.isConnected }
    }

    /// Send a message in standalone mesh mode
    func sendStandaloneMeshMessage(to destination: UInt32, text: String) {
        guard isStandaloneMeshMode else {
            lastError = "Not in standalone mesh mode"
            return
        }

        // Track this content so we can ignore echoes from radios
        recentlySentContent.insert(text)

        // Build message packet for phone-to-phone mesh
        var meshData = Data()
        meshData.append(0x01)  // Text message type
        var src = virtualNodeAddress.littleEndian
        meshData.append(Data(bytes: &src, count: 4))
        var dest = destination.littleEndian
        meshData.append(Data(bytes: &dest, count: 4))
        var timestamp = UInt32(Date().timeIntervalSince1970).littleEndian
        meshData.append(Data(bytes: &timestamp, count: 4))
        if let textData = text.data(using: .utf8) {
            meshData.append(textData)
        }

        // Build message packet for LNK-22 radios (different format)
        var radioData = Data()
        radioData.append(0x01)  // Text message type
        radioData.append(Data(bytes: &dest, count: 4))  // Destination
        radioData.append(0x00)  // Channel 0
        if let textData = text.data(using: .utf8) {
            radioData.append(textData)
        }

        var sentToRadio = false

        // Send to connected peripherals - but only ONE radio to prevent chaos!
        // Multiple radios broadcasting the same message causes network spam.
        var radioUsed: String? = nil

        for (_, peripheral) in standalonePeerPeripherals {
            // Try LNK-22 radio characteristic - but only if we haven't already sent via a radio
            if !sentToRadio,
               let service = peripheral.services?.first(where: { $0.uuid == LNK22BLEService.serviceUUID }),
               let msgChar = service.characteristics?.first(where: { $0.uuid == LNK22BLEService.messageRxUUID }) {
                peripheral.writeValue(radioData, for: msgChar, type: .withResponse)
                sentToRadio = true
                radioUsed = peripheral.name ?? "unknown"
                print("[BLE-MESH] Sent via radio: \(radioUsed!) (only using one radio to prevent spam)")
            }
            // Also try standalone mesh characteristic (for other phones) - send to ALL phones
            else if let service = peripheral.services?.first(where: { $0.uuid == StandaloneMeshService.serviceUUID }),
                    let meshChar = service.characteristics?.first(where: { $0.uuid == StandaloneMeshService.meshDataUUID }) {
                peripheral.writeValue(meshData, for: meshChar, type: .withResponse)
                print("[BLE-MESH] Sent via phone mesh: \(peripheral.name ?? "unknown")")
            }
        }

        // Also broadcast to subscribed centrals (other phones connected to us)
        if let characteristic = meshDataCharacteristic {
            peripheralManager?.updateValue(meshData, for: characteristic, onSubscribedCentrals: nil)
        }

        // Mark this message as sent so we don't receive it as a duplicate
        _ = isDuplicateMessage(source: virtualNodeAddress, content: text, timestamp: Date())

        let targetDesc = destination == 0xFFFFFFFF ? "broadcast" : String(format: "0x%08X", destination)
        print("[BLE-MESH] Sent message to \(targetDesc) (via radio: \(sentToRadio))")
    }

    /// Send a broadcast message in standalone mesh mode
    func sendStandaloneMeshBroadcast(text: String) {
        sendStandaloneMeshMessage(to: 0xFFFFFFFF, text: text)
    }

    /// Send a message to a destination address
    func sendMessage(to destination: UInt32, text: String, channel: UInt8 = 0) {
        // In standalone mesh mode, use the standalone message function
        if isStandaloneMeshMode {
            sendStandaloneMeshMessage(to: destination, text: text)
            return
        }

        guard connectionState == .ready,
              let characteristic = characteristics[LNK22BLEService.messageRxUUID] else {
            lastError = "Not connected to device"
            return
        }

        // Track this content so we can ignore echoes from radio
        recentlySentContent.insert(text)

        // Build message packet
        // New format: [type:1][source:4][destination:4][channel:1][payload:N]
        var data = Data()

        // Message type (1 byte) - 0x01 = text message
        data.append(0x01)

        // Source address (4 bytes, little-endian) - our virtual node address
        var src = virtualNodeAddress.littleEndian
        data.append(Data(bytes: &src, count: 4))

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
        isPaired = false
        isPairingRequired = false
        nodeName = nil

        // Auto-return to standalone mesh mode after radio disconnect
        isStandaloneMeshMode = true
        connectionState = .standaloneMode
        if centralManager.state == .poweredOn {
            autoStartStandaloneMesh()
        }
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

    /// Generate a hash for message deduplication
    private func messageHash(source: UInt32, content: String, timestamp: Date) -> Int {
        var hasher = Hasher()
        hasher.combine(source)
        hasher.combine(content)
        // Round timestamp to nearest 5 seconds to account for slight timing differences
        let roundedTime = Int(timestamp.timeIntervalSince1970 / 5) * 5
        hasher.combine(roundedTime)
        return hasher.finalize()
    }

    /// Check if a message is a duplicate and add to tracking if not
    private func isDuplicateMessage(source: UInt32, content: String, timestamp: Date) -> Bool {
        let hash = messageHash(source: source, content: content, timestamp: timestamp)

        if recentMessageHashes.contains(hash) {
            print("[BLE] Duplicate message detected, ignoring")
            return true
        }

        recentMessageHashes.insert(hash)

        // Start cleanup timer if not running (clear old hashes after 30 seconds)
        if messageHashCleanupTimer == nil {
            messageHashCleanupTimer = Timer.scheduledTimer(withTimeInterval: 30, repeats: true) { [weak self] _ in
                Task { @MainActor in
                    self?.recentMessageHashes.removeAll()
                    self?.recentRawDataHashes.removeAll()
                    self?.recentlySentContent.removeAll()
                }
            }
        }

        return false
    }

    private func parseReceivedMessage(_ data: Data) {
        print("[BLE] parseReceivedMessage: \(data.count) bytes")

        // Early dedup: Check if we've seen this exact raw data recently
        let rawHash = data.hashValue
        if recentRawDataHashes.contains(rawHash) {
            print("[BLE] Duplicate raw data detected, ignoring")
            return
        }
        recentRawDataHashes.insert(rawHash)

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

        let messageTimestamp = Date(timeIntervalSince1970: TimeInterval(timestamp))

        // Check for duplicate messages (prevents showing same message twice)
        if isDuplicateMessage(source: source, content: content, timestamp: messageTimestamp) {
            return
        }

        // Also skip if this is our own message echoed back (by source address)
        if source == virtualNodeAddress || (deviceStatus != nil && source == deviceStatus!.nodeAddress) {
            print("[BLE] Ignoring own message echo (source address match)")
            return
        }

        // Skip if this is content we recently sent (echo from radio network)
        if recentlySentContent.contains(content) {
            print("[BLE] Ignoring echoed content: '\(content)'")
            return
        }

        let message = MeshMessage(
            id: UUID(),
            source: source,
            destination: 0xFFFFFFFF, // Will be updated by firmware
            channel: channel,
            type: MessageType(rawValue: type) ?? .text,
            content: content,
            timestamp: messageTimestamp,
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
                // Auto-start standalone mesh mode when Bluetooth becomes available
                if isStandaloneMeshMode {
                    autoStartStandaloneMesh()
                }
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

            // Filter to only LNK-22 devices or standalone mesh peers
            let serviceUUIDs = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
            let hasLNK22Service = serviceUUIDs.contains(LNK22BLEService.serviceUUID)
            let hasStandaloneMeshService = serviceUUIDs.contains(StandaloneMeshService.serviceUUID)
            let hasLNK22Name = name.lowercased().contains("lnk") || name.lowercased().contains("mesh") || name.lowercased().contains("rak")

            // In standalone mesh mode, treat BOTH phones AND radios as mesh peers
            if isStandaloneMeshMode {
                // Check if this is a mesh-compatible device (phone or radio)
                let isMeshDevice = hasStandaloneMeshService || hasLNK22Service || hasLNK22Name

                if isMeshDevice {
                    // Update or add to mesh peers list
                    if let index = standaloneMeshPeers.firstIndex(where: { $0.id == peripheral.identifier }) {
                        standaloneMeshPeers[index].rssi = RSSI.intValue
                        standaloneMeshPeers[index].lastSeen = Date()
                    } else {
                        // Generate node address from name or peripheral identifier
                        var nodeAddr: UInt32 = 0
                        if let match = name.range(of: "([0-9A-Fa-f]{4})$", options: .regularExpression) {
                            let hexStr = String(name[match])
                            nodeAddr = UInt32(hexStr, radix: 16) ?? 0
                        }
                        if nodeAddr == 0 {
                            // Generate from peripheral UUID
                            let hash = peripheral.identifier.uuidString.hashValue
                            nodeAddr = UInt32(truncatingIfNeeded: abs(hash)) & 0x7FFFFFFF
                        }

                        let isRadio = hasLNK22Service || hasLNK22Name
                        let peerName = isRadio ? "📻 \(name)" : "📱 \(name)"

                        let peer = StandaloneMeshPeer(
                            id: peripheral.identifier,
                            peripheral: peripheral,
                            nodeAddress: nodeAddr,
                            nodeName: peerName,
                            rssi: RSSI.intValue,
                            lastSeen: Date(),
                            isConnected: false
                        )
                        standaloneMeshPeers.append(peer)
                        print("[BLE-MESH] Discovered \(isRadio ? "radio" : "phone"): \(name) (0x\(String(format: "%08X", nodeAddr)))")

                        // Auto-connect to LNK-22 radios to enable message relay
                        if isRadio && !standalonePeerPeripherals.keys.contains(peripheral.identifier) {
                            centralManager.connect(peripheral, options: nil)
                            standalonePeerPeripherals[peripheral.identifier] = peripheral
                        }
                    }
                }

                // Also add radios to discoveredDevices for the connect list
                if hasLNK22Service || hasLNK22Name {
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
                return
            }

            // Not in standalone mode - only show LNK-22 compatible devices
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

            // Check if this is a standalone mesh peer (including radios in standalone mode)
            if standalonePeerPeripherals[peripheral.identifier] != nil {
                print("[BLE-MESH] Connected to mesh peer: \(peripheral.name ?? "unknown")")
                peripheral.delegate = self

                // Update peer status
                if let index = standaloneMeshPeers.firstIndex(where: { $0.id == peripheral.identifier }) {
                    standaloneMeshPeers[index].isConnected = true
                }

                // Discover BOTH LNK-22 service (for radios) AND standalone mesh service (for phones)
                peripheral.discoverServices([LNK22BLEService.serviceUUID, StandaloneMeshService.serviceUUID])
                return
            }

            // Regular LNK-22 radio connection (not in standalone mode)
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
                if !isStandaloneMeshMode {
                    connectionState = .error
                }
                return
            }

            guard let services = peripheral.services else {
                print("[BLE] ❌ No services found on device")
                if !isStandaloneMeshMode {
                    lastError = "No services found"
                    connectionState = .error
                }
                return
            }

            print("[BLE] ✅ Found \(services.count) services on \(peripheral.name ?? "unknown")")
            var foundAnyService = false

            for service in services {
                print("[BLE]   - Service: \(service.uuid)")

                // Discover characteristics for LNK-22 service
                if service.uuid == LNK22BLEService.serviceUUID {
                    foundAnyService = true
                    print("[BLE] ✅ Found LNK-22 service! Discovering characteristics...")
                    peripheral.discoverCharacteristics(nil, for: service)
                }

                // Discover characteristics for StandaloneMesh service (phone-to-phone)
                if service.uuid == StandaloneMeshService.serviceUUID {
                    foundAnyService = true
                    print("[BLE] ✅ Found StandaloneMesh service! Discovering characteristics...")
                    peripheral.discoverCharacteristics(nil, for: service)
                }
            }

            if !foundAnyService && !isStandaloneMeshMode {
                print("[BLE] ❌ No compatible service found. Expected LNK-22 or StandaloneMesh service")
                lastError = "Compatible service not found. Is this an LNK-22 device?"
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

            print("[BLE] Found \(chars.count) characteristics in service \(service.uuid)")

            // Check if this is a standalone mesh peer (including radios in standalone mode)
            let isStandalonePeer = standalonePeerPeripherals[peripheral.identifier] != nil

            for characteristic in chars {
                print("[BLE]   - Characteristic: \(characteristic.uuid)")

                if isStandalonePeer || isStandaloneMeshMode {
                    // In standalone mode, subscribe to message notifications from radios
                    if characteristic.uuid == LNK22BLEService.messageTxUUID {
                        print("[BLE-MESH] Subscribing to message notifications from radio")
                        peripheral.setNotifyValue(true, for: characteristic)
                    }
                    // Subscribe to standalone mesh data notifications
                    if characteristic.uuid == StandaloneMeshService.meshDataUUID {
                        print("[BLE-MESH] Subscribing to mesh data notifications")
                        peripheral.setNotifyValue(true, for: characteristic)
                    }
                } else {
                    // Normal connection mode - store characteristics
                    characteristics[characteristic.uuid] = characteristic
                }
            }

            // Only set ready state for non-standalone connections
            if !isStandalonePeer && !isStandaloneMeshMode {
                connectionState = .ready
                isPaired = true
                print("[BLE] Connection ready - \(characteristics.count) characteristics stored")

                // Subscribe to notifications
                subscribeToNotifications()

                // Request initial status
                requestStatus()
                requestNeighbors()
                requestRoutes()
                requestNodeName()
            } else {
                print("[BLE-MESH] Standalone peer ready for messaging: \(peripheral.name ?? "unknown")")
            }
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

            case StandaloneMeshService.meshDataUUID:
                // Handle standalone mesh data (from other phones)
                handleStandaloneMeshData(data)

            default:
                print("[BLE] Unknown characteristic update: \(characteristic.uuid)")
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

// MARK: - CBPeripheralManagerDelegate (for Standalone Mesh Mode)

extension BluetoothManager: CBPeripheralManagerDelegate {
    nonisolated func peripheralManagerDidUpdateState(_ peripheral: CBPeripheralManager) {
        Task { @MainActor in
            switch peripheral.state {
            case .poweredOn:
                print("[BLE-MESH] Peripheral manager powered on")
                if isStandaloneMeshMode {
                    setupStandaloneMeshService()
                }
            case .poweredOff:
                print("[BLE-MESH] Peripheral manager powered off")
            default:
                break
            }
        }
    }

    nonisolated func peripheralManager(_ peripheral: CBPeripheralManager, didAdd service: CBService, error: Error?) {
        Task { @MainActor in
            if let error = error {
                print("[BLE-MESH] Failed to add service: \(error.localizedDescription)")
                return
            }
            print("[BLE-MESH] Service added successfully")
            startStandaloneMeshAdvertising()
        }
    }

    nonisolated func peripheralManagerDidStartAdvertising(_ peripheral: CBPeripheralManager, error: Error?) {
        Task { @MainActor in
            if let error = error {
                print("[BLE-MESH] Failed to start advertising: \(error.localizedDescription)")
                lastError = "Failed to advertise: \(error.localizedDescription)"
            } else {
                print("[BLE-MESH] Advertising started successfully")
            }
        }
    }

    nonisolated func peripheralManager(_ peripheral: CBPeripheralManager, central: CBCentral, didSubscribeTo characteristic: CBCharacteristic) {
        Task { @MainActor in
            print("[BLE-MESH] Central \(central.identifier) subscribed to \(characteristic.uuid)")
            if !connectedStandalonePeers.contains(where: { $0.identifier == central.identifier }) {
                connectedStandalonePeers.append(central)
            }
        }
    }

    nonisolated func peripheralManager(_ peripheral: CBPeripheralManager, central: CBCentral, didUnsubscribeFrom characteristic: CBCharacteristic) {
        Task { @MainActor in
            print("[BLE-MESH] Central \(central.identifier) unsubscribed from \(characteristic.uuid)")
            connectedStandalonePeers.removeAll { $0.identifier == central.identifier }
        }
    }

    nonisolated func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveWrite requests: [CBATTRequest]) {
        Task { @MainActor in
            for request in requests {
                if request.characteristic.uuid == StandaloneMeshService.meshDataUUID {
                    if let data = request.value {
                        handleStandaloneMeshData(data)
                    }
                    peripheral.respond(to: request, withResult: .success)
                } else {
                    peripheral.respond(to: request, withResult: .attributeNotFound)
                }
            }
        }
    }

    nonisolated func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveRead request: CBATTRequest) {
        Task { @MainActor in
            if request.characteristic.uuid == StandaloneMeshService.nodeInfoUUID {
                let data = buildNodeInfoData()
                request.value = data
                peripheral.respond(to: request, withResult: .success)
            } else {
                peripheral.respond(to: request, withResult: .attributeNotFound)
            }
        }
    }
}

// MARK: - Standalone Mesh Data Handling

extension BluetoothManager {
    /// Handle incoming mesh data from standalone peers
    func handleStandaloneMeshData(_ data: Data) {
        guard data.count >= 13 else {
            print("[BLE-MESH] Received data too short: \(data.count) bytes")
            return
        }

        // Parse: [type:1][source:4][destination:4][timestamp:4][payload...]
        let messageType = data[0]
        let source = data.subdata(in: 1..<5).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        let destination = data.subdata(in: 5..<9).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        let timestamp = data.subdata(in: 9..<13).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        let payload = data.subdata(in: 13..<data.count)

        // Skip our own messages
        guard source != virtualNodeAddress else { return }

        // Check if message is for us or broadcast
        guard destination == virtualNodeAddress || destination == 0xFFFFFFFF else {
            // Forward message to other peers (mesh relay)
            relayStandaloneMeshMessage(data)
            return
        }

        if messageType == 0x01, let content = String(data: payload, encoding: .utf8) {
            let messageTimestamp = Date(timeIntervalSince1970: TimeInterval(timestamp))

            // Check for duplicate messages
            if isDuplicateMessage(source: source, content: content, timestamp: messageTimestamp) {
                return
            }

            let message = MeshMessage(
                id: UUID(),
                source: source,
                destination: destination,
                channel: 0,
                type: .text,
                content: content,
                timestamp: messageTimestamp,
                rssi: nil,
                snr: nil
            )

            receivedMessages.append(message)
            onMessageReceived?(message)
            print("[BLE-MESH] Received message from 0x\(String(format: "%08X", source)): \(content)")
        }

        // Also relay to connected radios if any
        relayToConnectedRadios(data)
    }

    /// Relay message to other standalone mesh peers
    private func relayStandaloneMeshMessage(_ data: Data) {
        // Broadcast to all subscribed centrals
        if let characteristic = meshDataCharacteristic {
            peripheralManager?.updateValue(data, for: characteristic, onSubscribedCentrals: nil)
        }

        // Write to connected peripherals
        for (_, peripheral) in standalonePeerPeripherals {
            if let service = peripheral.services?.first(where: { $0.uuid == StandaloneMeshService.serviceUUID }),
               let meshChar = service.characteristics?.first(where: { $0.uuid == StandaloneMeshService.meshDataUUID }) {
                peripheral.writeValue(data, for: meshChar, type: .withoutResponse)
            }
        }
    }

    /// Relay message to any connected LNK-22 radios (bridge phone mesh to radio mesh)
    private func relayToConnectedRadios(_ data: Data) {
        guard connectionState == .ready,
              let characteristic = characteristics[LNK22BLEService.messageRxUUID] else {
            return
        }

        // Forward the message to the connected radio
        connectedPeripheral?.writeValue(data, for: characteristic, type: .withResponse)
        print("[BLE-MESH] Relayed message to connected radio")
    }

    /// Connect to an LNK-22 radio while in standalone mesh mode
    /// This allows the phone to bridge the BLE mesh to the radio mesh
    func connectToRadioInMeshMode(device: DiscoveredDevice) {
        print("[BLE-MESH] Connecting to radio \(device.name) while in mesh mode")
        centralManager.connect(device.peripheral, options: nil)
    }

    /// Scan for both standalone peers AND LNK-22 radios
    func scanForAllMeshDevices() {
        guard centralManager.state == .poweredOn else { return }

        print("[BLE-MESH] Scanning for mesh peers and radios...")
        centralManager.scanForPeripherals(
            withServices: [StandaloneMeshService.serviceUUID, LNK22BLEService.serviceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
    }
}
