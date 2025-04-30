// Windows headers - order matters!
#include <winsock2.h>  // Must come before windows.h
#include <windows.h>

// Application headers
#include "ClipboardManager.h"
#include "NetworkManager.h"
#include "BLEManager.h"
#include "MessageProtocol.h"
#include "ClipboardEncryption.h"
#include "UUIDGenerator.h"

// Standard library
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <conio.h>  // For _kbhit() and _getch()
#include <fstream>  // For file operations with credentials
#include <random>   // For generating keys

// Forward declarations for message handlers
void handleMessageReceived(MessageContentType contentType, const std::vector<uint8_t>& data);
void handleClipboardUpdate(const std::vector<uint8_t>& content, MessageContentType contentType);
void handleClientStatusChange(const std::string& clientAddress, bool connected);
void handleBLEConnectionChange(const std::string& deviceId, bool connected);
void handleBLEDataReceived(const std::vector<uint8_t>& data, MessageContentType contentType);

// Forward declarations for authentication functions
bool loadCredentials(std::string& userName, std::string& syncPassword);
bool saveCredentials(const std::string& userName, const std::string& syncPassword);
std::string generateKey();
bool showAuthenticationPrompt(std::string& userName, std::string& syncPassword);

// Global managers
ClipboardManager* clipboardManager = nullptr;
NetworkManager* networkManager = nullptr;
BLEManager* bleManager = nullptr;

// Flag to indicate if we're currently processing a remote update
bool processingRemoteUpdate = false;

// Constants for authentication
const std::string CREDENTIALS_FILE = "clipboard_sync_credentials.dat";

