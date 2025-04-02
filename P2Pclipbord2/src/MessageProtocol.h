#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <string>
#include <memory>

// Message content types
enum class MessageContentType : uint8_t {
    PLAIN_TEXT = 1,
    RTF_TEXT = 2,
    PNG_IMAGE = 3,
    JPEG_IMAGE = 4,
    PDF_DOCUMENT = 5,
    HTML_CONTENT = 6
};

// Transport types
enum class TransportType {
    BLE,  // Bluetooth Low Energy - requires chunking
    TCP   // TCP - can send as single message
};

class MessageProtocol {
public:
    // Message structure returned by the decoder
    struct Message {
        MessageContentType contentType;
        uint32_t transferId;
        std::vector<uint8_t> payload;

        std::string getStringPayload() const;
    };

    // Encode a message with the specified content type and payload
    static std::vector<std::vector<uint8_t>> encodeMessage(
        MessageContentType contentType,
        const std::vector<uint8_t>& payload,
        TransportType transport
    );

    // Convenience method for encoding text messages
    static std::vector<std::vector<uint8_t>> encodeTextMessage(
        const std::string& text,
        TransportType transport
    );

    // Process a received data packet according to the protocol
    // Returns a complete message if available, nullptr if more chunks are expected
    static std::shared_ptr<Message> decodeData(const std::vector<uint8_t>& data);

    // Clean up any partial messages older than the specified timeout
    static void cleanupPartialMessages(uint64_t olderThanMilliseconds);

private:
    // Message chunk structure used internally for reassembly
    struct MessageChunk {
        MessageContentType contentType;
        uint32_t transferId;
        uint32_t chunkIndex;   // Changed from int to uint32_t for 4-byte support
        uint32_t totalChunks;  // Changed from int to uint32_t for 4-byte support
        std::vector<uint8_t> payload;
    };

    // BLE packet size constraint
    static constexpr int BLE_MAX_CHUNK_SIZE = 512;

    // Current protocol version
    static constexpr uint16_t PROTOCOL_VERSION = 1;  //

    // Header size: 4 (length) + 2 (version) + 1 (type) + 4 (transferId) + 4 (chunkIndex) + 4 (totalChunks)
    static constexpr int HEADER_SIZE = 19;  // Increased from 15 to 19 due to expanded chunk counter fields

    // Generate a unique transfer ID for new messages
    static uint32_t generateTransferId();

    // In-memory store of partial messages being reassembled
    static std::map<uint32_t, std::vector<MessageChunk>> partialMessages;

    // Map of transfer ID to timestamp for cleanup
    static std::map<uint32_t, uint64_t> partialMessageTimestamps;

    // Next transfer ID counter
    static uint32_t nextTransferId;

    // Splits data into chunks of specified size
    static std::vector<std::vector<uint8_t>> chunkedData(
        const std::vector<uint8_t>& data,
        int chunkSize
    );

    static uint64_t getCurrentTimeMillis();
};