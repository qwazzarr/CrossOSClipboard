#include "MessageProtocol.h"
#include "ByteUtils.h"
#include "ClipboardEncryption.h"
#include <chrono>
#include <algorithm>
#include <string>
#include <iostream>

// Initialize static members
std::map<uint32_t, std::vector<MessageProtocol::MessageChunk>> MessageProtocol::partialMessages;
std::map<uint32_t, uint64_t> MessageProtocol::partialMessageTimestamps;
uint32_t MessageProtocol::nextTransferId = 0;

std::string MessageProtocol::Message::getStringPayload() const {
    if (contentType != MessageContentType::PLAIN_TEXT &&
        contentType != MessageContentType::HTML_CONTENT) {
        return "";
    }

    return std::string(payload.begin(), payload.end());
}
// Helper method to get binary payload
const std::vector<uint8_t>& MessageProtocol::Message::getBinaryPayload() const {
    return payload;
}


uint32_t MessageProtocol::generateTransferId() {
    uint32_t id = nextTransferId;
    nextTransferId++;
    return id;
}

std::vector<std::vector<uint8_t>> MessageProtocol::encodeMessage(
    MessageContentType contentType,
    const std::vector<uint8_t>& payload,
    TransportType transport
) {
    // Generate a unique transfer ID for this message
    uint32_t transferId = generateTransferId();

    // Encrypt the payload
    std::vector<uint8_t> encryptedPayload = ClipboardEncryption::encrypt(payload);
    if (encryptedPayload.empty()) {
        std::cerr << "Failed to encrypt payload or encryption not configured" << std::endl;
        return {}; // Return empty vector to indicate failure
    }

    if (transport == TransportType::TCP) {
        // For TCP, send as one chunk regardless of size
        std::vector<uint8_t> chunk;

        // Calculate total length of this message (header + payload)
        uint32_t totalLength = HEADER_SIZE + static_cast<uint32_t>(payload.size());

        // Reserve space for the entire message
        chunk.reserve(totalLength);

        // Append headers
        auto lengthBytes = ByteUtils::uint32ToBytes(totalLength);
        chunk.insert(chunk.end(), lengthBytes.begin(), lengthBytes.end());

        auto versionBytes = ByteUtils::uint16ToBytes(PROTOCOL_VERSION);
        chunk.insert(chunk.end(), versionBytes.begin(), versionBytes.end());

        chunk.push_back(static_cast<uint8_t>(contentType));

        auto transferIdBytes = ByteUtils::uint32ToBytes(transferId);
        chunk.insert(chunk.end(), transferIdBytes.begin(), transferIdBytes.end());

        auto chunkIndexBytes = ByteUtils::uint32ToBytes(0);  // Chunk index 0 (now 4 bytes)
        chunk.insert(chunk.end(), chunkIndexBytes.begin(), chunkIndexBytes.end());

        auto totalChunksBytes = ByteUtils::uint32ToBytes(1);  // Total chunks 1 (now 4 bytes)
        chunk.insert(chunk.end(), totalChunksBytes.begin(), totalChunksBytes.end());

        // Append payload
        chunk.insert(chunk.end(), payload.begin(), payload.end());

        return { chunk };
    }
    else {
        // For BLE, we need to chunk the data
        auto chunks = chunkedData(payload, BLE_MAX_CHUNK_SIZE);
        uint32_t totalChunks = static_cast<uint32_t>(chunks.size());  // Now 4-byte value

        // Create a data chunk for each piece
        std::vector<std::vector<uint8_t>> encodedChunks;
        encodedChunks.reserve(totalChunks);

        for (uint32_t index = 0; index < totalChunks; index++) {
            std::vector<uint8_t> chunk;

            // Calculate total length of this chunk (header + payload)
            uint32_t chunkLength = HEADER_SIZE + static_cast<uint32_t>(chunks[index].size());
            chunk.reserve(chunkLength);

            // Append headers
            auto lengthBytes = ByteUtils::uint32ToBytes(chunkLength);
            chunk.insert(chunk.end(), lengthBytes.begin(), lengthBytes.end());

            auto versionBytes = ByteUtils::uint16ToBytes(PROTOCOL_VERSION);
            chunk.insert(chunk.end(), versionBytes.begin(), versionBytes.end());

            chunk.push_back(static_cast<uint8_t>(contentType));

            auto transferIdBytes = ByteUtils::uint32ToBytes(transferId);
            chunk.insert(chunk.end(), transferIdBytes.begin(), transferIdBytes.end());

            auto chunkIndexBytes = ByteUtils::uint32ToBytes(index);  // Now 4 bytes
            chunk.insert(chunk.end(), chunkIndexBytes.begin(), chunkIndexBytes.end());

            auto totalChunksBytes = ByteUtils::uint32ToBytes(totalChunks);  // Now 4 bytes
            chunk.insert(chunk.end(), totalChunksBytes.begin(), totalChunksBytes.end());

            // Append payload
            chunk.insert(chunk.end(), chunks[index].begin(), chunks[index].end());

            encodedChunks.push_back(std::move(chunk));
        }

        return encodedChunks;
    }
}

