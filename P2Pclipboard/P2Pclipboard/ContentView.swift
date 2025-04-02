import SwiftUI
import CoreBluetooth
import CryptoKit

struct ContentView: View {
    @ObservedObject private var clipboardSyncManager = ClipboardSyncManager.shared
    
    // Store the password securely in the system
    @AppStorage("syncPassword") private var syncPassword: String = ""
    @AppStorage("userName") private var userName: String = ""
    
    // State for UI control
    @State private var isPasswordSheetShown = false
    @State private var tempPassword = ""
    @State private var tempUserName = ""
    
    var body: some View {
        NavigationView {
            VStack(spacing: 20) {
                // Status section
                GroupBox(label: Text("Connection Status").font(.headline)) {
                    VStack(alignment: .leading, spacing: 12) {
                        // Show connection status
                        HStack {
                            Text("Status:")
                            Spacer()
                            HStack {
                                Circle()
                                    .fill(connectionColor)
                                    .frame(width: 10, height: 10)
                                Text(clipboardSyncManager.connectionStatus)
                            }
                        }
                        
                        // Network status
                        HStack {
                            Text("Network:")
                            Spacer()
                            Text(clipboardSyncManager.isNetworkConnected ? "Connected" : "Disconnected")
                                .foregroundColor(clipboardSyncManager.isNetworkConnected ? .green : .red)
                        }
                        
                        // BLE status
                        HStack {
                            Text("Bluetooth:")
                            Spacer()
                            Text(clipboardSyncManager.isBleConnected ? "Connected" : "Disconnected")
                                .foregroundColor(clipboardSyncManager.isBleConnected ? .green : .red)
                        }
                        
                        if !userName.isEmpty {
                            HStack {
                                Text("User Name:")
                                Spacer()
                                Text(userName)
                            }
                        }
                    }
                    .padding(.vertical, 5)
                }
                .padding(.horizontal)
                
                // Last received content
                if !clipboardSyncManager.lastReceivedContent.isEmpty {
                    GroupBox(label: Text("Last Received Content").font(.headline)) {
                        ScrollView {
                            Text(clipboardSyncManager.lastReceivedContent)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .padding(5)
                        }
                        .frame(height: 100)
                    }
                    .padding(.horizontal)
                }
                
                // Action buttons
                GroupBox {
                    VStack(spacing: 15) {
                        // Connection control button
                        Button(action: {
                            if isConnected {
                                clipboardSyncManager.stopServices()
                            } else {
                                clipboardSyncManager.startServices()
                            }
                        }) {
                            HStack {
                                Image(systemName: isConnected ? "network.slash" : "network")
                                Text(isConnected ? "Disconnect" : "Connect")
                            }
                            .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.bordered)
                        .tint(isConnected ? .red : .blue)
                        .disabled(syncPassword.isEmpty)
                        
                        // Setup/update password button
                        Button(action: {
                            tempPassword = syncPassword
                            tempUserName = userName
                            isPasswordSheetShown = true
                        }) {
                            HStack {
                                Image(systemName: "key")
                                Text(syncPassword.isEmpty ? "Set Password & Name" : "Update Password & Name")
                            }
                            .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.bordered)
                    }
                }
                .padding(.horizontal)
                
                Spacer()
            }
            .navigationTitle("Clipboard Sync")
            .sheet(isPresented: $isPasswordSheetShown) {
                PasswordSheetView(
                    password: $tempPassword,
                    userName: $tempUserName,
                    onSave: {
                        syncPassword = tempPassword
                        userName = tempUserName
                        
                        // If the app is connected, restart services with the new password
                        if isConnected {
                            clipboardSyncManager.stopServices()
                            clipboardSyncManager.startServices()
                        }
                    }
                )
            }
            .onAppear {
                // Auto-start services if we have a password
                if !syncPassword.isEmpty {
                    clipboardSyncManager.startServices()
                }
            }
        }
    }
    
    private var isConnected: Bool {
        return clipboardSyncManager.isNetworkConnected || clipboardSyncManager.isBleConnected
    }
    
    private var connectionColor: Color {
        
        if clipboardSyncManager.isNetworkConnected || clipboardSyncManager.isBleConnected {
            return .green
        } else if clipboardSyncManager.connectionStatus == "Scanning..." {
            return .yellow
        }
        else {
            return .red
        }
    }
}
