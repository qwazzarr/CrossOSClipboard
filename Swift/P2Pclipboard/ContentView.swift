import SwiftUI
import CoreBluetooth
import CryptoKit

enum AuthOption {
    case none, generateKey, connectKey
}

struct ContentView: View {
    @ObservedObject private var clipboardSyncManager = ClipboardSyncManager.shared
    
    // Credentials stored securely using AppStorage
    @AppStorage("syncPassword") private var syncPassword: String = ""
    @AppStorage("userName") private var userName: String = ""
    
    // Local state for authentication flow
    @State private var authOption: AuthOption = .none
    @State private var generatedKey: String = "" // Example key; typically generate dynamically
    @State private var otherDeviceKey: String = ""
    @State private var password: String = ""
    
    
    
    var body: some View {
            VStack(spacing: 30) {
                if syncPassword.isEmpty || userName.isEmpty {
                    // If credentials haven't been set or have been reset
                    if authOption == .none {
                        // Show two choices: "Generate Key" and "Connect to other device"
                        Spacer()
                        VStack(spacing: 20) {
                            Button(action: {
                                authOption = .generateKey
                                generatedKey = KeyGenerator.generateFormattedKey()
                            }) {
                                Text("Generate Key")
                                    .font(.largeTitle)
                                    .frame(maxWidth: .infinity)
                                    .padding(.vertical  )
                                    .background(Color.blue.opacity(0.8))
                                    .foregroundColor(.white)
                                    .cornerRadius(10)
                            }
                            
                            Button(action: {
                                authOption = .connectKey
                            }) {
                                Text("Connect to other device")
                                    .font(.largeTitle)
                                    .frame(maxWidth: .infinity)
                                    .padding()
                                    .background(Color.green.opacity(0.8))
                                    .foregroundColor(.white)
                                    .cornerRadius(10)
                            }
                        }
                        .padding(.horizontal)
                        .multilineTextAlignment(.center)
                    } else if authOption == .generateKey {
                        // User chose "Generate Key"
                        Spacer()
                        VStack(spacing: 20) {
                            // Display generated key in bold, large letters
                            Text(generatedKey)
                                .font(.system(size: 36, weight: .bold))
                                .multilineTextAlignment(.center)
                                .padding(.horizontal)
                            
                            // Instructional subtext
                            Text("Copy this key to other devices to connect")
                                .font(.title3)
                                .multilineTextAlignment(.center)
                                .padding(.horizontal)
                            
                            // Password field (big and centered)
                            TextField("Input the same password on all devices", text: $password)
                                .font(.system(size: 24))
                                .padding(EdgeInsets(top: 10, leading: 16, bottom: 10, trailing: 16))
                                .background(Color(.gray))
                                .cornerRadius(8)
                                .frame(maxWidth: .infinity)
                                .multilineTextAlignment(.center)
                                .padding(.horizontal)
                            
                            // Continue button to save password and start the services
                            Button(action: {
                                syncPassword = password
                                userName = generatedKey  // Example: You might want to assign a different value
                                ClipboardSyncManager.shared.setBLEUUID(userName)
                                
                                ClipboardEncryption.setPassword(password)
                                clipboardSyncManager.startServices()
                                authOption = .none
                            }) {
                                Text("Continue")
                                    .font(.title2)
                                    .frame(maxWidth: .infinity)
                                    .padding()
                                    .background(Color.blue)
                                    .foregroundColor(.white)
                                    .cornerRadius(10)
                            }
                            .padding(.horizontal)
                        }
                        .padding()
                        
                        Spacer()
                    } else if authOption == .connectKey {
                        // User chose "Connect to other device"
                        VStack(spacing: 20) {
                            // Field to input the key from another device
                        
                            Spacer()
                            
                            TextField("Input key of other device", text: $otherDeviceKey)
                                .font(.system(size: 24))
                                .padding(EdgeInsets(top: 10, leading: 16, bottom: 10, trailing: 16))
                                .background(Color(.gray))
                                .cornerRadius(8)
                                .frame(maxWidth: .infinity)
                                .multilineTextAlignment(.center)
                                .padding(.horizontal)
                            
                            
                            // Password field (big and centered)
                            TextField("Input the same password on all devices", text: $password)
                                .font(.system(size: 24))
                                .padding(EdgeInsets(top: 10, leading: 16, bottom: 10, trailing: 16))
                                .background(Color(.gray))
                                .cornerRadius(8)
                                .frame(maxWidth: .infinity)
                                .multilineTextAlignment(.center)
                                .padding(.horizontal)
                            
                            // Button to connect using the provided key and password
                            Button(action: {
                                // Save the provided key and password
                                syncPassword = password
                                userName = otherDeviceKey   // or store key info separately
                                
                                ClipboardSyncManager.shared.setBLEUUID(userName)
                                ClipboardEncryption.setPassword(password)
                                
                                clipboardSyncManager.startServices()
                                authOption = .none
                            }) {
                                Text("Connect")
                                    .font(.title2)
                                    .frame(maxWidth: .infinity)
                                    .padding()
                                    .background(Color.green)
                                    .foregroundColor(.white)
                                    .cornerRadius(10)
                            }
                            .padding(.horizontal)
                            
                            Spacer()
                        }
                        .padding()
                    }
                } else {
                    // If credentials are already set, you can show your regular connected UI.
                    ConnectedModuleView().onAppear {
                        // Restart services when view appears with existing credentials
                        clipboardSyncManager.startServices()
                    }
                }
                Spacer()
            }.multilineTextAlignment(.center)
            .onAppear {
                // Set encryption password on app launch if credentials exist
                if !syncPassword.isEmpty && !ClipboardEncryption.isPasswordSet {
                    ClipboardEncryption.setPassword(syncPassword)
                }
                
                ClipboardSyncManager.shared.setBLEUUID(userName)
            }
    }
}
