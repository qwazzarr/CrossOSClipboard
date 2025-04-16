import Foundation
import CoreBluetooth
import SwiftUI

// Callbacks for BLE communication
typealias BLEMessageCallback = (Data, MessageContentType) -> Void
typealias BLEConnectionCallback = (Bool) -> Void
typealias BLEWakeupCallback = () -> Void

// Response types for wakeup - matching the C++ implementation
enum ClientResponseType: UInt8 {
    case USE_BLE = 0x01
    case USE_TCP = 0x02
}

@MainActor
class BLEManager: NSObject, ObservableObject {
    // Published properties for UI updates
    @Published var isScanning = false
    @Published var connectedDevice: CBPeripheral?
    @Published var discoveredDevices: [CBPeripheral] = []
    @Published var isAvailable = false
    
    private var pendingScan = false
    
    // BLE connection constants
    private var serviceUUID = CBUUID(string: "6C871015-D93C-437B-9F13-9349987E6FB3")
    private let wakeupCharUUID = CBUUID(string: "84FB7F28-93DA-4A5B-8172-2545B391E2C6")
    private let dataCharUUID = CBUUID(string: "D752C5FB-1A50-4682-B308-593E96CE1E5D")
    
    // CoreBluetooth objects
    private var centralManager: CBCentralManager?
    private var wakeupCharacteristic: CBCharacteristic?
    private var dataCharacteristic: CBCharacteristic?
    
    // Callbacks
    private var wakeupCallback: BLEWakeupCallback?
    private var connectionCallback: BLEConnectionCallback?
    private var messageCallback: BLEMessageCallback?
    
    // Connection handling
    private var reconnectTimer: Timer?
    private var scanTimeoutTimer: Timer?
    private var connectionAttempt = 0
    private var maxConnectionAttempts = 3
    private var lastConnectedPeripheralIdentifier: UUID?
    
    //handling writequeues
    private var writeQueue: [Data] = []
    private var isTransferringData = false
    private var currentWriteOperation: (chunks: [Data], contentType: MessageContentType, startTime: Date, totalBytes: Int)?
    private var currentChunkIndex = 0
    
    
    // Track discovered devices to avoid duplicate connections
    private var discoveredDeviceIds = Set<UUID>()
    
    // Flag to manage connection state
    private var isConnecting = false
    
    override init() {
        super.init()
        // Initialize centralManager in init to ensure it's always available
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }
    
    deinit {
        
        Task { @MainActor in
            print("BLEManager deinit called")
            cancelAllTimers()
            cleanupConnection(resetCentralManager: true)
        }
    }
    
    // MARK: - Public API
    
    func setWakeupCallback(_ callback: @escaping BLEWakeupCallback) {
        wakeupCallback = callback
    }
    
    func setConnectionCallback(_ callback: @escaping BLEConnectionCallback) {
        connectionCallback = callback
    }
    
    func setMessageCallback(_ callback: @escaping BLEMessageCallback) {
        messageCallback = callback
    }
    
    func setServiceUUID(_ key: String) -> CBUUID {
        // Convert the key to a UUID string using the deterministic generator
        let uuid = GenerateUUID.cbuuidFromString(key)
        
        // Create a CBUUID from the generated UUID string
        serviceUUID = uuid
        
        print("Service UUID set to: \(serviceUUID.uuidString) (from key: \(key))")
        
        // If already scanning/advertising, restart to use the new UUID
        
        return serviceUUID
    }
    
    func stopScanning() {
        print("Stopping BLE scan...")
        cancelScanTimeoutTimer()
        
        if let manager = centralManager, isScanning {
            manager.stopScan()
            isScanning = false
            print("Stopped scanning for BLE devices")
        }
    }
    
    func disconnect() {
        print("Disconnecting from BLE device...")
        cleanupConnection(resetCentralManager: false)
    }
    
    func resetDiscoveredDevices() {
        print("Resetting discovered devices list")
        discoveredDeviceIds.removeAll()
        discoveredDevices.removeAll()
    }
    
    // Respond to wake-up notification
    func respondToWakeup(useTCP: Bool) {
        guard let peripheral = connectedDevice,
              let characteristic = wakeupCharacteristic,
              peripheral.state == .connected else {
            print("Cannot respond to wakeup - no connected device or wakeup characteristic")
            return
        }
        
        let responseType: ClientResponseType = useTCP ? .USE_TCP : .USE_BLE
        let data = Data([responseType.rawValue])
        
        print("Responding to wakeup with: \(useTCP ? "USE_TCP" : "USE_BLE")")
        
        peripheral.writeValue(data, for: characteristic, type: .withResponse)
    }
    
