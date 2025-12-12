//
//  NetworkView.swift
//  LNK22
//
//  View for displaying network status, neighbors, and routing information
//

import SwiftUI

struct NetworkView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager
    @State private var selectedSection = 0

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                // Device Status Card - show for connected radio OR standalone mode
                if bluetoothManager.connectionState == .ready,
                   let status = bluetoothManager.deviceStatus {
                    DeviceStatusCard(status: status)
                } else if bluetoothManager.isStandaloneMeshMode {
                    StandaloneStatusCard(
                        nodeAddress: bluetoothManager.virtualNodeAddress,
                        peerCount: bluetoothManager.standaloneMeshPeers.count,
                        connectedCount: bluetoothManager.standaloneMeshPeers.filter { $0.isConnected }.count
                    )
                }

                // Section Picker
                Picker("Section", selection: $selectedSection) {
                    Text("Neighbors").tag(0)
                    Text("Routes").tag(1)
                    Text("Stats").tag(2)
                }
                .pickerStyle(.segmented)
                .padding()

                // Content
                switch selectedSection {
                case 0:
                    if bluetoothManager.isStandaloneMeshMode {
                        StandalonePeersListView()
                    } else {
                        NeighborsListView()
                    }
                case 1:
                    RoutesListView()
                case 2:
                    if bluetoothManager.isStandaloneMeshMode {
                        StandaloneStatsView()
                    } else {
                        StatisticsView()
                    }
                default:
                    EmptyView()
                }
            }
            .navigationTitle("Network")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button(action: refreshNetwork) {
                        Image(systemName: "arrow.clockwise")
                    }
                }
            }
        }
    }

    private func refreshNetwork() {
        if bluetoothManager.isStandaloneMeshMode {
            // In standalone mode, trigger a fresh scan
            bluetoothManager.startScanning()
        } else {
            bluetoothManager.requestStatus()
            bluetoothManager.requestNeighbors()
            bluetoothManager.requestRoutes()
        }
    }
}

// MARK: - Device Status Card

struct DeviceStatusCard: View {
    let status: DeviceStatus

    var body: some View {
        VStack(spacing: 12) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Node ID")
                        .font(.caption)
                        .foregroundColor(.secondary)
                    Text(status.nodeAddressHex)
                        .font(.headline)
                        .fontWeight(.bold)
                }

                Spacer()

                VStack(alignment: .trailing, spacing: 4) {
                    Text("Channel")
                        .font(.caption)
                        .foregroundColor(.secondary)
                    Text("CH\(status.currentChannel)")
                        .font(.headline)
                        .fontWeight(.bold)
                }
            }

            Divider()

            HStack(spacing: 20) {
                StatusItem(
                    icon: "arrow.up.circle",
                    label: "TX",
                    value: "\(status.txCount)"
                )

                StatusItem(
                    icon: "arrow.down.circle",
                    label: "RX",
                    value: "\(status.rxCount)"
                )

                StatusItem(
                    icon: "person.2",
                    label: "Neighbors",
                    value: "\(status.neighborCount)"
                )

                StatusItem(
                    icon: "arrow.triangle.branch",
                    label: "Routes",
                    value: "\(status.routeCount)"
                )
            }

            Divider()

            HStack {
                // Battery
                HStack(spacing: 4) {
                    Image(systemName: batteryIcon(for: status.batteryPercent))
                        .foregroundColor(batteryColor(for: status.batteryPercent))
                    Text("\(status.batteryPercent)%")
                        .font(.caption)
                }

                Spacer()

                // TX Power
                HStack(spacing: 4) {
                    Image(systemName: "antenna.radiowaves.left.and.right")
                    Text("\(status.txPower) dBm")
                        .font(.caption)
                }

                Spacer()

                // Uptime
                HStack(spacing: 4) {
                    Image(systemName: "clock")
                    Text(status.uptimeFormatted)
                        .font(.caption)
                }

                Spacer()

                // Features
                HStack(spacing: 8) {
                    if status.isEncryptionEnabled {
                        Image(systemName: "lock.fill")
                            .foregroundColor(.green)
                            .font(.caption)
                    }
                    if status.isGPSEnabled {
                        Image(systemName: "location.fill")
                            .foregroundColor(.blue)
                            .font(.caption)
                    }
                }
            }
            .font(.caption)
            .foregroundColor(.secondary)
        }
        .padding()
        .background(Color(.systemGray6))
        .cornerRadius(12)
        .padding()
    }

    private func batteryIcon(for percent: UInt8) -> String {
        switch percent {
        case 0..<25: return "battery.25"
        case 25..<50: return "battery.50"
        case 50..<75: return "battery.75"
        default: return "battery.100"
        }
    }

    private func batteryColor(for percent: UInt8) -> Color {
        switch percent {
        case 0..<20: return .red
        case 20..<50: return .orange
        default: return .green
        }
    }
}

