import XCTest
@testable import P2Pclipboard // Replace with your actual module name

class ClipboardEncryptionTests: XCTestCase {
    
    // MARK: - Test Setup and Teardown
    
    override func setUp() {
        super.setUp()
        // Clear any existing password before each test
        ClipboardEncryption.clearPassword()
    }
    
    override func tearDown() {
        // Clean up after each test
        ClipboardEncryption.clearPassword()
        super.tearDown()
    }
    
    // MARK: - Password Setting Tests
    
    func testSetValidPassword() {
        // Test that setting a valid password returns true
        let result = ClipboardEncryption.setPassword("SecurePassword123")
        XCTAssertTrue(result, "Setting a valid password should return true")
        XCTAssertTrue(ClipboardEncryption.isPasswordSet, "isPasswordSet should be true after setting password")
    }
    
    func testSetEmptyPassword() {
        // Test that setting an empty password returns false
        let result = ClipboardEncryption.setPassword("")
        XCTAssertFalse(result, "Setting an empty password should return false")
        XCTAssertFalse(ClipboardEncryption.isPasswordSet, "isPasswordSet should be false after setting empty password")
    }
    
    func testClearPassword() {
        // Set a password first
        XCTAssertTrue(ClipboardEncryption.setPassword("TestPassword"))
        XCTAssertTrue(ClipboardEncryption.isPasswordSet)
        
        // Now clear it
        ClipboardEncryption.clearPassword()
        XCTAssertFalse(ClipboardEncryption.isPasswordSet, "isPasswordSet should be false after clearing password")
    }
    
    // MARK: - Encryption/Decryption Tests
    
    func testEncryptionDecryptionWithValidPassword() {
        // Set a password
        ClipboardEncryption.setPassword("SecurePassword123")
        
        // Test data to encrypt
        let originalText = "This is a secret message."
        let originalData = Data(originalText.utf8)
        
        // Encrypt the data
        guard let encryptedData = ClipboardEncryption.encrypt(data: originalData) else {
            XCTFail("Encryption should succeed with valid password")
            return
        }
        
        // Verify encrypted data is different from original
        XCTAssertNotEqual(encryptedData, originalData, "Encrypted data should be different from original data")
        
        // Decrypt the data
        guard let decryptedData = ClipboardEncryption.decrypt(encryptedData: encryptedData) else {
            XCTFail("Decryption should succeed with valid password")
            return
        }
        
        // Verify decrypted data matches original
        let decryptedText = String(data: decryptedData, encoding: .utf8)
        XCTAssertEqual(decryptedText, originalText, "Decrypted text should match original text")
    }
    
    func testEncryptWithoutPassword() {
        // Don't set a password
        
        // Try to encrypt data
        let data = Data("Test data".utf8)
        let encryptedData = ClipboardEncryption.encrypt(data: data)
        
        // Should fail and return nil
        XCTAssertNil(encryptedData, "Encryption should fail without a password")
    }
    
    func testDecryptWithoutPassword() {
        // Set a password and encrypt some data
        ClipboardEncryption.setPassword("TemporaryPassword")
        let originalData = Data("Test data".utf8)
        let encryptedData = ClipboardEncryption.encrypt(data: originalData)
        
        // Clear the password
        ClipboardEncryption.clearPassword()
        
        // Try to decrypt with no password set
        let decryptedData = ClipboardEncryption.decrypt(encryptedData: encryptedData!)
        
        // Should fail and return nil
        XCTAssertNil(decryptedData, "Decryption should fail without a password")
    }
    
    func testDecryptWithWrongPassword() {
        // Set a password and encrypt some data
        ClipboardEncryption.setPassword("CorrectPassword")
        let originalData = Data("Test data".utf8)
        let encryptedData = ClipboardEncryption.encrypt(data: originalData)!
        
        // Change to a different password
        ClipboardEncryption.setPassword("WrongPassword")
        
        // Try to decrypt with wrong password
        let decryptedData = ClipboardEncryption.decrypt(encryptedData: encryptedData)
        
        // Should fail and return nil
        XCTAssertNil(decryptedData, "Decryption should fail with wrong password")
    }
    