    func sendMessage(data: Data, contentType: MessageContentType) {
        guard let peripheral = connectedDevice,
              let characteristic = dataCharacteristic,
              peripheral.state == .connected else {
            print("Cannot send message - no connected device or data characteristic")
            return
        }
        
        // Record the start time and total bytes
        let startTime = Date()
        let totalBytes = data.count
        
        // Encode message using MessageProtocol
        let transportType: TransportType = .ble
        let encodedChunks = MessageProtocol.encodeMessage(contentType: contentType, payload: data, transport: transportType)
        
        print("Queuing \(encodedChunks.count) chunks via BLE GATT characteristic (\(totalBytes) bytes total)")
        
        // Store the write operation
        currentWriteOperation = (chunks: encodedChunks, contentType: contentType, startTime: startTime, totalBytes: totalBytes)
        currentChunkIndex = 0
        
        // Start the sending process
        sendNextChunk()
    }
    
    
    private func sendNextChunk() {
        guard let peripheral = connectedDevice,
              let characteristic = dataCharacteristic,
              peripheral.state == .connected,
              let currentOp = currentWriteOperation,
              currentChunkIndex < currentOp.chunks.count else {
            // If we're done or can't continue, reset state
            completeCurrentTransfer()
            return
        }
        
        // Check if the peripheral is ready to receive data
        if peripheral.canSendWriteWithoutResponse {
            let chunk = currentOp.chunks[currentChunkIndex]
            
            peripheral.writeValue(chunk, for: characteristic, type: .withoutResponse)
            print("Sent chunk \(currentChunkIndex + 1)/\(currentOp.chunks.count) (\(chunk.count) bytes)")
            
            // Move to the next chunk
            currentChunkIndex += 1
            
            // If we've sent all chunks, complete the transfer
            if currentChunkIndex >= currentOp.chunks.count {
                completeCurrentTransfer()
            } else {
                // Continue sending after a small delay
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.01) { [weak self] in
                    self?.sendNextChunk()
                }
            }
        } else {
            // If not ready, wait for the peripheral to notify us
            print("Peripheral not ready for write without response, waiting...")
            // We'll continue in the peripheralIsReadyToSendWriteWithoutResponse delegate method
        }
    }
    
    // New method to complete the current transfer and report stats
    private func completeCurrentTransfer() {
        guard let currentOp = currentWriteOperation else { return }
        
        let elapsedTime = Date().timeIntervalSince(currentOp.startTime) // in seconds
        let throughput = Double(currentOp.totalBytes) / elapsedTime // bytes per second
        
        print("Message sent: Total bytes: \(currentOp.totalBytes)")
        print(String(format: "Time required: %.3f seconds", elapsedTime))
        print(String(format: "Throughput: %.2f bytes/second", throughput))
        
        // Reset the current operation
        currentWriteOperation = nil
        currentChunkIndex = 0
    }
    
    // MARK: - Connection Management
    
    private func cancelAllTimers() {
        reconnectTimer?.invalidate()
        reconnectTimer = nil
        
        cancelScanTimeoutTimer()
    }
    
    private func cancelScanTimeoutTimer() {
        scanTimeoutTimer?.invalidate()
        scanTimeoutTimer = nil
    }
    
    private func cleanupConnection(resetCentralManager: Bool = false) {
        print("Cleaning up BLE connection (resetCentralManager: \(resetCentralManager))")
        
        // Cancel all timers first to prevent racing conditions
        cancelAllTimers()
        
        // Reset connection tracking
        isConnecting = false
        
        pendingScan = false
        
        // If we have an active peripheral, cancel any pending connection
        if let peripheral = connectedDevice {
            if peripheral.state == .connected || peripheral.state == .connecting {
                if let wakeupChar = wakeupCharacteristic {
                    peripheral.setNotifyValue(false, for: wakeupChar)
                }
                if let dataChar = dataCharacteristic {
                    peripheral.setNotifyValue(false, for: dataChar)
                }
                
                centralManager?.cancelPeripheralConnection(peripheral)
                print("Cancelled connection to peripheral: \(peripheral.identifier)")
            }
        }
        
        // Clear characteristics and connected device reference
        wakeupCharacteristic = nil
        dataCharacteristic = nil
        connectedDevice = nil
        
        // Stop scanning if active
        if isScanning {
            stopScanning()
        }
        
        // Reset connection attempt counter
        connectionAttempt = 0
        
        // Optionally reset the central manager completely
        if resetCentralManager {
            if let cm = centralManager {
                cm.delegate = nil
                centralManager = nil
                print("Central manager reset")
            }
        }
    }

    func startScanning() {
         print("Queueing BLE scan request...")
         
         // Clean up any existing connection first
         cleanupConnection(resetCentralManager: false)
         
         // Reset discovered devices list
         resetDiscoveredDevices()
         
         // Make sure we have a working centralManager
         if centralManager == nil {
             centralManager = CBCentralManager(delegate: self, queue: nil)
         } else {
             centralManager?.delegate = self
         }
         
         // Check if Bluetooth is ready
         if let manager = centralManager, manager.state == .poweredOn {
             // Bluetooth is already on, start scanning immediately
             startScanningInternal()
         } else {
             // Queue the scan request for when Bluetooth is ready
             pendingScan = true
             print("Bluetooth not yet ready, scan queued")
         }
     }
    
    private func startScanningInternal() {
        guard let manager = centralManager, manager.state == .poweredOn else {
            print("Cannot start scan - Bluetooth not powered on")
            return
        }
        
        isScanning = true
        connectionAttempt = 0
        
        print("Started scanning for BLE devices with service UUID: \(serviceUUID)")
        
        // Set scan options - don't allow duplicates to reduce callback frequency
        let scanOptions: [String: Any] = [
            CBCentralManagerScanOptionAllowDuplicatesKey: false
        ]
        
        manager.scanForPeripherals(
            withServices: [serviceUUID],
            options: scanOptions
        )
        
        // Setup a timeout to stop scanning after 30 seconds if nothing is found
        DispatchQueue.main.asyncAfter(deadline: .now() + 30) { [weak self] in
            guard let self = self, self.isScanning else { return }
            
            if self.connectedDevice == nil {
                print("Scan timeout - stopping scan after 30 seconds")
                self.stopScanning()
            }
        }
        
    }
    
    private func attemptReconnect() {
        // Check if we have a peripheral to reconnect to
        guard let identifier = lastConnectedPeripheralIdentifier,
              connectionAttempt < maxConnectionAttempts,
              centralManager?.state == .poweredOn ,
              !isConnecting else {
            print("Cannot reconnect - no previous peripheral, too many attempts, or already connecting")
            return
        }
        
        connectionAttempt += 1
        print("Attempting reconnection (attempt \(connectionAttempt)/\(maxConnectionAttempts))...")
        
        // Look for the device in our discoveries
        if let peripheral = discoveredDevices.first(where: { $0.identifier == identifier }) {
            print("Found cached peripheral, attempting to reconnect")
            connectToPeripheral(peripheral)
        } else {
            // Start scanning to find the device again
            print("Peripheral not in cache, scanning to find it")
            startScanning()
        }
    }
    
    // In connectToPeripheral method:
    private func connectToPeripheral(_ peripheral: CBPeripheral) {
        guard let manager = centralManager,
              manager.state == .poweredOn,
              !isConnecting else {
            print("Cannot connect: Bluetooth not available or already connecting")
            return
        }
        
        print("==== CONNECTION ATTEMPT START ====")
        print("Connecting to peripheral: \(peripheral.identifier)")
        print("Peripheral name: \(peripheral.name ?? "Unnamed")")
        print("Peripheral state: \(peripheralStateString(peripheral.state))")
        isConnecting = true
        
        // Stop scanning during connection attempt to reduce interference
        if isScanning {
            stopScanning()
        }
        
        // Log connection options
        print("Using connection options: notify on connection, disconnection, and notification")
        
        
        // Log connection attempt details
        print("Initiating connection to peripheral: \(peripheral.identifier)")
        
        // Initiate the connection
        manager.connect(peripheral, options: [
            CBConnectPeripheralOptionNotifyOnConnectionKey: true,
            CBConnectPeripheralOptionNotifyOnDisconnectionKey: true,
            CBConnectPeripheralOptionNotifyOnNotificationKey: true
        ])
        
        print("Connection request sent to CoreBluetooth")
    }

    // Add a helper method to convert CBPeripheralState to string
    private func peripheralStateString(_ state: CBPeripheralState) -> String {
        switch state {
        case .disconnected: return "disconnected"
        case .connecting: return "connecting"
        case .connected: return "connected"
        case .disconnecting: return "disconnecting"
        @unknown default: return "unknown (\(state.rawValue))"
        }
    }
    
    // Handle fully decoded messages
    private func handleDecodedMessage(_ message: MessageProtocol.Message) {
        messageCallback?(message.payload, message.contentType)
    }
}


