import SwiftUI
import CoreBluetooth

struct ConnectedModuleView: View {
    // Observe the shared instance of ClipboardSyncManager
    @ObservedObject var clipboardSyncManager: ClipboardSyncManager = ClipboardSyncManager.shared
    
    // Stored credentials using AppStorage
    @AppStorage("syncPassword") private var syncPassword: String = ""
    @AppStorage("userName") private var userName: String = ""
    
    var body: some View {
        VStack(spacing: 20) {
            // Display the current connectionStatus from the manager:
            Text(clipboardSyncManager.connectionStatus)
                .font(.largeTitle)
                .fontWeight(.bold)
                .foregroundColor(foregroundColor(for: clipboardSyncManager.connectionStatus))
                .multilineTextAlignment(.center)
                .padding(.bottom, 10)
            
            // Network status indicator
            HStack(spacing: 10) {
                Image(systemName: clipboardSyncManager.isNetworkConnected ? "wifi" : "wifi.slash")
                    .font(.system(size: 40))
                    .foregroundColor(clipboardSyncManager.isNetworkConnected ? .green : .red)
                Text("Network: \(clipboardSyncManager.isNetworkConnected ? "Connected" : "Disconnected")")
                    .font(.title2)
            }
            
            // If the connectionStatus mentions a cached server, display extra information.
            if clipboardSyncManager.connectionStatus.contains("Server Known") {
                Text("Using Cached Server")
                    .font(.headline)
                    .foregroundColor(.blue)
            }
            
            // Bluetooth status indicator
            HStack(spacing: 10) {
                let bleStatus = clipboardSyncManager.isBleConnected
                Image(systemName: bleStatus ? "dot.radiowaves.left.and.right" : "xmark.circle.fill")
                    .font(.system(size: 40))
                    .foregroundColor(bleStatus ? .green : .red)
                Text("Bluetooth: \(bleStatus ? "Connected" : "Disconnected")")
                    .font(.title2)
            }
            
            // Display user information if available
            if !userName.isEmpty {
                Text("User: \(userName)")
                    .font(.headline)
                    .foregroundColor(.gray)
            }
            
            // "Change Settings" button resets the configuration.
            Button(action: {
                clipboardSyncManager.stopServices()
                syncPassword = ""
                userName = ""
                clipboardSyncManager.resetAllData()
            }) {
                Text("Change Settings")
                    .font(.headline)
                    .padding()
                    .frame(maxWidth: .infinity)
                    .background(Color.blue.opacity(0.8))
                    .foregroundColor(.white)
                    .cornerRadius(10)
            }
            .padding(.top, 20)
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
    
    // Helper function that determines the text color based on connectionStatus.
    func foregroundColor(for status: String) -> Color {
        if status.contains("Connected") {
            return .green
        } else if status.contains("Scanning") {
            return .yellow
        } else {
            return .red
        }
    }
}
