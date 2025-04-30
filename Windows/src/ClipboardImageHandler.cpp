#include "ClipboardImageHandler.h"
#include <wininet.h>
#include <shlwapi.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <atlbase.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shlwapi.lib")

ClipboardImageHandler::ClipboardImageHandler() {
    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

ClipboardImageHandler::~ClipboardImageHandler() {
    // Shut down GDI+
    Gdiplus::GdiplusShutdown(gdiplusToken);
}

bool ClipboardImageHandler::hasImage() {
    if (!OpenClipboard(NULL)) {
        std::cerr << "Failed to open clipboard" << std::endl;
        return false;
    }

    bool result = IsClipboardFormatAvailable(CF_BITMAP) ||
        IsClipboardFormatAvailable(CF_DIB) ||
        IsClipboardFormatAvailable(CF_DIBV5);

    CloseClipboard();
    return result;
}

ImageProcessResult ClipboardImageHandler::getImageFromClipboard(ClipboardImageFormat format, bool isCompressed) {
    ImageProcessResult result = { {}, 0, false };

    std::unique_ptr<Gdiplus::Bitmap> originalImage = getRawClipboardImage();
    if (!originalImage) {
        std::cerr << "No image found in clipboard" << std::endl;
        return result;
    }

    // Always calculate the hash from the original unprocessed image
    result.originalHash = getImageHash(originalImage.get(), format);

    // Process the image based on the isCompressed flag
    std::vector<uint8_t> processedData;
    if (isCompressed) {
        // Process and compress the image
        processedData = processImage(originalImage.get(), format);
    }
    else {
        // Simple conversion without resizing
        processedData = convertImageFormat(originalImage.get(), format);
    }

    if (!processedData.empty()) {
        result.data = std::move(processedData);
        result.success = true;
    }

    return result;
}

bool ClipboardImageHandler::setClipboardImage(const std::vector<uint8_t>& data, ClipboardImageFormat format) {
    // Create bitmap from data
    std::unique_ptr<Gdiplus::Bitmap> bitmap = createBitmapFromData(data);
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        std::cerr << "Failed to create valid bitmap from data" << std::endl;
        return false;
    }

    // Create a GDI+ bitmap in 32-bit ARGB format
    UINT width = bitmap->GetWidth();
    UINT height = bitmap->GetHeight();

    // Create a DIB that's compatible with the clipboard
    HDC hdc = GetDC(NULL);
    if (!hdc) {
        std::cerr << "Failed to get device context" << std::endl;
        return false;
    }

    // Create a global memory object
    BITMAPINFOHEADER bi = { 0 };
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = height;  // Positive for bottom-up DIB which is standard
    bi.biPlanes = 1;
    bi.biBitCount = 24;    // 24-bit RGB for maximum compatibility
    bi.biCompression = BI_RGB;

    // Calculate size of DIB data
    int dibSize = sizeof(BITMAPINFOHEADER) + (width * height * 3);  // 3 bytes per pixel (RGB)

    // Allocate global memory for DIB
    HGLOBAL hDIB = GlobalAlloc(GMEM_MOVEABLE, dibSize);
    if (!hDIB) {
        std::cerr << "Failed to allocate memory for DIB" << std::endl;
        ReleaseDC(NULL, hdc);
        return false;
    }

    // Lock the memory and get a pointer to it
    LPVOID pDIB = GlobalLock(hDIB);
    if (!pDIB) {
        std::cerr << "Failed to lock DIB memory" << std::endl;
        GlobalFree(hDIB);
        ReleaseDC(NULL, hdc);
        return false;
    }

    // Copy the BITMAPINFOHEADER into the DIB
    memcpy(pDIB, &bi, sizeof(BITMAPINFOHEADER));

    // Create a compatible DC for the bitmap
    HDC memDC = CreateCompatibleDC(hdc);
    if (!memDC) {
        std::cerr << "Failed to create compatible DC" << std::endl;
        GlobalUnlock(hDIB);
        GlobalFree(hDIB);
        ReleaseDC(NULL, hdc);
        return false;
    }

    // Create a temporary GDI bitmap we can draw to
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, width, height);
    if (!hBitmap) {
        std::cerr << "Failed to create compatible bitmap" << std::endl;
        DeleteDC(memDC);
        GlobalUnlock(hDIB);
        GlobalFree(hDIB);
        ReleaseDC(NULL, hdc);
        return false;
    }

    // Select bitmap into DC
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hBitmap);

    // Draw the GDI+ bitmap onto the GDI bitmap
    Gdiplus::Graphics graphics(memDC);
    graphics.DrawImage(bitmap.get(), 0, 0, width, height);

    // Get the bits from the bitmap
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader = bi;
    int result = GetDIBits(memDC, hBitmap, 0, height,
        (BYTE*)pDIB + sizeof(BITMAPINFOHEADER), &bmi, DIB_RGB_COLORS);

    if (!result) {
        std::cerr << "Failed to get DIB bits" << std::endl;
        SelectObject(memDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(memDC);
        GlobalUnlock(hDIB);
        GlobalFree(hDIB);
        ReleaseDC(NULL, hdc);
        return false;
    }

    // Clean up GDI resources
    SelectObject(memDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(NULL, hdc);

    // Unlock the global memory
    GlobalUnlock(hDIB);

    // Open the clipboard
    if (!OpenClipboard(NULL)) {
        std::cerr << "Failed to open clipboard" << std::endl;
        GlobalFree(hDIB);
        return false;
    }

    // Empty the clipboard
    if (!EmptyClipboard()) {
        std::cerr << "Failed to empty clipboard" << std::endl;
        CloseClipboard();
        GlobalFree(hDIB);
        return false;
    }

    // Set the clipboard data
    HANDLE clipResult = SetClipboardData(CF_DIB, hDIB);

    // Close the clipboard
    CloseClipboard();

    if (!clipResult) {
        DWORD error = GetLastError();
        std::cerr << "Failed to set clipboard data. Error: " << error << std::endl;
        GlobalFree(hDIB);
        return false;
    }

    std::cout << "Successfully set image to clipboard" << std::endl;
    // The system now owns the DIB handle
    return true;
}

bool ClipboardImageHandler::isImageURL(const std::string& urlString) {
    // List of common image extensions
    const std::vector<std::string> imageExtensions = {
        ".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".tiff", ".tif"
    };

    // Extract the file extension
    std::string extension;
    size_t dotPos = urlString.find_last_of('.');
    if (dotPos != std::string::npos) {
        extension = urlString.substr(dotPos);
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    }

    // Check if the extension matches any image extension
    return std::find(imageExtensions.begin(), imageExtensions.end(), extension) != imageExtensions.end();
}

//void ClipboardImageHandler::downloadImage(const std::string& urlString, bool isCompressed,
//    std::function<void(std::vector<uint8_t>, ClipboardImageFormat)> callback) {
//    // Start the download on another thread
//    std::thread([this, urlString, isCompressed, callback]() {
//        HINTERNET hInternet = InternetOpen("ClipboardSync/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
//        if (!hInternet) {
//            std::cerr << "Failed to initialize WinInet" << std::endl;
//            callback({}, ClipboardImageFormat::JPEG);
//            return;
//        }
//
//        HINTERNET hUrl = InternetOpenUrl(hInternet, urlString.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
//        if (!hUrl) {
//            std::cerr << "Failed to open URL" << std::endl;
//            InternetCloseHandle(hInternet);
//            callback({}, ClipboardImageFormat::JPEG);
//            return;
//        }
//
//        // Get content type
//        char contentType[256];
//        DWORD contentTypeSize = sizeof(contentType);
//        bool isJpeg = false;
//        bool isPng = false;
//
//        if (HttpQueryInfo(hUrl, HTTP_QUERY_CONTENT_TYPE, contentType, &contentTypeSize, NULL)) {
//            std::string contentTypeStr(contentType);
//            std::transform(contentTypeStr.begin(), contentTypeStr.end(), contentTypeStr.begin(), ::tolower);
//
//            isJpeg = contentTypeStr.find("jpeg") != std::string::npos || contentTypeStr.find("jpg") != std::string::npos;
//            isPng = contentTypeStr.find("png") != std::string::npos;
//        }
//
//        // If content type couldn't be determined, try from the URL
//        if (!isJpeg && !isPng) {
//            isJpeg = urlString.find(".jpg") != std::string::npos || urlString.find(".jpeg") != std::string::npos;
//            isPng = urlString.find(".png") != std::string::npos;
//        }
//
//        // Default to JPEG if we still can't determine
//        ClipboardImageFormat format = isJpeg ? ClipboardImageFormat::JPEG :
//            (isPng ? ClipboardImageFormat::PNG : ClipboardImageFormat::JPEG);
//
//        // Download the image data
//        std::vector<uint8_t> imageData;
//        DWORD bytesRead = 0;
//        char buffer[4096];
//
//        while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
//            imageData.insert(imageData.end(), buffer, buffer + bytesRead);
//        }
//
//        InternetCloseHandle(hUrl);
//        InternetCloseHandle(hInternet);
//
//        if (imageData.empty()) {
//            std::cerr << "Failed to download image data" << std::endl;
//            callback({}, format);
//            return;
//        }
//
//        // Process the image if required
//        if (isCompressed) {
//            std::unique_ptr<Gdiplus::Bitmap> bitmap = createBitmapFromData(imageData);
//            if (bitmap) {
//                std::vector<uint8_t> processedData = processImage(bitmap.get(), format);
//                callback(processedData, format);
//                return;
//            }
//        }
//
//        // Return the original data if processing failed or not required
//        callback(imageData, format);
//        }).detach();
//}

uint8_t ClipboardImageHandler::getContentType(ClipboardImageFormat format) const {
    return static_cast<uint8_t>(format);
}

std::string ClipboardImageHandler::getFileExtension(ClipboardImageFormat format) const {
    switch (format) {
    case ClipboardImageFormat::JPEG:
        return ".jpg";
    case ClipboardImageFormat::PNG:
        return ".png";
    default:
        return ".bin";
    }
}

std::string ClipboardImageHandler::getMimeType(ClipboardImageFormat format) const {
    switch (format) {
    case ClipboardImageFormat::JPEG:
        return "image/jpeg";
    case ClipboardImageFormat::PNG:
        return "image/png";
    default:
        return "application/octet-stream";
    }
}

std::unique_ptr<Gdiplus::Bitmap> ClipboardImageHandler::getRawClipboardImage() {
    if (!OpenClipboard(NULL)) {
        std::cerr << "Failed to open clipboard" << std::endl;
        return nullptr;
    }

    HBITMAP hBitmap = NULL;

    // Try to get a bitmap handle from the clipboard
    if (IsClipboardFormatAvailable(CF_BITMAP)) {
        hBitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    }
    else if (IsClipboardFormatAvailable(CF_DIB)) {
        HANDLE hDib = GetClipboardData(CF_DIB);
        if (hDib) {
            LPBITMAPINFO lpbi = (LPBITMAPINFO)GlobalLock(hDib);
            if (lpbi) {
                HDC hdc = GetDC(NULL);
                hBitmap = CreateDIBitmap(hdc, &lpbi->bmiHeader, CBM_INIT,
                    (BYTE*)lpbi + lpbi->bmiHeader.biSize + lpbi->bmiHeader.biClrUsed * sizeof(RGBQUAD),
                    lpbi, DIB_RGB_COLORS);
                ReleaseDC(NULL, hdc);
                GlobalUnlock(hDib);
            }
        }
    }
    else if (IsClipboardFormatAvailable(CF_DIBV5)) {
        HANDLE hDib = GetClipboardData(CF_DIBV5);
        if (hDib) {
            LPBITMAPINFO lpbi = (LPBITMAPINFO)GlobalLock(hDib);
            if (lpbi) {
                HDC hdc = GetDC(NULL);
                hBitmap = CreateDIBitmap(hdc, &lpbi->bmiHeader, CBM_INIT,
                    (BYTE*)lpbi + lpbi->bmiHeader.biSize + lpbi->bmiHeader.biClrUsed * sizeof(RGBQUAD),
                    lpbi, DIB_RGB_COLORS);
                ReleaseDC(NULL, hdc);
                GlobalUnlock(hDib);
            }
        }
    }

    CloseClipboard();

    if (!hBitmap) {
        std::cerr << "No bitmap found in clipboard" << std::endl;
        return nullptr;
    }

    // Create a Gdiplus::Bitmap from the HBITMAP
    std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromHBITMAP(hBitmap, NULL));

    // Clean up the HBITMAP as Gdiplus::Bitmap creates a copy
    DeleteObject(hBitmap);

    return bitmap;
}

