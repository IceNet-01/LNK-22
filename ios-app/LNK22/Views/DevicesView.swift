//
//  DevicesView.swift
//  LNK22
//
//  View for discovering and managing Bluetooth connections to LNK-22 radios
//

import SwiftUI

struct DevicesView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager
    @State private var isScanning = false

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                // Bluetooth status
                if !bluetoothManager.isBluetoothEnabled {
                    bluetoothDisabledBanner
                }

                // Connected device
                if let device = bluetoothManager.connectedDevice {
                    connectedDeviceCard(device)
                }

                // Device list
                List {
                    // Discovered devices section
                    if !bluetoothManager.discoveredDevices.isEmpty {
                        Section("Available Devices") {
                            ForEach(bluetoothManager.discoveredDevices) { device in
                                DiscoveredDeviceRow(
                                    device: device,
                                    isConnected: bluetoothManager.connectedDevice?.id == device.id,
                                    isConnecting: bluetoothManager.connectionState == .connecting
                                ) {
                                    if bluetoothManager.connectedDevice?.id == device.id {
                                        bluetoothManager.disconnect()
                                    } else {
                                        bluetoothManager.connect(to: device)
                                    }
                                }
                            }
                        }
                    }

                    // Instructions section
                    if bluetoothManager.discoveredDevices.isEmpty &&
                       bluetoothManager.connectionState != .scanning {
                        Section {
                            VStack(spacing: 16) {
                                Image(systemName: "antenna.radiowaves.left.and.right")
                                    .font(.system(size: 48))
                                    .foregroundColor(.secondary)

                                Text("Find Your LNK-22 Radio")
                                    .font(.headline)

                                Text("Make sure your LNK-22 radio is powered on and within range. Tap the scan button to discover nearby devices.")
                                    .font(.subheadline)
                                    .foregroundColor(.secondary)
                                    .multilineTextAlignment(.center)

                                Button(action: startScanning) {
                                    Label("Scan for Devices", systemImage: "magnifyingglass")
                                        .frame(maxWidth: .infinity)
                                }
                                .buttonStyle(.borderedProminent)
                                .disabled(!bluetoothManager.isBluetoothEnabled)
                            }
                            .padding(.vertical, 20)
                        }
                    }

                    // Help section
                    Section("Help") {
                        NavigationLink {
                            TroubleshootingView()
                        } label: {
                            Label("Troubleshooting", systemImage: "questionmark.circle")
                        }

                        NavigationLink {
                            AboutDeviceView()
                        } label: {
                            Label("About LNK-22", systemImage: "info.circle")
                        }
                    }
                }
                .listStyle(.insetGrouped)
            }
            .navigationTitle("Devices")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    if bluetoothManager.connectionState == .scanning {
                        ProgressView()
                    } else {
                        Button(action: startScanning) {
                            Image(systemName: "arrow.clockwise")
                        }
                        .disabled(!bluetoothManager.isBluetoothEnabled)
                    }
                }
            }
        }
    }

    // MARK: - Subviews

    private var bluetoothDisabledBanner: some View {
        HStack {
            Image(systemName: "bluetooth.slash")
                .foregroundColor(.red)

            Text("Bluetooth is disabled. Enable it in Settings to connect to radios.")
                .font(.subheadline)

            Spacer()
        }
        .padding()
        .background(Color.red.opacity(0.1))
    }

    private func connectedDeviceCard(_ device: DiscoveredDevice) -> some View {
        VStack(spacing: 12) {
            HStack {
                ZStack {
                    Circle()
                        .fill(Color.green.opacity(0.2))
                        .frame(width: 56, height: 56)

                    Image(systemName: "antenna.radiowaves.left.and.right")
                        .font(.title2)
                        .foregroundColor(.green)
                }

                VStack(alignment: .leading, spacing: 4) {
                    Text(device.name)
                        .font(.headline)

                    HStack(spacing: 4) {
                        Circle()
                            .fill(Color.green)
                            .frame(width: 8, height: 8)

                        Text(bluetoothManager.connectionState.rawValue)
                            .font(.subheadline)
                            .foregroundColor(.secondary)
                    }
                }

                Spacer()

                Button("Disconnect") {
                    bluetoothManager.disconnect()
                }
                .buttonStyle(.bordered)
                .tint(.red)
            }

            if let status = bluetoothManager.deviceStatus {
                Divider()

                HStack {
                    VStack {
                        Text(status.nodeAddressHex)
                            .font(.caption)
                            .fontWeight(.medium)
                        Text("Node ID")
                            .font(.caption2)
                            .foregroundColor(.secondary)
                    }

                    Spacer()

                    VStack {
                        Text("CH\(status.currentChannel)")
                            .font(.caption)
                        Text("Channel")
                            .font(.caption2)
                            .foregroundColor(.secondary)
                    }

                    Spacer()

                    VStack {
                        Text("\(status.batteryPercent)%")
                            .font(.caption)
                        Text("Battery")
                            .font(.caption2)
                            .foregroundColor(.secondary)
                    }

                    Spacer()

                    VStack {
                        Text("\(status.neighborCount)")
                            .font(.caption)
                        Text("Neighbors")
                            .font(.caption2)
                            .foregroundColor(.secondary)
                    }
                }
            }
        }
        .padding()
        .background(Color(.systemGray6))
        .cornerRadius(12)
        .padding()
    }

    // MARK: - Actions

    private func startScanning() {
        bluetoothManager.startScanning()
    }
}

// MARK: - Discovered Device Row

