#if os(iOS)
import Foundation
import UIKit
import AVKit

class ClipboardPiPController: NSObject, AVPictureInPictureControllerDelegate {
    
    // Singleton instance
    static let shared = ClipboardPiPController()
    
    // Picture in Picture controller
    private var pipController: AVPictureInPictureController?
    
    // The layer that will be displayed in PiP mode
    private var pipVideoCallLayer: AVSampleBufferDisplayLayer?
    
    // Timer for checking clipboard
    private var clipboardCheckTimer: Timer?
    
    // Variable to track the last clipboard content
    private var lastClipboardContent: String = ""
    
    // The view containing our sample buffer layer
    private var containerView: UIView?
    
    // Callback for clipboard changes
    var clipboardChangeCallback: ((String) -> Void)?
    
    // Current status text to display
    private var statusText: String = "Monitoring clipboard..."
    
    private override init() {
        super.init()
        setupPiP()
    }
    
    // Set up the PiP controller
    private func setupPiP() {
        // Create a container view to hold our layer
        let container = UIView(frame: CGRect(x: 0, y: 0, width: 1080, height: 720))
        container.backgroundColor = .black
        self.containerView = container
        
        // Create a display layer
        let displayLayer = AVSampleBufferDisplayLayer()
        displayLayer.frame = container.bounds
        displayLayer.backgroundColor = UIColor.darkGray.cgColor
        displayLayer.videoGravity = .resizeAspect
        
        // Add the layer to the view
        container.layer.addSublayer(displayLayer)
        self.pipVideoCallLayer = displayLayer
        
        // Create a PiP controller with our layer
        if AVPictureInPictureController.isPictureInPictureSupported() {
            pipController = AVPictureInPictureController(contentSource:
                AVPictureInPictureController.ContentSource(
                    sampleBufferDisplayLayer: displayLayer,
                    playbackDelegate: self))
            
            pipController?.delegate = self
            
            // Configure the PiP controller
            if #available(iOS 14.2, *) {
                pipController?.canStartPictureInPictureAutomaticallyFromInline = true
            }
        }
        
