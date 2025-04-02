import Foundation
import CoreBluetooth
import SwiftUI

// Callbacks for BLE communication
typealias BLEMessageCallback = (String) -> Void
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
    
    // BLE connection constants
    private let serviceUUID = CBUUID(string: "6C871015-D93C-437B-9F13-9349987E6FB3")
    private let wakeupCharUUID = CBUUID(string: "84FB7F28-93DA-4A5B-8172-2545B391E2C6")
    private let dataCharUUID = CBUUID(string: "D752C5FB-1A50-4682-B308-593E96CE1E5D")
    
    // CoreBluetooth objects
    private var centralManager: CBCentralManager!
    private var wakeupCharacteristic: CBCharacteristic?
    private var dataCharacteristic: CBCharacteristic?
    
    // Callbacks
    private var wakeupCallback: BLEWakeupCallback?
    private var connectionCallback: BLEConnectionCallback?
    private var messageCallback: BLEMessageCallback?
    
    // Connection handling
    private var reconnectTimer: Timer?
    private var connectionAttempt = 0
    private var maxConnectionAttempts = 3
    private var lastConnectedPeripheralIdentifier: UUID?
    
    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: nil, queue: nil)
        centralManager.delegate = self
    }
    
    deinit {
        // Use Task to dispatch to the main actor
        Task { @MainActor in
            self.cleanupConnection()
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
    
    func startScanning() {
        guard centralManager.state == .poweredOn else {
            print("Bluetooth not available")
            return
        }
        
        // Clear previous connections
        cleanupConnection()
        
        isScanning = true
        discoveredDevices.removeAll()
        connectionAttempt = 0
        
        print("Started scanning for BLE devices with service UUID: \(serviceUUID)")
        
        centralManager.scanForPeripherals(
            withServices: [serviceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
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
    
    func stopScanning() {
        centralManager.stopScan()
        isScanning = false
        print("Stopped scanning for BLE devices")
    }
    
    func disconnect() {
        cleanupConnection()
    }
    
    // Respond to wake-up notification
    func respondToWakeup(useTCP: Bool) {
        guard let peripheral = connectedDevice, let characteristic = wakeupCharacteristic else {
            print("Cannot respond to wakeup - no connected device or wakeup characteristic")
            return
        }
        
        let responseType: ClientResponseType = useTCP ? .USE_TCP : .USE_BLE
        let data = Data([responseType.rawValue])
        
        print("Responding to wakeup with: \(useTCP ? "USE_TCP" : "USE_BLE")")
        
        peripheral.writeValue(data, for: characteristic, type: .withResponse)
    }
    
    // Send message via BLE by writing to data characteristic
    func sendMessage(message: String) {
        guard let peripheral = connectedDevice, let characteristic = dataCharacteristic else {
            print("Cannot send message - no connected device or data characteristic")
            return
        }
        
        // Encode message using MessageProtocol
        let transportType: TransportType = .ble
        let encodedChunks = MessageProtocol.encodeTextMessage(text: message, transport: transportType)
        
        print("Sending \(encodedChunks.count) chunks via BLE GATT characteristic")
        
        // Send each chunk with a small delay between them
        for (index, chunk) in encodedChunks.enumerated() {
            // Use a delay to avoid overwhelming the peripheral
            DispatchQueue.main.asyncAfter(deadline: .now() + Double(index) * 0.05) {
                peripheral.writeValue(chunk, for: characteristic, type: .withoutResponse)
                print("Sent chunk \(index+1)/\(encodedChunks.count) (\(chunk.count) bytes)")
            }
        }
    }
    
    // MARK: - Connection Management
    
    private func cleanupConnection() {
        // Stop any reconnection timer
        reconnectTimer?.invalidate()
        reconnectTimer = nil
        
        // Unsubscribe from notifications
        if let peripheral = connectedDevice, peripheral.state == .connected {
            if let wakeupChar = wakeupCharacteristic {
                peripheral.setNotifyValue(false, for: wakeupChar)
            }
            if let dataChar = dataCharacteristic {
                peripheral.setNotifyValue(false, for: dataChar)
            }
            
            // Disconnect the peripheral
            centralManager.cancelPeripheralConnection(peripheral)
        }
        
        // Reset characteristics
        wakeupCharacteristic = nil
        dataCharacteristic = nil
        connectedDevice = nil
    }
    
    private func attemptReconnect() {
        // Check if we have a peripheral to reconnect to
        guard let identifier = lastConnectedPeripheralIdentifier,
              connectionAttempt < maxConnectionAttempts,
              centralManager.state == .poweredOn else {
            print("Cannot reconnect - no previous peripheral or too many attempts")
            return
        }
        
        connectionAttempt += 1
        print("Attempting reconnection (attempt \(connectionAttempt)/\(maxConnectionAttempts))...")
        
        // Look for the device in our discoveries
        if let peripheral = discoveredDevices.first(where: { $0.identifier == identifier }) {
            print("Found cached peripheral, attempting to reconnect")
            centralManager.connect(peripheral, options: nil)
        } else {
            // Start scanning to find the device again
            print("Peripheral not in cache, scanning to find it")
            startScanning()
        }
    }
    
    // Handle fully decoded messages
    private func handleDecodedMessage(_ message: MessageProtocol.Message) {
        switch message.contentType {
        case .plainText:
            if let text = message.stringPayload {
                print("Received text via BLE, length: \(text.count) characters")
                messageCallback?(text)
            }
        
        case .htmlContent:
            if let html = message.stringPayload {
                print("Received HTML content via BLE")
                messageCallback?(html)
            }
            
        case .pngImage, .jpegImage, .pdfDocument, .rtfText:
            // Handle binary content types
            print("Received binary content of type: \(message.contentType), size: \(message.payload.count) bytes")
            // Binary data handling would require additional callbacks
        }
    }
}

// MARK: - CBCentralManagerDelegate
extension BLEManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            print("Bluetooth central is powered on")
            isAvailable = true
            
            // If we were previously connected, try to reconnect
            if let identifier = lastConnectedPeripheralIdentifier {
                print("Bluetooth turned on, attempting to reconnect to previous device")
                connectionAttempt = 0
                attemptReconnect()
            } else {
                startScanning()
            }
            
        case .poweredOff:
            print("Bluetooth central is powered off")
            isScanning = false
            stopScanning()
            
            // Clean up connection but keep the identifier for reconnection
            cleanupConnection()
            
            discoveredDevices.removeAll()
            connectionCallback?(false)
            isAvailable = false
            
        case .resetting:
            print("Bluetooth is resetting")
            // This is often followed by powerOn again, so prepare for reconnection
            
        case .unauthorized, .unsupported, .unknown:
            print("Bluetooth is unavailable: \(central.state)")
            isAvailable = false
            cleanupConnection()
            lastConnectedPeripheralIdentifier = nil  // Clear identifier as reconnection won't work
        
        @unknown default:
            print("Unknown Bluetooth state")
            isAvailable = false
        }
    }
    
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String : Any], rssi RSSI: NSNumber) {
        // Check if this is a relevant device
        let hasService = (advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID])?.contains(serviceUUID) ?? false
        
        // Also check manufacturer data as an alternative
        let hasManufacturerData = advertisementData[CBAdvertisementDataManufacturerDataKey] != nil
        
        if (hasService || hasManufacturerData) {
            // Add to discovered devices if not already present
            if !discoveredDevices.contains(where: { $0.identifier == peripheral.identifier }) {
                print("Discovered new device: \(peripheral.name ?? "Unnamed") (\(peripheral.identifier))")
                discoveredDevices.append(peripheral)
                
                // If this is our previous device, prioritize reconnecting to it
                if peripheral.identifier == lastConnectedPeripheralIdentifier {
                    print("Found previously connected device, connecting...")
                    central.connect(peripheral, options: nil)
                    return
                }
            }
            
            // If we don't have a connected device yet, connect to this one
            if connectedDevice == nil {
                print("Connecting to device: \(peripheral.name ?? "Unnamed")")
                central.connect(peripheral, options: nil)
            }
        }
    }
    
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        print("Connected to peripheral: \(peripheral.name ?? "Unnamed")")
        
        // Stop scanning once connected
        if isScanning {
            stopScanning()
        }
        
        connectedDevice = peripheral
        lastConnectedPeripheralIdentifier = peripheral.identifier
        connectionAttempt = 0 // Reset connection attempt counter
        
        peripheral.delegate = self
        peripheral.discoverServices([serviceUUID])
        
        // Notify client of connection
        connectionCallback?(true)
    }
    
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        print("Failed to connect to peripheral: \(error?.localizedDescription ?? "unknown error")")
        
        // Schedule reconnection attempt
        reconnectTimer?.invalidate()
        reconnectTimer = Timer.scheduledTimer(withTimeInterval: 3.0, repeats: false) { [weak self] _ in
            self?.attemptReconnect()
        }
    }
    
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        print("Disconnected from peripheral: \(peripheral.name ?? "Unnamed")")
        
        if peripheral.identifier == connectedDevice?.identifier {
            wakeupCharacteristic = nil
            dataCharacteristic = nil
            connectedDevice = nil
            
            // Notify client of disconnection
            connectionCallback?(false)
            
            // Attempt reconnection if the disconnect was unexpected
            if error != nil {
                print("Unexpected disconnection: \(error?.localizedDescription ?? "unknown error")")
                
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
        
        for service in services where service.uuid == serviceUUID {
            print("Found service: \(service.uuid)")
            peripheral.discoverCharacteristics([wakeupCharUUID, dataCharUUID], for: service)
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
        
        for characteristic in characteristics {
            if characteristic.uuid == wakeupCharUUID {
                wakeupCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
                print("Subscribed to wakeup characteristic")
            } else if characteristic.uuid == dataCharUUID {
                dataCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
                print("Subscribed to data characteristic")
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
}
