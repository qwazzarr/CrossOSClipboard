// ClipboardEncryption.h
#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

class ClipboardEncryption {
private:
    // Constants for key derivation
    static const std::string saltString;
    static const std::string infoString;

    // Storage for the derived key
    static std::vector<uint8_t> symmetricKey;

    // HKDF key derivation (internal function)
    static std::vector<uint8_t> deriveSymmetricKey(const std::string& password);

public:
    // Set the password for encryption/decryption
    static bool setPassword(const std::string& password);

    // Check if password is set
    static bool isPasswordSet();

    // Clear the current password
    static void clearPassword();

    // Encrypt data
    static std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data);

    // Decrypt data
    static std::vector<uint8_t> decrypt(const std::vector<uint8_t>& encryptedData);
};