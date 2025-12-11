//
//  LNK22App.swift
//  LNK22 - LoRa Mesh Network Controller
//
//  Professional LoRa mesh networking controller for iOS
//  Controls LNK-22 radios over Bluetooth Low Energy
//

import SwiftUI

@main
struct LNK22App: App {
    @StateObject private var bluetoothManager = BluetoothManager()
    @StateObject private var meshNetwork = MeshNetworkViewModel()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(bluetoothManager)
                .environmentObject(meshNetwork)
        }
    }
}
