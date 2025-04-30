import Foundation
import CoreBluetooth
@testable import P2Pclipboard

// MARK: - BLE Protocol-Based Testing Approach

// Protocol for anything that can be a peripheral-like object
protocol PeripheralType {
    var name: String? { get }
    var identifier: UUID { get }
    var state: CBPeripheralState { get }
    var services: [ServiceType]? { get }
    
    func discoverServices(_ serviceUUIDs: [CBUUID]?)
    func discoverCharacteristics(_ characteristicUUIDs: [CBUUID]?, for service: ServiceType)
    func setNotifyValue(_ enabled: Bool, for characteristic: CharacteristicType)
    func writeValue(_ data: Data, for characteristic: CharacteristicType, type: CBCharacteristicWriteType)
    
    // For testing
    func simulateValueUpdate(data: Data, for characteristic: CharacteristicType)
}

// Protocol for service-like objects
protocol ServiceType {
    var uuid: CBUUID { get }
    var isPrimary: Bool { get }
    var characteristics: [CharacteristicType]? { get }
}

// Protocol for characteristic-like objects
protocol CharacteristicType {
    var uuid: CBUUID { get }
    var properties: CBCharacteristicProperties { get }
    var value: Data? { get }
    var isNotifying: Bool { get }
    var service: ServiceType? { get }
}

// MARK: - Mock Implementations

class MockPeripheral: PeripheralType {
    var name: String?
    var identifier: UUID
    private var _state: CBPeripheralState = .disconnected
    var state: CBPeripheralState { return _state }
    
    var services: [ServiceType]?
    private var delegate: Any? // This would be the equivalent of CBPeripheralDelegate
    
    // Lookup tables for quick access
    private var characteristicsByUUID: [CBUUID: MockCharacteristic] = [:]
    private var servicesByUUID: [CBUUID: MockService] = [:]
    
    init(name: String?, identifier: UUID = UUID()) {
        self.name = name
        self.identifier = identifier
    }
    
    func setState(_ newState: CBPeripheralState) {
        _state = newState
    }
    
    func setDelegate(_ delegate: Any) {
        self.delegate = delegate
    }
    
    func addService(_ service: MockService) {
        if services == nil {
            services = []
        }
        services?.append(service)
        servicesByUUID[service.uuid] = service
    }
    
    // MARK: - PeripheralType Protocol Methods
    
    func discoverServices(_ serviceUUIDs: [CBUUID]?) {
        // In a real implementation, we'd call the appropriate delegate method
        // Here, we'll just simulate it being called after a delay
        
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in
            // This would call the delegate.peripheral(_:didDiscoverServices:) method
            print("Mock peripheral discovered services")
        }
    }
    
    func discoverCharacteristics(_ characteristicUUIDs: [CBUUID]?, for service: ServiceType) {
        guard let mockService = service as? MockService else { return }
        
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { [weak self] in
            // This would call the delegate.peripheral(_:didDiscoverCharacteristicsFor:error:) method
            print("Mock peripheral discovered characteristics for service \(service.uuid)")
        }
    }
    
    func setNotifyValue(_ enabled: Bool, for characteristic: CharacteristicType) {
        guard let mockCharacteristic = characteristic as? MockCharacteristic else { return }
        
        mockCharacteristic.setNotifying(enabled)
        
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { [weak self] in
            // This would call the delegate.peripheral(_:didUpdateNotificationStateFor:error:) method
            print("Mock peripheral updated notification state for characteristic \(characteristic.uuid)")
        }
    }
    
    func writeValue(_ data: Data, for characteristic: CharacteristicType, type: CBCharacteristicWriteType) {
        guard let mockCharacteristic = characteristic as? MockCharacteristic else { return }
        
        mockCharacteristic.setValue(data)
        
        if type == .withResponse {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { [weak self] in
                // This would call the delegate.peripheral(_:didWriteValueFor:error:) method
                print("Mock peripheral wrote value to characteristic \(characteristic.uuid)")
            }
        }
    }
    
    // MARK: - Testing Methods
    
    func simulateValueUpdate(data: Data, for characteristic: CharacteristicType) {
        guard let mockCharacteristic = characteristic as? MockCharacteristic else { return }
        
        mockCharacteristic.setValue(data)
        
        DispatchQueue.main.async { [weak self] in
            // This would call the delegate.peripheral(_:didUpdateValueFor:error:) method
            print("Mock peripheral updated value for characteristic \(characteristic.uuid)")
        }
    }
}

class MockService: ServiceType {
    var uuid: CBUUID
    var isPrimary: Bool
    var characteristics: [CharacteristicType]?
    
    init(uuid: CBUUID, isPrimary: Bool = true, characteristics: [CharacteristicType]? = nil) {
        self.uuid = uuid
        self.isPrimary = isPrimary
        self.characteristics = characteristics
    }
    
