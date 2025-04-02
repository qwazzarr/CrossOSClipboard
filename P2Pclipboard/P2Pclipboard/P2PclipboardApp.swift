import SwiftUI

@main
struct P2PclipboardApp: App {
    // Declare the manager at the app level to keep it alive throughout the app lifecycle
    @ObservedObject private var clipboardSyncManager = ClipboardSyncManager.shared
    
    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(clipboardSyncManager)
                .onAppear {
                    // Request Bluetooth permission dialog by starting the service briefly
                    // This is important for iOS especially
                    #if os(iOS)
                    syncManager.startServices()
                    DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
                        // Stop the service after permission dialog appears
                        // User can then explicitly start it from the UI
                        syncManager.stopServices()
                    }
                    #endif
                }
        }
    }
}

// Add required Info.plist keys for your app:
// iOS:
// - NSBluetoothAlwaysUsageDescription: "This app uses Bluetooth to sync clipboard data with your other devices"
// - NSLocalNetworkUsageDescription: "This app uses your local network to discover and connect to clipboard sync devices"
// - NSBonjourServices: ["_clipboard._tcp"]
//
// macOS:
// - NSBluetoothAlwaysUsageDescription: "This app uses Bluetooth to sync clipboard data with your other devices"
// - NSLocalNetworkUsageDescription: "This app uses your local network to discover and connect to clipboard sync devices"
// - NSBonjourServices: ["_clipboard._tcp"]