// MARK: - CBCentralManagerDelegate
extension BLEManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        print("Bluetooth state changed: \(central.state.rawValue)")
        
        switch central.state {
        case .poweredOn:
            print("Bluetooth central is powered on")
            isAvailable = true
            
            // If we were previously connected, try to reconnect
            if let identifier = lastConnectedPeripheralIdentifier {
                print("Bluetooth turned on, attempting to reconnect to previous device")
                connectionAttempt = 0
                isConnecting = false
                attemptReconnect()
            }
            
            if pendingScan {
                print("Executing pending scan request")
                pendingScan = false
                startScanningInternal()
            }
            
        case .poweredOff:
            print("Bluetooth central is powered off")
            isScanning = false
            isConnecting = false
            
            // Clean up connection but keep the identifier for reconnection
            cleanupConnection(resetCentralManager: false)
            
            discoveredDevices.removeAll()
            connectionCallback?(false)
            isAvailable = false
            
        case .resetting:
            print("Bluetooth is resetting")
            // This state is often transient, followed by another state change
            isConnecting = false
            
        case .unauthorized, .unsupported, .unknown:
            print("Bluetooth is unavailable: \(central.state)")
            isAvailable = false
            isConnecting = false
            cleanupConnection(resetCentralManager: false)
            lastConnectedPeripheralIdentifier = nil  // Clear identifier as reconnection won't work
        
        @unknown default:
            print("Unknown Bluetooth state: \(central.state.rawValue)")
            isAvailable = false
            isConnecting = false
        }
    }
    
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String : Any], rssi RSSI: NSNumber) {
        // Check if this is a relevant device
        let hasService = (advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID])?.contains(serviceUUID) ?? false
        let hasManufacturerData = advertisementData[CBAdvertisementDataManufacturerDataKey] != nil

        // Debug output for advertisement data
        print("Discovered device: \(peripheral.name ?? "Unnamed") (\(peripheral.identifier))")
        print("  RSSI: \(RSSI.intValue) dBm")
        print("  Has service: \(hasService)")
        print("  Has manufacturer data: \(hasManufacturerData)")
        
        if (hasService || hasManufacturerData) {
            // Check if we've already discovered this device by UUID
            if !discoveredDeviceIds.contains(peripheral.identifier) {
                print("Adding new device to discovered list: \(peripheral.name ?? "Unnamed")")
                discoveredDeviceIds.insert(peripheral.identifier)
                discoveredDevices.append(peripheral)
                
                // If this is our previous device, prioritize reconnecting to it
                if peripheral.identifier == lastConnectedPeripheralIdentifier && !isConnecting {
                    print("Found previously connected device, connecting...")
                    connectToPeripheral(peripheral)
                    return
                }
            } else {
                // Even if discovered already, update the RSSI for signal strength tracking
                print("Rediscovered existing device: \(peripheral.name ?? "Unnamed")")
                
                // Update the stored peripheral reference to ensure it's current
                if let index = discoveredDevices.firstIndex(where: { $0.identifier == peripheral.identifier }) {
                    discoveredDevices[index] = peripheral
                }
                return
            }
            
            // Only connect to this device if we don't have any active connection and aren't already connecting
            if connectedDevice == nil && !isConnecting {
                print("No active connection, connecting to device: \(peripheral.name ?? "Unnamed")")
                connectToPeripheral(peripheral)
            }
        }
    }
    
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        print("Connected to peripheral: \(peripheral.name ?? "Unnamed") (\(peripheral.identifier))")
        
        // Reset connection tracking
        isConnecting = false
        connectionAttempt = 0
        
        // Update connected device reference
        connectedDevice = peripheral
        lastConnectedPeripheralIdentifier = peripheral.identifier
        
        // Stop scanning once connected
        if isScanning {
            stopScanning()
        }
        
        // Set up the peripheral delegate and discover services
        peripheral.delegate = self
        peripheral.discoverServices([serviceUUID])  
        
        // Notify client of connection
        connectionCallback?(true)
    }
    
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        print("Failed to connect to peripheral: \(peripheral.name ?? "Unnamed") - \(error?.localizedDescription ?? "unknown error")")
        
        // Reset connecting flag
        isConnecting = false
        
        // Clear connected device if this was the active one
        if connectedDevice?.identifier == peripheral.identifier {
            connectedDevice = nil
        }
        
        // Exponential backoff for reconnection attempts
        let delay = pow(2.0, Double(connectionAttempt)).clamped(to: 1...30)
        
        reconnectTimer?.invalidate()
        reconnectTimer = Timer.scheduledTimer(withTimeInterval: delay, repeats: false) { [weak self] _ in
            self?.attemptReconnect()
        }
    }
    private func checkMTUSizes() {
        guard let peripheral = connectedDevice else { return }
        
        let maxWriteWithoutResponse = peripheral.maximumWriteValueLength(for: .withoutResponse)
        let maxWriteWithResponse = peripheral.maximumWriteValueLength(for: .withResponse)
        
        print("Maximum Write Value Length (Without Response): \(maxWriteWithoutResponse) bytes")
        print("Maximum Write Value Length (With Response): \(maxWriteWithResponse) bytes")
        
    }
    
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        print("Disconnected from peripheral: \(peripheral.name ?? "Unnamed") - \(error?.localizedDescription ?? "no error")")
        
        // Reset connection tracking
        isConnecting = false
        
        // Only process if this was our connected device
        if peripheral.identifier == connectedDevice?.identifier {
            wakeupCharacteristic = nil
            dataCharacteristic = nil
            connectedDevice = nil
            
            // Notify client of disconnection
            connectionCallback?(false)
            
            // Attempt reconnection if the disconnect was unexpected (indicated by an error)
            if error != nil {
                print("Unexpected disconnection, scheduling reconnection attempt")
                
                // Use a short delay before reconnecting
                reconnectTimer?.invalidate()
                reconnectTimer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: false) { [weak self] _ in
                    self?.attemptReconnect()
                }
            }
        }
    }
}