std::vector<uint8_t> ClipboardImageHandler::convertImageFormat(Gdiplus::Bitmap* image, ClipboardImageFormat format) {
    CLSID encoderClsid;
    if (getCodecForFormat(format, &encoderClsid) < 0) {
        std::cerr << "Failed to get encoder for format" << std::endl;
        return {};
    }

    // For JPEG, use default quality (no compression)
    if (format == ClipboardImageFormat::JPEG) {
        return saveToMemory(image, encoderClsid, 100.0f);
    }
    else {
        return saveToMemory(image, encoderClsid);
    }
}

std::vector<uint8_t> ClipboardImageHandler::processImage(Gdiplus::Bitmap* image, ClipboardImageFormat format) {
    // First resize the image if needed
    std::unique_ptr<Gdiplus::Bitmap> resizedImage = resizeImageIfNeeded(image);

    CLSID encoderClsid;
    if (getCodecForFormat(format, &encoderClsid) < 0) {
        std::cerr << "Failed to get encoder for format" << std::endl;
        return {};
    }

    // For JPEG, apply compression
    if (format == ClipboardImageFormat::JPEG) {
        return saveToMemory(resizedImage.get() ? resizedImage.get() : image, encoderClsid, jpegCompressionQuality * 100.0f);
    }
    else {
        return saveToMemory(resizedImage.get() ? resizedImage.get() : image, encoderClsid);
    }
}

