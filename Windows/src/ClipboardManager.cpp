#include "ClipboardManager.h"
#include <iostream>
#include <vector>

// Initialize static instance for window procedure callback
ClipboardManager* ClipboardManager::instance = nullptr;

ClipboardManager::ClipboardManager()
    : monitorWindow(nullptr), ignoreNextChange(false) {
    instance = this;
}

ClipboardManager::~ClipboardManager() {
    if (monitorWindow) {
        RemoveClipboardFormatListener(monitorWindow);
        DestroyWindow(monitorWindow);
    }

    // Unregister window class
    UnregisterClass(WINDOW_CLASS_NAME, GetModuleHandle(NULL));

    instance = nullptr;
}

bool ClipboardManager::initialize() {
    return createHiddenWindow();
}

bool ClipboardManager::createHiddenWindow() {
    // Register window class for clipboard monitoring
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = ClipboardWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassEx(&wc)) {
        std::cerr << "Failed to register window class: " << GetLastError() << std::endl;
        return false;
    }

    // Create hidden window for clipboard monitoring
    monitorWindow = CreateWindowEx(
        0,
        WINDOW_CLASS_NAME,
        "ClipboardMonitor",
        0,
        0, 0, 0, 0,
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (!monitorWindow) {
        std::cerr << "Failed to create window: " << GetLastError() << std::endl;
        return false;
    }

    if (!AddClipboardFormatListener(monitorWindow)) {
        std::cerr << "Failed to add clipboard format listener: " << GetLastError() << std::endl;
        DestroyWindow(monitorWindow);
        monitorWindow = nullptr;
        return false;
    }

    std::cout << "Clipboard monitoring started" << std::endl;

    // Get initial clipboard content for debug purposes
    auto [content, contentType] = getClipboardContent();

    // Just print initial content if it's text
    if (contentType == MessageContentType::PLAIN_TEXT && !content.empty()) {
        std::string textContent(content.begin(), content.end());
        std::cout << "Initial clipboard text: " << textContent.substr(0, 100)
            << (textContent.length() > 100 ? "..." : "") << std::endl;
    }
    else if (!content.empty()) {
        std::cout << "Initial clipboard content: ["
            << getContentTypeName(contentType) << "], "
            << content.size() << " bytes" << std::endl;
    }
    else {
        std::cout << "Clipboard is empty" << std::endl;
    }

    return true;
}

// Helper method to get a string representation of content type
std::string ClipboardManager::getContentTypeName(MessageContentType type) {
    switch (type) {
    case MessageContentType::PLAIN_TEXT: return "Text";
    case MessageContentType::JPEG_IMAGE: return "JPEG Image";
    case MessageContentType::PNG_IMAGE: return "PNG Image";
    case MessageContentType::RTF_TEXT: return "RTF";
    case MessageContentType::HTML_CONTENT: return "HTML";
    case MessageContentType::PDF_DOCUMENT: return "PDF";
    default: return "Unknown";
    }
}

bool ClipboardManager::setClipboardContent(const std::string& content, bool fromRemote) {
    std::lock_guard<std::mutex> lock(clipboardMutex);

    // First convert UTF-8 string to wide string (Unicode)
    int wideLength = MultiByteToWideChar(CP_UTF8, 0, content.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wideStr(wideLength);
    MultiByteToWideChar(CP_UTF8, 0, content.c_str(), -1, wideStr.data(), wideLength);

    if (!OpenClipboard(nullptr)) {
        DWORD error = GetLastError();
        std::cerr << "Failed to open clipboard for writing. Error: " << error << std::endl;
        return false;
    }

    // Empty the clipboard
    if (!EmptyClipboard()) {
        DWORD error = GetLastError();
        std::cerr << "Failed to empty clipboard. Error: " << error << std::endl;
        CloseClipboard();
        return false;
    }

    // Allocate global memory for the wide text
    size_t dataSize = (wideLength) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, dataSize);
    if (hMem == nullptr) {
        DWORD error = GetLastError();
        std::cerr << "Failed to allocate memory for clipboard. Error: " << error << std::endl;
        CloseClipboard();
        return false;
    }

    // Lock the memory and copy the text
    wchar_t* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
    if (pMem == nullptr) {
        DWORD error = GetLastError();
        std::cerr << "Failed to lock memory. Error: " << error << std::endl;
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    wcscpy_s(pMem, wideLength, wideStr.data());
    GlobalUnlock(hMem);

    // Set the clipboard data as Unicode text
    HANDLE result = SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();

    if (result == nullptr) {
        DWORD error = GetLastError();
        std::cerr << "Failed to set clipboard data. Error: " << error << std::endl;
        GlobalFree(hMem);
        return false;
    }

    // Ignore next clipboard update as it's from us
    if (fromRemote) {
        ignoreNextChange.store(true);
    }

    return true;
}

std::string ClipboardManager::getClipboardText() {
    std::lock_guard<std::mutex> lock(clipboardMutex);
    std::string result;

    if (!OpenClipboard(nullptr)) {
        std::cerr << "Failed to open clipboard" << std::endl;
        return result;
    }

    // First try CF_UNICODETEXT (preferred for all languages)
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData != nullptr) {
        WCHAR* pwszText = static_cast<WCHAR*>(GlobalLock(hData));
        if (pwszText != nullptr) {
            // Convert from wide string to UTF-8 string
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, pwszText, -1, NULL, 0, NULL, NULL);
            if (size_needed > 0) {
                std::vector<char> utf8Text(size_needed);
                WideCharToMultiByte(CP_UTF8, 0, pwszText, -1, utf8Text.data(), size_needed, NULL, NULL);
                result = utf8Text.data();
            }
            GlobalUnlock(hData);
        }
    }
    // Fallback to CF_TEXT only if Unicode failed
    else {
        hData = GetClipboardData(CF_TEXT);
        if (hData != nullptr) {
            char* pszText = static_cast<char*>(GlobalLock(hData));
            if (pszText != nullptr) {
                result = pszText;
                GlobalUnlock(hData);
            }
        }
    }

    CloseClipboard();
    return result;
}

