import Foundation


// MARK: - Transport Type
enum TransportType {
    case ble  // Bluetooth Low Energy - requires chunking
    case tcp  // TCP - can send as single message
}

// MARK: - Protocol Handler
class MessageProtocol {
    // BLE packet size constraint
    private static let bleMaxChunkSize = 512
    
    // Current protocol version
    private static let protocolVersion: UInt16 = 1
    
    // Header size: 4 (length) + 2 (version) + 1 (type) + 4 (transferId) + 4 (chunkIndex) + 4 (totalChunks)
    private static let headerSize = 19 // Increased from 13 to 19 due to length field change + total chunks
    
    private static var nextTransferId: UInt32 = 0
    
    // MARK: - Encoding
    
    private static func generateTransferId() -> UInt32 {
        let id = nextTransferId
        nextTransferId = nextTransferId &+ 1
        return id
    }

    
    static func encodeMessage(contentType: MessageContentType, payload: Data, transport: TransportType) -> [Data] {
        // Generate a unique transfer ID for this message
        let transferId = generateTransferId()
        
        switch transport {
        case .tcp:
            // For TCP, send as one chunk regardless of size
            var chunk = Data()
            
            // Calculate total length of this message (header + payload)
            let totalLength: UInt32 = UInt32(headerSize + payload.count)
            
            // Append headers
            chunk.append(contentsOf: totalLength.bigEndianBytes)   // Now 4 bytes
            chunk.append(contentsOf: protocolVersion.bigEndianBytes)
            chunk.append(contentType.rawValue)
            chunk.append(contentsOf: transferId.bigEndianBytes)
            chunk.append(contentsOf: UInt32(0).bigEndianBytes)     // Chunk index 0
            chunk.append(contentsOf: UInt32(1).bigEndianBytes)     // Total chunks 1
            
            // Append payload
            chunk.append(payload)
        
            return [chunk]
            
        case .ble:
            // For BLE, we need to chunk the data
            let chunks = chunkedData(data: payload, chunkSize: bleMaxChunkSize)
            let totalChunks = chunks.count
            
            // Create a data chunk for each piece
            return chunks.enumerated().map { index, chunkData in
                var chunk = Data()
                
                // Calculate total length of this chunk (header + payload)
                let totalLength: UInt32 = UInt32(headerSize + chunkData.count)
                
                // Append headers
                chunk.append(contentsOf: totalLength.bigEndianBytes) //totalLength
                chunk.append(contentsOf: protocolVersion.bigEndianBytes) //protocolVersion
                chunk.append(contentType.rawValue) // Type
                chunk.append(contentsOf: transferId.bigEndianBytes) //transferID
                chunk.append(contentsOf: UInt32(index).bigEndianBytes) //number of this chunk
                chunk.append(contentsOf: UInt32(totalChunks).bigEndianBytes)//total chunks
                
                // Append payload
                chunk.append(chunkData)
                
                return chunk
            }
        }
    }
    
    /// Convenience method for encoding text messages
    static func encodeTextMessage(text: String, transport: TransportType) -> [Data] {
        guard let data = text.data(using: .utf8) else {
            return []
        }
        
        return encodeMessage(contentType: .plainText, payload: data, transport: transport)
    }
    
    // MARK: - Decoding
    
    /// Message structure returned by the decoder
    struct Message {
        let contentType: MessageContentType
        let transferId: UInt32
        let payload: Data
        
        var stringPayload: String? {
            guard contentType == .plainText || contentType == .htmlContent else {
                return nil
            }
            
            print("trying to return string form stringPayload method")
            return String(data: payload, encoding: .utf8)
        }
    }
    
    /// Message chunk structure used internally for reassembly
    private struct MessageChunk {
        let contentType: MessageContentType
        let transferId: UInt32
        let chunkIndex: Int
        let totalChunks: Int
        let payload: Data
    }
    
    /// In-memory store of partial messages being reassembled
    private static var partialMessages: [UInt32: [MessageChunk]] = [:]
    
    /// Processes a received data packet according to the protocol
    /// - Parameter data: Raw received data
    /// - Returns: Complete message if available, nil if more chunks are expected
    static func decodeData(_ data: Data) -> Message? {
        // Ensure we have at least a complete header
        guard data.count >= headerSize else {
            print("Received data too small for a valid header")
            return nil
        }
        
        // Extract header fields
        let length = UInt32(bigEndianBytes: [data[0], data[1], data[2], data[3]])
        let version = UInt16(bigEndianBytes: [data[4], data[5]])
        let typeRaw = data[6]
        let transferId = UInt32(bigEndianBytes: [data[7], data[8], data[9], data[10]])
        let chunkIndex = UInt32(bigEndianBytes: [data[11], data[12], data[13], data[14]])
        let totalChunks = UInt32(bigEndianBytes: [data[15], data[16], data[17], data[18]])
        
        // Validate message type
        guard let contentType = MessageContentType(rawValue: typeRaw) else {
            print("Unknown content type: \(typeRaw)")
            return nil
        }
        
        // Extract payload
        let payload = data.suffix(from: headerSize)
        
        // Create chunk
        let chunk = MessageChunk(
            contentType: contentType,
            transferId: transferId,
            chunkIndex: Int(chunkIndex),
            totalChunks: Int(totalChunks),
            payload: payload
        )
        
        // For single-chunk messages, return immediately
        if totalChunks == 1 {
            let message:Message = Message(contentType: contentType, transferId: transferId, payload: payload)
            print("We've just decoded a complete message: \(message)")
            return message
        }
        
        // Store this chunk for multi-part message
        if partialMessages[transferId] == nil {
            partialMessages[transferId] = []
        }
        partialMessages[transferId]?.append(chunk)
        
        // Check if we have all chunks for this message
        if let chunks = partialMessages[transferId], chunks.count == Int(totalChunks) {
            // Sort chunks by index
            let sortedChunks = chunks.sorted { $0.chunkIndex < $1.chunkIndex }
            
            // Combine payloads
            var fullPayload = Data()
            for chunk in sortedChunks {
                fullPayload.append(chunk.payload)
            }
            
            // Clean up the temporary storage
            partialMessages.removeValue(forKey: transferId)
            
            print("Ok, we fully reassembled the message packed into \(Int(totalChunks)) chunks")
            
            // Return the reassembled message
            return Message(contentType: contentType, transferId: transferId, payload: fullPayload)
        }
        
        print("Waiting for more chunks , \(partialMessages[transferId]?.count ?? 0)/\(Int(totalChunks))")
        
        // Still waiting for more chunks
        return nil
    }
    
    // MARK: - Helper Methods
    
    /// Splits data into chunks of specified size
    private static func chunkedData(data: Data, chunkSize: Int) -> [Data] {
        var chunks: [Data] = []
        var remainingData = data
        
        while !remainingData.isEmpty {
            let chunk = remainingData.prefix(chunkSize)
            chunks.append(chunk)
            remainingData = remainingData.dropFirst(chunk.count)
        }
        
        return chunks
    }
}
