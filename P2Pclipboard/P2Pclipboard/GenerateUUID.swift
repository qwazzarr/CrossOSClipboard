import Foundation
import CryptoKit
import CoreBluetooth

class GenerateUUID {
    static func cbuuidFromString(_ input: String) -> CBUUID {
        let inputData = Data(input.utf8)
        let hashedData = SHA256.hash(data: inputData)

        var uuidBytes = Array(hashedData.prefix(16)) // 16 bytes = 128 bits

        // Set version to 5 (SHA-1-based deterministic UUID in RFC 4122, even if you're using SHA-256)
        uuidBytes[6] = (uuidBytes[6] & 0x0F) | 0x50
        
        // Set variant to RFC 4122
        uuidBytes[8] = (uuidBytes[8] & 0x3F) | 0x80

        let uuid = UUID(uuid: (
            uuidBytes[0], uuidBytes[1], uuidBytes[2], uuidBytes[3],
            uuidBytes[4], uuidBytes[5],
            uuidBytes[6], uuidBytes[7],
            uuidBytes[8], uuidBytes[9],
            uuidBytes[10], uuidBytes[11], uuidBytes[12], uuidBytes[13], uuidBytes[14], uuidBytes[15]
        ))

        return CBUUID(nsuuid: uuid)
    }
}