struct DiscoveredDeviceRow: View {
    let device: DiscoveredDevice
    let isConnected: Bool
    let isConnecting: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 12) {
                // Icon
                ZStack {
                    Circle()
                        .fill(signalColor.opacity(0.2))
                        .frame(width: 44, height: 44)

                    Image(systemName: "antenna.radiowaves.left.and.right")
                        .font(.title3)
                        .foregroundColor(signalColor)
                }

                // Info
                VStack(alignment: .leading, spacing: 4) {
                    Text(device.name)
                        .font(.headline)
                        .foregroundColor(.primary)

                    HStack(spacing: 8) {
                        Text("RSSI: \(device.rssi) dBm")
                            .font(.caption)
                            .foregroundColor(.secondary)

                        Text("LNK-22 Radio")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }

                Spacer()

                // Status
                if isConnected {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundColor(.green)
                } else if isConnecting {
                    ProgressView()
                } else {
                    Image(systemName: "chevron.right")
                        .foregroundColor(.secondary)
                }
            }
        }
        .disabled(isConnecting)
    }

    private var signalColor: Color {
        if device.rssi >= -60 { return .green }
        if device.rssi >= -75 { return .blue }
        if device.rssi >= -90 { return .orange }
        return .red
    }
}

// MARK: - Troubleshooting View

struct TroubleshootingView: View {
    var body: some View {
        List {
            Section("Connection Issues") {
                TroubleshootingItem(
                    icon: "bluetooth",
                    title: "Radio Not Found",
                    steps: [
                        "Ensure the radio is powered on",
                        "Check that Bluetooth is enabled on your iPhone",
                        "Move closer to the radio (within 10 meters)",
                        "Restart the radio by turning it off and on"
                    ]
                )

                TroubleshootingItem(
                    icon: "bolt.slash",
                    title: "Connection Drops",
                    steps: [
                        "Stay within range of the radio",
                        "Reduce interference from other Bluetooth devices",
                        "Check battery level on the radio",
                        "Try disconnecting and reconnecting"
                    ]
                )
            }

            Section("Message Issues") {
                TroubleshootingItem(
                    icon: "message.badge.circle",
                    title: "Messages Not Sending",
                    steps: [
                        "Verify you're connected to the radio",
                        "Check that the destination is reachable",
                        "Ensure you're on the correct channel",
                        "Wait for route discovery to complete"
                    ]
                )
            }

            Section("Reset") {
                Button(role: .destructive) {
                    // Reset action
                } label: {
                    Label("Reset App Settings", systemImage: "arrow.counterclockwise")
                }
            }
        }
        .navigationTitle("Troubleshooting")
        .navigationBarTitleDisplayMode(.inline)
    }
}

struct TroubleshootingItem: View {
    let icon: String
    let title: String
    let steps: [String]

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Image(systemName: icon)
                    .foregroundColor(.accentColor)
                Text(title)
                    .fontWeight(.medium)
            }

            VStack(alignment: .leading, spacing: 8) {
                ForEach(Array(steps.enumerated()), id: \.offset) { index, step in
                    HStack(alignment: .top, spacing: 8) {
                        Text("\(index + 1).")
                            .font(.caption)
                            .foregroundColor(.secondary)
                            .frame(width: 16, alignment: .trailing)

                        Text(step)
                            .font(.subheadline)
                            .foregroundColor(.secondary)
                    }
                }
            }
        }
        .padding(.vertical, 4)
    }
}

// MARK: - About Device View

struct AboutDeviceView: View {
    var body: some View {
        List {
            Section {
                VStack(spacing: 16) {
                    Image(systemName: "antenna.radiowaves.left.and.right.circle.fill")
                        .font(.system(size: 72))
                        .foregroundColor(.accentColor)

                    Text("LNK-22")
                        .font(.title)
                        .fontWeight(.bold)

                    Text("Professional LoRa Mesh Networking")
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 20)
            }

            Section("Features") {
                FeatureRow(icon: "arrow.left.arrow.right", title: "Extended Range", description: "10-15 km line-of-sight")
                FeatureRow(icon: "arrow.triangle.branch", title: "Smart Routing", description: "AODV mesh protocol")
                FeatureRow(icon: "lock.shield", title: "Encryption", description: "ChaCha20-Poly1305 AEAD")
                FeatureRow(icon: "location", title: "GPS Tracking", description: "Position sharing")
                FeatureRow(icon: "speaker.wave.2", title: "Voice Messaging", description: "Codec2 compression")
            }

            Section("Specifications") {
                LabeledContent("Frequency", value: "915 MHz (US ISM)")
                LabeledContent("Bandwidth", value: "125 kHz")
                LabeledContent("TX Power", value: "22 dBm max")
                LabeledContent("Range", value: "10-15 km LOS")
                LabeledContent("Channels", value: "8")
                LabeledContent("Protocol", value: "AODV")
            }

            Section("Links") {
                Link(destination: URL(string: "https://github.com/LNK-22")!) {
                    Label("GitHub Repository", systemImage: "link")
                }

                Link(destination: URL(string: "https://lnk-22.io/docs")!) {
                    Label("Documentation", systemImage: "book")
                }
            }
        }
        .navigationTitle("About LNK-22")
        .navigationBarTitleDisplayMode(.inline)
    }
}

struct FeatureRow: View {
    let icon: String
    let title: String
    let description: String

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .font(.title3)
                .foregroundColor(.accentColor)
                .frame(width: 32)

            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .fontWeight(.medium)
                Text(description)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
    }
}

#Preview {
    DevicesView()
        .environmentObject(BluetoothManager())
}
