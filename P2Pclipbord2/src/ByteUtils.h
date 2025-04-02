#pragma once

#include <cstdint>
#include <vector>

/**
 * Utility class for basic byte operations.
 */
class ByteUtils {
public:
    /**
     * Converts a uint32_t value to a big-endian byte array.
     * @param value The value to convert
     * @return A vector containing the bytes in big-endian order
     */
    static std::vector<uint8_t> uint32ToBytes(uint32_t value);

    /**
     * Converts a uint16_t value to a big-endian byte array.
     * @param value The value to convert
     * @return A vector containing the bytes in big-endian order
     */
    static std::vector<uint8_t> uint16ToBytes(uint16_t value);

    /**
     * Extracts a uint32_t value from a big-endian byte array.
     * @param bytes The byte array to extract from
     * @param offset The starting position in the byte array
     * @param value Output parameter where the result will be stored
     * @return True if successful, false if there aren't enough bytes
     */
    static bool bytesToUint32(const std::vector<uint8_t>& bytes, size_t offset, uint32_t& value);

    /**
     * Extracts a uint16_t value from a big-endian byte array.
     * @param bytes The byte array to extract from
     * @param offset The starting position in the byte array
     * @param value Output parameter where the result will be stored
     * @return True if successful, false if there aren't enough bytes
     */
    static bool bytesToUint16(const std::vector<uint8_t>& bytes, size_t offset, uint16_t& value);

    /**
     * Convenience overload that doesn't require checking the return value.
     * Returns 0 if there aren't enough bytes.
     */
    static uint32_t bytesToUint32(const std::vector<uint8_t>& bytes, size_t offset = 0);

    /**
     * Convenience overload that doesn't require checking the return value.
     * Returns 0 if there aren't enough bytes.
     */
    static uint16_t bytesToUint16(const std::vector<uint8_t>& bytes, size_t offset = 0);
};