// ClipboardEncryption.cpp
#include "ClipboardEncryption.h"
#include <iostream>

// Static member initialization
const std::string ClipboardEncryption::saltString = "P2PClipboardSyncSalt2025";
const std::string ClipboardEncryption::infoString = "P2PClipboardEncryptionContext";
std::vector<uint8_t> ClipboardEncryption::symmetricKey;

// Simple HMAC function since Windows doesn't expose HKDF directly
std::vector<uint8_t> HMAC_SHA256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    std::vector<uint8_t> result(32, 0); // SHA-256 is 32 bytes

    // Open algorithm provider
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        // Create hash object
        if (BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, NULL, 0, (PUCHAR)key.data(), (ULONG)key.size(), 0))) {
            // Hash the data
            if (BCRYPT_SUCCESS(BCryptHashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0))) {
                // Finalize hash
                BCryptFinishHash(hHash, result.data(), (ULONG)result.size(), 0);
            }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    return result;
}

// HKDF implementation (Extract + Expand based on RFC 5869)
std::vector<uint8_t> ClipboardEncryption::deriveSymmetricKey(const std::string& password) {
    // Convert strings to byte vectors
    std::vector<uint8_t> passwordBytes(password.begin(), password.end());
    std::vector<uint8_t> saltBytes(saltString.begin(), saltString.end());
    std::vector<uint8_t> infoBytes(infoString.begin(), infoString.end());

    // HKDF-Extract
    std::vector<uint8_t> prk = HMAC_SHA256(saltBytes, passwordBytes);

    // HKDF-Expand
    std::vector<uint8_t> output(32, 0); // 32 bytes output (256 bits)
    std::vector<uint8_t> T;
    std::vector<uint8_t> lastT;

    // T(1) = HMAC-Hash(PRK, info || 0x01)
    std::vector<uint8_t> data = infoBytes;
    data.push_back(0x01);
    lastT = HMAC_SHA256(prk, data);

    // Copy first block to output
    std::copy(lastT.begin(), lastT.end(), output.begin());

    return output;
}

bool ClipboardEncryption::setPassword(const std::string& password) {
    if (password.empty()) {
        std::cerr << "Password cannot be empty" << std::endl;
        return false;
    }

    symmetricKey = deriveSymmetricKey(password);
    return true;
}

bool ClipboardEncryption::isPasswordSet() {
    return !symmetricKey.empty();
}

void ClipboardEncryption::clearPassword() {
    symmetricKey.clear();
}

std::vector<uint8_t> ClipboardEncryption::encrypt(const std::vector<uint8_t>& data) {
    if (symmetricKey.empty()) {
        std::cerr << "Error: No password has been set. Call setPassword() first." << std::endl;
        return {};
    }

    BCRYPT_ALG_HANDLE hAlg = NULL;
    std::vector<uint8_t> result;

    // Open algorithm provider
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0))) {
        std::cerr << "Failed to open algorithm provider" << std::endl;
        return {};
    }

    // Set chaining mode to GCM
    if (!BCRYPT_SUCCESS(BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        std::cerr << "Failed to set chaining mode" << std::endl;
        return {};
    }

    // Generate 12-byte nonce (IV)
    std::vector<uint8_t> nonce(12);
    NTSTATUS status = BCryptGenRandom(NULL, nonce.data(), (ULONG)nonce.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        std::cerr << "Failed to generate random nonce" << std::endl;
        return {};
    }

    // Create key object
    BCRYPT_KEY_HANDLE hKey = NULL;
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, symmetricKey.data(), (ULONG)symmetricKey.size(), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        std::cerr << "Failed to create key" << std::endl;
        return {};
    }

    // Tag length is 16 bytes for GCM
    ULONG tagLength = 16;

    // Get cipher text length
    ULONG cipherTextLength = 0;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);

    authInfo.pbNonce = nonce.data();
    authInfo.cbNonce = (ULONG)nonce.size();
    authInfo.pbAuthData = NULL;
    authInfo.cbAuthData = 0;
    authInfo.pbTag = NULL;
    authInfo.cbTag = tagLength;

    status = BCryptEncrypt(hKey, (PBYTE)data.data(), (ULONG)data.size(),
        &authInfo, NULL, 0, NULL, 0, &cipherTextLength, 0);

    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        std::cerr << "Failed to get ciphertext length" << std::endl;
        return {};
    }

    // Allocate memory for the ciphertext
    std::vector<uint8_t> ciphertext(cipherTextLength);
    std::vector<uint8_t> tag(tagLength);

    // Set auth tag buffer
    authInfo.pbTag = tag.data();
    authInfo.cbTag = (ULONG)tag.size();

    // Encrypt
    status = BCryptEncrypt(hKey, (PBYTE)data.data(), (ULONG)data.size(),
        &authInfo, NULL, 0, ciphertext.data(), (ULONG)ciphertext.size(),
        &cipherTextLength, 0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status)) {
        std::cerr << "Encryption failed" << std::endl;
        return {};
    }

    // Format the output as nonce + ciphertext + tag
    result.reserve(nonce.size() + cipherTextLength + tag.size());
    result.insert(result.end(), nonce.begin(), nonce.end());
    result.insert(result.end(), ciphertext.begin(), ciphertext.begin() + cipherTextLength);
    result.insert(result.end(), tag.begin(), tag.end());

    return result;
}

