//
//  SettingsView.swift
//  LNK22
//
//  View for app and device configuration
//

import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager
    @EnvironmentObject var meshNetwork: MeshNetworkViewModel
    @AppStorage("userName") private var userName = ""
    @AppStorage("notificationsEnabled") private var notificationsEnabled = true
    @AppStorage("soundEnabled") private var soundEnabled = true
    @AppStorage("hapticEnabled") private var hapticEnabled = true
    @AppStorage("autoReconnect") private var autoReconnect = true
    @AppStorage("showSignalStrength") private var showSignalStrength = true

    @State private var showingRadioConfig = false
    @State private var showingResetConfirmation = false

    var body: some View {
        NavigationStack {
            List {
                // User Profile
                Section("Profile") {
                    HStack {
                        ZStack {
                            Circle()
                                .fill(Color.accentColor.opacity(0.2))
                                .frame(width: 56, height: 56)

                            Text(userInitials)
                                .font(.title2)
                                .fontWeight(.semibold)
                                .foregroundColor(.accentColor)
                        }

                        VStack(alignment: .leading, spacing: 4) {
                            TextField("Your Name", text: $userName)
                                .font(.headline)

                            if let status = bluetoothManager.deviceStatus {
                                Text(status.nodeAddressHex)
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                            }
                        }
                    }
                    .padding(.vertical, 4)
                }

                // Radio Configuration
                if bluetoothManager.connectionState == .ready {
                    Section("Radio") {
                        NavigationLink {
                            RadioConfigView()
                        } label: {
                            HStack {
                                Label("Radio Configuration", systemImage: "antenna.radiowaves.left.and.right")
                                Spacer()
                                if let status = bluetoothManager.deviceStatus {
                                    Text("CH\(status.currentChannel)")
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                }
                            }
                        }

                        NavigationLink {
                            ChannelConfigView()
                        } label: {
                            Label("Channel Settings", systemImage: "slider.horizontal.3")
                        }

                        NavigationLink {
                            SecuritySettingsView()
                        } label: {
                            HStack {
                                Label("Security", systemImage: "lock.shield")
                                Spacer()
                                if let status = bluetoothManager.deviceStatus {
                                    Image(systemName: status.isEncryptionEnabled ? "checkmark.circle.fill" : "xmark.circle")
                                        .foregroundColor(status.isEncryptionEnabled ? .green : .orange)
                                }
                            }
                        }
                    }
                }

                // App Settings
                Section("App Settings") {
                    Toggle(isOn: $notificationsEnabled) {
                        Label("Notifications", systemImage: "bell")
                    }

                    Toggle(isOn: $soundEnabled) {
                        Label("Sound Effects", systemImage: "speaker.wave.2")
                    }

                    Toggle(isOn: $hapticEnabled) {
                        Label("Haptic Feedback", systemImage: "iphone.radiowaves.left.and.right")
                    }
                }

                // Connection Settings
                Section("Connection") {
                    Toggle(isOn: $autoReconnect) {
                        Label("Auto Reconnect", systemImage: "arrow.clockwise")
                    }

                    Toggle(isOn: $showSignalStrength) {
                        Label("Show Signal Strength", systemImage: "wifi")
                    }
                }

                // Bluetooth Pairing
                Section {
                    HStack {
                        Label("Pairing Status", systemImage: "lock.shield")
                        Spacer()
                        if bluetoothManager.isPaired {
                            HStack(spacing: 4) {
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundColor(.green)
                                Text("Paired")
                                    .foregroundColor(.green)
                            }
                        } else if bluetoothManager.connectionState == .ready {
                            Text("Connected (No Pairing)")
                                .foregroundColor(.secondary)
                        } else {
                            Text("Not Connected")
                                .foregroundColor(.secondary)
                        }
                    }

                    HStack {
                        Label("Default PIN", systemImage: "lock.fill")
                        Spacer()
                        Text(LNK22BLEService.defaultPairingPIN)
                            .font(.system(.body, design: .monospaced))
                            .foregroundColor(.secondary)
                    }
                } header: {
                    Text("Bluetooth Pairing")
                } footer: {
                    Text("When connecting to a new LNK-22 device, iOS will prompt for a PIN. Enter the default PIN shown above, or the custom PIN if one was configured on the device.")
                }

                // Advanced
                Section("Advanced") {
                    NavigationLink {
                        ConsoleView()
                    } label: {
                        Label("Console", systemImage: "terminal")
                    }

                    NavigationLink {
                        DiagnosticsView()
                    } label: {
                        Label("Diagnostics", systemImage: "stethoscope")
                    }

                    Button(role: .destructive) {
                        showingResetConfirmation = true
                    } label: {
                        Label("Reset All Settings", systemImage: "arrow.counterclockwise")
                    }
                }

                // About
                Section("About") {
                    LabeledContent("App Version", value: "1.8.0")
                    LabeledContent("Build", value: "1")

                    if let status = bluetoothManager.deviceStatus {
                        LabeledContent("Firmware Version", value: status.firmwareVersion)
                    }

                    NavigationLink {
                        LicensesView()
                    } label: {
                        Text("Open Source Licenses")
                    }
                }
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .confirmationDialog(
                "Reset All Settings",
                isPresented: $showingResetConfirmation,
                titleVisibility: .visible
            ) {
                Button("Reset", role: .destructive) {
                    resetSettings()
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("This will reset all app settings to their defaults. Your message history will be preserved.")
            }
        }
    }

    private var userInitials: String {
        let components = userName.split(separator: " ")
        if components.isEmpty {
            return "?"
        } else if components.count == 1 {
            return String(components[0].prefix(1)).uppercased()
        } else {
            return String(components[0].prefix(1) + components[1].prefix(1)).uppercased()
        }
    }

    private func resetSettings() {
        userName = ""
        notificationsEnabled = true
        soundEnabled = true
        hapticEnabled = true
        autoReconnect = true
        showSignalStrength = true
    }
}

// MARK: - Radio Config View

struct RadioConfigView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager
    @State private var config = DeviceConfig.default
    @State private var showingSaveConfirmation = false

    var body: some View {
        Form {
            Section("Transmission") {
                Picker("TX Power", selection: $config.txPower) {
                    Text("10 dBm").tag(Int8(10))
                    Text("14 dBm").tag(Int8(14))
                    Text("17 dBm").tag(Int8(17))
                    Text("20 dBm").tag(Int8(20))
                    Text("22 dBm (Max)").tag(Int8(22))
                }

                Picker("Spreading Factor", selection: $config.spreadingFactor) {
                    Text("SF7 (Fastest)").tag(UInt8(7))
                    Text("SF8").tag(UInt8(8))
                    Text("SF9").tag(UInt8(9))
                    Text("SF10 (Default)").tag(UInt8(10))
                    Text("SF11").tag(UInt8(11))
                    Text("SF12 (Longest Range)").tag(UInt8(12))
                }

                Picker("Bandwidth", selection: $config.bandwidth) {
                    Text("62.5 kHz").tag(UInt32(62500))
                    Text("125 kHz (Default)").tag(UInt32(125000))
                    Text("250 kHz").tag(UInt32(250000))
                    Text("500 kHz").tag(UInt32(500000))
                }
            }

            Section("Network") {
                Picker("Channel", selection: $config.channel) {
                    ForEach(0..<8) { channel in
                        Text("Channel \(channel)").tag(UInt8(channel))
                    }
                }

                Stepper(value: $config.beaconInterval, in: 10...300, step: 10) {
                    HStack {
                        Text("Beacon Interval")
                        Spacer()
                        Text("\(config.beaconInterval)s")
                            .foregroundColor(.secondary)
                    }
                }
            }

            Section("Features") {
                Toggle("Encryption", isOn: $config.encryptionEnabled)
                Toggle("GPS", isOn: $config.gpsEnabled)
                Toggle("Display", isOn: $config.displayEnabled)
            }

            Section {
                Button("Save Configuration") {
                    showingSaveConfirmation = true
                }
                .frame(maxWidth: .infinity)
            }
        }
        .navigationTitle("Radio Configuration")
        .navigationBarTitleDisplayMode(.inline)
        .alert("Save Configuration?", isPresented: $showingSaveConfirmation) {
            Button("Save") {
                bluetoothManager.updateConfig(config)
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("This will update the radio configuration. The device may restart.")
        }
    }
}

// MARK: - Channel Config View

struct ChannelConfigView: View {
    @EnvironmentObject var meshNetwork: MeshNetworkViewModel

    var body: some View {
        List {
            ForEach(ChannelInfo.channels) { channel in
                HStack {
                    VStack(alignment: .leading, spacing: 4) {
                        Text(channel.name)
                            .fontWeight(.medium)
                        Text(channel.description)
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }

                    Spacer()

                    if meshNetwork.selectedChannel == channel.id {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundColor(.accentColor)
                    }
                }
                .contentShape(Rectangle())
                .onTapGesture {
                    meshNetwork.switchChannel(channel.id)
                }
            }
        }
        .navigationTitle("Channels")
        .navigationBarTitleDisplayMode(.inline)
    }
}

// MARK: - Security Settings View

struct SecuritySettingsView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager
    @State private var encryptionEnabled = true
    @State private var showKeyEntry = false
    @State private var networkKey = ""

    var body: some View {
        Form {
            Section {
                Toggle("Encryption", isOn: $encryptionEnabled)

                if encryptionEnabled {
                    Button("Change Network Key") {
                        showKeyEntry = true
                    }
                }
            } footer: {
                Text("Encryption uses ChaCha20-Poly1305 AEAD for secure communication across the mesh network.")
            }

            Section("Encryption Details") {
                LabeledContent("Algorithm", value: "ChaCha20-Poly1305")
                LabeledContent("Key Size", value: "256-bit")
                LabeledContent("Key Exchange", value: "X25519 (ECDH)")
                LabeledContent("Signatures", value: "Ed25519")
            }
        }
        .navigationTitle("Security")
        .navigationBarTitleDisplayMode(.inline)
        .sheet(isPresented: $showKeyEntry) {
            NetworkKeyEntryView(key: $networkKey)
        }
    }
}

