#include "NetworkManager.h"
#include "MessageProtocol.h"
#include <iostream>
#include <algorithm>

NetworkManager::NetworkManager(const std::string& serviceName, const std::string& serviceType, int port)
    : serviceName(serviceName), serviceType(serviceType), servicePort(port),
    serviceRef(nullptr), serverSocket(INVALID_SOCKET), running(false) {
}

NetworkManager::~NetworkManager() {
    stop();
}

bool NetworkManager::initialize() {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    return true;
}

bool NetworkManager::start() {
    if (running) {
        return true; // Already running
    }

    // Register the DNS-SD service
    if (!registerDNSSDService()) {
        return false;
    }

    // Create and set up the TCP server socket
    if (!createServerSocket()) {
        stop(); // Clean up DNS-SD
        return false;
    }

    // Start the DNS service thread
    running = true;
    dnsServiceThread = std::thread(&NetworkManager::dnsServiceThreadFunc, this);

    // Start the client accept thread
    acceptThread = std::thread(&NetworkManager::acceptClientThreadFunc, this);

    std::cout << "Network services started successfully" << std::endl;
    return true;
}

void NetworkManager::stop() {
    running = false;

    // Close all client sockets
    {
        std::lock_guard<std::mutex> lock(clientSocketsMutex);
        for (SOCKET socket : clientSockets) {
            closesocket(socket);
        }
        clientSockets.clear();
    }

    // Close server socket
    if (serverSocket != INVALID_SOCKET) {
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
    }

    // Clean up DNS-SD
    if (serviceRef) {
        DNSServiceRefDeallocate(serviceRef);
        serviceRef = nullptr;
    }

    // Join threads
    if (dnsServiceThread.joinable()) {
        dnsServiceThread.join();
    }

    if (acceptThread.joinable()) {
        acceptThread.join();
    }

    // Client threads are detached, so no need to join them

    // Clean up Winsock
    WSACleanup();

    std::cout << "Network services stopped" << std::endl;
}

bool NetworkManager::broadcastMessage(MessageContentType contentType, const std::vector<uint8_t>& data) {
    // Encode the message using MessageProtocol
    std::vector<std::vector<uint8_t>> encodedChunks =
        MessageProtocol::encodeMessage(contentType, data, TransportType::TCP);

    if (encodedChunks.empty()) {
        std::cerr << "Failed to encode message" << std::endl;
        return false;
    }

    const std::vector<uint8_t>& encodedMessage = encodedChunks[0];

    std::cout << "Broadcasting message of type " << static_cast<int>(contentType)
        << " with " << data.size() << " bytes of data" << std::endl;

    std::lock_guard<std::mutex> lock(clientSocketsMutex);
    std::vector<SOCKET> disconnectedClients;
    bool success = true;

    for (SOCKET clientSocket : clientSockets) {
        int bytesSent = send(clientSocket, reinterpret_cast<const char*>(encodedMessage.data()),
            static_cast<int>(encodedMessage.size()), 0);
        if (bytesSent == SOCKET_ERROR) {
            std::cerr << "Failed to send to a client: " << WSAGetLastError() << std::endl;
            disconnectedClients.push_back(clientSocket);
            success = false;
        }
        else {
            std::cout << "Sent " << bytesSent << " bytes to client" << std::endl;
        }
    }

    // Remove disconnected clients
    for (SOCKET socket : disconnectedClients) {
        clientSockets.erase(std::remove(clientSockets.begin(), clientSockets.end(), socket),
            clientSockets.end());
        closesocket(socket);
    }

    return success || clientSockets.empty(); // Success if we sent to all clients or had none
}

bool NetworkManager::broadcastTextMessage(const std::string& text) {
    std::vector<uint8_t> textData(text.begin(), text.end());
    return broadcastMessage(MessageContentType::PLAIN_TEXT, textData);
}

bool NetworkManager::sendMessageToClient(SOCKET clientSocket, MessageContentType contentType, const std::vector<uint8_t>& data) {
    // Encode the message using MessageProtocol
    auto encodedChunks = MessageProtocol::encodeMessage(contentType, data, TransportType::TCP);

    if (encodedChunks.empty()) {
        std::cerr << "Failed to encode message for client" << std::endl;
        return false;
    }

    const std::vector<uint8_t>& encodedMessage = encodedChunks[0];

    int bytesSent = send(clientSocket, reinterpret_cast<const char*>(encodedMessage.data()),
        static_cast<int>(encodedMessage.size()), 0);
    if (bytesSent == SOCKET_ERROR) {
        std::cerr << "Failed to send to client: " << WSAGetLastError() << std::endl;
        return false;
    }

    std::cout << "Sent " << bytesSent << " bytes to client" << std::endl;
    return true;
}

bool NetworkManager::sendTextToClient(SOCKET clientSocket, const std::string& text) {
    std::vector<uint8_t> textData(text.begin(), text.end());
    return sendMessageToClient(clientSocket, MessageContentType::PLAIN_TEXT, textData);
}

void NetworkManager::setMessageReceivedCallback(MessageReceivedCallback callback) {
    messageCallback = callback;
}

void NetworkManager::setClientStatusCallback(ClientStatusCallback callback) {
    clientStatusCallback = callback;
}

