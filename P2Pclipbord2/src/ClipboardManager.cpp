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

    // Initialize the last clipboard content
    lastClipboardContent = getClipboardContent();
    std::cout << "Initial clipboard content: " << lastClipboardContent.substr(0, 100)
        << (lastClipboardContent.length() > 100 ? "..." : "") << std::endl;

    return true;
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

    // Update our cached value
    lastClipboardContent = content;
    return true;
}

std::string ClipboardManager::getClipboardContent() {
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

void ClipboardManager::setClipboardUpdateCallback(ClipboardUpdateCallback callback) {
    updateCallback = callback;
}

void ClipboardManager::processRemoteMessage(const std::string& message) {
    std::cout << "Entered processRemoteMessage: " << message << std::endl;
    // Handle clipboard data from client
    bool result = setClipboardContent(message, true);
    std::cout << "SetClipboardContent result: " << (result ? "SUCCESS" : "FAILED") << std::endl;
    /*updateCallback(message);*/
}

bool ClipboardManager::shouldIgnoreNextChange() {
    return ignoreNextChange.load();
}

void ClipboardManager::resetIgnoreFlag() {
    ignoreNextChange.store(false);
}

LRESULT CALLBACK ClipboardManager::ClipboardWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CLIPBOARDUPDATE && instance) {
        std::cout << "Clipboard change detected!" << std::endl;

        //// Check if we should ignore this change
        //if (instance->shouldIgnoreNextChange()) {
        //    std::cout << "Ignoring clipboard change (from remote update)" << std::endl;
        //    instance->resetIgnoreFlag();
        //    return 0;
        //}cupsize

        std::string currentContent = instance->getClipboardContent();
        if (!currentContent.empty() && currentContent != instance->lastClipboardContent) {
            std::cout << "Clipboard content changed. New length: " << currentContent.length() << std::endl;

            // Update our cached content
            instance->lastClipboardContent = currentContent;

            // Notify callback if registered
            if (instance->updateCallback) {
                instance->updateCallback(currentContent);
            }
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}