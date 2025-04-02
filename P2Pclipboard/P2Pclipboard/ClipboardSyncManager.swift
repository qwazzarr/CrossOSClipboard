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
    private var clipboardCheckTimer: Timer?
    
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
    
    func stopServices() {
        print("Stopping clipboard sync services...")
        mdnsBrowser.stopBrowsing()
        //bleManager.stopScanning()
        
        isNetworkConnected = false
        isBleConnected = false
        connectionStatus = "Disconnected"
    }
    
    // Force push current clipboard content to all connected devices
    func sendClipboardContent() {
        if let content = getClipboardContent() {
            print("Manually sending clipboard content")
            broadcast(content)
        }
    }
    
    // MARK: - Connection and Message Handling
    
    private func setupCallbacks() {
        // Set up MDNSBrowser callbacks
        mdnsBrowser.setMessageCallback { [weak self] message in
            self?.handleIncomingMessage(message)
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
        
        bleManager.setMessageCallback { [weak self] message in
            // Handle messages received via BLE GATT characteristic
            self?.handleIncomingMessage(message)
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
    
    private func handleIncomingMessage(_ message: String) {
        if !ignoreNextIncomingMessage {
            // Process the clipboard content directly
            handleReceivedClipboardContent(message)
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
        guard let currentContent = getClipboardContent() else { return }
        
        if currentContent != lastLocalClipboardContent && !ignoreNextClipboardChange {
            print("Clipboard changed: \(currentContent)")
            lastLocalClipboardContent = currentContent
            
            // Broadcast to connected devices
            broadcast(currentContent)
        }
        
        ignoreNextClipboardChange = false
    }
    
    private func handleReceivedClipboardContent(_ content: String) {
        print("Handling received clipboard content: \(content)")
        
        Task { @MainActor in
            lastReceivedContent = content
            
            // Update the clipboard
            updateClipboard(with: content)
            
            lastLocalClipboardContent = content
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
    
    private func updateClipboard(with string: String) {
        ignoreNextClipboardChange = true
        
        #if os(iOS)
        UIPasteboard.general.string = string
        #elseif os(macOS)
        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()
        pasteboard.setString(string, forType: .string)
        #endif
        
        lastLocalClipboardContent = string
    }
    
    // MARK: - Communication
    
    // Broadcast clipboard content to all connected devices
    private func broadcast(_ content: String) {
        if isNetworkConnected || mdnsBrowser.hasCachedServer {
            // Use TCP if available
            sendViaNetwork(content)
        } else if isBleConnected { 
            // Fall back to BLE if TCP not available but BLE is connected
            sendViaBLE(content)
        } else {
            print("Unable to send clipboard content - no active connections")
        }
        
       //ignoreNextIncomingMessage = true // it will come from the server and we sort of want to ignore it
    }
    
    private func sendViaNetwork(_ message: String) {
        if !isNetworkConnected {
            // Connect first if not already connected
            mdnsBrowser.connectToServerIfAvailable()
            
            // Give it a moment to connect
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
                if self?.isNetworkConnected == true {
                    self?.mdnsBrowser.sendMessage(message: message)
                    print("Sent clipboard content via TCP network")
                } else {
                    // Fall back to BLE if network connection failed
                    self?.sendViaBLE(message)
                }
            }
        } else {
            // Already connected, just send
            mdnsBrowser.sendMessage(message: message)
            print("Sent clipboard content via TCP network")
        }
    }
    
    private func sendViaBLE(_ message: String) {
        if isBleConnected {
            bleManager.sendMessage(message: message)
            print("Sent clipboard content via BLE GATT characteristic")
        } else {
            print("No BLE connection available")
        }
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
