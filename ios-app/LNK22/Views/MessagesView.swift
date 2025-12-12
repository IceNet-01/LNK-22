//
//  MessagesView.swift
//  LNK22
//
//  View for sending and receiving mesh messages
//

import SwiftUI

struct MessagesView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager
    @EnvironmentObject var meshNetwork: MeshNetworkViewModel
    @State private var messageText = ""
    @State private var showingDestinationPicker = false
    @State private var selectedDestination: UInt32 = 0xFFFFFFFF

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                // Connection status banner
                ConnectionStatusBanner()

                // Channel selector
                ChannelSelectorBar()

                // Messages list
                if meshNetwork.messages.isEmpty {
                    emptyStateView
                } else {
                    messagesList
                }

                // Compose area
                composeBar
            }
            .navigationTitle("Messages")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Menu {
                        Button(action: { meshNetwork.clearMessages() }) {
                            Label("Clear Messages", systemImage: "trash")
                        }

                        Button(action: { bluetoothManager.sendBeacon() }) {
                            Label("Send Beacon", systemImage: "antenna.radiowaves.left.and.right")
                        }
                    } label: {
                        Image(systemName: "ellipsis.circle")
                    }
                }
            }
        }
    }

    // MARK: - Subviews

    private var emptyStateView: some View {
        VStack(spacing: 16) {
            Spacer()

            Image(systemName: "message.badge.circle")
                .font(.system(size: 64))
                .foregroundColor(.secondary)

            Text("No Messages")
                .font(.title2)
                .fontWeight(.medium)

            Text("Messages from the mesh network will appear here")
                .font(.subheadline)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal)

            Spacer()
        }
    }

    private var messagesList: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(spacing: 8) {
                    ForEach(meshNetwork.messages) { message in
                        MessageBubble(message: message)
                            .id(message.id)
                    }
                }
                .padding()
            }
            .onChange(of: meshNetwork.messages.count) { _, _ in
                if let lastMessage = meshNetwork.messages.last {
                    withAnimation {
                        proxy.scrollTo(lastMessage.id, anchor: .bottom)
                    }
                }
            }
        }
    }

    private var composeBar: some View {
        VStack(spacing: 0) {
            Divider()

            // Destination indicator
            HStack {
                Text("To:")
                    .font(.caption)
                    .foregroundColor(.secondary)

                Button(action: { showingDestinationPicker = true }) {
                    HStack(spacing: 4) {
                        Text(destinationText)
                            .font(.caption)
                            .fontWeight(.medium)

                        Image(systemName: "chevron.down")
                            .font(.caption2)
                    }
                    .foregroundColor(.accentColor)
                }

                Spacer()

                // Done button to dismiss keyboard
                Button("Done") {
                    hideKeyboard()
                }
                .font(.caption)
                .foregroundColor(.accentColor)

                if bluetoothManager.connectionState == .ready,
                   let status = bluetoothManager.deviceStatus {
                    Text("CH\(status.currentChannel)")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 2)
                        .background(Color.secondary.opacity(0.2))
                        .cornerRadius(4)
                }
            }
            .padding(.horizontal)
            .padding(.top, 8)

            // Message input
            HStack(spacing: 12) {
                TextField("Message", text: $messageText, axis: .vertical)
                    .textFieldStyle(.plain)
                    .padding(10)
                    .background(Color(.systemGray6))
                    .cornerRadius(20)
                    .lineLimit(1...4)
                    .submitLabel(.send)
                    .onSubmit {
                        if canSend {
                            sendMessage()
                        }
                    }
                    .toolbar {
                        ToolbarItemGroup(placement: .keyboard) {
                            Spacer()
                            Button("Done") {
                                hideKeyboard()
                            }
                            .fontWeight(.semibold)
                        }
                    }

                Button(action: sendMessage) {
                    Image(systemName: "arrow.up.circle.fill")
                        .font(.system(size: 32))
                        .foregroundColor(canSend ? .accentColor : .gray)
                }
                .disabled(!canSend)
            }
            .padding(.horizontal)
            .padding(.vertical, 8)
        }
        .background(Color(.systemBackground))
        .sheet(isPresented: $showingDestinationPicker) {
            DestinationPickerView(selectedDestination: $selectedDestination)
        }
    }

    private func hideKeyboard() {
        UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil)
    }

    // MARK: - Computed Properties

    private var destinationText: String {
        if selectedDestination == 0xFFFFFFFF {
            return "Broadcast"
        } else {
            // Use friendly name format instead of hex
            return String(format: "Node-%04X", selectedDestination & 0xFFFF)
        }
    }

    private var canSend: Bool {
        // Allow sending in both connected (.ready) and standalone mesh mode
        let isConnected = bluetoothManager.connectionState == .ready ||
                          bluetoothManager.connectionState == .standaloneMode
        return !messageText.isEmpty && isConnected
    }

    // MARK: - Actions

    private func sendMessage() {
        guard canSend else { return }

        bluetoothManager.sendMessage(
            to: selectedDestination,
            text: messageText,
            channel: meshNetwork.selectedChannel
        )

        // Add to local messages (use virtual node address in standalone mode)
        let sourceAddress: UInt32
        if bluetoothManager.isStandaloneMeshMode {
            sourceAddress = bluetoothManager.virtualNodeAddress
        } else if let status = bluetoothManager.deviceStatus {
            sourceAddress = status.nodeAddress
        } else {
            sourceAddress = 0
        }

        if sourceAddress != 0 {
            let message = MeshMessage(
                id: UUID(),
                source: sourceAddress,
                destination: selectedDestination,
                channel: meshNetwork.selectedChannel,
                type: .text,
                content: messageText,
                timestamp: Date(),
                rssi: nil,
                snr: nil
            )
            meshNetwork.messages.append(message)
        }

        messageText = ""
    }
}

