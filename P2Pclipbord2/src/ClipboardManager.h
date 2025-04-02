#pragma once

// Windows headers - order matters!
#include <winsock2.h>   // Must come before windows.h even if not directly used
#include <windows.h>
#include <string>
#include <mutex>
#include <atomic>
#include <functional>


// Callback type for clipboard updates
using ClipboardUpdateCallback = std::function<void(const std::string&)>;

class ClipboardManager {
public:
    ClipboardManager();
    ~ClipboardManager();

    // Initialize the clipboard monitoring
    bool initialize();

    // Set clipboard content (with option to specify if it's from remote source)
    bool setClipboardContent(const std::string& content, bool fromRemote = false);

    // Get current clipboard content
    std::string getClipboardContent();

    // Set callback for when clipboard content changes
    void setClipboardUpdateCallback(ClipboardUpdateCallback callback);

    // Process incoming message from remote clients
    void processRemoteMessage(const std::string& message);

    // Check if we should ignore the next clipboard change
    bool shouldIgnoreNextChange();

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

    // Last known clipboard content
    std::string lastClipboardContent;

    // Mutex for thread-safe clipboard operations
    std::mutex clipboardMutex;

    // Flag to ignore clipboard changes when we set content from remote
    std::atomic<bool> ignoreNextChange;

    // Callback for when clipboard content changes
    ClipboardUpdateCallback updateCallback;

    // Static instance for window procedure callback
    static ClipboardManager* instance;
};