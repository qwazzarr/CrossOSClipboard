import Foundation
import CoreBluetooth

/// Custom data type for clipboard sync service, matching C++ implementation
let CLIPBOARD_SYNC_DATA_TYPE: UInt8 = 0xA0

/// Version of the clipboard sync protocol
let CLIPBOARD_SYNC_VERSION: UInt8 = 1

/// Structure to represent the clipboard sync payload
struct ClipboardSyncPayload {
    var deviceName: String
    var version: UInt8
    var deviceId: String // Unique identifier for this device
    
    init(deviceName: String, deviceId: String, version: UInt8 = CLIPBOARD_SYNC_VERSION) {
        self.deviceName = deviceName
        self.deviceId = deviceId
        self.version = version
    }
}

class BLEPayloadManager {
    
    /// Microsoft's company ID for manufacturer-specific data
    static let microsoftCompanyId: UInt16 = 0x0006
    
    /// Magic number to identify our application (0xC5 for "ClipboardSync")
    static let magicNumber: UInt8 = 0xC5
    
    /// Create advertisement data with our custom payload using manufacturer data
    static func createAdvertisementData(from payload: ClipboardSyncPayload) -> [String: Any] {
        // Create a data object to hold our custom payload
        let data = NSMutableData()
        
        // Format:
        // [1 byte] Magic number (0xC5 for "ClipboardSync")
        // [1 byte] Protocol version
        // [1 byte] Device name length
        // [n bytes] Device name
        // [1 byte] Device ID length
        // [m bytes] Device ID
        
        // Add magic number
        var magicByte = magicNumber
        data.append(&magicByte, length: 1)
        
        // Add protocol version
        var versionByte = payload.version
        data.append(&versionByte, length: 1)
        
        // Add device name length and content
        let nameData = payload.deviceName.data(using: .utf8)!
        var nameLength = UInt8(nameData.count)
        data.append(&nameLength, length: 1)
        data.append(nameData)
        
        // Add device ID length and content
        let idData = payload.deviceId.data(using: .utf8)!
        var idLength = UInt8(idData.count)
        data.append(&idLength, length: 1)
        data.append(idData)
        
        // Create the manufacturer data dictionary
        let manufacturerData = NSMutableData()
        var companyId = microsoftCompanyId.littleEndian
        manufacturerData.append(&companyId, length: 2)
        manufacturerData.append(data as Data)
        
        // Return the advertisement data dictionary
        return [
            CBAdvertisementDataManufacturerDataKey: manufacturerData as Data,
            CBAdvertisementDataLocalNameKey: payload.deviceName
        ]
    }
    
    /// Try to parse a received advertisement to extract our payload
    static func tryParseAdvertisement(advertisementData: [String: Any]) -> ClipboardSyncPayload? {
        // First try to extract from manufacturer data
        if let manufacturerData = advertisementData[CBAdvertisementDataManufacturerDataKey] as? Data {
            // Manufacturer data should at least contain company ID (2 bytes) plus our minimum data
            if manufacturerData.count >= 5 {
                // Extract company ID (first 2 bytes)
                let companyId = UInt16(manufacturerData[0]) | (UInt16(manufacturerData[1]) << 8)
                
                // Check if this is Microsoft's company ID
                if companyId == microsoftCompanyId {
                    // Skip company ID bytes and try to parse our payload
                    var index = 2
                    
                    // Check magic number
                    guard index < manufacturerData.count,
                          manufacturerData[index] == magicNumber else {
                        return nil
                    }
                    index += 1
                    
                    // Extract version
                    guard index < manufacturerData.count else { return nil }
                    let version = manufacturerData[index]
                    index += 1
                    
                    // Extract device name
                    guard index < manufacturerData.count else { return nil }
                    let nameLength = Int(manufacturerData[index])
                    index += 1
                    
                    guard index + nameLength <= manufacturerData.count else { return nil }
                    let nameData = manufacturerData.subdata(in: index..<(index + nameLength))
                    guard let deviceName = String(data: nameData, encoding: .utf8) else { return nil }
                    index += nameLength
                    
                    // Extract device ID
                    guard index < manufacturerData.count else { return nil }
                    let idLength = Int(manufacturerData[index])
                    index += 1
                    
                    guard index + idLength <= manufacturerData.count else { return nil }
                    let idData = manufacturerData.subdata(in: index..<(index + idLength))
                    guard let deviceId = String(data: idData, encoding: .utf8) else { return nil }
                    
                    return ClipboardSyncPayload(deviceName: deviceName, deviceId: deviceId, version: version)
                }
            }
        }
        
        // If method fails, return nil
        return nil
    }
    
    /// Generate a unique device ID suitable for clipboard syncing
    static func generateDeviceId() -> String {
        // Get device model
        #if os(iOS)
        let model = UIDevice.current.model
        #elseif os(macOS)
        let model = "Mac"
        #endif
        
        // Get a unique identifier - use UUID
        let uuid = UUID().uuidString.prefix(8)
        
        // Combine for a readable but unique ID
        return "\(model)-\(uuid)"
    }
}
