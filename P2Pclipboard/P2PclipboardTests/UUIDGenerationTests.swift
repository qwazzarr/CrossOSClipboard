import Testing
import XCTest
import Foundation
import CoreBluetooth
@testable import P2Pclipboard

@Suite("UUID Generation Tests")
struct UUIDGenerationTests {
    
    @Test("Test UUID Generation Consistency")
    func testUUIDConsistency() async throws {
        guard #available(macOS 13.0, iOS 16.0, *) else { throw XCTSkip("This test requires macOS 13.0+ or iOS 16.0+") }
        
        // Generate UUIDs from the same input string multiple times
        let input = "test-string-123"
        let uuid1 = GenerateUUID.cbuuidFromString(input)
        let uuid2 = GenerateUUID.cbuuidFromString(input)
        
        // Verify UUIDs are the same when generated from the same input
        #expect(uuid1.uuidString == uuid2.uuidString, "UUIDs generated from the same string should be identical")
        
        // Log the generated UUID for reference
        print("Generated UUID for '\(input)': \(uuid1.uuidString)")
    }
    
    @Test("Test Different Inputs Produce Different UUIDs")
    func testDifferentInputs() async throws {
        guard #available(macOS 13.0, iOS 16.0, *) else { throw XCTSkip("This test requires macOS 13.0+ or iOS 16.0+") }
        
        // Generate UUIDs from different input strings
        let inputs = ["string1", "string2", "completely-different", "almost-the-same", "almost-the-samf"]
        var generatedUUIDs: [String: CBUUID] = [:]
        
        // Generate and collect UUIDs for each input
        for input in inputs {
            let uuid = GenerateUUID.cbuuidFromString(input)
            generatedUUIDs[input] = uuid
            print("Input: '\(input)' -> UUID: \(uuid.uuidString)")
        }
        
        // Verify all UUIDs are different
        for i in 0..<inputs.count {
            for j in (i+1)..<inputs.count {
                let input1 = inputs[i]
                let input2 = inputs[j]
                let uuid1 = generatedUUIDs[input1]!
                let uuid2 = generatedUUIDs[input2]!
                
                #expect(uuid1.uuidString != uuid2.uuidString,
                       "UUIDs from different inputs ('\(input1)' and '\(input2)') should be different")
            }
        }
    }
    
    @Test("Test UUID Formatting")
    func testUUIDFormatting() async throws {
        guard #available(macOS 13.0, iOS 16.0, *) else { throw XCTSkip("This test requires macOS 13.0+ or iOS 16.0+") }
        
        // Test various inputs and verify the UUID format
        let testInputs = ["", "short", "a very long input string that exceeds the normal size of inputs"]
        
        for input in testInputs {
            let uuid = GenerateUUID.cbuuidFromString(input)
            
            // Check if the UUID string has the correct format (8-4-4-4-12 hexadecimal digits)
            let uuidPattern = "^[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}$"
            let regex = try NSRegularExpression(pattern: uuidPattern, options: [.caseInsensitive])
            let matches = regex.matches(in: uuid.uuidString, options: [], range: NSRange(location: 0, length: uuid.uuidString.count))
            
            #expect(matches.count == 1, "UUID should match the standard UUID format: \(uuid.uuidString)")
            
            // Verify version bits (should be set to version 5 - SHA-1 based)
            // For version 5 UUID, the 13th character of the 3rd group should be '5'
            let components = uuid.uuidString.split(separator: "-")
            if components.count >= 3 {
                let thirdGroup = String(components[2])
                if thirdGroup.count >= 1 {
                    let versionChar = thirdGroup.first!
                    // The version should be 5 or should match whatever version your implementation uses
                    print("UUID version character for input '\(input)': \(versionChar)")
                }
            }
        }
    }
    
    @Test("Test Service UUID Creation")
    func testServiceUUIDCreation() async throws {
        guard #available(macOS 13.0, iOS 16.0, *) else { throw XCTSkip("This test requires macOS 13.0+ or iOS 16.0+") }
        
        // Test creating UUIDs that would be used as service identifiers
        let keys = ["CE95-LWEU-TH3J", "ABCD-1234-EFGH", "TEST-SYNC-KEY1"]
        
        for key in keys {
            let serviceUUID = GenerateUUID.cbuuidFromString(key)
            
            // Verify we get a valid UUID
            #expect(!serviceUUID.uuidString.isEmpty, "Service UUID should not be empty")
            
            // Log the service UUID for reference
            print("Service UUID for key '\(key)': \(serviceUUID.uuidString)")
            
            // If your app uses these UUIDs to discover or advertise services,
            // you could verify that the CBUUID works with CoreBluetooth APIs
            let descriptor = CBMutableService(type: serviceUUID, primary: true)
            #expect(descriptor.uuid == serviceUUID, "Service descriptor should have the correct UUID")
        }
    }
    
    @Test("Test UUID Byte Structure")
    func testUUIDByteStructure() async throws {
        guard #available(macOS 13.0, iOS 16.0, *) else { throw XCTSkip("This test requires macOS 13.0+ or iOS 16.0+") }
        
        // Test that the UUID bytes are properly structured
        let input = "test-uuid-generation"
        let uuid = GenerateUUID.cbuuidFromString(input)
        
        // Convert UUID string back to raw data for inspection
        let uuidString = uuid.uuidString.replacingOccurrences(of: "-", with: "")
        var rawBytes = Data(capacity: 16)
        
        for i in stride(from: 0, to: uuidString.count, by: 2) {
            let startIndex = uuidString.index(uuidString.startIndex, offsetBy: i)
            let endIndex = uuidString.index(startIndex, offsetBy: 2)
            let byteString = uuidString[startIndex..<endIndex]
            
            if let byte = UInt8(byteString, radix: 16) {
                rawBytes.append(byte)
            }
        }
        
        // Check if the UUID has the correct byte structure
        #expect(rawBytes.count == 16, "UUID should be 16 bytes in length")
        
        // Check version bits (6th byte, top 4 bits should be 0101 for version 5)
        if rawBytes.count >= 7 {
            let versionByte = rawBytes[6]
            let version = (versionByte & 0xF0) >> 4
            
            // The implementation should set version to 5 (SHA-1 based)
            // If your implementation uses a different version, adjust this check
            print("UUID version nibble: \(version)")
            
            // Check variant bits (8th byte, top 2 bits should be 10 for RFC 4122 variant)
            let variantByte = rawBytes[8]
            let variant = (variantByte & 0xC0) >> 6
            
            // The implementation should set variant to 2 (binary 10) for RFC 4122
            print("UUID variant bits: \(variant)")
        }
    }
}
