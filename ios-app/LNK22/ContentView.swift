//
//  ContentView.swift
//  LNK22
//
//  Main navigation view with tab bar
//

import SwiftUI

struct ContentView: View {
    @EnvironmentObject var bluetoothManager: BluetoothManager
    @EnvironmentObject var meshNetwork: MeshNetworkViewModel
    @State private var selectedTab = 0

    var body: some View {
        TabView(selection: $selectedTab) {
            // Messages Tab
            MessagesView()
                .tabItem {
                    Label("Messages", systemImage: "message.fill")
                }
                .tag(0)

            // Network Tab
            NetworkView()
                .tabItem {
                    Label("Network", systemImage: "network")
                }
                .tag(1)

            // Map Tab
            MapView()
                .tabItem {
                    Label("Map", systemImage: "map.fill")
                }
                .tag(2)

            // Devices Tab
            DevicesView()
                .tabItem {
                    Label("Devices", systemImage: "antenna.radiowaves.left.and.right")
                }
                .tag(3)

            // Settings Tab
            SettingsView()
                .tabItem {
                    Label("Settings", systemImage: "gear")
                }
                .tag(4)
        }
        .accentColor(.blue)
    }
}

#Preview {
    ContentView()
        .environmentObject(BluetoothManager())
        .environmentObject(MeshNetworkViewModel())
}