struct StatusItem: View {
    let icon: String
    let label: String
    let value: String

    var body: some View {
        VStack(spacing: 4) {
            Image(systemName: icon)
                .font(.title3)
                .foregroundColor(.accentColor)

            Text(value)
                .font(.headline)
                .fontWeight(.semibold)

            Text(label)
                .font(.caption2)
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity)
    }
}

// MARK: - Neighbors List

struct NeighborsListView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager

    var body: some View {
        if bluetoothManager.neighbors.isEmpty {
            emptyState
        } else {
            List(bluetoothManager.neighbors) { neighbor in
                NeighborRow(neighbor: neighbor, nodeName: bluetoothManager.nodeNames[neighbor.address])
            }
            .listStyle(.plain)
        }
    }

    private var emptyState: some View {
        VStack(spacing: 16) {
            Spacer()
            Image(systemName: "person.2.slash")
                .font(.system(size: 48))
                .foregroundColor(.secondary)
            Text("No Neighbors")
                .font(.title3)
                .fontWeight(.medium)
            Text("No neighboring nodes have been discovered yet")
                .font(.subheadline)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
            Spacer()
        }
        .padding()
    }
}

struct NeighborRow: View {
    let neighbor: Neighbor
    let nodeName: String?

    var displayName: String {
        nodeName ?? neighbor.friendlyName
    }

    var body: some View {
        HStack(spacing: 12) {
            // Signal indicator
            ZStack {
                Circle()
                    .fill(neighbor.signalQuality.color.opacity(0.2))
                    .frame(width: 44, height: 44)

                Image(systemName: neighbor.signalQuality.icon)
                    .font(.title3)
                    .foregroundColor(neighbor.signalQuality.color)
            }

            VStack(alignment: .leading, spacing: 4) {
                Text(displayName)
                    .font(.headline)

                HStack(spacing: 8) {
                    Label("\(neighbor.rssi) dBm", systemImage: "antenna.radiowaves.left.and.right")
                    Label("SNR: \(neighbor.snr)", systemImage: "waveform")
                }
                .font(.caption)
                .foregroundColor(.secondary)
            }

            Spacer()

            VStack(alignment: .trailing, spacing: 4) {
                Text("\(neighbor.qualityPercent)%")
                    .font(.headline)
                    .foregroundColor(neighbor.signalQuality.color)

                Text(neighbor.timeSinceLastSeen)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .padding(.vertical, 4)
    }
}

// MARK: - Routes List

struct RoutesListView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager

    var body: some View {
        if bluetoothManager.routes.isEmpty {
            emptyState
        } else {
            List(bluetoothManager.routes) { route in
                RouteRow(route: route)
            }
            .listStyle(.plain)
        }
    }

    private var emptyState: some View {
        VStack(spacing: 16) {
            Spacer()
            Image(systemName: "arrow.triangle.branch")
                .font(.system(size: 48))
                .foregroundColor(.secondary)
            Text("No Routes")
                .font(.title3)
                .fontWeight(.medium)
            Text("Routing table is empty")
                .font(.subheadline)
                .foregroundColor(.secondary)
            Spacer()
        }
        .padding()
    }
}

struct RouteRow: View {
    let route: RouteEntry

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Destination")
                    .font(.caption)
                    .foregroundColor(.secondary)
                Spacer()
                Text(route.destinationHex)
                    .font(.subheadline)
                    .fontWeight(.medium)
            }

            HStack {
                Text("Next Hop")
                    .font(.caption)
                    .foregroundColor(.secondary)
                Spacer()
                Text(route.nextHopHex)
                    .font(.subheadline)
            }