    func addCharacteristic(_ characteristic: MockCharacteristic) {
        if characteristics == nil {
            characteristics = []
        }
        characteristics?.append(characteristic)
        characteristic.setService(self)
    }
}

class MockCharacteristic: CharacteristicType {
    var uuid: CBUUID
    var properties: CBCharacteristicProperties
    private var _value: Data?
    var value: Data? { return _value }
    private var _isNotifying: Bool = false
    var isNotifying: Bool { return _isNotifying }
    private weak var _service: ServiceType?
    var service: ServiceType? { return _service }
    
    init(uuid: CBUUID, properties: CBCharacteristicProperties, value: Data? = nil) {
        self.uuid = uuid
        self.properties = properties
        self._value = value
    }
    
    func setValue(_ data: Data) {
        _value = data
    }
    
    func setNotifying(_ enabled: Bool) {
        _isNotifying = enabled
    }
    
    func setService(_ service: ServiceType) {
        _service = service
    }
}

// MARK: - Mock Central Manager

class MockCentralManager {
    enum State {
        case unknown
        case resetting
        case unsupported
        case unauthorized
        case poweredOff
        case poweredOn
        
        var cbManagerState: CBManagerState {
            switch self {
            case .unknown: return .unknown
            case .resetting: return .resetting
            case .unsupported: return .unsupported
            case .unauthorized: return .unauthorized
            case .poweredOff: return .poweredOff
            case .poweredOn: return .poweredOn
            }
        }
    }
    
    private var state: State = .poweredOff
    private var delegate: Any? // This would be CBCentralManagerDelegate
    private var peripherals: [UUID: MockPeripheral] = [:]
    private var connectedPeripherals: [UUID: MockPeripheral] = [:]
    private var isScanning = false
    
    init(state: State = .poweredOff) {
        self.state = state
    }
    
    func setDelegate(_ delegate: Any) {
        self.delegate = delegate
    }
    
    func setState(_ newState: State) {
        state = newState
        
        // This would call delegate.centralManagerDidUpdateState
        DispatchQueue.main.async { [weak self] in
            print("Mock central manager state changed to \(newState)")
        }
    }
    
    func scanForPeripherals(withServices serviceUUIDs: [CBUUID]?, options: [String: Any]? = nil) {
        guard state == .poweredOn else { return }
        
        isScanning = true
        
        // This would call delegate.centralManager(_:didDiscover:advertisementData:rssi:) for each peripheral
        for (_, peripheral) in peripherals {
            let advertisementData: [String: Any] = [
                CBAdvertisementDataLocalNameKey: peripheral.name ?? "Unknown",
                CBAdvertisementDataServiceUUIDsKey: serviceUUIDs ?? []
            ]
            
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { [weak self] in
                print("Mock central manager discovered peripheral \(peripheral.identifier)")
            }
        }
    }
    
    func stopScan() {
        isScanning = false
    }
    
    func connect(_ peripheral: PeripheralType, options: [String: Any]? = nil) {
        guard state == .poweredOn, let mockPeripheral = peripheral as? MockPeripheral else { return }
        
        mockPeripheral.setState(.connecting)
        
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
            guard let self = self else { return }
            
            mockPeripheral.setState(.connected)
            self.connectedPeripherals[mockPeripheral.identifier] = mockPeripheral
            
            // This would call delegate.centralManager(_:didConnect:)
            print("Mock central manager connected to peripheral \(peripheral.identifier)")
        }
    }
    
    func cancelPeripheralConnection(_ peripheral: PeripheralType) {
        guard let mockPeripheral = peripheral as? MockPeripheral else { return }
        
        mockPeripheral.setState(.disconnecting)
        
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in
            guard let self = self else { return }
            
            mockPeripheral.setState(.disconnected)
            self.connectedPeripherals.removeValue(forKey: mockPeripheral.identifier)
            
            // This would call delegate.centralManager(_:didDisconnectPeripheral:error:)
            print("Mock central manager disconnected from peripheral \(peripheral.identifier)")
        }
    }
    
    // MARK: - Testing Methods
    
    func addPeripheral(_ peripheral: MockPeripheral) {
        peripherals[peripheral.identifier] = peripheral
    }
}

// MARK: - Mock BLE Manager

class MockBLEManager: BLEManager {
    private var mockCentralManager: MockCentralManager
    private var mockPeripherals: [MockPeripheral] = []
    private var connectedPeripheral: MockPeripheral?
    
    // Callbacks
    private var wakeupCallback: BLEWakeupCallback?
    private var connectionCallback: BLEConnectionCallback?
    private var messageCallback: BLEMessageCallback?
    
