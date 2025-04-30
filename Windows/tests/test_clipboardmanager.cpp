// tests/test_clipboardmanager.cpp
#include <catch2/catch_all.hpp>

#include "ClipboardManager.h"
#include "ClipboardEncryption.h"

TEST_CASE("Local → internal clipboard round-trip", "[ClipboardManager]") {
    // make sure encryption is initialized
    REQUIRE(ClipboardEncryption::setPassword("xyz"));

    ClipboardManager mgr;
    REQUIRE(mgr.initialize());

    const std::string text = u8"Hello, мир 🌟";

    // this should write into the real clipboard buffer
    REQUIRE(mgr.setClipboardContent(text, /*fromRemote=*/false));

    // now read it back
    auto [data, type] = mgr.getClipboardContent();
    REQUIRE(type == MessageContentType::PLAIN_TEXT);

    std::string roundTrip(data.begin(), data.end());
    REQUIRE(roundTrip == text);
}

TEST_CASE("Remote → clipboard via processRemoteMessage", "[ClipboardManager]") {
    REQUIRE(ClipboardEncryption::setPassword("xyz"));

    ClipboardManager mgr;
    REQUIRE(mgr.initialize());

    const std::string incoming = u8"From remote: привет 🛰️";
    std::vector<uint8_t> payload(incoming.begin(), incoming.end());

    // simulate a remote push (must supply both args)
    mgr.processRemoteMessage(payload, MessageContentType::PLAIN_TEXT);

    // now read the OS clipboard text
    std::string out = mgr.getClipboardText();
    REQUIRE(out == incoming);
}
