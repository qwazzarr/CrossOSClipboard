import XCTest
@testable import P2Pclipboard // Replace with your actual module name

class KeyGeneratorTests: XCTestCase {
    
    // MARK: - Helper Functions
    
    /// Checks if a string contains only characters from a specified set
    func string(_ str: String, containsOnlyCharactersIn characterSet: String) -> Bool {
        let allowedChars = CharacterSet(charactersIn: characterSet)
        let strChars = CharacterSet(charactersIn: str)
        return allowedChars.isSuperset(of: strChars)
    }
    
    /// Checks if a formatted key matches expected pattern
    func isValidFormattedKey(_ key: String, segmentCount: Int, segmentLength: Int) -> Bool {
        let segments = key.split(separator: "-")
        guard segments.count == segmentCount else { return false }
        
        // Check that each segment has the correct length
        for segment in segments {
            guard segment.count == segmentLength else { return false }
        }
        
        // Check that key only contains expected characters
        let allowedChars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789-"
        return string(key, containsOnlyCharactersIn: allowedChars)
    }
    
    // MARK: - Random String Tests
    
    func testGenerateRandomStringLength() {
        // Test strings of various lengths
        for length in [1, 4, 8, 16, 32, 64] {
            let randomString = KeyGenerator.generateRandomString(length: length)
            XCTAssertEqual(randomString.count, length, "Random string should have length \(length)")
        }
    }
    
    func testGenerateRandomStringCharacters() {
        // The allowed character set from the implementation
        let allowedChars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
        
        // Test with different lengths
        for length in [10, 20, 30] {
            let randomString = KeyGenerator.generateRandomString(length: length)
            
            // Check that the string only contains allowed characters
            XCTAssertTrue(
                string(randomString, containsOnlyCharactersIn: allowedChars),
                "Random string should only contain allowed characters"
            )
        }
    }
    
    func testGenerateRandomStringUniqueness() {
        // Generate multiple strings and verify they're different
        let count = 100
        var strings = Set<String>()
        
        for _ in 0..<count {
            let randomString = KeyGenerator.generateRandomString(length: 8)
            strings.insert(randomString)
        }
        
        // If strings are truly random, nearly all should be unique
        // Allow for a very small chance of collision
        XCTAssertGreaterThan(strings.count, count - 3, "Random strings should be unique")
    }
    
    func testGenerateRandomStringZeroLength() {
        let randomString = KeyGenerator.generateRandomString(length: 0)
        XCTAssertEqual(randomString, "", "Zero-length random string should be an empty string")
    }
    
    // MARK: - Formatted Key Tests
    
    func testGenerateFormattedKeyDefaultParameters() {
        let key = KeyGenerator.generateFormattedKey()
        
        // Default is 3 segments of 4 characters each
        XCTAssertTrue(
            isValidFormattedKey(key, segmentCount: 3, segmentLength: 4),
            "Default key should have 3 segments of 4 characters each"
        )
        
        // Length should be: 3 segments × 4 chars + 2 dashes = 14
        XCTAssertEqual(key.count, 14, "Default key should be 14 characters long")
    }
    
    func testGenerateFormattedKeyCustomParameters() {
        // Test different combinations of segment count and length
        let testCases = [
            (2, 3),  // 2 segments of 3 chars
            (4, 2),  // 4 segments of 2 chars
            (5, 5),  // 5 segments of 5 chars
            (1, 10)  // 1 segment of 10 chars
        ]
        
        for (segmentCount, segmentLength) in testCases {
            let key = KeyGenerator.generateFormattedKey(
                segmentCount: segmentCount,
                segmentLength: segmentLength
            )
            
            // Verify the format
            XCTAssertTrue(
                isValidFormattedKey(key, segmentCount: segmentCount, segmentLength: segmentLength),
                "Key should have \(segmentCount) segments of \(segmentLength) characters each"
            )
            
            // Verify the length: segments × length + (segments-1) dashes
            let expectedLength = segmentCount * segmentLength + (segmentCount - 1)
            XCTAssertEqual(key.count, expectedLength, "Key should be \(expectedLength) characters long")
        }
    }
    
    func testGenerateFormattedKeyUniqueness() {
        // Generate multiple keys and verify they're different
        let count = 100
        var keys = Set<String>()
        
        for _ in 0..<count {
            let key = KeyGenerator.generateFormattedKey()
            keys.insert(key)
        }
        
        // If keys are truly random, nearly all should be unique
        // Allow for a very small chance of collision
        XCTAssertGreaterThan(keys.count, count - 3, "Formatted keys should be unique")
    }
    
    func testGenerateFormattedKeyEdgeCases() {
        // Test with zero segments (should return empty string)
        let zeroSegments = KeyGenerator.generateFormattedKey(segmentCount: 0, segmentLength: 4)
        XCTAssertEqual(zeroSegments, "", "Key with zero segments should be empty")
        
        // Test with zero segment length (should return just dashes)
        let zeroLength = KeyGenerator.generateFormattedKey(segmentCount: 3, segmentLength: 0)
        XCTAssertEqual(zeroLength, "--", "Key with zero segment length should be just dashes")
        
        // Test single segment with zero length
        let singleZeroLength = KeyGenerator.generateFormattedKey(segmentCount: 1, segmentLength: 0)
        XCTAssertEqual(singleZeroLength, "", "Single segment with zero length should be empty")
    }
    
    // MARK: - Distribution Tests
    
    func testCharacterDistribution() {
        // This test ensures that the random generation has reasonable distribution
        // Not a perfect test, but should catch obvious issues
        
        // Generate a large sample of characters
        let sampleSize = 10000
        var characterCounts: [Character: Int] = [:]
        let allowedChars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
        
        let randomString = KeyGenerator.generateRandomString(length: sampleSize)
        
        // Count occurrences of each character
        for char in randomString {
            characterCounts[char, default: 0] += 1
        }
        
        // Each character should appear approximately the same number of times
        // We expect each character to appear about 1/32 of the time (given 32 possible characters)
        let expectedCount = sampleSize / allowedChars.count
        let variance = Double(expectedCount) * 0.5 // Allow 50% variance
        
        // Check that every allowed character appears and within reasonable distribution
        for char in allowedChars {
            let count = characterCounts[char] ?? 0
            
            // Character should appear in the sample
            XCTAssertGreaterThan(count, 0, "Character '\(char)' should appear in the random sample")
            
            // Check if distribution is reasonable
            XCTAssertTrue(
                abs(count - expectedCount) < Int(variance),
                "Character '\(char)' appears \(count) times, expected approximately \(expectedCount) ± \(variance)"
            )
        }
        
        // No unexpected characters should appear
        XCTAssertEqual(
            characterCounts.keys.count, allowedChars.count,
            "No unexpected characters should appear in the generated strings"
        )
    }
}
