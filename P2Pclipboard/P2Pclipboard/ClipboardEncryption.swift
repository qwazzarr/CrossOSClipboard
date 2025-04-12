//
//  ClipboardEncryption.swift
//  P2Pclipboard
import Foundation
import CryptoKit

/// Module for handling encryption/decryption of clipboard data
class ClipboardEncryption {
    
    // Constants for key derivation
    // Salt is a fixed value used to add additional entropy to the key derivation
    // It's typically public and doesn't need to be secret
    private static let saltString = "P2PClipboardSyncSalt2025"
    
    // Info is additional context for the key derivation
    // It helps ensure keys derived for different purposes are distinct
    private static let infoString = "P2PClipboardEncryptionContext"
    
    // Internal storage for the derived key
    private static var symmetricKey: SymmetricKey?
    
    /// Set the encryption password for the module
    /// - Parameter password: The shared secret password
    /// - Returns: True if password was successfully set
    @discardableResult
    static func setPassword(_ password: String) -> Bool {
        guard !password.isEmpty else {
            print("Password cannot be empty")
            return false
        }
        
        // Derive and store the symmetric key
        symmetricKey = deriveSymmetricKey(from: password)
        return true
    }
    
    /// Check if a password has been set
    /// - Returns: True if a password is set
    static var isPasswordSet: Bool {
        return symmetricKey != nil
    }
    
    /// Clear the current password
    static func clearPassword() {
        symmetricKey = nil
    }
    
    /// Derive a symmetric key using HKDF from the password
    /// - Parameter password: User-provided shared secret password
    /// - Returns: A 32-byte cryptographic key
    private static func deriveSymmetricKey(from password: String) -> SymmetricKey {
        let passwordData = Data(password.utf8)
        let salt = Data(saltString.utf8) // Use a constant salt
        
        // HKDF (HMAC-based Key Derivation Function) transforms the password
        // into a cryptographically strong key using the salt and info string
        let key = HKDF<SHA256>.deriveKey(
            inputKeyMaterial: SymmetricKey(data: passwordData),
            salt: salt,
            info: Data(infoString.utf8),
            outputByteCount: 32)
        
        return key
    }
    
    /// Encrypt data using the set password
    /// - Parameter data: Raw data to encrypt
    /// - Returns: Encrypted data (includes nonce, ciphertext, and authentication tag)
    static func encrypt(data: Data) -> Data? {
        guard let key = symmetricKey else {
            print("Error: No password has been set. Call setPassword() first.")
            return nil
        }
        
        return encrypt(data: data, using: key)
    }
    
    /// Decrypt data using the set password
    /// - Parameter encryptedData: Combined data (nonce, ciphertext, tag)
    /// - Returns: Decrypted original data or nil if decryption fails
    static func decrypt(encryptedData: Data) -> Data? {
        guard let key = symmetricKey else {
            print("Error: No password has been set. Call setPassword() first.")
            return nil
        }
        
        return decrypt(encryptedData: encryptedData, using: key)
    }
    
    /// Encrypt data using AES-GCM with the provided key
    /// - Parameters:
    ///   - data: Raw data to encrypt
    ///   - key: Symmetric key for encryption
    /// - Returns: Encrypted data (includes nonce, ciphertext, and authentication tag)
    private static func encrypt(data: Data, using key: SymmetricKey) -> Data? {
        do {
            // AES-GCM automatically generates a random nonce and authentication tag
            let sealedBox = try AES.GCM.seal(data, using: key)
            // Return combined data containing nonce, ciphertext, and tag
            return sealedBox.combined
        } catch {
            print("Encryption error: \(error)")
            return nil
        }
    }
    
    /// Decrypt data using AES-GCM with the provided key
    /// - Parameters:
    ///   - encryptedData: Combined data (nonce, ciphertext, tag)
    ///   - key: Symmetric key for decryption
    /// - Returns: Decrypted original data or nil if decryption fails
    private static func decrypt(encryptedData: Data, using key: SymmetricKey) -> Data? {
        do {
            let sealedBox = try AES.GCM.SealedBox(combined: encryptedData)
            let decryptedData = try AES.GCM.open(sealedBox, using: key)
            return decryptedData
        } catch {
            print("Decryption error: \(error)")
            return nil
        }
    }
}
