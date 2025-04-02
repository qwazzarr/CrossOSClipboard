// Windows headers - order matters!
#include <winsock2.h>  // Must come before windows.h
#include <windows.h>

// Application headers
#include "ClipboardManager.h"
#include "NetworkManager.h"
#include "BLEManager.h"
#include "MessageProtocol.h"

// Standard library
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <conio.h>  // For _kbhit() and _getch()

// Forward declarations for message handlers
void handleMessageReceived(MessageContentType contentType, const std::vector<uint8_t>& data);
void handleClipboardUpdate(const std::string& content);
void handleClientStatusChange(const std::string& clientAddress, bool connected);
void handleBLEConnectionChange(const std::string& deviceId, bool connected);
void handleBLEDataReceived(const std::string& data);

// Global managers
ClipboardManager* clipboardManager = nullptr;
NetworkManager* networkManager = nullptr;
BLEManager* bleManager = nullptr;

// Flag to indicate if we're currently processing a remote update
bool processingRemoteUpdate = false;


int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "=== Clipboard Sync Service ===\n" << std::endl;

    // Create managers
    clipboardManager = new ClipboardManager();
    bleManager = new BLEManager("ClipboardSyncBLE");
    networkManager = new NetworkManager("clipboardDevice12345", "_clipboard._tcp", 8080);

    // Set up callbacks
    clipboardManager->setClipboardUpdateCallback(handleClipboardUpdate);
    networkManager->setMessageReceivedCallback(handleMessageReceived);
    networkManager->setClientStatusCallback(handleClientStatusChange);
    bleManager->setConnectionCallback(handleBLEConnectionChange);
    bleManager->setDataReceivedCallback(handleBLEDataReceived);

    // Initialize components
    if (!clipboardManager->initialize()) {
        std::cerr << "Failed to initialize clipboard manager" << std::endl;
        delete clipboardManager;
        delete networkManager;
        delete bleManager;
        return 1;
    }

    if (!networkManager->initialize()) {
        std::cerr << "Failed to initialize network manager" << std::endl;
        delete clipboardManager;
        delete networkManager;
        delete bleManager;
        return 1;
    }

    // Start network services
    if (!networkManager->start()) {
        std::cerr << "Failed to start network services" << std::endl;
        delete clipboardManager;
        delete networkManager;
        delete bleManager;
        return 1;
    }

    if (!bleManager->initialize()) {
        std::cerr << "Failed to initialize BLE manager" << std::endl;
        // Continue anyway, as we can still use DNS-SD
    }
    else {
        // Start advertising only - we're just a peripheral
        bleManager->startAdvertising();
    }

    std::cout << "Clipboard Sync Service running. Press Enter to exit." << std::endl;

    // Message loop for the main thread
    MSG msg;
    bool running = true;

    while (running) {
        // Process Windows messages
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
        }

        // Check for user input to exit
        if (_kbhit()) {
            int ch = _getch();
            if (ch == '\r' || ch == '\n') {
                std::cout << "Exiting..." << std::endl;
                running = false;
            }
        }

        // Sleep to prevent high CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Cleanup
    networkManager->stop();
    bleManager->stopAdvertising();

    delete clipboardManager;
    delete networkManager;
    delete bleManager;

    return 0;
}

