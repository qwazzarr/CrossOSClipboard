import Foundation
//  MessageType.swift
//  P2Pclipboard
//
//  Created by Арсений Хмара on 30.03.2025.
//

enum MessageContentType: UInt8 {
    case plainText = 1
    case rtfText = 2
    case pngImage = 3
    case jpegImage = 4
    case pdfDocument = 5
    case htmlContent = 6
    case webpImage = 7
}

enum CompressionLevel {
    case low    // 0.9 quality
    case medium // 0.7 quality (default)
    case high   // 0.4 quality for BLE
    case extreme // 0.2 quality for very slow connections
    
    var jpegQuality: CGFloat {
        switch self {
        case .low: return 0.9
        case .medium: return 0.7
        case .high: return 0.4
        case .extreme: return 0.2
        }
    }
    
    var webpQuality: CGFloat {
        switch self {
        case .low: return 0.9
        case .medium: return 0.7
        case .high: return 0.5
        case .extreme: return 0.3
        }
    }
    
    var maxDimension: CGFloat {
        switch self {
        case .low: return 1600
        case .medium: return 1200
        case .high: return 800
        case .extreme: return 600
        }
    }
}

enum ClipboardImageFormat: UInt8 {
    case png = 3
    case jpeg = 4
    
    var contentType: MessageContentType {
        return MessageContentType(rawValue: self.rawValue) ?? .plainText
    }
    
    var fileExtension: String {
        switch self {
        case .jpeg: return "jpg"
        case .png: return "png"
        }
    }
    
    var mimeType: String {
        switch self {
        case .jpeg: return "image/jpeg"
        case .png: return "image/png"
        }
    }
}