std::vector<std::vector<uint8_t>> MessageProtocol::encodeTextMessage(
    const std::string& text,
    TransportType transport
) {
    std::vector<uint8_t> payload(text.begin(), text.end());
    return encodeMessage(MessageContentType::PLAIN_TEXT, payload, transport);
}

std::shared_ptr<MessageProtocol::Message> MessageProtocol::decodeData(
    const std::vector<uint8_t>& data
) {
    std::cout << "[decodeData] Received data of size: " << data.size() << std::endl;

    if (data.size() < HEADER_SIZE) {
        std::cout << "[decodeData] Data too small for header. Returning nullptr." << std::endl;
        return nullptr;
    }

    uint32_t length = ByteUtils::bytesToUint32(data, 0);
    uint16_t version = ByteUtils::bytesToUint16(data, 4);
    uint8_t typeRaw = data[6];
    uint32_t transferId = ByteUtils::bytesToUint32(data, 7);

    // New protocol (4-byte chunk counters)
    uint32_t chunkIndex = ByteUtils::bytesToUint32(data, 11);
    uint32_t totalChunks = ByteUtils::bytesToUint32(data, 15);

    std::cout << "[decodeData] Protocol V2 Header: length=" << length
        << " version=" << version
        << " typeRaw=" << static_cast<int>(typeRaw)
        << " transferId=" << transferId
        << " chunkIndex=" << chunkIndex
        << " totalChunks=" << totalChunks << std::endl;

    if (typeRaw < 1 || typeRaw > 6) {
        std::cout << "[decodeData] Invalid typeRaw: " << static_cast<int>(typeRaw) << ". Returning nullptr." << std::endl;
        return nullptr;
    }

    MessageContentType contentType = static_cast<MessageContentType>(typeRaw);
    std::vector<uint8_t> payload(data.begin() + HEADER_SIZE, data.end());

    if (totalChunks == 1) {
        std::cout << "[decodeData] Single-chunk message. Returning immediately." << std::endl;
        auto message = std::make_shared<Message>();
        message->contentType = contentType;
        message->transferId = transferId;
        message->payload = std::move(payload);
        // Decrypt the payload
        std::vector<uint8_t> decryptedPayload = ClipboardEncryption::decrypt(message->payload);
        if (!decryptedPayload.empty()) {
            // Replace the encrypted payload with the decrypted one
            message->payload = std::move(decryptedPayload);
        }
        else {
            std::cerr << "Failed to decrypt message payload" << std::endl;
            return nullptr;
        }
        return message;
    }

    std::cout << "[decodeData] Multi-chunk message. Storing chunk." << std::endl;

    MessageChunk chunk;
    chunk.contentType = contentType;
    chunk.transferId = transferId;
    chunk.chunkIndex = chunkIndex;
    chunk.totalChunks = totalChunks;

    // Extract payload from the correct position based on version
    if (version == 1) {
        const int HEADER_SIZE_V1 = 19;
        chunk.payload = std::vector<uint8_t>(data.begin() + HEADER_SIZE_V1, data.end());
    }
    else {
        chunk.payload = std::vector<uint8_t>(data.begin() + HEADER_SIZE, data.end());
    }

    partialMessageTimestamps[transferId] = getCurrentTimeMillis();
    partialMessages[transferId].push_back(std::move(chunk));

    std::cout << "[decodeData] Chunks received for transferId " << transferId
        << ": " << partialMessages[transferId].size() << " / " << totalChunks << std::endl;

    if (partialMessages[transferId].size() == totalChunks) {
        std::cout << "[decodeData] All chunks received. Reassembling message." << std::endl;

        std::sort(partialMessages[transferId].begin(), partialMessages[transferId].end(),
            [](const MessageChunk& a, const MessageChunk& b) {
                return a.chunkIndex < b.chunkIndex;
            });

        std::vector<uint8_t> fullPayload;
        for (const auto& chunk : partialMessages[transferId]) {
            fullPayload.insert(fullPayload.end(), chunk.payload.begin(), chunk.payload.end());
        }

        auto message = std::make_shared<Message>();
        message->contentType = contentType;
        message->transferId = transferId;
        message->payload = std::move(fullPayload);

        // Decrypt the payload
        std::vector<uint8_t> decryptedPayload = ClipboardEncryption::decrypt(message->payload);
        if (!decryptedPayload.empty()) {
            // Replace the encrypted payload with the decrypted one
            message->payload = std::move(decryptedPayload);
        }
        else {
            std::cerr << "Failed to decrypt message payload" << std::endl;
            // You could either return nullptr to indicate failure
            // or continue with the encrypted payload (not recommended)
            return nullptr;
        }

        partialMessages.erase(transferId);
        partialMessageTimestamps.erase(transferId);

        std::cout << "[decodeData] Message reassembled and returned." << std::endl;
        return message;
    }

    std::cout << "[decodeData] Waiting for more chunks." << std::endl;
    return nullptr;
}