struct NetworkKeyEntryView: View {
    @Binding var key: String
    @Environment(\.dismiss) var dismiss

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    SecureField("Network Key", text: $key)
                        .textContentType(.password)
                } footer: {
                    Text("Enter a network key that all nodes in your mesh should share. This key encrypts all communication.")
                }
            }
            .navigationTitle("Network Key")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") {
                        // Save key
                        dismiss()
                    }
                }
            }
        }
    }
}

// MARK: - Console View

struct ConsoleView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager
    @State private var commandInput = ""
    @State private var consoleOutput: [String] = []

    var body: some View {
        VStack(spacing: 0) {
            // Output area
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 2) {
                        ForEach(Array(consoleOutput.enumerated()), id: \.offset) { index, line in
                            Text(line)
                                .font(.system(.caption, design: .monospaced))
                                .foregroundColor(lineColor(for: line))
                                .id(index)
                        }
                    }
                    .padding()
                }
                .background(Color.black)
                .onChange(of: consoleOutput.count) { _, _ in
                    if let lastIndex = consoleOutput.indices.last {
                        proxy.scrollTo(lastIndex, anchor: .bottom)
                    }
                }
            }

            // Input area
            HStack(spacing: 8) {
                Text(">")
                    .font(.system(.body, design: .monospaced))
                    .foregroundColor(.green)

                TextField("Command", text: $commandInput)
                    .font(.system(.body, design: .monospaced))
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .onSubmit(sendCommand)

                Button(action: sendCommand) {
                    Image(systemName: "arrow.right.circle.fill")
                }
                .disabled(commandInput.isEmpty)
            }
            .padding()
            .background(Color(.systemGray6))
        }
        .navigationTitle("Console")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .navigationBarTrailing) {
                Menu {
                    Button("Clear") {
                        consoleOutput.removeAll()
                    }

                    Divider()

                    Button("help") { sendCommand("help") }
                    Button("status") { sendCommand("status") }
                    Button("neighbors") { sendCommand("neighbors") }
                    Button("routes") { sendCommand("routes") }
                    Button("radio") { sendCommand("radio") }
                } label: {
                    Image(systemName: "ellipsis.circle")
                }
            }
        }
        .onAppear {
            consoleOutput.append("LNK-22 Console Ready")
            consoleOutput.append("Type 'help' for available commands")
        }
    }

    private func lineColor(for line: String) -> Color {
        if line.contains("[ERROR]") { return .red }
        if line.contains("[WARN]") { return .orange }
        if line.contains("[MESH]") { return .cyan }
        if line.contains("[RADIO]") { return .yellow }
        if line.starts(with: ">") { return .green }
        return .white
    }

    private func sendCommand() {
        sendCommand(commandInput)
        commandInput = ""
    }

    private func sendCommand(_ cmd: String) {
        guard !cmd.isEmpty else { return }

        consoleOutput.append("> \(cmd)")

        // Send to device
        let command = DeviceCommand.requestStatus // Map command to appropriate action
        bluetoothManager.sendCommand(command)
    }
}

