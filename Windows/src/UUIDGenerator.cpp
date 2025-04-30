// UUIDGenerator.cpp
#include "UUIDGenerator.h"
#include <iomanip>
#include <sstream>
#include <iostream>

std::string UUIDGenerator::uuidFromString(const std::string& input) {
    // Calculate SHA-256 hash of the input
    auto hash = sha256(input);

    // Convert the first 16 bytes to a hex string
    std::stringstream ss;

    // Format: 8-4-4-4-12 hexadecimal digits
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            ss << "-";
        }
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)(hash[i] & 0xFF);
    }

    std::string uuidStr = ss.str();

    // Now we'll modify the string representation
    // Version 5 (SHA-1-based) - set the high nibble of the 7th byte to 5
    // Position 14-15 in the string (including hyphens)
    char highNibble = '5';  // Version 5
    char lowNibble = uuidStr[15] & 0x0F;
    if (lowNibble >= 10) {
        lowNibble = 'a' + (lowNibble - 10);
    }
    else {
        lowNibble = '0' + lowNibble;
    }
    uuidStr[14] = highNibble;
    uuidStr[15] = lowNibble;

    // Variant 1 (RFC 4122) - set the high bits of the 9th byte to 10xx
    // Position 19-20 in the string
    int variantValue = (uuidStr[19] >= 'a' ? (uuidStr[19] - 'a' + 10) : (uuidStr[19] - '0'));
    variantValue = (variantValue & 0x3) | 0x8;  // Set to 10xx

    if (variantValue >= 10) {
        uuidStr[19] = 'a' + (variantValue - 10);
    }
    else {
        uuidStr[19] = '0' + variantValue;
    }

    return uuidStr;
}

std::vector<uint8_t> UUIDGenerator::sha256(const std::string& input) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    DWORD hashLength = 0;
    DWORD dataSize = 0;
    std::vector<uint8_t> result;

    // Open algorithm provider
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0))) {
        return result;
    }

    // Get hash length
    if (!BCRYPT_SUCCESS(BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&hashLength, sizeof(DWORD), &dataSize, 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    result.resize(hashLength);

    // Create hash object
    if (!BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    // Hash the input
    if (!BCRYPT_SUCCESS(BCryptHashData(hHash, (PBYTE)input.c_str(), (ULONG)input.length(), 0))) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    // Finalize hash
    if (!BCRYPT_SUCCESS(BCryptFinishHash(hHash, result.data(), (ULONG)result.size(), 0))) {
        result.clear();
    }

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    return result;
}

std::string UUIDGenerator::generateFormattedKey(int segmentCount, int segmentLength) {
    std::stringstream ss;

    for (int i = 0; i < segmentCount; i++) {
        if (i > 0) {
            ss << "-";
        }
        ss << generateRandomString(segmentLength);
    }

    return ss.str();
}

std::string UUIDGenerator::generateRandomString(int length) {
    const std::string characters = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // Omitting easily confused characters
    std::vector<BYTE> randomBytes(length);

    // Generate cryptographically secure random bytes
    BCryptGenRandom(NULL, randomBytes.data(), length, BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    std::stringstream ss;
    for (int i = 0; i < length; i++) {
        int index = randomBytes[i] % characters.length();
        ss << characters[index];
    }

    return ss.str();
}