import Network
import Foundation
import SwiftUI

// Callbacks for network communication
typealias MDNSMessageCallback = (String) -> Void
typealias MDNSConnectionCallback = (Bool) -> Void

@MainActor
class MDNSBrowser: ObservableObject {
    @Published private(set) var discoveredServices: [String] = []
    @Published private(set) var isConnected: Bool = false
    
    private var browser: NWBrowser?
    private var connection: NWConnection?
    
    // Store the server endpoint for reconnection
    private var serverEndpoint: NWEndpoint?
    private var connectionCheckTimer: Timer?
    private let checkInterval: TimeInterval = 30.0 // Check connection every 30 seconds
    
    // Flag for auto-close after message exchange
    private var closeAfterMessage: Bool = false
    private var messageCompleteHandler: (() -> Void)?
    
    // Callbacks
    private var messageCallback: MDNSMessageCallback?
    private var connectionCallback: MDNSConnectionCallback?
    
    private var receiveBuffer = Data()
    
    init() {
        // Empty initialization
    }
    
    // MARK: - Public API
    
    func setMessageCallback(_ callback: @escaping MDNSMessageCallback) {
        messageCallback = callback
    }
    
    func setConnectionCallback(_ callback: @escaping MDNSConnectionCallback) {
        connectionCallback = callback
    }
    
    func setMessageCompleteHandler(_ handler: @escaping () -> Void) {
        messageCompleteHandler = handler
    }
    
    // Check if we have a cached server endpoint
    var hasCachedServer: Bool {
        return serverEndpoint != nil
    }
    
    // MARK: - Service Discovery and Connection
    
    func startBrowsing() {
        let parameters = NWParameters.tcp
        parameters.includePeerToPeer = true
        
        print("Starting MDNS browser with peer-to-peer enabled")
        
        let browserDescriptor = NWBrowser.Descriptor.bonjour(type: "_clipboard._tcp", domain: "local")
        browser = NWBrowser(for: browserDescriptor, using: parameters)
        
        browser?.stateUpdateHandler = { state in
            print("\n=== Browser State Changed ===")
            switch state {
            case .ready:
                print("Browser is ready to discover services")
            case .failed(let error):
                print("Browser failed: \(error)")
            case .waiting(let error):
                print("Browser waiting: \(error)")
            case .setup:
                print("Browser setting up")
            case .cancelled:
                print("Browser cancelled")
            @unknown default:
                print("Unknown browser state")
            }
        }
        
        browser?.browseResultsChangedHandler = { [weak self] results, changes in
            print("\n=== Services Update ===")
            print("Found \(results.count) services")
            
            Task { @MainActor in
                self?.discoveredServices = results.compactMap { result in
                    if case .service(let name, let type, let domain, _) = result.endpoint {
                        print("Service Name: \(name)")
                        print("Type: \(type)")
                        print("Domain: \(domain)")
                        
                        // Save the endpoint for future connections
                        self?.serverEndpoint = result.endpoint
                        
                        // Connect if we're not already connected
                        if self?.isConnected == false {
                            self?.connectToService(endpoint: result.endpoint)
                        }
                        
                        return name
                    }
                    return nil
                }
            }
        }
        
        print("Starting MDNS browser...")
        browser?.start(queue: .main)
        print("MDNS browser started")
        
        // Start the connection check timer
        startConnectionCheckTimer()
    }
    
    func stopBrowsing() {
        // Close connection and browser
        disconnectFromService()
        browser?.cancel()
        
        // Stop check timer
        stopConnectionCheckTimer()
        
        print("Stopped browsing for services")
    }
    
    // Explicitly disconnect from current service
    func disconnectFromService() {
        connection?.cancel()
        
        Task { @MainActor in
            self.isConnected = false
            self.connectionCallback?(false)
        }
    }
    
    // Set whether to auto-close the connection after message exchange
    func setAutoClose(_ autoClose: Bool) {
        closeAfterMessage = autoClose
    }
    
    // MARK: - Connection Management
    
    private func startConnectionCheckTimer() {
        // Cancel any existing timer
        stopConnectionCheckTimer()
        
        // Create a new timer
        connectionCheckTimer = Timer.scheduledTimer(withTimeInterval: checkInterval, repeats: true) { [weak self] _ in
            self?.checkServerConnection()
        }
    }
    
    private func stopConnectionCheckTimer() {
        connectionCheckTimer?.invalidate()
        connectionCheckTimer = nil
    }
    
    private func checkServerConnection() {
        // Only check if we have a cached endpoint but no active connection
        if let endpoint = serverEndpoint, !isConnected {
            print("Attempting to reconnect to cached server...")
            connectToService(endpoint: endpoint)
        }
    }
    
    // Connect when triggered by a BLE wake-up
    func connectToServerIfAvailable() {
        if !isConnected, let endpoint = serverEndpoint {
            print("Connecting to cached server after wake-up")
            connectToService(endpoint: endpoint)
        } else if !isConnected {
            print("Received wake-up but no cached server endpoint available")
            // Start browsing to find the server
            startBrowsing()
        } else {
            print("Already connected to server")
        }
    }
    
