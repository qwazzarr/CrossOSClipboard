//
//  ClipboardEncryption.swift
//  P2Pclipboard
import Foundation
import CryptoKit

// Module for handling encryption/decryption of clipboard data
class ClipboardEncryption {
    
    // Constants for key derivation
    private static let saltString = "ClipboardSyncSalt"
    private static let infoString = "ClipboardSyncInfo"
    
    // Derive a symmetric key using HKDF from the password and a salt
    static func deriveSymmetricKey(from password: String) -> SymmetricKey {
        let passwordData = Data(password.utf8)
        let salt = saltString.data(using: .utf8)! // Use a constant salt
        let key = HKDF<SHA256>.deriveKey(
            inputKeyMaterial: SymmetricKey(data: passwordData),
            salt: salt,
            info: Data(infoString.utf8),
            outputByteCount: 32)
        return key
    }
    
    // Encrypt a message (string) into Data using AES-GCM
    static func encrypt(message: String, using key: SymmetricKey) -> Data? {
        let messageData = Data(message.utf8)
        do {
            // AES-GCM will generate a random nonce automatically
            let sealedBox = try AES.GCM.seal(messageData, using: key)
            // Return combined data containing nonce, ciphertext, and tag
            return sealedBox.combined
        } catch {
            print("Encryption error: \(error)")
            return nil
        }
    }
    
    // Decrypt Data using AES-GCM and return the original message string
    static func decrypt(data: Data, using key: SymmetricKey) -> String? {
        do {
            let sealedBox = try AES.GCM.SealedBox(combined: data)
            let decryptedData = try AES.GCM.open(sealedBox, using: key)
            return String(data: decryptedData, encoding: .utf8)
        } catch {
            print("Decryption error: \(error)")
            return nil
        }
    }
    
    // Encrypt a message and return it as a Base64 encoded string (for easy transmission)
    static func encryptToBase64(message: String, using key: SymmetricKey) -> String? {
        guard let encryptedData = encrypt(message: message, using: key) else {
            return nil
        }
        return encryptedData.base64EncodedString()
    }
    
    // Decrypt a Base64 encoded string
    static func decryptFromBase64(encoded: String, using key: SymmetricKey) -> String? {
        guard let data = Data(base64Encoded: encoded) else {
            return nil
        }
        return decrypt(data: data, using: key)
    }
}