// MARK: - CBPeripheralDelegate
extension BLEManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error = error {
            print("Error discovering services: \(error.localizedDescription)")
            return
        }
        
        guard let services = peripheral.services else {
            print("No services found")
            return
        }
        
        print("Discovered \(services.count) services")
        
        for service in services {
            print("Service: \(service.uuid)")
            
            if service.uuid == serviceUUID {
                print("Found target service: \(service.uuid)")
                peripheral.discoverCharacteristics([wakeupCharUUID, dataCharUUID], for: service)
            }
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error = error {
            print("Error discovering characteristics: \(error.localizedDescription)")
            return
        }
        
        guard let characteristics = service.characteristics else {
            print("No characteristics found")
            return
        }
        
        print("Discovered \(characteristics.count) characteristics for service \(service.uuid)")
        checkMTUSizes()
        for characteristic in characteristics {
            print("Characteristic: \(characteristic.uuid)")
            
            if characteristic.uuid == wakeupCharUUID {
                print("Found wakeup characteristic")
                wakeupCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
            } else if characteristic.uuid == dataCharUUID {
                print("Found data characteristic")
                dataCharacteristic = characteristic
                dataCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
            }
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if let error = error {
            print("Error changing notification state: \(error.localizedDescription)")
            return
        }
        
        let charName = characteristic.uuid == wakeupCharUUID ? "wakeup" : "data"
        print("Notification state updated for \(charName) characteristic: \(characteristic.isNotifying ? "enabled" : "disabled")")
    }
    
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard error == nil else {
            print("Error updating value: \(error!.localizedDescription)")
            return
        }
        
        guard let data = characteristic.value else {
            print("No data received in characteristic update")
            return
        }
        
        if characteristic.uuid == wakeupCharUUID {
            print("Received wakeup notification with \(data.count) bytes")
            // Call wakeup callback without passing data
            wakeupCallback?()
        } else if characteristic.uuid == dataCharUUID {
            print("Received data notification with \(data.count) bytes")
            
            // Process the chunk directly by passing to MessageProtocol
            if let message = MessageProtocol.decodeData(data) {
                // Handle the complete message
                handleDecodedMessage(message)
            }
            // If nil is returned, the chunk has been stored by MessageProtocol
            // and we'll wait for more chunks to complete the message
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error = error {
            print("Error writing to characteristic: \(error.localizedDescription)")
        } else {
            let charName = characteristic.uuid == wakeupCharUUID ? "wakeup" : "data"
            print("Successfully wrote to \(charName) characteristic")
        }
    }
    
    // Add this delegate method to your CBPeripheralDelegate extension
    func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        // Called when the peripheral is ready to receive more data
        print("Peripheral is ready to receive more data")
        sendNextChunk()
    }
}

// Helper extension for numeric clamping
extension Double {
    func clamped(to range: ClosedRange<Double>) -> Double {
        return max(range.lowerBound, min(self, range.upperBound))
    }
}