std::pair<std::vector<uint8_t>, MessageContentType> ClipboardManager::getClipboardContent() {
    std::cout << "entered" << std::endl;
    // Check for images first
    if (imageHandler.hasImage()) {
        auto result = imageHandler.getImageFromClipboard();
        if (result.success) {
            return { result.data, MessageContentType::JPEG_IMAGE };
        }
    }
    // Then check for text
    std::string text = getClipboardText();
    if (!text.empty()) {
        std::vector<uint8_t> data(text.begin(), text.end());
        return { data, MessageContentType::PLAIN_TEXT };
    }

    return { {}, MessageContentType::PLAIN_TEXT };
}

void ClipboardManager::setClipboardUpdateCallback(ClipboardUpdateCallback callback) {
    updateCallback = callback;
}

// Updated method to process remote messages
void ClipboardManager::processRemoteMessage(const std::vector<uint8_t>& data, MessageContentType contentType) {
    std::cout << "Processing remote message with content type: " << static_cast<int>(contentType) << std::endl;

    switch (contentType) {
    case MessageContentType::PLAIN_TEXT: {
        // Convert binary payload to string
        std::string textContent(data.begin(), data.end());
        setClipboardContent(textContent, true);
        break;
    }

    case MessageContentType::JPEG_IMAGE:
    case MessageContentType::PNG_IMAGE: {
        // For images, use ClipboardImageHandler
        ClipboardImageFormat format = (contentType == MessageContentType::JPEG_IMAGE) ?
            ClipboardImageFormat::JPEG : ClipboardImageFormat::PNG;

        bool result = imageHandler.setClipboardImage(data, format);
        std::cout << "Set clipboard image result: " << (result ? "SUCCESS" : "FAILED") << std::endl;

        // Mark that we should ignore the next clipboard update
        if (result) {
            ignoreNextChange.store(true);
            lastContentHash = std::hash<std::string>{}(std::string(data.begin(), data.end()));
        }
        break;
    }

    default:
        std::cerr << "Unsupported content type: " << static_cast<int>(contentType) << std::endl;
        break;
    }
}

bool ClipboardManager::shouldIgnoreNextChange() {
    return ignoreNextChange.load();
}

void ClipboardManager::resetIgnoreFlag() {
    ignoreNextChange.store(false);
}

// Update the window procedure for content type handling
LRESULT CALLBACK ClipboardManager::ClipboardWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CLIPBOARDUPDATE && instance) {
        std::cout << "Clipboard change detected!" << std::endl;

        if (instance->shouldIgnoreNextChange()) {
            std::cout << "Ignoring clipboard change (from remote update)" << std::endl;
            instance->resetIgnoreFlag();
            return 0;
        }
        auto [data, contentType] = instance->getClipboardContent();
        if (!data.empty()) {
            // Calculate a hash for change detection
            size_t contentHash = std::hash<std::string>{}(std::string(data.begin(), data.end()));

            if (contentHash != instance->lastContentHash) {
                std::cout << "Clipboard content changed. Type: " << static_cast<int>(contentType)
                    << ", Size: " << data.size() << " bytes" << std::endl;

                // Update our cached hash
                instance->lastContentHash = contentHash;

                // Notify callback if registeredNew length
                if (instance->updateCallback) {
                    instance->updateCallback(data, contentType);
                }
            }
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}