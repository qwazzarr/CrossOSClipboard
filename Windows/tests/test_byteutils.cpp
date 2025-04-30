#include <catch2/catch_all.hpp>
#include "ByteUtils.h"
#include <vector>

TEST_CASE("uint32ToBytes produces big-endian output", "[ByteUtils]") {
    uint32_t value = 0x12345678;
    auto bytes = ByteUtils::uint32ToBytes(value);
    REQUIRE(bytes.size() == 4);
    REQUIRE(bytes[0] == 0x12);
    REQUIRE(bytes[1] == 0x34);
    REQUIRE(bytes[2] == 0x56);
    REQUIRE(bytes[3] == 0x78);
}

TEST_CASE("bytesToUint32 parses big-endian input", "[ByteUtils]") {
    std::vector<uint8_t> bytes = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint32_t out = 0;
    REQUIRE(ByteUtils::bytesToUint32(bytes, 0, out));
    REQUIRE(out == 0xDEADBEEF);
}

TEST_CASE("bytesToUint32 convenience overload and error cases", "[ByteUtils]") {
    std::vector<uint8_t> good = { 0x00, 0x00, 0x00, 0x01 };
    REQUIRE(ByteUtils::bytesToUint32(good, 0) == 1);

    // too short => returns 0
    std::vector<uint8_t> bad = { 0xFF };
    REQUIRE(ByteUtils::bytesToUint32(bad, 0) == 0);
    uint32_t dummy = 0xFFFFFFFF;
    REQUIRE_FALSE(ByteUtils::bytesToUint32(bad, 0, dummy));
    REQUIRE(dummy == 0xFFFFFFFF); // unchanged
}

TEST_CASE("uint16ToBytes produces big-endian output", "[ByteUtils]") {
    uint16_t value = 0xABCD;
    auto bytes = ByteUtils::uint16ToBytes(value);
    REQUIRE(bytes.size() == 2);
    REQUIRE(bytes[0] == 0xAB);
    REQUIRE(bytes[1] == 0xCD);
}

TEST_CASE("bytesToUint16 parses big-endian input", "[ByteUtils]") {
    std::vector<uint8_t> bytes = { 0xFE, 0xED };
    uint16_t out = 0;
    REQUIRE(ByteUtils::bytesToUint16(bytes, 0, out));
    REQUIRE(out == 0xFEED);
}

TEST_CASE("bytesToUint16 convenience overload and error cases", "[ByteUtils]") {
    std::vector<uint8_t> good = { 0x00, 0x02 };
    REQUIRE(ByteUtils::bytesToUint16(good, 0) == 2);

    std::vector<uint8_t> bad = { 0xAA };
    REQUIRE(ByteUtils::bytesToUint16(bad, 0) == 0);
    uint16_t dummy = 0xFFFF;
    REQUIRE_FALSE(ByteUtils::bytesToUint16(bad, 0, dummy));
    REQUIRE(dummy == 0xFFFF);
}

TEST_CASE("bytesToUint32 and bytesToUint16 with non-zero offset", "[ByteUtils]") {
    std::vector<uint8_t> data32 = { 0x00, 0x11, 0x22, 0x33, 0x44 };
    uint32_t out32 = 0;
    REQUIRE(ByteUtils::bytesToUint32(data32, 1, out32));
    REQUIRE(out32 == 0x11223344);

    std::vector<uint8_t> data16 = { 0x00, 0xAA, 0xBB };
    uint16_t out16 = 0;
    REQUIRE(ByteUtils::bytesToUint16(data16, 1, out16));
    REQUIRE(out16 == 0xAABB);
}
