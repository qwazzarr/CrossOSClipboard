#include <iostream>
#include <string>
#include <vector>
#include <cassert>

#include "MessageProtocol.h"
#include "ClipboardEncryption.h"

// Simple helper to test one round-trip
bool testRoundTrip(const std::string& text, TransportType transport) {
    // Set up encryption key (must match whatever your app uses)
    ClipboardEncryption::setPassword("TestPassword123");

    // Encode the text into one or more chunks
    auto chunks = MessageProtocol::encodeTextMessage(text, transport);
    if (chunks.empty()) {
        std::cerr << "  ✗ encodeMessage returned no chunks\n";
        return false;
    }

    // Feed each chunk into the decoder until it returns a complete message
    std::shared_ptr<MessageProtocol::Message> decoded;
    for (auto& chunk : chunks) {
        decoded = MessageProtocol::decodeData(chunk);
        if (decoded) break;
    }

    if (!decoded) {
        std::cerr << "  ✗ decodeData never returned a complete message\n";
        return false;
    }

    // Compare the decoded payload to the original
    std::string roundTrip = decoded->getStringPayload();
    if (roundTrip != text) {
        std::cerr << "  ✗ Round-trip mismatch:\n"
            << "      original: \"" << text << "\"\n"
            << "      decoded:  \"" << roundTrip << "\"\n";
        return false;
    }

    std::cout << "  ✓ \"" << text << "\" via "
        << (transport == TransportType::TCP ? "TCP" : "BLE")
        << " round-trip OK\n";
    return true;
}

int main() {
    std::vector<std::string> tests = {
        "Hello, world!",
        std::string(1500, 'A'), // force BLE multi-chunk for a long string
    };

    bool allPassed = true;
    for (auto& t : tests) {
        allPassed &= testRoundTrip(t, TransportType::TCP);
        allPassed &= testRoundTrip(t, TransportType::BLE);
    }

    if (allPassed) {
        std::cout << "\nAll tests passed!\n";
        return 0;
    }
    else {
        std::cerr << "\nSome tests FAILED.\n";
        return 1;
    }
}
