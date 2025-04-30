

# P2P Clipboard Sync

A cross-platform clipboard synchronization application that allows real-time sharing of clipboard content between devices using Bluetooth Low Energy (BLE) and TCP/IP networking via mDNS (Bonjour).

## Overview

P2P Clipboard Sync enables seamless clipboard synchronization across devices running iOS, macOS, and Windows. The application supports:

- Text synchronization
- Image synchronization (JPEG/PNG with configurable compression)
- Automatic device discovery via Bluetooth and mDNS
- Secure data transfer with encryption
- Fallback mechanisms when either Bluetooth or network connectivity is unavailable

## Project Structure

The project consists of two main implementations:

1. **Swift implementation** for iOS and macOS
2. **C++ implementation** for Windows

### Swift Implementation (iOS/macOS)

The Swift codebase handles both iOS and macOS platforms with a shared core and platform-specific adaptations where necessary.

Key components:
- `ClipboardSyncManager`: Core manager class that coordinates all functionality
- `BLEManager`: Handles Bluetooth Low Energy communication
- `MDNSBrowser`: Manages network discovery and TCP communication
- `MessageProtocol`: Defines the data format and chunking for cross-platform communication
- `ClipboardImageHandler`: Processes clipboard images for efficient transfer

### C++ Implementation (Windows)

The Windows implementation uses native Win32 APIs, WinRT for Bluetooth functionality, and Bonjour for network discovery.

Key components:
- `ClipboardManager`: Monitors and manages Windows clipboard
- `BLEManager`: Handles Bluetooth Low Energy peripheral mode
- `NetworkManager`: Manages TCP/IP connections and Bonjour service advertisement
- `MessageProtocol`: Implements the same protocol as the Swift version for cross-platform compatibility

## Requirements

### macOS/iOS Requirements
- iOS 14.0+ / macOS 11.0+
- Xcode 13.0+
- Swift 5.5+
- Bluetooth capability
- Network capability with Bonjour support

### Windows Requirements
- Windows 10 version 1803 or newer
- Visual Studio 2019+ with C++17 support
- **Bonjour for Windows** (Apple's mDNS implementation)
- Bluetooth adapter that **supports BLE peripheral mode** (important: many adapters only support central mode)
- Windows 10 SDK 17134 or newer

## Setup and Installation

### macOS/iOS Setup
1. Clone the repository
2. Open the Xcode project
3. Select your target device
4. Build and run the application

### Windows Setup
1. Clone the repository
2. Install [Bonjour for Windows](https://developer.apple.com/bonjour/)
3. Open the Visual Studio solution
4. Ensure your Bluetooth adapter supports peripheral mode (check manufacturer specifications)
5. Build and run the application

## Usage

1. Launch the application on two or more devices
2. On first launch, choose either "Generate Key" or "Connect to other device"
3. If generating a key, share the displayed key with other devices
4. Enter the same password on all devices
5. Once connected, any content copied to the clipboard on one device will automatically appear on all connected devices

## Troubleshooting

### Windows BLE Peripheral Mode
Not all Bluetooth adapters support BLE peripheral (advertiser) mode on Windows. If you encounter issues with BLE connectivity:
- Verify your Bluetooth adapter specifications for peripheral mode support
- Update to the latest Bluetooth drivers from your adapter manufacturer
- Some Intel and Broadcom adapters are known to work well with peripheral mode

### Bonjour Service
If devices are not discovering each other over the network:
- Ensure Bonjour for Windows is properly installed
- Check that required ports (5353 for mDNS and 8080 for the application) are not blocked by firewalls
- Verify all devices are on the same network subnet

## Technical Details

### Message Protocol
The application uses a custom binary protocol for clipboard data transmission that works across all platforms:

- 4-byte message length
- 2-byte protocol version
- 1-byte content type
- 4-byte transfer ID
- 4-byte chunk index
- 4-byte total chunks count
- Variable-length payload

This allows for efficient chunking of large data for BLE transmission while maintaining data integrity.

### Security
Clipboard data can be optionally encrypted using:
- AES-GCM encryption with 256-bit keys
- HKDF for password-based key derivation
- Unique nonce for each message

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under the MIT License - see the LICENSE file for details.
