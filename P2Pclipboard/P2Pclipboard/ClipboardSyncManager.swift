import Foundation
import SwiftUI
import Combine

#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

@MainActor
class ClipboardSyncManager: ObservableObject {
    // Singleton instance
    static let shared = ClipboardSyncManager()
    
    // Published properties for UI updates
    @Published var lastReceivedContent: String = ""
    @Published var isNetworkConnected = false
    @Published var isBleConnected = false
    @Published var connectionStatus: String = "Disconnected"
    
    // Managers
    private let mdnsBrowser = MDNSBrowser()
    private let bleManager = BLEManager()
    
    // Clipboard monitoring
    //exists for the purpose of avoiding sending message when the new clipboard content is identical/or when there is duplicated API updates
    private var lastLocalClipboardContent: String = ""
    //exists for the purpose of avoiding endless loops when the client after receiving a new clipboard content from server would send notification about it to the server again
    private var ignoreNextClipboardChange = false
    //exists to ignore the first message that receives from either ble or tcp connection, since the behaviour of a server to immidiately send its clipboard content when the connetcion is established
    private var ignoreNextIncomingMessage = false
    
    private var lastPasteboardChangeCount: Int = 0
    private var clipboardCheckTimer: Timer?
    
    private let imageHandler = ClipboardImageHandler()
    
    // Private initializer for singleton pattern
    private init() {
        setupCallbacks()
        setupClipboardMonitoring()
    }
    
    // MARK: - Public Methods
    
    func startServices() {
        print("Starting clipboard sync services...")
        
        // Start BLE scanning - this will handle server discovery via BLE
        bleManager.startScanning()
        
        // Start MDNS browser for TCP connection discovery
        //mdnsBrowser.startBrowsing()
        
        connectionStatus = "Scanning..."
        
        // Initialize with current clipboard content if available
        if let initialContent = getClipboardContent() {
            lastLocalClipboardContent = initialContent
        }
    }
    
    @MainActor
    func resetAllData() {
        // First stop all services to close connections
        stopServices()
        
        // Reset all the connection states
        isNetworkConnected = false
        isBleConnected = false
        connectionStatus = "Disconnected"
        
        // Clear any received content
        lastReceivedContent = ""
        
        // Reset clipboard monitoring states
        lastLocalClipboardContent = ""
        ignoreNextClipboardChange = false
        ignoreNextIncomingMessage = false
        
        // Reset BLE manager
        bleManager.resetDiscoveredDevices()
        
        // Reset the MDNS browser (clear cached servers)
        mdnsBrowser.stopBrowsing()
        
        // Reset pasteboard change count to avoid false triggers
        #if os(macOS)
        lastPasteboardChangeCount = NSPasteboard.general.changeCount
        #endif
        
        print("All connections and cached data have been reset")
    }
    
    func stopServices() {
        print("Stopping clipboard sync services...")
        //mdnsBrowser.stopBrowsing()
        bleManager.stopScanning()
        
        isNetworkConnected = false
        isBleConnected = false
        connectionStatus = "Disconnected"
    }
    
    // MARK: - Connection and Message Handling
    
