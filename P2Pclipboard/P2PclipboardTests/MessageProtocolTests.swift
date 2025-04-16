import Testing
import XCTest
import Foundation
@testable import P2Pclipboard

@Suite("MessageProtocol Tests")
struct MessageProtocolTests {
    // Move availability check inside individual tests if needed
    
    @Test("Test Message Encoding and Decoding via BLE")
    func testEncodeDecodeBLE() async throws {
        guard #available(macOS 13.0, iOS 16.0, *) else { throw XCTSkip("This test requires macOS 13.0+ or iOS 16.0+") }
        // Create a test message
        let originalText = "This is a test message that will be chunked for BLE transport"
        let originalData = originalText.data(using: .utf8)!
        
        // Encode the message for BLE transport (which will chunk it)
        let chunks = MessageProtocol.encodeTextMessage(text: originalText, transport: .ble)
        
        // Verify multiple chunks were created for BLE
        #expect(chunks.count > 0, "Should create at least one chunk")
        
        // Try to decode each chunk
        var decodedMessage: MessageProtocol.Message? = nil
        
        for chunk in chunks {
            decodedMessage = MessageProtocol.decodeData(chunk)
            if decodedMessage != nil {
                break // Stop once we get a complete message
            }
        }
        
        // Verify the message was successfully decoded
        #expect(decodedMessage != nil, "Should decode the complete message")
        
        if let message = decodedMessage {
            // Verify the content type is correct
            #expect(message.contentType == .plainText, "Content type should be plainText")
            
            // Verify the message content matches the original
            let decodedText = message.stringPayload ?? ""
            #expect(decodedText == originalText, "Decoded text should match original")
        }
    }
    
    @Test("Test Message Encoding and Decoding via TCP")
    func testEncodeDecodeTCP() async throws {
        guard #available(macOS 13.0, iOS 16.0, *) else { throw XCTSkip("This test requires macOS 13.0+ or iOS 16.0+") }
        // Create a test message
        let originalText = "This is a test message for TCP transport"
        let originalData = originalText.data(using: .utf8)!
        
        // Encode the message for TCP transport (should be a single chunk)
        let chunks = MessageProtocol.encodeTextMessage(text: originalText, transport: .tcp)
        
        // Verify only one chunk was created for TCP
        #expect(chunks.count == 1, "Should create exactly one chunk for TCP")
        
        if chunks.count > 0 {
            // Decode the single chunk
            let decodedMessage = MessageProtocol.decodeData(chunks[0])
            
            // Verify the message was successfully decoded
            #expect(decodedMessage != nil, "Should decode the message")
            
            if let message = decodedMessage {
                // Verify the content type is correct
                #expect(message.contentType == .plainText, "Content type should be plainText")
                
                // Verify the message content matches the original
                let decodedText = message.stringPayload ?? ""
                #expect(decodedText == originalText, "Decoded text should match original")
            }
        }
    }
    
    @Test("Test Long Message Chunking")
    func testLongMessageChunking() async throws {
        guard #available(macOS 13.0, iOS 16.0, *) else { throw XCTSkip("This test requires macOS 13.0+ or iOS 16.0+") }
        // Create a very long test message (over 100KB)
        let repeatText = "This is a lengthy test message that will require multiple chunks for BLE transport. "
        let longText = String(repeating: repeatText, count: 500) // ~50KB
        let originalData = longText.data(using: .utf8)!
        
        print("Long message size: \(originalData.count / 1024) KB")
        
        // Encode the message for BLE transport (which will chunk it)
        let chunks = MessageProtocol.encodeTextMessage(text: longText, transport: .ble)
        
        // Verify multiple chunks were created
        #expect(chunks.count > 10, "Should create multiple chunks for a large message")
        print("Number of chunks: \(chunks.count)")
        
        // Verify the chunks have reasonable sizes
        for (index, chunk) in chunks.enumerated() {
            #expect(chunk.count <= 519, "Chunk \(index) should be <= 519 bytes")
        }
        
        // Try to decode all chunks in sequence
        var decodedMessage: MessageProtocol.Message? = nil
        
        for chunk in chunks {
            decodedMessage = MessageProtocol.decodeData(chunk)
            if decodedMessage != nil {
                break // Stop once we get a complete message
            }
        }
        
        // Verify the message was successfully decoded
        #expect(decodedMessage != nil, "Should decode the complete message")
        
        if let message = decodedMessage {
            // Verify the content type is correct
            #expect(message.contentType == .plainText, "Content type should be plainText")
            
            // Verify the decoded content matches the original
            let decodedText = message.stringPayload ?? ""
            #expect(decodedText.count == longText.count, "Decoded text length should match original")
            #expect(decodedText == longText, "Decoded text should match original")
        }
    }
    
    @Test("Test Different Content Types")
    func testDifferentContentTypes() async throws {
        guard #available(macOS 13.0, iOS 16.0, *) else { throw XCTSkip("This test requires macOS 13.0+ or iOS 16.0+") }
        // Test binary data encoding/decoding
        let binaryData = Data([0x01, 0x02, 0x03, 0x04, 0x05])
        
        // Test with different content types
        let contentTypes: [MessageContentType] = [
            .plainText,
            .htmlContent,
            .pngImage,
            .jpegImage,
            .pdfDocument,
            .rtfText
        ]
        
        for contentType in contentTypes {
            // Encode with the current content type
            let chunks = MessageProtocol.encodeMessage(contentType: contentType, payload: binaryData, transport: .tcp)
            
            #expect(chunks.count == 1, "Should create one chunk for TCP")
            
            if let chunk = chunks.first {
                let decodedMessage = MessageProtocol.decodeData(chunk)
                
                #expect(decodedMessage != nil, "Should decode the message")
                #expect(decodedMessage?.contentType == contentType, "Content type should match")
                #expect(decodedMessage?.payload == binaryData, "Payload should match original binary data")
            }
        }
    }
    
    @Test("Test UTF-8 Character Boundaries")
    func testUTF8CharacterBoundaries() async throws {
        guard #available(macOS 13.0, iOS 16.0, *) else { throw XCTSkip("This test requires macOS 13.0+ or iOS 16.0+") }
        // Create a message with multi-byte UTF-8 characters
        let text = "UTF-8 test: 😀🌍✨🏆 - Special characters: äöüß éèê 你好世界"
        let originalData = text.data(using: .utf8)!
        
        // Encode for BLE to force chunking
        let chunks = MessageProtocol.encodeTextMessage(text: text, transport: .ble)
        
        // Verify chunks were created
        #expect(chunks.count > 0, "Should create at least one chunk")
        
        // Decode chunks
        var decodedMessage: MessageProtocol.Message? = nil
        
        for chunk in chunks {
            decodedMessage = MessageProtocol.decodeData(chunk)
            if decodedMessage != nil {
                break
            }
        }
        
        // Verify decoding
        #expect(decodedMessage != nil, "Should decode the message")
        
        if let message = decodedMessage {
            let decodedText = message.stringPayload ?? ""
            #expect(decodedText == text, "Decoded text should match original with all UTF-8 characters intact")
        }
    }
}
