//
//  MapView.swift
//  LNK22
//
//  View for displaying mesh nodes on a map
//

import SwiftUI
import MapKit

struct MapView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager
    @EnvironmentObject var meshNetwork: MeshNetworkViewModel
    @State private var cameraPosition: MapCameraPosition = .automatic
    @State private var selectedNode: MeshNode?
    @State private var showingNodeDetail = false

    var body: some View {
        NavigationStack {
            ZStack {
                // Map
                Map(position: $cameraPosition, selection: $selectedNode) {
                    // Show nodes with positions
                    ForEach(nodesWithPositions) { node in
                        if let position = node.position {
                            Annotation(
                                node.displayName,
                                coordinate: CLLocationCoordinate2D(
                                    latitude: position.latitude,
                                    longitude: position.longitude
                                ),
                                anchor: .bottom
                            ) {
                                NodeMapMarker(node: node)
                            }
                            .tag(node)
                        }
                    }

                    // User's current location
                    UserAnnotation()
                }
                .mapStyle(.standard(elevation: .realistic))
                .mapControls {
                    MapUserLocationButton()
                    MapCompass()
                    MapScaleView()
                }

                // Overlay info
                VStack {
                    Spacer()

                    // Node count indicator
                    HStack {
                        Spacer()

                        VStack(alignment: .trailing, spacing: 4) {
                            HStack(spacing: 4) {
                                Circle()
                                    .fill(Color.green)
                                    .frame(width: 8, height: 8)
                                Text("\(onlineNodesCount) online")
                                    .font(.caption)
                            }

                            HStack(spacing: 4) {
                                Circle()
                                    .fill(Color.gray)
                                    .frame(width: 8, height: 8)
                                Text("\(offlineNodesCount) offline")
                                    .font(.caption)
                            }
                        }
                        .padding(12)
                        .background(.ultraThinMaterial)
                        .cornerRadius(8)
                        .padding()
                    }
                }
            }
            .navigationTitle("Map")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Menu {
                        Button(action: centerOnNodes) {
                            Label("Center on Nodes", systemImage: "scope")
                        }

                        Button(action: refreshPositions) {
                            Label("Refresh Positions", systemImage: "arrow.clockwise")
                        }
                    } label: {
                        Image(systemName: "ellipsis.circle")
                    }
                }
            }
            .sheet(item: $selectedNode) { node in
                NodeDetailSheet(node: node)
            }
        }
    }

    // MARK: - Computed Properties

    private var nodesWithPositions: [MeshNode] {
        meshNetwork.nodes.filter { $0.position != nil }
    }

    private var onlineNodesCount: Int {
        meshNetwork.nodes.filter { $0.isOnline }.count
    }

    private var offlineNodesCount: Int {
        meshNetwork.nodes.filter { !$0.isOnline }.count
    }

    // MARK: - Actions

    private func centerOnNodes() {
        guard !nodesWithPositions.isEmpty else { return }

        // Calculate bounds
        var minLat = 90.0, maxLat = -90.0
        var minLon = 180.0, maxLon = -180.0

        for node in nodesWithPositions {
            if let pos = node.position {
                minLat = min(minLat, pos.latitude)
                maxLat = max(maxLat, pos.latitude)
                minLon = min(minLon, pos.longitude)
                maxLon = max(maxLon, pos.longitude)
            }
        }

        let center = CLLocationCoordinate2D(
            latitude: (minLat + maxLat) / 2,
            longitude: (minLon + maxLon) / 2
        )

        let span = MKCoordinateSpan(
            latitudeDelta: (maxLat - minLat) * 1.5 + 0.01,
            longitudeDelta: (maxLon - minLon) * 1.5 + 0.01
        )

        withAnimation {
            cameraPosition = .region(MKCoordinateRegion(center: center, span: span))
        }
    }

    private func refreshPositions() {
        bluetoothManager.requestStatus()
        bluetoothManager.requestNeighbors()
    }
}

// MARK: - Node Map Marker

struct NodeMapMarker: View {
    let node: MeshNode

    var body: some View {
        VStack(spacing: 0) {
            ZStack {
                Circle()
                    .fill(node.isOnline ? Color.green : Color.gray)
                    .frame(width: 36, height: 36)

                Image(systemName: "antenna.radiowaves.left.and.right")
                    .font(.system(size: 16))
                    .foregroundColor(.white)
            }

            // Pointer
            Triangle()
                .fill(node.isOnline ? Color.green : Color.gray)
                .frame(width: 12, height: 8)
                .offset(y: -1)
        }
    }
}

struct Triangle: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        path.move(to: CGPoint(x: rect.midX, y: rect.maxY))
        path.addLine(to: CGPoint(x: rect.minX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        path.closeSubpath()
        return path
    }
}

// MARK: - Node Detail Sheet

struct NodeDetailSheet: View {
    let node: MeshNode
    @Environment(\.dismiss) var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section("Identification") {
                    LabeledContent("Address", value: node.addressHex)

                    if let name = node.name {
                        LabeledContent("Name", value: name)
                    }

                    HStack {
                        Text("Status")
                        Spacer()
                        HStack(spacing: 4) {
                            Circle()
                                .fill(node.isOnline ? Color.green : Color.gray)
                                .frame(width: 8, height: 8)
                            Text(node.isOnline ? "Online" : "Offline")
                        }
                    }
                }

                if let position = node.position {
                    Section("Position") {
                        LabeledContent("Latitude", value: String(format: "%.6f", position.latitude))
                        LabeledContent("Longitude", value: String(format: "%.6f", position.longitude))
                        LabeledContent("Altitude", value: String(format: "%.1f m", position.altitude))
                        LabeledContent("Satellites", value: "\(position.satellites)")
                    }
                }

                if let status = node.lastStatus {
                    Section("Last Status") {
                        LabeledContent("TX Count", value: "\(status.txCount)")
                        LabeledContent("RX Count", value: "\(status.rxCount)")
                        LabeledContent("Battery", value: "\(status.batteryPercent)%")
                        LabeledContent("Channel", value: "CH\(status.currentChannel)")
                    }
                }

                Section("Activity") {
                    LabeledContent("Last Seen") {
                        Text(node.lastSeen, style: .relative)
                    }
                }
            }
            .navigationTitle(node.displayName)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .presentationDetents([.medium, .large])
    }
}

// MARK: - Empty Map State

struct EmptyMapView: View {
    var body: some View {
        VStack(spacing: 16) {
            Spacer()

            Image(systemName: "map")
                .font(.system(size: 64))
                .foregroundColor(.secondary)

            Text("No Node Positions")
                .font(.title2)
                .fontWeight(.medium)

            Text("Nodes with GPS will appear on the map when they broadcast their position")
                .font(.subheadline)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 40)

            Spacer()
        }
    }
}

#Preview {
    MapView()
        .environmentObject(BluetoothManager())
        .environmentObject(MeshNetworkViewModel())
}
