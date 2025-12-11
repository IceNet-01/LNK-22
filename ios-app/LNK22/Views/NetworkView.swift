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
                // Device Status Card
                if bluetoothManager.connectionState == .ready,
                   let status = bluetoothManager.deviceStatus {
                    DeviceStatusCard(status: status)
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
                    NeighborsListView()
                case 1:
                    RoutesListView()
                case 2:
                    StatisticsView()
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
                    .disabled(bluetoothManager.connectionState != .ready)
                }
            }
        }
    }

    private func refreshNetwork() {
        bluetoothManager.requestStatus()
        bluetoothManager.requestNeighbors()
        bluetoothManager.requestRoutes()
    }
}

// MARK: - Device Status Card

struct DeviceStatusCard: View {
    let status: DeviceStatus

    var body: some View {
        VStack(spacing: 12) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Node Address")
                        .font(.caption)
                        .foregroundColor(.secondary)
                    Text(status.nodeAddressHex)
                        .font(.headline)
                        .fontWeight(.bold)
                        .monospaced()
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
                    .monospaced()

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
                    .monospaced()
            }

            HStack {
                Text("Next Hop")
                    .font(.caption)
                    .foregroundColor(.secondary)
                Spacer()
                Text(route.nextHopHex)
                    .font(.subheadline)
                    .monospaced()
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

#Preview {
    NetworkView()
        .environmentObject(BluetoothManager())
        .environmentObject(MeshNetworkViewModel())
}
