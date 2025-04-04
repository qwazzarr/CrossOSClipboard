import Foundation
import SwiftUI

#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

// Content types for image messages - should match the C++ enum in MessageProtocol.h
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

// Class to handle clipboard image detection, processing and transfer
class ClipboardImageHandler {
    // Configuration options
    private let maxImageDimension: CGFloat = 1200
    private let jpegCompressionQuality: CGFloat = 0.7
    private let maxImageSizeBytes: Int = 1024 * 1024 // 1MB default max size
    
    // For hash generation
    private let hashingBufferSize: Int = 16384 // 16KB
    
    // MARK: - Public Methods
    
    /// Check if clipboard contains an image
    func hasImage() -> Bool {
#if os(iOS)
        return UIPasteboard.general.hasImages
#elseif os(macOS)
        return NSPasteboard.general.canReadObject(forClasses: [NSImage.self], options: nil)
#else
        return false
#endif
    }
    
    /// Get image from clipboard, process it and return as Data along with original hash
    /// - Parameters:
    ///   - format: The desired output format (JPEG or PNG)
    ///   - isCompressed: Whether to apply resizing and compression
    /// - Returns: A tuple with processed image data and hash of the original image, or nil if no image
    func getImageFromClipboard(format: ClipboardImageFormat = .jpeg, isCompressed: Bool = true) -> (data: Data, originalHash: Int)? {
        guard let originalImage = getRawClipboardImage() else {
            print("No image found in clipboard")
            return nil
        }
        
        // Always calculate the hash from the original unprocessed image
        let originalHash = getImageHash(originalImage, format: format)
        
        // Process the image based on the isCompressed flag
        let processedData: Data?
        if isCompressed {
            // Process and compress the image
            processedData = processImage(originalImage, format: format)
        } else {
            // Simple conversion without resizing
            processedData = convertImageFormat(originalImage, format: format)
        }
        
        // Return both the processed data and original hash if successful
        if let data = processedData {
            return (data: data, originalHash: originalHash)
        }
        
        return nil
    }
    
    /// Process and set an image to the clipboard
    /// - Parameters:
    ///   - data: The image data to set
    ///   - format: The format of the provided image data
    /// - Returns: Success or failure
    func setClipboardImage(_ data: Data, format: ClipboardImageFormat) -> Bool {
        guard let image = createImage(from: data) else {
            print("Failed to create image from data")
            return false
        }
        
#if os(iOS)
        UIPasteboard.general.image = image
        return true
#elseif os(macOS)
        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()
        return pasteboard.writeObjects([image])
#else
        return false
#endif
    }
    
    /// Check if a URL string points to an image
    /// - Parameter urlString: The URL string to check
    /// - Returns: True if the URL appears to point to an image
    func isImageURL(_ urlString: String) -> Bool {
        let imageExtensions = ["jpg", "jpeg", "png", "gif", "webp", "bmp", "tiff", "tif"]
        guard let url = URL(string: urlString),
              let pathExtension = url.pathExtension.lowercased() as String? else {
            return false
        }
        
        return imageExtensions.contains(pathExtension)
    }
    
    /// Try to download an image from a URL
    /// - Parameters:
    ///   - urlString: The URL of the image
    ///   - isCompressed: Whether to compress and resize the downloaded image
    ///   - completion: Callback with the image data and format, or nil if failed
    func downloadImage(from urlString: String, isCompressed: Bool = true, completion: @escaping (Data?, ClipboardImageFormat?) -> Void) {
        guard let url = URL(string: urlString) else {
            completion(nil, nil)
            return
        }
        
        let task = URLSession.shared.dataTask(with: url) { data, response, error in
            guard let data = data, error == nil else {
                completion(nil, nil)
                return
            }
            
            // Try to determine the image format
            if let mimeType = response?.mimeType {
                if mimeType.contains("jpeg") || mimeType.contains("jpg") {
                    if isCompressed, let image = self.createImage(from: data) {
                        if let processedData = self.processImage(image, format: .jpeg) {
                            completion(processedData, .jpeg)
                        } else {
                            completion(data, .jpeg)
                        }
                    } else {
                        completion(data, .jpeg)
                    }
                } else if mimeType.contains("png") {
                    if isCompressed, let image = self.createImage(from: data) {
                        if let processedData = self.processImage(image, format: .png) {
                            completion(processedData, .png)
                        } else {
                            completion(data, .png)
                        }
                    } else {
                        completion(data, .png)
                    }
                } else {
                    // For other formats, try to convert to JPEG
                    if let image = self.createImage(from: data) {
                        if isCompressed {
                            if let jpegData = self.processImage(image, format: .jpeg) {
                                completion(jpegData, .jpeg)
                            } else {
                                completion(nil, nil)
                            }
                        } else {
                            if let jpegData = self.convertImageFormat(image, format: .jpeg) {
                                completion(jpegData, .jpeg)
                            } else {
                                completion(nil, nil)
                            }
                        }
                    } else {
                        completion(nil, nil)
                    }
                }
            } else {
                // No MIME type, try to guess from the URL
                let pathExtension = url.pathExtension.lowercased()
                if pathExtension == "jpg" || pathExtension == "jpeg" {
                    completion(data, .jpeg)
                } else if pathExtension == "png" {
                    completion(data, .png)
                } else {
                    // For other formats, try to convert to JPEG
                    if let image = self.createImage(from: data) {
                        if isCompressed {
                            if let jpegData = self.processImage(image, format: .jpeg) {
                                completion(jpegData, .jpeg)
                            } else {
                                completion(nil, nil)
                            }
                        } else {
                            if let jpegData = self.convertImageFormat(image, format: .jpeg) {
                                completion(jpegData, .jpeg)
                            } else {
                                completion(nil, nil)
                            }
                        }
                    } else {
                        completion(nil, nil)
                    }
                }
            }
        }
        
        task.resume()
    }
    