            HStack {
                Label("\(route.hopCount) hops", systemImage: "arrow.right.arrow.left")
                    .font(.caption)
                    .foregroundColor(.secondary)

                Spacer()

                Text("Quality: \(route.qualityPercent)%")
                    .font(.caption)
                    .foregroundColor(route.qualityPercent > 50 ? .green : .orange)
            }
        }
        .padding(.vertical, 4)
    }
}

// MARK: - Statistics View

struct StatisticsView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager

    var body: some View {
        if let status = bluetoothManager.deviceStatus {
            List {
                Section("Packets") {
                    StatRow(label: "Transmitted", value: "\(status.txCount)")
                    StatRow(label: "Received", value: "\(status.rxCount)")
                    StatRow(label: "Success Rate", value: successRate)
                }

                Section("Network") {
                    StatRow(label: "Neighbors", value: "\(status.neighborCount)")
                    StatRow(label: "Known Routes", value: "\(status.routeCount)")
                    StatRow(label: "Current Channel", value: "CH\(status.currentChannel)")
                }

                Section("Radio") {
                    StatRow(label: "TX Power", value: "\(status.txPower) dBm")
                    StatRow(label: "Frequency", value: "915 MHz")
                    StatRow(label: "Bandwidth", value: "125 kHz")
                    StatRow(label: "Spreading Factor", value: "SF10")
                }

                Section("Device") {
                    StatRow(label: "Firmware", value: status.firmwareVersion)
                    StatRow(label: "Uptime", value: status.uptimeFormatted)
                    StatRow(label: "Battery", value: "\(status.batteryPercent)%")
                }

                Section("Features") {
                    StatRow(
                        label: "Encryption",
                        value: status.isEncryptionEnabled ? "Enabled" : "Disabled",
                        color: status.isEncryptionEnabled ? .green : .orange
                    )
                    StatRow(
                        label: "GPS",
                        value: status.isGPSEnabled ? "Enabled" : "Disabled",
                        color: status.isGPSEnabled ? .green : .gray
                    )
                }
            }
            .listStyle(.insetGrouped)
        } else {
            VStack {
                Spacer()
                Text("Connect to a device to view statistics")
                    .foregroundColor(.secondary)
                Spacer()
            }
        }
    }

    private var successRate: String {
        guard let status = bluetoothManager.deviceStatus else { return "N/A" }
        let total = status.txCount + status.rxCount
        guard total > 0 else { return "N/A" }
        return String(format: "%.1f%%", Double(status.rxCount) / Double(total) * 100)
    }
}

struct StatRow: View {
    let label: String
    let value: String
    var color: Color = .primary

    var body: some View {
        HStack {
            Text(label)
                .foregroundColor(.secondary)
            Spacer()
            Text(value)
                .fontWeight(.medium)
                .foregroundColor(color)
        }
    }
}

// MARK: - Standalone Mode Status Card

struct StandaloneStatusCard: View {
    let nodeAddress: UInt32
    let peerCount: Int
    let connectedCount: Int

    var body: some View {
        VStack(spacing: 12) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Standalone Mode")
                        .font(.caption)
                        .foregroundColor(.blue)
                    Text(String(format: "Node-%04X", nodeAddress & 0xFFFF))
                        .font(.headline)
                        .fontWeight(.bold)
                }

                Spacer()

                VStack(alignment: .trailing, spacing: 4) {
                    Text("Mode")
                        .font(.caption)
                        .foregroundColor(.secondary)
                    HStack(spacing: 4) {
                        Image(systemName: "iphone.radiowaves.left.and.right")
                            .foregroundColor(.blue)
                        Text("BLE Mesh")
                            .font(.headline)
                            .fontWeight(.bold)
                    }
                }
            }

            Divider()

            HStack(spacing: 20) {
                StatusItem(
                    icon: "antenna.radiowaves.left.and.right",
                    label: "Discovered",
                    value: "\(peerCount)"
                )

                StatusItem(
                    icon: "link",
                    label: "Connected",
                    value: "\(connectedCount)"
                )

                StatusItem(
                    icon: "iphone.gen2",
                    label: "Phones",
                    value: "\(peerCount)"
                )

                StatusItem(
                    icon: "dot.radiowaves.left.and.right",
                    label: "Radios",
                    value: "\(connectedCount)"
                )
            }
        }
        .padding()
        .background(Color.blue.opacity(0.1))
        .cornerRadius(12)
        .padding()
    }
}