// Handler for message data received from the network
void handleMessageReceived(MessageContentType contentType, const std::vector<uint8_t>& data) {
    // Set flag to indicate we're processing a remote update
    processingRemoteUpdate = true;

    switch (contentType) {
    case MessageContentType::PLAIN_TEXT: {
        // Convert binary data to string
        std::string message(data.begin(), data.end());
        std::cout << "Received text data from network, length: " << message.length() << std::endl;

        // Process using clipboard manager
        clipboardManager->processRemoteMessage(message);
        break;
    }

    case MessageContentType::RTF_TEXT:
    case MessageContentType::HTML_CONTENT: {
        // These could be handled by the clipboard manager if it supports rich text
        std::string formattedText(data.begin(), data.end());
        std::cout << "Received formatted text data, length: " << formattedText.length() << std::endl;
        // Would need to add support in ClipboardManager for these formats
        break;
    }

    case MessageContentType::PNG_IMAGE:
    case MessageContentType::JPEG_IMAGE:
    case MessageContentType::PDF_DOCUMENT: {
        // Handle binary formats - future implementation
        std::cout << "Received binary data of type " << static_cast<int>(contentType)
            << ", size: " << data.size() << " bytes" << std::endl;
        // Would need to add support in ClipboardManager for these formats
        break;
    }

    default:
        std::cout << "Received unknown content type: " << static_cast<int>(contentType) << std::endl;
        break;
    }

    // Reset the flag
    processingRemoteUpdate = false;
}

// Handler for client connection status changes
void handleClientStatusChange(const std::string& clientAddress, bool connected) {
    if (connected) {
        std::cout << "Client connected: " << clientAddress << std::endl;

        // Optionally send current clipboard content to new client
        std::string clipboardContent = clipboardManager->getClipboardContent();
        if (!clipboardContent.empty()) {
            // Send the current clipboard content to the newly connected client
            std::string message = clipboardContent;
            networkManager->broadcastTextMessage(message);
        }
    }
    else {
        std::cout << "Client disconnected: " << clientAddress << std::endl;
    }
}

// Handler for BLE connection changes
void handleBLEConnectionChange(const std::string& deviceId, bool connected) {
    // When a device connects via BLE, send the current clipboard content
    if (connected) {
        std::cout << "BLE device connected: " << deviceId << std::endl;

        // Send the current clipboard content via BLE characteristic
        std::string clipboardContent = clipboardManager->getClipboardContent();
        if (!clipboardContent.empty()) {
            bleManager->sendClipboardData(clipboardContent);
        }
    }
    else {
        std::cout << "BLE device disconnected: " << deviceId << std::endl;
    }
}

// Handler for data received via BLE GATT
void handleBLEDataReceived(const std::string& data) {
    std::cout << "Received data via BLE GATT, length: " << data.length() << std::endl;

    // Set flag to indicate we're processing a remote update
    processingRemoteUpdate = true;

    // Process the clipboard data
    clipboardManager->processRemoteMessage(data);

    // Reset the flag
    processingRemoteUpdate = false;
}

// Handler for clipboard updates
void handleClipboardUpdate(const std::string& content) {
    // Local clipboard changed, synchronize
    std::cout << "Local clipboard changed, synchronizing..." << std::endl;

    // First, send a BLE notification to wake up clients and wait for their response
    if (bleManager) {
        auto response = bleManager->sendWakeupAndWaitForResponse(2000); // Wait up to 1 second for response

        if (response == BLEManager::ClientResponseType::USE_BLE) {
            // Client wants to use BLE for data transfer
            std::cout << "Client requested BLE transfer, sending clipboard data via BLE..." << std::endl;
            bool dataSent = bleManager->sendClipboardData(content);
            std::cout << "BLE clipboard data sent: " << (dataSent ? "success" : "failed") << std::endl;
        }
        else if (response == BLEManager::ClientResponseType::USE_TCP) {
            // Client will initiate TCP connection, nothing to do here
            std::cout << "Client requested TCP transfer, waiting for TCP connection..." << std::endl;
        }
        else {
            // No response or unable to determine preference, use TCP as fallback
            std::cout << "No client response or timeout, using TCP as fallback..." << std::endl;
        }
    }

    // Then, send the clipboard content via TCP to any connected clients
    if (networkManager) {
        // Prepare the message with content
        std::string message = content;

        // Broadcast to all connected clients
        bool broadcastSuccess = networkManager->broadcastTextMessage(message);
        std::cout << "TCP broadcast: " << (broadcastSuccess ? "success" : "failed") << std::endl;
    }
    else {
        std::cerr << "Network manager not available" << std::endl;
    }
}