// MARK: - Diagnostics View

struct DiagnosticsView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager

    var body: some View {
        List {
            Section("Bluetooth") {
                LabeledContent("State") {
                    Text(bluetoothManager.isBluetoothEnabled ? "Enabled" : "Disabled")
                        .foregroundColor(bluetoothManager.isBluetoothEnabled ? .green : .red)
                }

                LabeledContent("Connection") {
                    Text(bluetoothManager.connectionState.rawValue)
                }

                if let device = bluetoothManager.connectedDevice {
                    LabeledContent("Device", value: device.name)
                    LabeledContent("RSSI", value: "\(device.rssi) dBm")
                }
            }

            if let status = bluetoothManager.deviceStatus {
                Section("Device") {
                    LabeledContent("Node Address", value: status.nodeAddressHex)
                    LabeledContent("Firmware", value: status.firmwareVersion)
                    LabeledContent("Uptime", value: status.uptimeFormatted)
                    LabeledContent("Battery", value: "\(status.batteryPercent)%")
                }

                Section("Network") {
                    LabeledContent("Neighbors", value: "\(status.neighborCount)")
                    LabeledContent("Routes", value: "\(status.routeCount)")
                    LabeledContent("Channel", value: "CH\(status.currentChannel)")
                }

                Section("Packets") {
                    LabeledContent("Transmitted", value: "\(status.txCount)")
                    LabeledContent("Received", value: "\(status.rxCount)")
                }
            }

            Section("Actions") {
                Button("Export Diagnostics") {
                    // Export action
                }

                Button("Run Self-Test") {
                    // Self-test action
                }
            }
        }
        .navigationTitle("Diagnostics")
        .navigationBarTitleDisplayMode(.inline)
    }
}

// MARK: - Licenses View

struct LicensesView: View {
    var body: some View {
        List {
            Section {
                Text("LNK-22 iOS App is open source software licensed under the MIT License.")
                    .font(.subheadline)
            }

            Section("Libraries") {
                LicenseRow(name: "SwiftUI", license: "Apple")
                LicenseRow(name: "CoreBluetooth", license: "Apple")
                LicenseRow(name: "MapKit", license: "Apple")
            }

            Section("Firmware Libraries") {
                LicenseRow(name: "Monocypher", license: "Public Domain (CC0)")
                LicenseRow(name: "ArduinoJson", license: "MIT")
                LicenseRow(name: "SX126x-Arduino", license: "MIT")
            }
        }
        .navigationTitle("Licenses")
        .navigationBarTitleDisplayMode(.inline)
    }
}

struct LicenseRow: View {
    let name: String
    let license: String

    var body: some View {
        HStack {
            Text(name)
            Spacer()
            Text(license)
                .foregroundColor(.secondary)
        }
    }
}

#Preview {
    SettingsView()
        .environmentObject(BluetoothManager())
        .environmentObject(MeshNetworkViewModel())
}
