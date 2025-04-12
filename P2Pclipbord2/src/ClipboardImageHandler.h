#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// Image formats that match the Swift/C++ enum in MessageProtocol
enum class ClipboardImageFormat : uint8_t {
    PNG = 3,
    JPEG = 4
};

// Structure to hold image processing result
struct ImageProcessResult {
    std::vector<uint8_t> data;
    size_t originalHash;
    bool success;
};

class ClipboardImageHandler {
public:
    ClipboardImageHandler();
    ~ClipboardImageHandler();

    // Check if clipboard contains an image
    bool hasImage();

    // Get image from clipboard, process it and return data with hash
    ImageProcessResult getImageFromClipboard(ClipboardImageFormat format = ClipboardImageFormat::JPEG, bool isCompressed = true);

    // Set an image to clipboard
    bool setClipboardImage(const std::vector<uint8_t>& data, ClipboardImageFormat format);

    // Check if a URL string points to an image
    bool isImageURL(const std::string& urlString);

    // Download an image from a URL
    void downloadImage(const std::string& urlString, bool isCompressed,
        std::function<void(std::vector<uint8_t>, ClipboardImageFormat)> callback);

    // Get content type corresponding to image format
    uint8_t getContentType(ClipboardImageFormat format) const;

    // Get file extension for format
    std::string getFileExtension(ClipboardImageFormat format) const;

    // Get MIME type for format
    std::string getMimeType(ClipboardImageFormat format) const;

private:
    // Configuration options
    const float maxImageDimension = 1200.0f;
    const float jpegCompressionQuality = 0.2f;
    const int maxImageSizeBytes = 1024 * 1024; // 1MB default max size

    // GDI+ token
    ULONG_PTR gdiplusToken;

    // Get the raw image from clipboard
    std::unique_ptr<Gdiplus::Bitmap> getRawClipboardImage();

    // Convert image to desired format without resizing
    std::vector<uint8_t> convertImageFormat(Gdiplus::Bitmap* image, ClipboardImageFormat format);

    // Process an image: resize if needed and convert to desired format
    std::vector<uint8_t> processImage(Gdiplus::Bitmap* image, ClipboardImageFormat format);

    // Resize the image if it exceeds maximum dimensions
    std::unique_ptr<Gdiplus::Bitmap> resizeImageIfNeeded(Gdiplus::Bitmap* image);

    // Save an image to memory stream
    std::vector<uint8_t> saveToMemory(Gdiplus::Bitmap* image, const CLSID& formatClsid, float quality = 0.0f);

    // Get JPEG codec
    int getCodecForFormat(ClipboardImageFormat format, CLSID* pClsid);

    // Create a bitmap from memory data
    std::unique_ptr<Gdiplus::Bitmap> createBitmapFromData(const std::vector<uint8_t>& data);

    // Get hash of image data
    size_t getImageDataHash(const std::vector<uint8_t>& data);

    // Get hash of image
    size_t getImageHash(Gdiplus::Bitmap* image, ClipboardImageFormat format = ClipboardImageFormat::JPEG);
};