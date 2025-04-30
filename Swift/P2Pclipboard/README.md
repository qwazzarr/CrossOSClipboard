# Clipboard Sync System

This project provides a cross-platform clipboard synchronization system between Windows (C++) and iOS/macOS (Swift) devices.

## System Overview

The clipboard sync system consists of two main components:
1. **Windows Application (C++)** - Runs on Windows and handles clipboard monitoring and sharing
2. **iOS/macOS Application (Swift)** - Runs on Apple devices and syncs clipboard with Windows

The system supports two communication methods:
- **Network Discovery (DNS-SD)** - For LAN-based clipboard sync
- **Bluetooth Low Energy (BLE)** - For direct device-to-device communication

## Swift Implementation

### Core Components

1. **BLEManager.swift**
   - Handles Bluetooth Low Energy connections
   - Matches the C++ BLE UUIDs and characteristic specifications
   - Provides clipboard data sharing over BLE

2. **MDNSBrowser.swift**
   - Manages network discovery using DNS-SD
   - Connects to the Windows application over TCP
   - Provides clipboard data sharing over the network

3. **ClipboardSyncManager.swift**
   - Coordinates both BLE and network connections
   - Manages clipboard monitoring and synchronization
   - Provides a unified API for the UI

4. **ContentView.swift**
   - User interface for controlling the sync service
   - Displays connection status and clipboard data
   - Allows manual sync operations

### Integration Steps

1. **Add files to your Swift project:**
   - Copy all Swift files into your Xcode project
   - Ensure the file structure matches the import statements

2. **Configure Info.plist:**
   - Add required permissions for Bluetooth and local network:
     ```xml
     <key>NSBluetoothAlwaysUsageDescription</key>
     <string>This app uses Bluetooth to sync clipboard data with your other devices</string>
     <key>NSLocalNetworkUsageDescription</key>
     <string>This app uses your local network to discover and connect to clipboard sync devices</string>
     <key>NSBonjourServices</key>
     <array>
         <string>_clipboard._tcp</string>
     </array>
     ```

3. **Update your app's entry point:**
   - Use `ClipboardSyncApp.swift` as a reference for integration
   - Initialize the ClipboardSyncManager at the app level

4. **Link required frameworks:**
   - CoreBluetooth.framework
   - Network.framework
   - Combine.framework

## Service UUIDs

The BLE service uses the following UUIDs (matching the C++ implementation):

- **Service UUID**: `6C871015-D93C-437B-9F13-934998776FB3`
- **Write Characteristic UUID**: `009E5498-1810-444F-9FC4-0CFAE0C22F53`
- **Notify Characteristic UUID**: `49E8A9EB-97DB-4304-A7B9-99701AD3D787`
- **Device Name**: `ClipboardSyncBLE`

## Protocol Format

The clipboard sync protocol uses the following message formats:

## Usage

1. Start the Windows application
2. Launch the iOS/macOS application
3. Enable the "Start Sync Service" in the app
4. Copy text on either device
5. The clipboard content will automatically sync to the other device

## Troubleshooting

- **Bluetooth Issues**: Ensure Bluetooth is enabled on both devices and permissions are granted
- **Network Issues**: Verify both devices are on the same network and firewall allows connections
- **Connection Problems**: Check if the service is running on both devices
- **Data Not Syncing**: Try the manual "Send Clipboard" button to force synchronization

## Security Considerations

- The clipboard sync service operates on local networks only
- Data is transmitted in plaintext - don't use for sensitive information
- BLE connections have limited range and require physical proximity
