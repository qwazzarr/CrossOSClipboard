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
}
