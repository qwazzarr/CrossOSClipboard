import Foundation
import CryptoKit

/// KeyGenerator provides functions for creating secure random keys
/// that can be used for device identification and authentication
class KeyGenerator {
    
    /// Generates a formatted key with segments separated by dashes
    /// - Parameters:
    ///   - segmentCount: Number of segments in the key (default: 3)
    ///   - segmentLength: Length of each segment (default: 4)
    /// - Returns: A formatted string key like "ABCD-1234-WXYZ"
    static func generateFormattedKey(segmentCount: Int = 3, segmentLength: Int = 4) -> String {
        var segments: [String] = []
        
        for _ in 0..<segmentCount {
            segments.append(generateRandomString(length: segmentLength))
        }
        
        return segments.joined(separator: "-")
    }
    
    /// Generates a random string of specified length
    /// - Parameter length: Length of the random string
    /// - Returns: A random alphanumeric string
    static func generateRandomString(length: Int) -> String {
        let characters = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789" // Omitting easily confused characters like O/0, I/1
        var result = ""
        
        // Create a cryptographically secure random number generator
        var randomBytes = [UInt8](repeating: 0, count: length)
        _ = SecRandomCopyBytes(kSecRandomDefault, length, &randomBytes)
        
        // Map random bytes to characters
        for i in 0..<length {
            let index = Int(randomBytes[i]) % characters.count
            let character = characters.index(characters.startIndex, offsetBy: index)
            result.append(characters[character])
        }
        
        return result
    }
    
}
