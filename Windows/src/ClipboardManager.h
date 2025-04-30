// In ClipboardManager.h
#pragma once

#include <windows.h>
#include <string>
#include <mutex>
#include <atomic>
#include <functional>
#include <vector>
#include "ClipboardImageHandler.h"  // Add this include
#include "MessageProtocol.h"        // For MessageContentType

// Updated callback type for clipboard updates
using ClipboardUpdateCallback = std::function<void(const std::vector<uint8_t>&, MessageContentType)>;

class ClipboardManager {
public:
    ClipboardManager();
    ~ClipboardManager();

    // Initialize the clipboard monitoring
    bool initialize();

    // Set clipboard content (with option to specify if it's from remote source)
    bool setClipboardContent(const std::string& content, bool fromRemote = false);

    // Get current clipboard content with content type
    std::pair<std::vector<uint8_t>, MessageContentType> getClipboardContent();

    // Helper method to get just text
    std::string getClipboardText();

    // Set callback for when clipboard content changes
    void setClipboardUpdateCallback(ClipboardUpdateCallback callback);

    // Process incoming message from remote clients
    void processRemoteMessage(const std::vector<uint8_t>& data, MessageContentType contentType);

    // Check if we should ignore the next clipboard change
    bool shouldIgnoreNextChange();

    std::string getContentTypeName(MessageContentType type);

    // Reset the ignore flag after handling a change
    void resetIgnoreFlag();

    // Static window procedure for clipboard messages
    static LRESULT CALLBACK ClipboardWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    // Create hidden window for clipboard monitoring
    bool createHiddenWindow();

    // Window class name
    static constexpr const char* WINDOW_CLASS_NAME = "ClipboardMonitorClass";

    // Window handle
    HWND monitorWindow;

    // Hash for tracking content changes
    size_t lastContentHash = 0;

    // Mutex for thread-safe clipboard operations
    std::mutex clipboardMutex;

    // Flag to ignore clipboard changes when we set content from remote
    std::atomic<bool> ignoreNextChange;

    // Image handler for clipboard image operations
    ClipboardImageHandler imageHandler;

    // Callback for when clipboard content changes
    ClipboardUpdateCallback updateCallback;

    // Static instance for window procedure callback
    static ClipboardManager* instance;
};  