int main() {
    try {
        SetConsoleOutputCP(CP_UTF8);
        std::cout << "=== Clipboard Sync Service ===\n" << std::endl;

        // Load or request authentication
        std::string userName, syncPassword;
        bool hasCredentials = loadCredentials(userName, syncPassword);

        if (!hasCredentials) {
            try {
                bool authSuccess = showAuthenticationPrompt(userName, syncPassword);
                if (!authSuccess) {
                    std::cerr << "Authentication cancelled by user" << std::endl;
                    return 1;
                }

                // Save credentials for future use
                if (!saveCredentials(userName, syncPassword)) {
                    std::cerr << "Warning: Failed to save credentials" << std::endl;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Error during authentication: " << e.what() << std::endl;
                return 1;
            }
            catch (...) {
                std::cerr << "Unknown error during authentication" << std::endl;
                return 1;
            }
        }

        BLEManager::setServiceUUID(userName);
        ClipboardEncryption::setPassword(syncPassword);
        std::cout << "Authenticated as: " << userName << std::endl;

        try {
            // Create managers
            clipboardManager = new ClipboardManager();
            // Use username in service name to identify this device
            bleManager = new BLEManager("ClipboardSync-" + userName);
            networkManager = new NetworkManager("ClipboardSync-" + userName, "_clipboard._tcp", 8080);

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
                // Set the UUID now that the BLE manager is initialized
                std::cerr << "Failed to initialize BLE manager" << std::endl;
                // Continue anyway, as we can still use DNS-SD
            }
            else {
                // Start advertising only - we're just a peripheral
                bleManager->setServiceUUID(userName);
                bleManager->startAdvertising();
            }

            std::cout << "Clipboard Sync Service running as: " << userName << "\nPress Enter to exit." << std::endl;

            // Message loop for the main thread
            MSG msg;
            bool running = true;

            while (running) {
                try {
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
                catch (const std::exception& e) {
                    std::cerr << "Exception in main loop: " << e.what() << std::endl;
                    // Continue running despite exceptions in the main loop
                }
                catch (...) {
                    std::cerr << "Unknown exception in main loop" << std::endl;
                    // Continue running despite exceptions in the main loop
                }
            }

            // Cleanup
            try {
                networkManager->stop();
                bleManager->stopAdvertising();
            }
            catch (const std::exception& e) {
                std::cerr << "Exception during cleanup: " << e.what() << std::endl;
            }
            catch (...) {
                std::cerr << "Unknown exception during cleanup" << std::endl;
            }

            delete clipboardManager;
            delete networkManager;
            delete bleManager;
        }
        catch (const std::exception& e) {
            std::cerr << "Exception in main application: " << e.what() << std::endl;

            // Cleanup if necessary
            delete clipboardManager;
            delete networkManager;
            delete bleManager;

            return 1;
        }
        catch (...) {
            std::cerr << "Unknown exception in main application" << std::endl;

            // Cleanup if necessary
            delete clipboardManager;
            delete networkManager;
            delete bleManager;

            return 1;
        }

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Critical exception in main: " << e.what() << std::endl;
        return 1;
    }
    catch (...) {
        std::cerr << "Unknown critical exception in main" << std::endl;
        return 1;
    }
}

// Handler for message data received from the network
void handleMessageReceived(MessageContentType contentType, const std::vector<uint8_t>& data) {
    try {
        // Set flag to indicate we're processing a remote update
        processingRemoteUpdate = true;

        std::cout << "Received data from network, type: " << static_cast<int>(contentType)
            << ", size: " << data.size() << " bytes" << std::endl;

        // Process using clipboard manager - handles both text and binary data
        clipboardManager->processRemoteMessage(data, contentType);

        // Reset the flag
        processingRemoteUpdate = false;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in handleMessageReceived: " << e.what() << std::endl;
        processingRemoteUpdate = false;
    }
    catch (...) {
        std::cerr << "Unknown exception in handleMessageReceived" << std::endl;
        processingRemoteUpdate = false;
    }
}

// Handler for client connection status changes
void handleClientStatusChange(const std::string& clientAddress, bool connected) {
    try {
        if (connected) {
            std::cout << "Client connected: " << clientAddress << std::endl;

            // Optionally send current clipboard content to new client
            auto [content, contentType] = clipboardManager->getClipboardContent();
            if (!content.empty()) {
                // Send the current clipboard content to the newly connected client
                networkManager->broadcastMessage(contentType, content);
            }
        }
        else {
            std::cout << "Client disconnected: " << clientAddress << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in handleClientStatusChange: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "Unknown exception in handleClientStatusChange" << std::endl;
    }
}

// Handler for BLE connection changes
void handleBLEConnectionChange(const std::string& deviceId, bool connected) {
    try {
        // When a device connects via BLE, send the current clipboard content
        if (connected) {
            std::cout << "BLE device connected: " << deviceId << std::endl;

            // Send the current clipboard content via BLE characteristic
            auto [content, contentType] = clipboardManager->getClipboardContent();
            if (!content.empty()) {
                bleManager->sendMessage(content, contentType);
            }
        }
        else {
            std::cout << "BLE device disconnected: " << deviceId << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in handleBLEConnectionChange: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "Unknown exception in handleBLEConnectionChange" << std::endl;
    }
}

// Handler for data received via BLE GATT
void handleBLEDataReceived(const std::vector<uint8_t>& data, MessageContentType contentType) {
    try {
        std::cout << "Received data via BLE GATT, type: " << static_cast<int>(contentType)
            << ", size: " << data.size() << " bytes" << std::endl;

        // Set flag to indicate we're processing a remote update
        processingRemoteUpdate = true;

        // Process the clipboard data
        clipboardManager->processRemoteMessage(data, contentType);

        // Reset the flag
        processingRemoteUpdate = false;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in handleBLEDataReceived: " << e.what() << std::endl;
        processingRemoteUpdate = false;
    }
    catch (...) {
        std::cerr << "Unknown exception in handleBLEDataReceived" << std::endl;
        processingRemoteUpdate = false;
    }
}

// Handler for clipboard updates
void handleClipboardUpdate(const std::vector<uint8_t>& content, MessageContentType contentType) {
    try {
        // Local clipboard changed, synchronize
        std::string contentTypeStr;
        switch (contentType) {
        case MessageContentType::PLAIN_TEXT: contentTypeStr = "Text"; break;
        case MessageContentType::JPEG_IMAGE: contentTypeStr = "JPEG Image"; break;
        case MessageContentType::PNG_IMAGE: contentTypeStr = "PNG Image"; break;
        default: contentTypeStr = "Unknown"; break;
        }

        std::cout << "Local clipboard changed: " << contentTypeStr
            << " (" << content.size() << " bytes), synchronizing..." << std::endl;

        // First, send a BLE notification to wake up clients and wait for their response
        if (bleManager) {
            auto response = bleManager->sendWakeupAndWaitForResponse(2000); // Wait up to 2 seconds for response

            if (response == BLEManager::ClientResponseType::USE_BLE) {
                // Client wants to use BLE for data transfer
                std::cout << "Client requested BLE transfer" << std::endl;
                bool dataSent = bleManager->sendMessage(content, contentType);
                std::cout << "BLE data sent: " << (dataSent ? "success" : "failed") << std::endl;
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
            // Broadcast to all connected clients
            bool broadcastSuccess = networkManager->broadcastMessage(contentType, content);
            std::cout << "TCP broadcast: " << (broadcastSuccess ? "success" : "failed") << std::endl;
        }
        else {
            std::cerr << "Network manager not available" << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in handleClipboardUpdate: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "Unknown exception in handleClipboardUpdate" << std::endl;
    }
}

// Authentication functions

bool loadCredentials(std::string& userName, std::string& syncPassword) {
    try {
        std::ifstream file(CREDENTIALS_FILE);
        if (!file.is_open()) {
            return false;
        }

        std::getline(file, userName);
        std::getline(file, syncPassword);

        return !userName.empty() && !syncPassword.empty();
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading credentials: " << e.what() << std::endl;
        return false;
    }
}

bool saveCredentials(const std::string& userName, const std::string& syncPassword) {
    try {
        std::ofstream file(CREDENTIALS_FILE);
        if (!file.is_open()) {
            return false;
        }

        file << userName << std::endl;
        file << syncPassword << std::endl;

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error saving credentials: " << e.what() << std::endl;
        return false;
    }
}

std::string generateKey() {
    const std::string charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, charset.size() - 1);

    std::string key;

    // Generate a format like XXXX-XXXX-XXXX
    for (int segment = 0; segment < 3; segment++) {
        for (int i = 0; i < 4; i++) {
            key += charset[dist(gen)];
        }
        if (segment < 2) {
            key += "-";
        }
    }

    return key;
}

bool showAuthenticationPrompt(std::string& userName, std::string& syncPassword) {
    std::cout << "=== Clipboard Sync Authentication ===" << std::endl;
    std::cout << "1. Generate a new key" << std::endl;
    std::cout << "2. Connect using an existing key" << std::endl;
    std::cout << "3. Cancel" << std::endl;
    std::cout << "Choose an option (1-3): ";

    int option = 0;
    std::cin >> option;
    std::cin.ignore(); // Clear the newline

    if (option == 1) {
        // Generate key
        std::string generatedKey = generateKey();
        std::cout << "\nGenerated Key: " << generatedKey << std::endl;
        std::cout << "Copy this key to other devices to connect.\n" << std::endl;

        userName = generatedKey;

        std::cout << "Enter a password to secure your connections: ";
        std::getline(std::cin, syncPassword);

        while (syncPassword.empty()) {
            std::cout << "Password cannot be empty. Please enter a password: ";
            std::getline(std::cin, syncPassword);
        }

        // Set the encryption key


        return true;
    }
    else if (option == 2) {
        // Connect using existing key
        std::cout << "\nEnter the key from another device: ";
        std::getline(std::cin, userName);

        while (userName.empty()) {
            std::cout << "Key cannot be empty. Please enter a key: ";
            std::getline(std::cin, userName);
        }

        std::cout << "Enter the same password used on other devices: ";
        std::getline(std::cin, syncPassword);

        while (syncPassword.empty()) {
            std::cout << "Password cannot be empty. Please enter a password: ";
            std::getline(std::cin, syncPassword);
        }

        // Set the encryption key


        return true;
    }
    else if (option == 3) {
        std::cout << "Authentication cancelled." << std::endl;
        return false;
    }
    else {
        std::cout << "Invalid option. Authentication cancelled." << std::endl;
        return false;
    }
}