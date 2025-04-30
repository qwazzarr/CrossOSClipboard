// tests/test_clipboardencryption.cpp
#include <catch2/catch_all.hpp>
#include "ClipboardEncryption.h"

TEST_CASE("Initial state: no password set", "[ClipboardEncryption]") {
    ClipboardEncryption::clearPassword();
    REQUIRE_FALSE(ClipboardEncryption::isPasswordSet());

    std::vector<uint8_t> dummy{ 1,2,3 };
    // encrypt without password should return empty
    auto enc = ClipboardEncryption::encrypt(dummy);
    REQUIRE(enc.empty());

    // decrypt without password should return empty
    auto dec = ClipboardEncryption::decrypt(dummy);
    REQUIRE(dec.empty());
}

TEST_CASE("setPassword / isPasswordSet / clearPassword", "[ClipboardEncryption]") {
    ClipboardEncryption::clearPassword();
    // empty password is rejected
    REQUIRE_FALSE(ClipboardEncryption::setPassword(""));
    REQUIRE_FALSE(ClipboardEncryption::isPasswordSet());

    // valid password
    REQUIRE(ClipboardEncryption::setPassword("s3cr3t"));
    REQUIRE(ClipboardEncryption::isPasswordSet());

    // clear again
    ClipboardEncryption::clearPassword();
    REQUIRE_FALSE(ClipboardEncryption::isPasswordSet());
}

TEST_CASE("Encrypt & decrypt round-trip", "[ClipboardEncryption]") {
    ClipboardEncryption::clearPassword();
    REQUIRE(ClipboardEncryption::setPassword("hunter2"));

    std::string plain = "The quick brown fox jumps over the lazy dog";
    std::vector<uint8_t> data(plain.begin(), plain.end());

    auto cipher = ClipboardEncryption::encrypt(data);
    REQUIRE_FALSE(cipher.empty());

    auto recovered = ClipboardEncryption::decrypt(cipher);
    REQUIRE(recovered == data);

    std::string round(recovered.begin(), recovered.end());
    REQUIRE(round == plain);
}

TEST_CASE("Decrypt with wrong password fails", "[ClipboardEncryption]") {
    ClipboardEncryption::clearPassword();
    REQUIRE(ClipboardEncryption::setPassword("first"));
    std::vector<uint8_t> someData{ 10,20,30,40 };
    auto cipher = ClipboardEncryption::encrypt(someData);
    REQUIRE_FALSE(cipher.empty());

    // switch password
    ClipboardEncryption::clearPassword();
    REQUIRE(ClipboardEncryption::setPassword("second"));

    auto bad = ClipboardEncryption::decrypt(cipher);
    REQUIRE(bad.empty());
}

TEST_CASE("Decrypt corrupted ciphertext fails", "[ClipboardEncryption]") {
    ClipboardEncryption::clearPassword();
    REQUIRE(ClipboardEncryption::setPassword("password"));

    std::vector<uint8_t> payload{ 5,4,3,2,1 };
    auto cipher = ClipboardEncryption::encrypt(payload);
    REQUIRE_FALSE(cipher.empty());

    // corrupt one byte in the ciphertext (after the 12-byte nonce)
    auto corrupted = cipher;
    if (corrupted.size() > 16) {
        corrupted[12] ^= 0xFF;
    }

    auto result = ClipboardEncryption::decrypt(corrupted);
    REQUIRE(result.empty());
}
