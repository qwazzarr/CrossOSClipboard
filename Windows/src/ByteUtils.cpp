#include "ByteUtils.h"

std::vector<uint8_t> ByteUtils::uint32ToBytes(uint32_t value) {
    std::vector<uint8_t> bytes(4);
    bytes[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    bytes[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    bytes[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    bytes[3] = static_cast<uint8_t>(value & 0xFF);
    return bytes;
}

std::vector<uint8_t> ByteUtils::uint16ToBytes(uint16_t value) {
    std::vector<uint8_t> bytes(2);
    bytes[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    bytes[1] = static_cast<uint8_t>(value & 0xFF);
    return bytes;
}

bool ByteUtils::bytesToUint32(const std::vector<uint8_t>& bytes, size_t offset, uint32_t& value) {
    if (bytes.size() < offset + 4) {
        return false;
    }

    value = static_cast<uint32_t>(bytes[offset]) << 24 |
        static_cast<uint32_t>(bytes[offset + 1]) << 16 |
        static_cast<uint32_t>(bytes[offset + 2]) << 8 |
        static_cast<uint32_t>(bytes[offset + 3]);
    return true;
}

bool ByteUtils::bytesToUint16(const std::vector<uint8_t>& bytes, size_t offset, uint16_t& value) {
    if (bytes.size() < offset + 2) {
        return false;
    }

    value = static_cast<uint16_t>(bytes[offset]) << 8 |
        static_cast<uint16_t>(bytes[offset + 1]);
    return true;
}

uint32_t ByteUtils::bytesToUint32(const std::vector<uint8_t>& bytes, size_t offset) {
    uint32_t value = 0;
    bytesToUint32(bytes, offset, value);
    return value;
}

uint16_t ByteUtils::bytesToUint16(const std::vector<uint8_t>& bytes, size_t offset) {
    uint16_t value = 0;
    bytesToUint16(bytes, offset, value);
    return value;
}