    private func setupCallbacks() {
        // Set up MDNSBrowser callbacks
        mdnsBrowser.setMessageCallback { [weak self] data, contentType in
            // Make sure to safely unwrap self first to prevent potential memory issues
            guard let self = self else { return }
            
            // Call your updated method that handles data and content type
            self.handleIncomingMessage(data, contentType: contentType)
        }
        
        mdnsBrowser.setConnectionCallback { [weak self] connected in
            Task { @MainActor in
                print("MDNS connection callback: \(connected)")
                self?.isNetworkConnected = connected
                self?.updateConnectionStatus()
            }
        }
        
        // Set up BLEManager callbacks
        bleManager.setWakeupCallback { [weak self] in
            Task { @MainActor in
                print("Received wake-up notification from server")
                guard let self = self else { return }
                
                self.ignoreNextIncomingMessage = false
                
                // Decide how to respond to the wake-up
                // If we have a cached TCP server, tell the BLE server to use TCP
                if self.mdnsBrowser.hasCachedServer || self.isNetworkConnected {
                    print("Responding with USE_TCP as we have a cached server")
                    self.bleManager.respondToWakeup(useTCP: true)
                    
                    // Connect to the TCP server to receive the message
                    print("Opening TCP connection to receive server message")
                    self.mdnsBrowser.connectToServerIfAvailable()
                } else {
                    // No TCP available, tell the server to use BLE
                    print("Responding with USE_BLE as we have no cached server")
                    self.bleManager.respondToWakeup(useTCP: false)
                    
                    // The server will send via BLE data characteristic
                    print("Server will send data via BLE GATT characteristic")
                }
            }
        }
        
        bleManager.setConnectionCallback { [weak self] connected in
            Task { @MainActor in
                print("BLE connection callback: \(connected)")
                self?.isBleConnected = connected
                self?.updateConnectionStatus()
            }
        }
        
        bleManager.setMessageCallback { [weak self] data, contentType in
            // Handle messages received via BLE GATT characteristic
            self?.handleIncomingMessage(data, contentType: contentType)
        }
    }
    
    private func updateConnectionStatus() {
        if isNetworkConnected && isBleConnected {
            connectionStatus = "Connected (Network + BLE)"
        } else if isNetworkConnected {
            connectionStatus = "Connected (Network)"
        } else if isBleConnected {
            connectionStatus = "Connected (Bluetooth)"
        } else if mdnsBrowser.hasCachedServer {
            connectionStatus = "Server Known"
        } else {
            connectionStatus = "Scanning..."
        }
    }
    
    private func handleIncomingMessage(_ data: Data, contentType: MessageContentType) {
        if !ignoreNextIncomingMessage {
            // Process the clipboard content directly
            updateClipboard(data: data, contentType: contentType)
        } else {
            print("Ignoring first message after connection")
            ignoreNextIncomingMessage = false
        }
    }
    
    // MARK: - Clipboard Monitoring
    
