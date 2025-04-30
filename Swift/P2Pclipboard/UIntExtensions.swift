// UIntExtensions.swift

import Foundation

extension UInt16 {
    var bigEndianBytes: [UInt8] {
        return [
            UInt8(truncatingIfNeeded: self >> 8),
            UInt8(truncatingIfNeeded: self)
        ]
    }
    
    init(bigEndianBytes: [UInt8]) {
        precondition(bigEndianBytes.count == 2)
        self = UInt16(bigEndianBytes[0]) << 8 | UInt16(bigEndianBytes[1])
    }
}

extension UInt32 {
    var bigEndianBytes: [UInt8] {
        return [
            UInt8(truncatingIfNeeded: self >> 24),
            UInt8(truncatingIfNeeded: self >> 16),
            UInt8(truncatingIfNeeded: self >> 8),
            UInt8(truncatingIfNeeded: self)
        ]
    }
    
    init(bigEndianBytes: [UInt8]) {
        precondition(bigEndianBytes.count == 4)
        self = UInt32(bigEndianBytes[0]) << 24 |
               UInt32(bigEndianBytes[1]) << 16 |
               UInt32(bigEndianBytes[2]) << 8 |
               UInt32(bigEndianBytes[3])
    }
}