std::unique_ptr<Gdiplus::Bitmap> ClipboardImageHandler::resizeImageIfNeeded(Gdiplus::Bitmap* image) {

    if (!image) return nullptr;

    UINT originalWidth = image->GetWidth();
    UINT originalHeight = image->GetHeight();

    // Check if resize is needed
    if (originalWidth <= maxImageDimension && originalHeight <= maxImageDimension) {
        return nullptr; // No resize needed
    }

    // Calculate new dimensions maintaining aspect ratio
    float newWidth, newHeight;
    if (originalWidth > originalHeight) {
        newWidth = maxImageDimension;
        newHeight = originalHeight * (newWidth / originalWidth);
    }
    else {
        newHeight = maxImageDimension;
        newWidth = originalWidth * (newHeight / originalHeight);
    }

    // Create a new bitmap with the calculated dimensions
    std::unique_ptr<Gdiplus::Bitmap> resizedBitmap(new Gdiplus::Bitmap(
        static_cast<INT>(newWidth),
        static_cast<INT>(newHeight),
        image->GetPixelFormat()));

    // Create graphics object for the new bitmap
    Gdiplus::Graphics graphics(resizedBitmap.get());

    // Set high quality resize mode
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

    // Draw the original image onto the new bitmap with resizing
    graphics.DrawImage(image, 0, 0, static_cast<INT>(newWidth), static_cast<INT>(newHeight));

    return resizedBitmap;
}