        // Render initial content
        updateDisplayContent()
    }
    
    // Start the PiP session
    func startPiP() {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            
            // Make sure we have the latest content
            self.updateDisplayContent()
            
            // Start PiP mode if available
            if self.pipController?.isPictureInPictureActive == false {
                self.pipController?.startPictureInPicture()
            }
            
            // Start clipboard monitoring timer
            self.startClipboardMonitoring()
        }
    }
    
    // Stop the PiP session
    func stopPiP() {
        clipboardCheckTimer?.invalidate()
        clipboardCheckTimer = nil
        
        if pipController?.isPictureInPictureActive == true {
            pipController?.stopPictureInPicture()
        }
    }
    
    // Set the status text displayed in PiP
    func setStatusText(_ text: String) {
        statusText = text
        updateDisplayContent()
    }
    
    // Start monitoring clipboard changes
    private func startClipboardMonitoring() {
        // Check immediately to get initial content
        checkClipboardForChanges()
        
        // Set up a timer to check periodically
        clipboardCheckTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            self?.checkClipboardForChanges()
        }
    }
    
    // Check if clipboard content has changed
    private func checkClipboardForChanges() {
        if let clipboardText = UIPasteboard.general.string {
            if clipboardText != lastClipboardContent {
                lastClipboardContent = clipboardText
                clipboardChangeCallback?(clipboardText)
                
                // Update the display to show new content
                updateDisplayContent()
            }
        }
    }
    
    // Update the visual content of the PiP window
    private func updateDisplayContent() {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            
            // Create an image context for rendering text
            UIGraphicsBeginImageContextWithOptions(CGSize(width: 1080, height: 720), false, 0)
            
            guard let context = UIGraphicsGetCurrentContext() else {
                UIGraphicsEndImageContext()
                return
            }
            
            // Fill background
            context.setFillColor(UIColor.black.cgColor)
            context.fill(CGRect(x: 0, y: 0, width: 1080, height: 720))
            
            // Draw header
            let headerText = "Clipboard Sync"
            let headerAttributes: [NSAttributedString.Key: Any] = [
                .font: UIFont.boldSystemFont(ofSize: 30),
                .foregroundColor: UIColor.white
            ]
            
            let headerSize = headerText.size(withAttributes: headerAttributes)
            let headerRect = CGRect(x: (1080 - headerSize.width) / 2, y: 40, width: headerSize.width, height: headerSize.height)
            headerText.draw(in: headerRect, withAttributes: headerAttributes)
            
            // Draw status text
            let statusAttributes: [NSAttributedString.Key: Any] = [
                .font: UIFont.systemFont(ofSize: 24),
                .foregroundColor: UIColor.lightGray
            ]
            
            let statusSize = self.statusText.size(withAttributes: statusAttributes)
            let statusRect = CGRect(x: (1080 - statusSize.width) / 2, y: 100, width: statusSize.width, height: statusSize.height)
            self.statusText.draw(in: statusRect, withAttributes: statusAttributes)
            
            // Draw latest clipboard content preview
            let contentPreview = self.lastClipboardContent.isEmpty ? "No clipboard content" :
                                (self.lastClipboardContent.count > 100 ?
                                String(self.lastClipboardContent.prefix(100)) + "..." :
                                self.lastClipboardContent)
            
            let contentAttributes: [NSAttributedString.Key: Any] = [
                .font: UIFont.systemFont(ofSize: 20),
                .foregroundColor: UIColor.white
            ]
            
            let contentRect = CGRect(x: 50, y: 180, width: 980, height: 500)
            contentPreview.draw(in: contentRect, withAttributes: contentAttributes)
            
            // Get the image from the context
            if let image = UIGraphicsGetImageFromCurrentImageContext() {
                UIGraphicsEndImageContext()
                
                // Convert the image to a sample buffer
                if let sampleBuffer = self.createSampleBufferFrom(image: image) {
                    // Display the sample buffer
                    self.pipVideoCallLayer?.enqueue(sampleBuffer)
                }
            } else {
                UIGraphicsEndImageContext()
            }
        }
    }
    
    // Create a sample buffer from a UIImage
    private func createSampleBufferFrom(image: UIImage) -> CMSampleBuffer? {
        guard let cgImage = image.cgImage else { return nil }
        
        let width = cgImage.width
        let height = cgImage.height
        
        let bytesPerRow = cgImage.bytesPerRow
        let dataSize = bytesPerRow * height
        
        var pixelBuffer: CVPixelBuffer?
        let options: [String: Any] = [
            kCVPixelBufferCGImageCompatibilityKey as String: true,
            kCVPixelBufferCGBitmapContextCompatibilityKey as String: true
        ]
        
        let status = CVPixelBufferCreate(
            kCFAllocatorDefault,
            width,
            height,
            kCVPixelFormatType_32ARGB,
            options as CFDictionary,
            &pixelBuffer
        )
        
        guard status == kCVReturnSuccess, let pixelBuffer = pixelBuffer else {
            return nil
        }
        
        CVPixelBufferLockBaseAddress(pixelBuffer, [])
        
        let pixelData = CVPixelBufferGetBaseAddress(pixelBuffer)
        
        let context = CGContext(
            data: pixelData,
            width: width,
            height: height,
            bitsPerComponent: 8,
            bytesPerRow: bytesPerRow,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.noneSkipFirst.rawValue
        )
        
        context?.draw(cgImage, in: CGRect(x: 0, y: 0, width: width, height: height))
        
        CVPixelBufferUnlockBaseAddress(pixelBuffer, [])
        
        var formatDescription: CMFormatDescription?
        CMVideoFormatDescriptionCreateForImageBuffer(
            allocator: kCFAllocatorDefault,
            imageBuffer: pixelBuffer,
            formatDescriptionOut: &formatDescription
        )
        
        guard let formatDescription = formatDescription else {
            return nil
        }
        
        var sampleBuffer: CMSampleBuffer?
        var timingInfo = CMSampleTimingInfo(
            duration: CMTime.invalid,
            presentationTimeStamp: CMTime.now,
            decodeTimeStamp: CMTime.invalid
        )
        
        CMSampleBufferCreateForImageBuffer(
            allocator: kCFAllocatorDefault,
            imageBuffer: pixelBuffer,
            dataReady: true,
            makeDataReadyCallback: nil,
            refcon: nil,
            formatDescription: formatDescription,
            sampleTiming: &timingInfo,
            sampleBufferOut: &sampleBuffer
        )
        
        return sampleBuffer
    }
}

// MARK: - AVPictureInPictureSampleBufferPlaybackDelegate
extension ClipboardPiPController: AVPictureInPictureSampleBufferPlaybackDelegate {
    func pictureInPictureController(_ pictureInPictureController: AVPictureInPictureController, setPlaying playing: Bool) {
        // Not needed for our use case since we're just displaying static content
    }
    
    func pictureInPictureControllerTimeRangeForPlayback(_ pictureInPictureController: AVPictureInPictureController) -> CMTimeRange {
        return CMTimeRange(start: .zero, duration: .positiveInfinity)
    }
    
    func pictureInPictureControllerIsPlaybackPaused(_ pictureInPictureController: AVPictureInPictureController) -> Bool {
        return false
    }
    
    func pictureInPictureController(_ pictureInPictureController: AVPictureInPictureController, didTransitionToRenderSize newRenderSize: CMVideoDimensions) {
        // Handle render size changes if needed
    }
    
    func pictureInPictureController(_ pictureInPictureController: AVPictureInPictureController, skipByInterval skipInterval: CMTime, completion completionHandler: @escaping () -> Void) {
        completionHandler()
    }
}
#endif