// MARK: - Connection Status Banner

struct ConnectionStatusBanner: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager

    var body: some View {
        if bluetoothManager.connectionState != .ready {
            HStack {
                Image(systemName: statusIcon)
                Text(bluetoothManager.connectionState.rawValue)
                    .font(.subheadline)

                Spacer()

                if bluetoothManager.connectionState == .disconnected {
                    Button("Connect") {
                        bluetoothManager.startScanning()
                    }
                    .font(.subheadline)
                    .fontWeight(.medium)
                }
            }
            .padding()
            .background(statusColor.opacity(0.15))
            .foregroundColor(statusColor)
        }
    }

    private var statusIcon: String {
        switch bluetoothManager.connectionState {
        case .disconnected: return "antenna.radiowaves.left.and.right.slash"
        case .scanning: return "antenna.radiowaves.left.and.right"
        case .connecting: return "antenna.radiowaves.left.and.right"
        case .connected: return "checkmark.circle"
        case .ready: return "checkmark.circle.fill"
        case .error: return "exclamationmark.triangle"
        case .standaloneMode: return "iphone.radiowaves.left.and.right"
        }
    }

    private var statusColor: Color {
        switch bluetoothManager.connectionState {
        case .disconnected: return .gray
        case .scanning, .connecting, .connected: return .orange
        case .ready: return .green
        case .error: return .red
        case .standaloneMode: return .blue
        }
    }
}

// MARK: - Channel Selector Bar

struct ChannelSelectorBar: View {
    @EnvironmentObject var meshNetwork: MeshNetworkViewModel

    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 8) {
                ForEach(ChannelInfo.channels) { channel in
                    ChannelChip(
                        channel: channel,
                        isSelected: meshNetwork.selectedChannel == channel.id
                    ) {
                        meshNetwork.switchChannel(channel.id)
                    }
                }
            }
            .padding(.horizontal)
            .padding(.vertical, 8)
        }
        .background(Color(.systemGray6))
    }
}

struct ChannelChip: View {
    let channel: ChannelInfo
    let isSelected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text("CH\(channel.id)")
                .font(.caption)
                .fontWeight(isSelected ? .semibold : .regular)
                .padding(.horizontal, 12)
                .padding(.vertical, 6)
                .background(isSelected ? Color.accentColor : Color(.systemBackground))
                .foregroundColor(isSelected ? .white : .primary)
                .cornerRadius(16)
        }
    }
}

// MARK: - Message Bubble

struct MessageBubble: View {
    let message: MeshMessage
    @EnvironmentObject var bluetoothManager: BluetoothManager

    private var isFromMe: Bool {
        // Check against radio's node address when connected
        if let status = bluetoothManager.deviceStatus {
            if message.source == status.nodeAddress {
                return true
            }
        }
        // Also check against virtual node address (standalone mode)
        if message.source == bluetoothManager.virtualNodeAddress {
            return true
        }
        return false
    }

    /// Get the delivery status for this message (for outgoing messages)
    private var deliveryStatus: MessageDeliveryStatus? {
        guard isFromMe else { return nil }
        // Find the most recent delivery status for this destination
        // Messages don't track packet IDs, so match by destination and approximate timestamp
        return bluetoothManager.deliveryStatuses.last { status in
            status.destination == message.destination &&
            abs(status.timestamp.timeIntervalSince(message.timestamp)) < 30 // Within 30 seconds
        }
    }