std::vector<uint8_t> ClipboardImageHandler::saveToMemory(Gdiplus::Bitmap* image, const CLSID& formatClsid, float quality) {
    if (!image) return {};

    // Create a stream to save the image to
    IStream* istream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &istream);
    if (FAILED(hr)) {
        std::cerr << "Failed to create stream" << std::endl;
        return {};
    }

    Gdiplus::Status status;

    // If quality is specified, set JPEG compression quality
    if (quality > 0.0f) {
        Gdiplus::EncoderParameters encoderParams;
        encoderParams.Count = 1;
        encoderParams.Parameter[0].Guid = Gdiplus::EncoderQuality;
        encoderParams.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
        encoderParams.Parameter[0].NumberOfValues = 1;
        ULONG qualityValue = static_cast<ULONG>(quality);
        encoderParams.Parameter[0].Value = &qualityValue;

        status = image->Save(istream, &formatClsid, &encoderParams);
    }
    else {
        // Save without compression parameters
        status = image->Save(istream, &formatClsid, NULL);
    }

    if (status != Gdiplus::Ok) {
        std::cerr << "Failed to save image to stream: " << status << std::endl;
        istream->Release();
        return {};
    }

    // Get the HGLOBAL from the stream
    HGLOBAL hg = NULL;
    hr = GetHGlobalFromStream(istream, &hg);
    if (FAILED(hr)) {
        std::cerr << "Failed to get HGLOBAL from stream" << std::endl;
        istream->Release();
        return {};
    }

    // Lock the global memory to get a pointer to the data
    SIZE_T size = GlobalSize(hg);
    void* data = GlobalLock(hg);
    if (!data) {
        std::cerr << "Failed to lock global memory" << std::endl;
        istream->Release();
        return {};
    }

    // Copy the data to our vector
    std::vector<uint8_t> result(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);

    // Unlock the global memory
    GlobalUnlock(hg);

    // Release the stream (this also releases the HGLOBAL)
    istream->Release();

    return result;
}