std::vector<uint8_t> ClipboardEncryption::decrypt(const std::vector<uint8_t>& encryptedData) {
    if (symmetricKey.empty()) {
        std::cerr << "Error: No password has been set. Call setPassword() first." << std::endl;
        return {};
    }

    // Ensure enough data for nonce (12) + at least some ciphertext + tag (16)
    if (encryptedData.size() < 29) {
        std::cerr << "Encrypted data too short" << std::endl;
        return {};
    }

    // Extract nonce (first 12 bytes)
    std::vector<uint8_t> nonce(encryptedData.begin(), encryptedData.begin() + 12);

    // Extract tag (last 16 bytes)
    std::vector<uint8_t> tag(encryptedData.end() - 16, encryptedData.end());

    // Extract ciphertext (everything in between)
    std::vector<uint8_t> ciphertext(encryptedData.begin() + 12, encryptedData.end() - 16);

    BCRYPT_ALG_HANDLE hAlg = NULL;
    std::vector<uint8_t> plaintext;

    // Open algorithm provider
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0))) {
        std::cerr << "Failed to open algorithm provider" << std::endl;
        return {};
    }

    // Set chaining mode to GCM
    if (!BCRYPT_SUCCESS(BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        std::cerr << "Failed to set chaining mode" << std::endl;
        return {};
    }

    // Create key object
    BCRYPT_KEY_HANDLE hKey = NULL;
    NTSTATUS status = BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, symmetricKey.data(), (ULONG)symmetricKey.size(), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        std::cerr << "Failed to create key" << std::endl;
        return {};
    }

    // Get plaintext length
    ULONG plaintextLength = 0;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);

    authInfo.pbNonce = nonce.data();
    authInfo.cbNonce = (ULONG)nonce.size();
    authInfo.pbAuthData = NULL;
    authInfo.cbAuthData = 0;
    authInfo.pbTag = tag.data();
    authInfo.cbTag = (ULONG)tag.size();

    status = BCryptDecrypt(hKey, ciphertext.data(), (ULONG)ciphertext.size(),
        &authInfo, NULL, 0, NULL, 0, &plaintextLength, 0);

    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        std::cerr << "Failed to get plaintext length" << std::endl;
        return {};
    }

    // Allocate memory for the plaintext
    plaintext.resize(plaintextLength);

    // Decrypt
    status = BCryptDecrypt(hKey, ciphertext.data(), (ULONG)ciphertext.size(),
        &authInfo, NULL, 0, plaintext.data(), (ULONG)plaintext.size(),
        &plaintextLength, 0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status)) {
        std::cerr << "Decryption failed: Tag mismatch (data corrupted or wrong key)" << std::endl;
        return {};
    }

    plaintext.resize(plaintextLength);
    return plaintext;
}