    private func connectToService(endpoint: NWEndpoint) {
        let parameters = NWParameters.tcp
        parameters.includePeerToPeer = true
        
        print("Connecting to service at endpoint: \(endpoint)")
        
        connection = NWConnection(to: endpoint, using: parameters)
        
        connection?.stateUpdateHandler = { [weak self] state in
            switch state {
            case .ready:
                print("Connected to MDNS service!")
                Task { @MainActor in
                    self?.isConnected = true
                    self?.connectionCallback?(true)
                    self?.startReceiving()
                }
            case .failed(let error):
                print("Connection failed: \(error)")
                Task { @MainActor in
                    self?.isConnected = false
                    self?.connectionCallback?(false)
                }
            case .waiting(let error):
                print("Connection waiting: \(error)")
            case .cancelled:
                Task { @MainActor in
                    self?.isConnected = false
                    self?.connectionCallback?(false)
                }
            default:
                break
            }
        }
        
        connection?.start(queue: .main)
    }
    
    private func startReceiving() {
        receiveNextMessage()
    }
    
    private func receiveNextMessage() {
         connection?.receive(minimumIncompleteLength: 1, maximumLength: Int.max) { [weak self] content, _, isComplete, error in
             guard let self = self else { return }
             
             if let error = error {
                 print("Receive error: \(error)")
                 return
             }
             
             if let data = content {
                 // Add received data to our buffer
                 self.receiveBuffer.append(data)
                 
                 // Try to process complete messages from the buffer
                 self.processReceiveBuffer()
                 
                 // Check if we should auto-close after this message
                 if self.closeAfterMessage && !self.receiveBuffer.isEmpty {
                     // We've received data and processed it, now schedule disconnection
                     DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
                         self?.disconnectFromService()
                         self?.messageCompleteHandler?()
                     }
                     return // Exit early to prevent setting up another receive
                 }
             }
             
             // Set up the next receive only if the connection is not complete
             // and we're not auto-closing
             if !isComplete && (!self.closeAfterMessage || self.receiveBuffer.isEmpty) {
                 self.receiveNextMessage()
             } else {
                 print("Connection marked as complete or auto-closing")
             }
         }
     }
    
    private func processReceiveBuffer() {
        // Keep processing while we potentially have complete messages
        while !receiveBuffer.isEmpty {
            // Check if we have enough data for at least a length field (4 bytes)
            guard receiveBuffer.count >= 4 else {
                // Not enough data yet, wait for more
                break
            }
            
            // Read message length (first 4 bytes)
            let lengthBytes = [receiveBuffer[0], receiveBuffer[1], receiveBuffer[2], receiveBuffer[3]]
            let messageLength = UInt32(bigEndianBytes: lengthBytes)
            
            // Check if we have received the complete message
            guard receiveBuffer.count >= Int(messageLength) else {
                // We haven't received the complete message yet, wait for more data
                break
            }
            
            // Extract the complete message
            let messageData = receiveBuffer.prefix(Int(messageLength))
            
            // Decode the message
            if let message = MessageProtocol.decodeData(messageData) {
                // Handle the decoded message
                handleDecodedMessage(message)
                
                // Auto-close connection if needed
                if closeAfterMessage {
                    print("Auto-closing TCP connection after message")
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
                        self?.disconnectFromService()
                        self?.messageCompleteHandler?()
                    }
                }
            } else {
                print("Failed to decode message")
            }
            
            // Remove the processed message from the buffer
            receiveBuffer.removeAll()
        }
    }
    
    private func handleDecodedMessage(_ message: MessageProtocol.Message) {
        // Handle based on content type
        switch message.contentType {
        case .plainText:
            if let text = message.stringPayload {
                print("Received text via TCP: \(text)")
                
                // Forward to callback
                Task { @MainActor in
                    self.messageCallback?(text)
                }
            }
        
        case .htmlContent:
            if let html = message.stringPayload {
                print("Received HTML content via TCP")
                
                // Forward to callback
                Task { @MainActor in
                    self.messageCallback?(html)
                }
            }
        
        case .pngImage, .jpegImage, .pdfDocument, .rtfText:
            // Handle binary content types
            print("Received binary content of type: \(message.contentType), size: \(message.payload.count) bytes")
        }
    }
    
    // MARK: - Sending Messages
    
    func sendMessage(message: String, completion: (() -> Void)? = nil) {
        // For TCP connections, we can use the TCP optimization
        let messageChunks = MessageProtocol.encodeTextMessage(text: message, transport: .tcp)
        
        guard let connection = connection, connection.state == .ready else {
            print("Cannot send message - no active connection")
            completion?()
            return
        }
        
        // There should only be one chunk for TCP
        if let chunk = messageChunks.first {
            connection.send(content: chunk, completion: .contentProcessed { [weak self] error in
                if let error = error {
                    print("Send error: \(error)")
                } else {
                    print("Message sent successfully via TCP")
                    
                    // Auto-close connection if needed
                    if self?.closeAfterMessage == true {
                        print("Auto-closing TCP connection after sending")
                        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                            self?.disconnectFromService()
                            self?.messageCompleteHandler?()
                        }
                    }
                }
                
                completion?()
            })
        } else {
            completion?()
        }
    }
}