// MARK: - Standalone Peers List

struct StandalonePeersListView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager

    var body: some View {
        if bluetoothManager.standaloneMeshPeers.isEmpty {
            emptyState
        } else {
            List(bluetoothManager.standaloneMeshPeers) { peer in
                StandalonePeerRow(peer: peer)
            }
            .listStyle(.plain)
        }
    }

    private var emptyState: some View {
        VStack(spacing: 16) {
            Spacer()
            Image(systemName: "antenna.radiowaves.left.and.right.slash")
                .font(.system(size: 48))
                .foregroundColor(.secondary)
            Text("No Peers Found")
                .font(.title3)
                .fontWeight(.medium)
            Text("Scanning for nearby phones and radios...")
                .font(.subheadline)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
            ProgressView()
                .padding(.top, 8)
            Spacer()
        }
        .padding()
    }
}

struct StandalonePeerRow: View {
    let peer: StandaloneMeshPeer

    var body: some View {
        HStack(spacing: 12) {
            // Connection indicator
            ZStack {
                Circle()
                    .fill(peer.isConnected ? Color.green.opacity(0.2) : Color.gray.opacity(0.2))
                    .frame(width: 44, height: 44)

                Image(systemName: (peer.nodeName ?? "").contains("📻") ? "antenna.radiowaves.left.and.right" : "iphone.gen2")
                    .font(.title3)
                    .foregroundColor(peer.isConnected ? .green : .gray)
            }

            VStack(alignment: .leading, spacing: 4) {
                Text(peer.displayName)
                    .font(.headline)

                HStack(spacing: 8) {
                    Label("\(peer.rssi) dBm", systemImage: "wifi")
                    Text(peer.isConnected ? "Connected" : "Discovered")
                        .foregroundColor(peer.isConnected ? .green : .orange)
                }
                .font(.caption)
                .foregroundColor(.secondary)
            }

            Spacer()

            VStack(alignment: .trailing, spacing: 4) {
                Text(String(format: "%04X", peer.nodeAddress & 0xFFFF))
                    .font(.caption)
                    .fontWeight(.medium)
                    .foregroundColor(.secondary)

                Text(peer.timeSinceLastSeen)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .padding(.vertical, 4)
    }
}

// MARK: - Standalone Stats View

struct StandaloneStatsView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager

    var body: some View {
        List {
            Section("Mesh Status") {
                StatRow(label: "Mode", value: "Standalone BLE Mesh", color: .blue)
                StatRow(label: "Node Address", value: String(format: "0x%08X", bluetoothManager.virtualNodeAddress))
                StatRow(label: "Peers Discovered", value: "\(bluetoothManager.standaloneMeshPeers.count)")
                StatRow(label: "Peers Connected", value: "\(bluetoothManager.standaloneMeshPeers.filter { $0.isConnected }.count)")
            }

            Section("Messages") {
                StatRow(label: "Sent", value: "\(bluetoothManager.receivedMessages.filter { $0.source == bluetoothManager.virtualNodeAddress }.count)")
                StatRow(label: "Received", value: "\(bluetoothManager.receivedMessages.filter { $0.source != bluetoothManager.virtualNodeAddress }.count)")
                StatRow(label: "Total", value: "\(bluetoothManager.receivedMessages.count)")
            }

            Section("Peers by Type") {
                let radioCount = bluetoothManager.standaloneMeshPeers.filter { ($0.nodeName ?? "").contains("📻") }.count
                let phoneCount = bluetoothManager.standaloneMeshPeers.count - radioCount
                StatRow(label: "LNK-22 Radios", value: "\(radioCount)", color: .orange)
                StatRow(label: "Phones", value: "\(phoneCount)", color: .blue)
            }

            Section("Connection") {
                StatRow(label: "Bluetooth", value: bluetoothManager.isBluetoothEnabled ? "Enabled" : "Disabled", color: bluetoothManager.isBluetoothEnabled ? .green : .red)
                StatRow(label: "Scanning", value: "Active", color: .green)
            }
        }
        .listStyle(.insetGrouped)
    }
}

#Preview {
    NetworkView()
        .environmentObject(BluetoothManager())
        .environmentObject(MeshNetworkViewModel())
}