int ClipboardImageHandler::getCodecForFormat(ClipboardImageFormat format, CLSID* pClsid) {
    UINT num = 0;          // number of image encoders
    UINT size = 0;         // size of the image encoder array in bytes

    // Get the number of encoders and the size required
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) {
        return -1;  // No encoders found
    }

    // Allocate memory for the encoder array
    std::vector<Gdiplus::ImageCodecInfo> codecInfo(size);

    // Get the encoder array
    Gdiplus::GetImageEncoders(num, size, codecInfo.data());

    // Find the encoder for the requested format
    std::wstring mimeType;
    switch (format) {
    case ClipboardImageFormat::JPEG:
        mimeType = L"image/jpeg";
        break;
    case ClipboardImageFormat::PNG:
        mimeType = L"image/png";
        break;
    default:
        return -1;
    }

    // Search for the specified encoder in the array
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(codecInfo[i].MimeType, mimeType.c_str()) == 0) {
            *pClsid = codecInfo[i].Clsid;
            return i;
        }
    }

    return -1;  // Encoder not found
}

std::unique_ptr<Gdiplus::Bitmap> ClipboardImageHandler::createBitmapFromData(const std::vector<uint8_t>& data) {
    if (data.empty()) return nullptr;

    // Create a stream from the data
    IStream* stream = SHCreateMemStream(data.data(), static_cast<UINT>(data.size()));
    if (!stream) {
        std::cerr << "Failed to create memory stream" << std::endl;
        return nullptr;
    }

    // Create a bitmap from the stream
    std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromStream(stream));

    // Release the stream
    stream->Release();

    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        std::cerr << "Failed to create bitmap from data" << std::endl;
        return nullptr;
    }

    return bitmap;
}

size_t ClipboardImageHandler::getImageDataHash(const std::vector<uint8_t>& data) {
    return std::hash<std::string>{}(std::string(data.begin(), data.end()));
}

size_t ClipboardImageHandler::getImageHash(Gdiplus::Bitmap* image, ClipboardImageFormat format) {
    if (!image) return 0;

    // Convert to data first
    std::vector<uint8_t> imageData = convertImageFormat(image, format);
    if (imageData.empty()) return 0;

    // Calculate hash
    return getImageDataHash(imageData);
}