    private func setupClipboardMonitoring() {
        #if os(iOS)
        // On iOS, use pasteboard change notifications
        if #available(iOS 14.0, *) {
            UIPasteboard.general.detectsValueChanged = true
        }
        
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(clipboardChanged),
            name: UIPasteboard.changedNotification,
            object: nil
        )
        
        // Also observe app foreground/background for clipboard sync
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(appWillEnterForeground),
            name: UIApplication.willEnterForegroundNotification,
            object: nil
        )
        #elseif os(macOS)
        // On macOS, use a timer to check for clipboard changes
        clipboardCheckTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            guard let self = self else { return }
            self.checkClipboardForChanges()
        }
        #endif
    }
    
    #if os(iOS)
    @objc private func clipboardChanged() {
        checkClipboardForChanges()
    }
    
    @objc private func appWillEnterForeground() {
        // Check clipboard when app comes to foreground
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
            self?.checkClipboardForChanges()
        }
    }
    #endif
    
    private func checkClipboardForChanges() {
        
        let currentChangeCount = NSPasteboard.general.changeCount
        if currentChangeCount != lastPasteboardChangeCount {
            
            if(!ignoreNextClipboardChange){
                // First check for images
                if imageHandler.hasImage() {
                    handleClipboardImageChange()
                }
                // Then check for text
                else if let currentContent = getClipboardText() {
                    handleClipboardTextChange(currentContent)
                }
            }
            ignoreNextClipboardChange = false
            lastPasteboardChangeCount = currentChangeCount
        }
    }
    
    /// Process a clipboard image change
     private func handleClipboardImageChange() {
         print("Clipboard contains image, checking if it's new...")
         
         // Get image from clipboard with compression along with original hash
         if let imageResult = imageHandler.getImageFromClipboard(format: .jpeg,
                                                                 isCompressed: self.isNetworkConnected || self.mdnsBrowser.hasCachedServer) {
             let imageData = imageResult.data
             let originalHash = imageResult.originalHash
             
             print("Clipboard image changed, hash: \(originalHash)")
             
             // Broadcast image to connected devices
             broadcastData(imageData, contentType: .jpegImage)

         } else {
             print("Failed to get image data from clipboard")
         }
     }
    
    private func handleClipboardTextChange(_ currentContent: String) {
        if currentContent != lastLocalClipboardContent {
            print("Clipboard text changed: \(currentContent)")
            lastLocalClipboardContent = currentContent
            
            // Broadcast text to connected devices
            if let data = currentContent.data(using: .utf8) {
                broadcastData(data, contentType: .plainText)
            }
        }
    }
    
    // MARK: - Clipboard Operations
    
    private func getClipboardContent() -> String? {
        #if os(iOS)
        return UIPasteboard.general.string
        #elseif os(macOS)
        return NSPasteboard.general.string(forType: .string)
        #endif
    }
    
    private func updateClipboard(data: Data, contentType: MessageContentType) {
        ignoreNextClipboardChange = true
        
        switch contentType {
            case .plainText:
                let text = String(data: data, encoding: .utf8)!
                setClipboardText(text)
            case .jpegImage:
                imageHandler.setClipboardImage(data, format: .jpeg)
            case .pngImage:
                imageHandler.setClipboardImage(data, format: .png)
            default:
                print("Unsupported content type for clipboard update")
        }
    }
    
    // MARK: - Communication
    
    /// Send data to all connected devices
    private func broadcastData(_ data: Data, contentType: MessageContentType) {
        // First try to send via network connection
        if isNetworkConnected || mdnsBrowser.hasCachedServer {
            sendViaNetwork(data, contentType: contentType)
        }
        // Fall back to BLE if network is not available
        else if isBleConnected {
            sendViaBLE(data, contentType: contentType)
        }
        else {
            print("No available connections for sending data")
        }
    }
    
    private func sendViaNetwork(_ data: Data , contentType: MessageContentType) {
        if !isNetworkConnected {
            // Connect first if not already connected
            mdnsBrowser.connectToServerIfAvailable()
            
            // Give it a moment to connect
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
                if self?.isNetworkConnected == true {
                    self?.mdnsBrowser.sendMessage(data: data, contentType: contentType)
                    print("Sent clipboard content via TCP network")
                } else {
                    // Fall back to BLE if network connection failed
                    self?.sendViaBLE(data , contentType: contentType)
                }
            }
        } else {
            // Already connected, just send
            mdnsBrowser.sendMessage(data: data, contentType: contentType)
            print("Sent clipboard content via TCP network")
        }
    }
    
    private func sendViaBLE(_ data: Data, contentType: MessageContentType) {
        if isBleConnected {
            bleManager.sendMessage(data: data, contentType: contentType)
            print("Sent clipboard content via BLE GATT characteristic")
        } else {
            print("No BLE connection available")
        }
    }
    
    // MARK: - Clipboard Operations
    
    /// Get text from clipboard
    private func getClipboardText() -> String? {
        #if os(iOS)
        return UIPasteboard.general.string
        #elseif os(macOS)
        return NSPasteboard.general.string(forType: .string)
        #else
        return nil
        #endif
    }
    
    /// Set text to clipboard
    private func setClipboardText(_ text: String) {
        ignoreNextClipboardChange = true
        
        #if os(iOS)
        UIPasteboard.general.string = text
        #elseif os(macOS)
        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()
        pasteboard.setString(text, forType: .string)
        #endif
        
        lastLocalClipboardContent = text
    }
    
    
    deinit {
        #if os(iOS)
        NotificationCenter.default.removeObserver(self)
        #elseif os(macOS)
        clipboardCheckTimer?.invalidate()
        clipboardCheckTimer = nil
        #endif
    }
}