    var body: some View {
        HStack {
            if isFromMe { Spacer(minLength: 60) }

            VStack(alignment: isFromMe ? .trailing : .leading, spacing: 4) {
                // Header
                HStack(spacing: 4) {
                    if !isFromMe {
                        Text(message.sourceHex)
                            .font(.caption2)
                            .fontWeight(.medium)
                            .foregroundColor(.secondary)
                    }

                    if message.isBroadcast {
                        Image(systemName: "megaphone")
                            .font(.caption2)
                            .foregroundColor(.orange)
                    }

                    if let rssi = message.rssi {
                        HStack(spacing: 2) {
                            Image(systemName: message.signalQuality.icon)
                            Text("\(rssi) dBm")
                        }
                        .font(.caption2)
                        .foregroundColor(message.signalQuality.color)
                    }
                }

                // Content
                Text(message.content)
                    .font(.body)
                    .padding(12)
                    .background(isFromMe ? Color.accentColor : Color(.systemGray5))
                    .foregroundColor(isFromMe ? .white : .primary)
                    .cornerRadius(16)

                // Timestamp and delivery status
                HStack(spacing: 4) {
                    Text(message.formattedTime)
                        .font(.caption2)
                        .foregroundColor(.secondary)

                    // Show delivery status for outgoing messages
                    if isFromMe, let status = deliveryStatus {
                        Image(systemName: status.status.icon)
                            .font(.caption2)
                            .foregroundColor(status.status.color)
                    } else if isFromMe {
                        // Pending status (no confirmation yet)
                        Image(systemName: "clock")
                            .font(.caption2)
                            .foregroundColor(.gray)
                    }
                }
            }

            if !isFromMe { Spacer(minLength: 60) }
        }
    }
}

// MARK: - Destination Picker

struct DestinationPickerView: View {
    @Binding var selectedDestination: UInt32
    @EnvironmentObject var bluetoothManager: BluetoothManager
    @EnvironmentObject var meshNetwork: MeshNetworkViewModel
    @Environment(\.dismiss) var dismiss

    var body: some View {
        NavigationStack {
            List {
                // Broadcast option
                Section {
                    Button(action: {
                        selectedDestination = 0xFFFFFFFF
                        dismiss()
                    }) {
                        HStack {
                            Image(systemName: "megaphone.fill")
                                .foregroundColor(.orange)

                            VStack(alignment: .leading) {
                                Text("Broadcast")
                                    .fontWeight(.medium)
                                Text("Send to all nodes")
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                            }

                            Spacer()

                            if selectedDestination == 0xFFFFFFFF {
                                Image(systemName: "checkmark")
                                    .foregroundColor(.accentColor)
                            }
                        }
                    }
                    .foregroundColor(.primary)
                }

                // Known nodes
                if !meshNetwork.nodes.isEmpty {
                    Section("Known Nodes") {
                        ForEach(meshNetwork.nodes) { node in
                            Button(action: {
                                selectedDestination = node.address
                                dismiss()
                            }) {
                                HStack {
                                    Circle()
                                        .fill(node.isOnline ? Color.green : Color.gray)
                                        .frame(width: 8, height: 8)

                                    VStack(alignment: .leading) {
                                        Text(node.displayName)
                                            .fontWeight(.medium)
                                    }

                                    Spacer()

                                    if selectedDestination == node.address {
                                        Image(systemName: "checkmark")
                                            .foregroundColor(.accentColor)
                                    }
                                }
                            }
                            .foregroundColor(.primary)
                        }
                    }
                }

                // Neighbors
                if !bluetoothManager.neighbors.isEmpty {
                    Section("Neighbors") {
                        ForEach(bluetoothManager.neighbors) { neighbor in
                            Button(action: {
                                selectedDestination = neighbor.address
                                dismiss()
                            }) {
                                HStack {
                                    VStack(alignment: .leading) {
                                        Text(neighbor.friendlyName)
                                            .fontWeight(.medium)
                                        Text("RSSI: \(neighbor.rssi) dBm")
                                            .font(.caption)
                                            .foregroundColor(.secondary)
                                    }

                                    Spacer()

                                    if selectedDestination == neighbor.address {
                                        Image(systemName: "checkmark")
                                            .foregroundColor(.accentColor)
                                    }
                                }
                            }
                            .foregroundColor(.primary)
                        }
                    }
                }
            }
            .navigationTitle("Send To")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}

#Preview {
    MessagesView()
        .environmentObject(BluetoothManager())
        .environmentObject(MeshNetworkViewModel())
}