    // Service and characteristic UUIDs
    let serviceUUID = CBUUID(string: "6C871015-D93C-437B-9F13-9349987E6FB3")
    let wakeupCharUUID = CBUUID(string: "84FB7F28-93DA-4A5B-8172-2545B391E2C6")
    let dataCharUUID = CBUUID(string: "D752C5FB-1A50-4682-B308-593E96CE1E5D")
    
    // Mock state
    private var _isScanning = false
    override var isScanning: Bool { return _isScanning }
    
    private var _isAvailable = false
    override var isAvailable: Bool { return _isAvailable }
    
    override init() {
        self.mockCentralManager = MockCentralManager(state: .poweredOff)
        super.init()
    }
    
    // MARK: - BLEManager Methods
    
    override func setWakeupCallback(_ callback: @escaping BLEWakeupCallback) {
        wakeupCallback = callback
    }
    
    override func setConnectionCallback(_ callback: @escaping BLEConnectionCallback) {
        connectionCallback = callback
    }
    
    override func setMessageCallback(_ callback: @escaping BLEMessageCallback) {
        messageCallback = callback
    }
    
    override func startScanning() {
        _isScanning = true
        
        // Simulate scanning start
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
            guard let self = self else { return }
            
            // Start mock scanning
            self.mockCentralManager.scanForPeripherals(withServices: [self.serviceUUID])
        }
    }
    
    override func stopScanning() {
        _isScanning = false
        mockCentralManager.stopScan()
    }
    
    override func disconnect() {
        if let peripheral = connectedPeripheral {
            mockCentralManager.cancelPeripheralConnection(peripheral)
            connectedPeripheral = nil
        }
    }
    
    override func resetDiscoveredDevices() {
        mockPeripherals = []
    }
    
    override func respondToWakeup(useTCP: Bool) {
        // Store the response for testing
        let responseType = useTCP ? "USE_TCP" : "USE_BLE"
        print("Mock BLE Manager: Responding to wakeup with \(responseType)")
    }
    
    override func sendMessage(message: String) {
        print("Mock BLE Manager: Sending message via GATT: \(message)")
    }
    
    // MARK: - Testing Methods
    
    func addMockDeviceForDiscovery(name: String, identifier: UUID = UUID()) {
        let peripheral = MockPeripheral(name: name, identifier: identifier)
        
        // Create service and characteristics
        let service = MockService(uuid: serviceUUID)
        
        // Create wakeup characteristic
        let wakeupChar = MockCharacteristic(
            uuid: wakeupCharUUID,
            properties: [.read, .write, .notify]
        )
        
        // Create data characteristic
        let dataChar = MockCharacteristic(
            uuid: dataCharUUID,
            properties: [.read, .write, .notify]
        )
        
        // Set characteristics on service
        service.addCharacteristic(wakeupChar)
        service.addCharacteristic(dataChar)
        
        // Set services on peripheral
        peripheral.addService(service)
        
        // Add to mock central manager
        mockCentralManager.addPeripheral(peripheral)
        mockPeripherals.append(peripheral)
    }
    
    func simulateBluetoothStateChange(to state: CBManagerState) {
        // Convert CBManagerState to our internal State enum
        let mockState: MockCentralManager.State
        switch state {
        case .unknown: mockState = .unknown
        case .resetting: mockState = .resetting
        case .unsupported: mockState = .unsupported
        case .unauthorized: mockState = .unauthorized
        case .poweredOff: mockState = .poweredOff
        case .poweredOn: mockState = .poweredOn
        @unknown default: mockState = .unknown
        }
        
        mockCentralManager.setState(mockState)
        
        // Update available state based on Bluetooth state
        _isAvailable = (state == .poweredOn)
    }
    
    func simulateConnection(to peripheralName: String) {
        guard let peripheral = mockPeripherals.first(where: { $0.name == peripheralName }) else {
            print("Mock peripheral with name '\(peripheralName)' not found")
            return
        }
        
        connectedPeripheral = peripheral
        connectedDevice = peripheral as? CBPeripheral
        
        // Trigger connection callback
        DispatchQueue.main.async { [weak self] in
            guard let self = self, let callback = self.connectionCallback else { return }
            callback(true)
        }
    }
    
    func simulateDisconnection() {
        connectedPeripheral = nil
        connectedDevice = nil
        
        // Trigger connection callback
        DispatchQueue.main.async { [weak self] in
            guard let self = self, let callback = self.connectionCallback else { return }
            callback(false)
        }
    }
    
    func simulateWakeupNotification() {
        DispatchQueue.main.async { [weak self] in
            guard let self = self, let callback = self.wakeupCallback else { return }
            callback()
        }
    }
    
    func simulateMessageReceived(_ message: String) {
        DispatchQueue.main.async { [weak self] in
            guard let self = self, let callback = self.messageCallback else { return }
            callback(message)
        }
    }
}