    func testEncryptDecryptEmptyData() {
        // Set a password
        ClipboardEncryption.setPassword("SecurePassword123")
        
        // Test with empty data
        let emptyData = Data()
        
        // Encrypt the empty data
        guard let encryptedData = ClipboardEncryption.encrypt(data: emptyData) else {
            XCTFail("Encryption of empty data should succeed")
            return
        }
        
        // Decrypt the data
        guard let decryptedData = ClipboardEncryption.decrypt(encryptedData: encryptedData) else {
            XCTFail("Decryption of encrypted empty data should succeed")
            return
        }
        
        // Verify decrypted data is still empty
        XCTAssertEqual(decryptedData, emptyData, "Decrypted data should be empty")
    }
    
    func testDecryptInvalidData() {
        // Set a password
        ClipboardEncryption.setPassword("SecurePassword123")
        
        // Create some invalid encrypted data
        let invalidData = Data([0x01, 0x02, 0x03, 0x04])
        
        // Try to decrypt invalid data
        let decryptedData = ClipboardEncryption.decrypt(encryptedData: invalidData)
        
        // Should fail and return nil
        XCTAssertNil(decryptedData, "Decryption should fail with invalid data")
    }
    
    // MARK: - Multiple Data Encryption Tests
    
    func testEncryptMultipleDataSets() {
        // Set a password
        ClipboardEncryption.setPassword("SecurePassword123")
        
        // Create multiple data sets
        let data1 = Data("First message".utf8)
        let data2 = Data("Second message".utf8)
        let data3 = Data("Third message".utf8)
        
        // Encrypt each data set
        let encrypted1 = ClipboardEncryption.encrypt(data: data1)
        let encrypted2 = ClipboardEncryption.encrypt(data: data2)
        let encrypted3 = ClipboardEncryption.encrypt(data: data3)
        
        // All encryptions should succeed
        XCTAssertNotNil(encrypted1)
        XCTAssertNotNil(encrypted2)
        XCTAssertNotNil(encrypted3)
        
        // Encrypted forms should all be different from each other
        XCTAssertNotEqual(encrypted1, encrypted2)
        XCTAssertNotEqual(encrypted2, encrypted3)
        XCTAssertNotEqual(encrypted1, encrypted3)
        
        // Should be able to decrypt each correctly
        let decrypted1 = ClipboardEncryption.decrypt(encryptedData: encrypted1!)
        let decrypted2 = ClipboardEncryption.decrypt(encryptedData: encrypted2!)
        let decrypted3 = ClipboardEncryption.decrypt(encryptedData: encrypted3!)
        
        // All decryptions should succeed and match original
        XCTAssertEqual(decrypted1, data1)
        XCTAssertEqual(decrypted2, data2)
        XCTAssertEqual(decrypted3, data3)
    }
    
    // MARK: - Large Data Tests
    
    func testEncryptDecryptLargeData() {
        // Set a password
        ClipboardEncryption.setPassword("SecurePassword123")
        
        // Create large data (1MB) - use a safer approach to generate random data
        let count = 1024 * 1024
        var bytes = [UInt8](repeating: 0, count: count)
        
        // Fill with random bytes
        for i in 0..<count {
            bytes[i] = UInt8.random(in: 0...255)
        }
        
        let largeData = Data(bytes)
        
        // Encrypt the large data
        guard let encryptedData = ClipboardEncryption.encrypt(data: largeData) else {
            XCTFail("Encryption of large data should succeed")
            return
        }
        
        // Decrypt the data
        guard let decryptedData = ClipboardEncryption.decrypt(encryptedData: encryptedData) else {
            XCTFail("Decryption of large data should succeed")
            return
        }
        
        // Verify decrypted data matches original
        XCTAssertEqual(decryptedData, largeData, "Decrypted large data should match original")
    }
}