    // MARK: - Private Helper Methods
    
    /// Get the raw image from clipboard without processing
    private func getRawClipboardImage() -> PlatformImage? {
#if os(iOS)
        return UIPasteboard.general.image
#elseif os(macOS)
        guard NSPasteboard.general.canReadObject(forClasses: [NSImage.self], options: nil) else {
            return nil
        }
        return NSPasteboard.general.readObjects(forClasses: [NSImage.self], options: nil)?.first as? NSImage
#else
        return nil
#endif
    }
    
    /// Convert image to desired format without resizing or quality reduction
    private func convertImageFormat(_ image: PlatformImage, format: ClipboardImageFormat) -> Data? {
#if os(iOS)
        switch format {
        case .jpeg:
            return image.jpegData(compressionQuality: 1.0) // No compression (quality = 1.0)
        case .png:
            return image.pngData()
        }
#elseif os(macOS)
        guard let cgImage = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
            return nil
        }
        
        let bitmapRep = NSBitmapImageRep(cgImage: cgImage)
        
        switch format {
        case .jpeg:
            return bitmapRep.representation(using: .jpeg, properties: [:]) // No compression properties
        case .png:
            return bitmapRep.representation(using: .png, properties: [:])
        }
#else
        return nil
#endif
    }
    
    /// Process an image: resize if needed and convert to the desired format with compression
    private func processImage(_ image: PlatformImage, format: ClipboardImageFormat) -> Data? {
        // First resize the image if needed
        let resizedImage = resizeImageIfNeeded(image)
        
        // Then convert to the requested format with compression for JPEG
#if os(iOS)
        switch format {
        case .jpeg:
            return resizedImage.jpegData(compressionQuality: jpegCompressionQuality)
        case .png:
            return resizedImage.pngData()
        }
#elseif os(macOS)
        guard let cgImage = resizedImage.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
            return nil
        }
        
        let bitmapRep = NSBitmapImageRep(cgImage: cgImage)
        
        switch format {
        case .jpeg:
            return bitmapRep.representation(using: .jpeg, properties: [.compressionFactor: jpegCompressionQuality])
        case .png:
            return bitmapRep.representation(using: .png, properties: [:])
        }
#else
        return nil
#endif
    }
    
    /// Resize the image if it exceeds the maximum dimensions
    private func resizeImageIfNeeded(_ image: PlatformImage) -> PlatformImage {
        let size = getImageSize(image)
        
        // Check if resize is needed
        if size.width <= maxImageDimension && size.height <= maxImageDimension {
            return image
        }
        
        // Calculate new dimensions maintaining aspect ratio
        var newSize: CGSize
        if size.width > size.height {
            let newWidth = maxImageDimension
            let newHeight = size.height * (newWidth / size.width)
            newSize = CGSize(width: newWidth, height: newHeight)
        } else {
            let newHeight = maxImageDimension
            let newWidth = size.width * (newHeight / size.height)
            newSize = CGSize(width: newWidth, height: newHeight)
        }
        
        // Perform resize
#if os(iOS)
        UIGraphicsBeginImageContextWithOptions(newSize, false, 0.0)
        image.draw(in: CGRect(origin: .zero, size: newSize))
        let resizedImage = UIGraphicsGetImageFromCurrentImageContext()
        UIGraphicsEndImageContext()
        return resizedImage ?? image
#elseif os(macOS)
        let resizedImage = NSImage(size: newSize)
        resizedImage.lockFocus()
        image.draw(in: CGRect(origin: .zero, size: newSize),
                   from: CGRect(origin: .zero, size: size),
                   operation: .copy,
                   fraction: 1.0)
        resizedImage.unlockFocus()
        return resizedImage
#else
        return image
#endif
    }
    
    /// Get the size of an image
    private func getImageSize(_ image: PlatformImage) -> CGSize {
#if os(iOS)
        return image.size
#elseif os(macOS)
        return image.size
#else
        return CGSize.zero
#endif
    }
    
    /// Create a platform-specific image from data
    private func createImage(from data: Data) -> PlatformImage? {
#if os(iOS)
        return UIImage(data: data)
#elseif os(macOS)
        return NSImage(data: data)
#else
        return nil
#endif
    }
    
    // MARK: - Image Hashing
    
    /// Generate a hash value for image data
    /// - Parameter data: The image data to hash
    /// - Returns: A hash value as Int
    func getImageDataHash(_ data: Data) -> Int {
        return data.hashValue
    }
    
    /// Generate a hash for an image
    /// - Parameters:
    ///   - image: The image to hash
    ///   - format: The format to use for conversion before hashing
    /// - Returns: A hash value, or 0 if hashing failed
    func getImageHash(_ image: PlatformImage, format: ClipboardImageFormat = .jpeg) -> Int {
        // Convert to data first
        if let imageData = convertImageFormat(image, format: format) {
            return getImageDataHash(imageData)
        }
        return 0
    }
    
}

// Define a platform-agnostic image type
#if os(iOS)
typealias PlatformImage = UIImage
#elseif os(macOS)
typealias PlatformImage = NSImage
#endif
