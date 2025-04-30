// tests/test_uuidgenerator.cpp
#include <catch2/catch_all.hpp>
#include "UUIDGenerator.h"
#include <regex>

TEST_CASE("uuidFromString is deterministic and RFC4122 v5 compliant", "[UUIDGenerator]") {
    auto u1 = UUIDGenerator::uuidFromString("hello");
    auto u2 = UUIDGenerator::uuidFromString("hello");
    auto u3 = UUIDGenerator::uuidFromString("world");

    REQUIRE(u1 == u2);
    REQUIRE(u1 != u3);

    static const std::regex uuid_regex{
        "^[0-9a-f]{8}-"
        "[0-9a-f]{4}-"
        "[0-9a-f]{4}-"
        "[0-9a-f]{4}-"
        "[0-9a-f]{12}$"
    , std::regex::icase };

    REQUIRE(std::regex_match(u1, uuid_regex));
    // version nibble '5' at pos 14
    REQUIRE(u1[14] == '5');
    // variant bits at pos 19: high two bits are '10'
    auto hexval = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
        };
    int v = hexval(u1[19]);
    REQUIRE((v & 0x8) == 0x8);
    REQUIRE((v & 0x4) == 0x0);
}

TEST_CASE("generateFormattedKey produces the right shape", "[UUIDGenerator]") {
    constexpr int SEGMENTS = 4, LEN = 3;
    auto key = UUIDGenerator::generateFormattedKey(SEGMENTS, LEN);

    // must have SEGMENTS-1 dashes
    REQUIRE(std::count(key.begin(), key.end(), '-') == SEGMENTS - 1);

    std::vector<std::string> parts;
    std::stringstream ss{ key };
    std::string p;
    while (std::getline(ss, p, '-')) parts.push_back(p);

    REQUIRE(parts.size() == SEGMENTS);
    for (auto& seg : parts) {
        REQUIRE((int)seg.size() == LEN);
        // each char in [A–Z0–9], you could add a charset check here if you like
    }
}
#include <catch2/catch_test_macros.hpp>
#include "MessageProtocol.h"
#include "ByteUtils.h"
#include "ClipboardEncryption.h"

// helper to set a fixed password so encryption is deterministic
struct EncryptionGuard {
    EncryptionGuard() { REQUIRE(ClipboardEncryption::setPassword("test-пароль🔑")); }
    ~EncryptionGuard() { ClipboardEncryption::clearPassword(); }
};

TEST_CASE("MessageProtocol handles English ASCII over TCP", "[MessageProtocol][ASCII]") {
    EncryptionGuard g;
    std::string orig = "The quick brown fox jumps over the lazy dog.";
    auto chunks = MessageProtocol::encodeTextMessage(orig, TransportType::TCP);
    REQUIRE(chunks.size() == 1);
    auto msg = MessageProtocol::decodeData(chunks[0]);
    REQUIRE(msg);
    CHECK(msg->getStringPayload() == orig);
}

TEST_CASE("MessageProtocol handles ASCII, Cyrillic and emojis over TCP", "[MessageProtocol]") {
    EncryptionGuard g;
    SECTION("ASCII only") {
        auto chunks = MessageProtocol::encodeTextMessage("Hello, world!", TransportType::TCP);
        REQUIRE(chunks.size() == 1);
        auto msg = MessageProtocol::decodeData(chunks[0]);
        REQUIRE(msg);
        CHECK(msg->getStringPayload() == "Hello, world!");
    }

    SECTION("Cyrillic round-trip") {
        std::string orig = "Привет, мир!";            // "Hello, world" in Russian
        auto chunks = MessageProtocol::encodeTextMessage(orig, TransportType::TCP);
        REQUIRE(chunks.size() == 1);
        auto msg = MessageProtocol::decodeData(chunks[0]);
        REQUIRE(msg);
        CHECK(msg->getStringPayload() == orig);
    }

    SECTION("Emoji round-trip") {
        std::string orig = "🚀🔥😊";                   // rocket, fire, smile
        auto chunks = MessageProtocol::encodeTextMessage(orig, TransportType::TCP);
        REQUIRE(chunks.size() == 1);
        auto msg = MessageProtocol::decodeData(chunks[0]);
        REQUIRE(msg);
        CHECK(msg->getStringPayload() == orig);
    }

    SECTION("Mixed ASCII + Cyrillic + Emojis") {
        std::string orig = "Test Пример 🚀";
        auto chunks = MessageProtocol::encodeTextMessage(orig, TransportType::TCP);
        REQUIRE(chunks.size() == 1);
        auto msg = MessageProtocol::decodeData(chunks[0]);
        REQUIRE(msg);
        CHECK(msg->getStringPayload() == orig);
    }
}

TEST_CASE("MessageProtocol chunking respects UTF-8 boundaries for BLE", "[MessageProtocol][BLE]") {
    EncryptionGuard g;
    // build a long string that mixes Cyrillic and emoji
    std::string block = "АбвГД😊";
    std::string big;
    for (int i = 0;i < 100;i++) big += block;

    auto chunks = MessageProtocol::encodeTextMessage(big, TransportType::BLE);
    REQUIRE(chunks.size() > 1);

    std::shared_ptr<MessageProtocol::Message> decoded;
    for (auto& c : chunks) {
        decoded = MessageProtocol::decodeData(c);
    }
    REQUIRE(decoded);
    CHECK(decoded->getStringPayload() == big);
}
