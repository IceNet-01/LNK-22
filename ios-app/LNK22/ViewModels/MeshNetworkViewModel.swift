//
//  MeshNetworkViewModel.swift
//  LNK22
//
//  ViewModel for managing mesh network state and operations
//

import Foundation
import Combine
import SwiftUI

@MainActor
class MeshNetworkViewModel: ObservableObject {
    // MARK: - Published Properties

    @Published var messages: [MeshMessage] = []
    @Published var nodes: [MeshNode] = []
    @Published var selectedChannel: UInt8 = 0
    @Published var draftMessage: String = ""
    @Published var selectedDestination: UInt32 = 0xFFFFFFFF // Broadcast by default

    // Persisted settings
    @AppStorage("userName") var userName: String = ""
    @AppStorage("savedNodes") private var savedNodesData: Data = Data()

    // MARK: - Private Properties

    private var cancellables = Set<AnyCancellable>()
    private weak var bluetoothManager: BluetoothManager?

    // MARK: - Initialization

    init() {
        loadSavedNodes()
    }

    // MARK: - Public Methods

    func configure(with bluetoothManager: BluetoothManager) {
        self.bluetoothManager = bluetoothManager

        // Subscribe to received messages - append to existing messages instead of replacing
        bluetoothManager.onMessageReceived = { [weak self] message in
            Task { @MainActor in
                self?.handleReceivedMessage(message)
            }
        }

        // DON'T sync all messages from bluetooth manager - it overwrites sent messages
        // Instead, we handle each message individually via onMessageReceived callback

        // Update nodes from neighbor list
        bluetoothManager.$neighbors
            .receive(on: DispatchQueue.main)
            .sink { [weak self] neighbors in
                self?.updateNodesFromNeighbors(neighbors)
            }
            .store(in: &cancellables)
    }

    func sendMessage() {
        guard !draftMessage.isEmpty else { return }

        bluetoothManager?.sendMessage(
            to: selectedDestination,
            text: draftMessage,
            channel: selectedChannel
        )

        // Add sent message to local list
        let sentMessage = MeshMessage(
            id: UUID(),
            source: bluetoothManager?.deviceStatus?.nodeAddress ?? 0,
            destination: selectedDestination,
            channel: selectedChannel,
            type: .text,
            content: draftMessage,
            timestamp: Date(),
            rssi: nil,
            snr: nil
        )
        messages.append(sentMessage)

        // Clear draft
        draftMessage = ""
    }

    func sendBroadcast(_ text: String) {
        bluetoothManager?.sendBroadcast(text: text, channel: selectedChannel)
    }

    func switchChannel(_ channel: UInt8) {
        selectedChannel = channel
        bluetoothManager?.switchChannel(channel)
    }

    func refreshNetwork() {
        bluetoothManager?.requestStatus()
        bluetoothManager?.requestNeighbors()
        bluetoothManager?.requestRoutes()
    }

    func setNodeName(_ address: UInt32, name: String) {
        if let index = nodes.firstIndex(where: { $0.address == address }) {
            nodes[index].name = name
            saveNodes()
        }
    }

    func clearMessages() {
        messages.removeAll()
    }

    // MARK: - Private Methods

    private func handleReceivedMessage(_ message: MeshMessage) {
        // Skip messages from our own address (sent messages are already added by the view)
        let myAddress = bluetoothManager?.deviceStatus?.nodeAddress ?? 0
        let myVirtualAddress = bluetoothManager?.virtualNodeAddress ?? 0
        if message.source == myAddress || message.source == myVirtualAddress {
            print("[MeshNetwork] Ignoring message from self (address match)")
            return
        }

        // Avoid duplicates - check by CONTENT alone within last 30 seconds
        // This catches both:
        // 1. Same message from same source (normal duplicate)
        // 2. Our own sent messages echoed back with different source address
        let isDuplicate = messages.contains { existing in
            existing.content == message.content &&
            abs(existing.timestamp.timeIntervalSince(message.timestamp)) < 30
        }

        guard !isDuplicate else {
            print("[MeshNetwork] Ignoring duplicate message (content match)")
            return
        }

        // Add message to history
        messages.append(message)

        // Update node last seen
        if let index = nodes.firstIndex(where: { $0.address == message.source }) {
            nodes[index].lastSeen = Date()
            nodes[index].isOnline = true
        } else {
            // Add new node
            let node = MeshNode(
                address: message.source,
                name: nil,
                position: nil,
                lastStatus: nil,
                lastSeen: Date(),
                isOnline: true
            )
            nodes.append(node)
        }
    }

    private func updateNodesFromNeighbors(_ neighbors: [Neighbor]) {
        for neighbor in neighbors {
            if let index = nodes.firstIndex(where: { $0.address == neighbor.address }) {
                nodes[index].lastSeen = neighbor.lastSeen
                nodes[index].isOnline = true
            } else {
                let node = MeshNode(
                    address: neighbor.address,
                    name: nil,
                    position: nil,
                    lastStatus: nil,
                    lastSeen: neighbor.lastSeen,
                    isOnline: true
                )
                nodes.append(node)
            }
        }
    }

    private func loadSavedNodes() {
        guard !savedNodesData.isEmpty else { return }

        do {
            nodes = try JSONDecoder().decode([MeshNode].self, from: savedNodesData)
            // Mark all as offline initially
            for i in nodes.indices {
                nodes[i].isOnline = false
            }
        } catch {
            print("Failed to load saved nodes: \(error)")
        }
    }

    private func saveNodes() {
        do {
            savedNodesData = try JSONEncoder().encode(nodes)
        } catch {
            print("Failed to save nodes: \(error)")
        }
    }
}

// MARK: - Channel Info

struct ChannelInfo: Identifiable {
    let id: UInt8
    let name: String
    let description: String

    static let channels: [ChannelInfo] = [
        ChannelInfo(id: 0, name: "Default", description: "Primary communication channel"),
        ChannelInfo(id: 1, name: "Channel 1", description: "Secondary channel"),
        ChannelInfo(id: 2, name: "Channel 2", description: "Alternate channel"),
        ChannelInfo(id: 3, name: "Channel 3", description: "Group channel"),
        ChannelInfo(id: 4, name: "Channel 4", description: "Team A"),
        ChannelInfo(id: 5, name: "Channel 5", description: "Team B"),
        ChannelInfo(id: 6, name: "Channel 6", description: "Emergency"),
        ChannelInfo(id: 7, name: "Channel 7", description: "Admin")
    ]
}
