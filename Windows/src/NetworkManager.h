#pragma once

// Windows networking headers - order matters!
#include <winsock2.h>   // Must come BEFORE windows.h
#include <ws2tcpip.h>
#include <windows.h>    // Windows definitions

// Standard library
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <functional>

// DNS-SD header
#include <dns_sd.h>

// Our message protocol
#include "MessageProtocol.h"

// Callback for receiving messages with content type
using MessageReceivedCallback = std::function<void(MessageContentType, const std::vector<uint8_t>&)>;
// Callback for client connection status
using ClientStatusCallback = std::function<void(const std::string&, bool)>;

class NetworkManager {
public:
    NetworkManager(const std::string& serviceName, const std::string& serviceType, int port);
    ~NetworkManager();

    // Initialize network services
    bool initialize();

    // Start the network services
    bool start();

    // Stop the network services
    void stop();

    // Send message to all connected clients
    bool broadcastMessage(MessageContentType contentType, const std::vector<uint8_t>& data);

    // Helper for text messages
    bool broadcastTextMessage(const std::string& text);

    // Send message to a specific client
    bool sendMessageToClient(SOCKET clientSocket, MessageContentType contentType, const std::vector<uint8_t>& data);

    // Helper for text messages to a specific client
    bool sendTextToClient(SOCKET clientSocket, const std::string& text);

    // Set callback for when a message is received
    void setMessageReceivedCallback(MessageReceivedCallback callback);

    // Set callback for client connection status changes
    void setClientStatusCallback(ClientStatusCallback callback);

private:
    // Register the DNS-SD service
    bool registerDNSSDService();

    // DNS-SD service callback
    static void DNSSD_API registerCallback(DNSServiceRef service,
        DNSServiceFlags flags,
        DNSServiceErrorType errorCode,
        const char* name,
        const char* type,
        const char* domain,
        void* context);

    // Create and set up the TCP server socket
    bool createServerSocket();

    // Thread function for DNS-SD processing
    void dnsServiceThreadFunc();

    // Thread function for handling client connections
    void acceptClientThreadFunc();

    // Thread function for handling a specific client
    void handleClient(SOCKET clientSocket, const std::string& clientAddress);

    // Service configuration
    std::string serviceName;
    std::string serviceType;
    int servicePort;

    // DNS-SD service reference
    DNSServiceRef serviceRef;

    // Server socket
    SOCKET serverSocket;

    // List of connected client sockets
    std::vector<SOCKET> clientSockets;
    std::mutex clientSocketsMutex;

    // Threads
    std::thread dnsServiceThread;
    std::thread acceptThread;
    std::vector<std::thread> clientThreads;

    // Control flags
    bool running;

    // Callbacks
    MessageReceivedCallback messageCallback;
    ClientStatusCallback clientStatusCallback;
};