void MessageProtocol::cleanupPartialMessages(uint64_t olderThanMilliseconds) {
    uint64_t currentTime = getCurrentTimeMillis();

    // Find IDs to remove
    std::vector<uint32_t> idsToRemove;

    for (const auto& entry : partialMessageTimestamps) {
        if (currentTime - entry.second > olderThanMilliseconds) {
            idsToRemove.push_back(entry.first);
        }
    }

    // Remove the expired partial messages
    for (uint32_t id : idsToRemove) {
        partialMessages.erase(id);
        partialMessageTimestamps.erase(id);
    }
}

std::vector<std::vector<uint8_t>> MessageProtocol::chunkedData(
    const std::vector<uint8_t>& data,
    int chunkSize
) {
    std::vector<std::vector<uint8_t>> chunks;
    size_t position = 0;

    while (position < data.size()) {
        // Calculate the end position for this chunk
        size_t endPos = (std::min)(position + static_cast<size_t>(chunkSize), data.size());

        // If we're not at the end of the data and might be in the middle of a UTF-8 character
        if (endPos < data.size()) {
            // Check if we're in the middle of a UTF-8 multi-byte character
            // UTF-8 continuation bytes always start with bits 10xxxxxx (0x80-0xBF)
            while (endPos > position && (data[endPos] & 0xC0) == 0x80) {
                // Move back to find the start of the character
                endPos--;
            }
        }

        // Create the chunk from position to endPos
        std::vector<uint8_t> chunk(data.begin() + position, data.begin() + endPos);
        chunks.push_back(std::move(chunk));

        // Move to the next position
        position = endPos;
    }

    return chunks;
}

uint64_t MessageProtocol::getCurrentTimeMillis() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}