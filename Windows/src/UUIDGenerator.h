// UUIDGenerator.h
#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

class UUIDGenerator {
public:
    // Generate a RFC 4122 UUID from a string
    static std::string uuidFromString(const std::string& input);

    // Generate a formatted key string (like "ABCD-1234-EFGH")
    static std::string generateFormattedKey(int segmentCount = 3, int segmentLength = 4);

private:
    // Calculate SHA-256 hash
    static std::vector<uint8_t> sha256(const std::string& input);

    // Generate a random string of the specified length
    static std::string generateRandomString(int length);
};