bool NetworkManager::registerDNSSDService() {
    std::cout << "Starting DNS-SD service advertisement..." << std::endl;

    DNSServiceErrorType err = DNSServiceRegister(&serviceRef,
        0,
        kDNSServiceInterfaceIndexAny,
        serviceName.c_str(),
        serviceType.c_str(),
        "local",
        nullptr,
        htons(servicePort),
        0,
        nullptr,
        registerCallback,
        this);

    if (err != kDNSServiceErr_NoError) {
        std::cerr << "DNSServiceRegister failed: " << err << std::endl;
        return false;
    }

    return true;
}

void DNSSD_API NetworkManager::registerCallback(DNSServiceRef service,
    DNSServiceFlags flags,
    DNSServiceErrorType errorCode,
    const char* name,
    const char* type,
    const char* domain,
    void* context) {
    if (errorCode == kDNSServiceErr_NoError) {
        std::cout << "\n=== Service Registration Success ===" << std::endl;
        std::cout << "Service Name: " << name << std::endl;
        std::cout << "Type: " << type << std::endl;
        std::cout << "Domain: " << domain << std::endl;
    }
    else {
        std::cerr << "Registration callback error: " << errorCode << std::endl;
    }
}

bool NetworkManager::createServerSocket() {
    // Create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create server socket: " << WSAGetLastError() << std::endl;
        return false;
    }

    // Set socket options for reuse
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
        std::cerr << "setsockopt failed: " << WSAGetLastError() << std::endl;
    }

    // Bind socket
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(servicePort);

    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
        return false;
    }

    // Start listening
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
        return false;
    }

    std::cout << "Server listening on port " << servicePort << "..." << std::endl;
    return true;
}

void NetworkManager::dnsServiceThreadFunc() {
    std::cout << "DNS-SD service thread started" << std::endl;

    while (running && serviceRef) {
        DNSServiceErrorType err = DNSServiceProcessResult(serviceRef);
        if (err != kDNSServiceErr_NoError) {
            std::cerr << "DNSServiceProcessResult error: " << err << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "DNS-SD service thread exiting" << std::endl;
}

void NetworkManager::acceptClientThreadFunc() {
    std::cout << "Accept client thread started" << std::endl;

    fd_set readSet;
    timeval timeout{ 0, 100000 }; // 100ms timeout

    while (running && serverSocket != INVALID_SOCKET) {
        // Check for new connections (non-blocking with select)
        FD_ZERO(&readSet);
        FD_SET(serverSocket, &readSet);

        int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
        if (selectResult > 0) {
            sockaddr_in clientAddr{};
            int clientAddrLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);

            if (clientSocket != INVALID_SOCKET) {
                // Get client IP address
                char clientIP[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
                std::string clientAddress = std::string(clientIP) + ":" + std::to_string(ntohs(clientAddr.sin_port));
                std::cout << "Client connected from: " << clientAddress << std::endl;

                // Add to client list
                {
                    std::lock_guard<std::mutex> lock(clientSocketsMutex);
                    clientSockets.push_back(clientSocket);
                }

                // Notify of client connection
                if (clientStatusCallback) {
                    clientStatusCallback(clientAddress, true);
                }

                // Start client handling thread
                std::thread clientThread(&NetworkManager::handleClient, this, clientSocket, clientAddress);
                clientThread.detach();
            }
        }
        else if (selectResult == SOCKET_ERROR) {
            std::cerr << "Select failed: " << WSAGetLastError() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::cout << "Accept client thread exiting" << std::endl;
}

void NetworkManager::handleClient(SOCKET clientSocket, const std::string& clientAddress) {
    std::cout << "Client handler thread started for " << clientAddress << std::endl;

    // Buffer for receiving data (increased to 10MB for larger transfers)
    const int BUFFER_SIZE = 10 * 1024 * 1024;
    std::vector<uint8_t> receiveBuffer(BUFFER_SIZE);
    std::vector<uint8_t> messageBuffer; // Buffer for accumulating message data

    while (running) {
        int bytesReceived = recv(clientSocket, reinterpret_cast<char*>(receiveBuffer.data()), BUFFER_SIZE, 0);
        if (bytesReceived <= 0) {
            if (bytesReceived == 0) {
                std::cout << "Client " << clientAddress << " disconnected gracefully" << std::endl;
            }
            else {
                std::cerr << "Receive error: " << WSAGetLastError() << std::endl;
            }
            break;
        }

        // Append received data to message buffer
        messageBuffer.insert(messageBuffer.end(), receiveBuffer.begin(), receiveBuffer.begin() + bytesReceived);

        // Try to decode a message from the buffer
        auto message = MessageProtocol::decodeData(messageBuffer);


        std::cout << "Tried decoding message!" << std::endl;
        if (message) {
            // Notify callback with the received message

            std::cout << "Tried decoding message!" << std::endl;
            if (messageCallback) {
                messageCallback(message->contentType, message->payload);
            }

            // Clear the buffer since we processed a complete message
            messageBuffer.clear();
        }

        // Cleanup partial messages older than 30 seconds
        MessageProtocol::cleanupPartialMessages(30000);
    }

    // Remove from client list
    {
        std::lock_guard<std::mutex> lock(clientSocketsMutex);
        clientSockets.erase(std::remove(clientSockets.begin(), clientSockets.end(), clientSocket),
            clientSockets.end());
    }

    // Notify of client disconnection
    if (clientStatusCallback) {
        clientStatusCallback(clientAddress, false);
    }

    closesocket(clientSocket);
    std::cout << "Client handler thread exiting for " << clientAddress << std::endl;
}