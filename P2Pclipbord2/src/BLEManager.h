#pragma once

// Windows headers - order matters!
#include <winsock2.h>   // Must come BEFORE windows.h
#include <windows.h>

// Windows Runtime headers
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Storage.Streams.h>

// Standard library
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <memory>
#include <map>

// Project headers
#include "MessageProtocol.h"  // Added for encoding/decoding

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Storage::Streams;

// Callbacks
using BLEConnectionCallback = std::function<void(const std::string&, bool)>;
using BLEDataReceivedCallback = std::function<void(const std::vector<uint8_t>& data, MessageContentType contentType)>;

class BLEManager {
public:
    BLEManager(const std::string& deviceName);
    ~BLEManager();

    // Initialize BLE module
    bool initialize();

    // Start advertising as peripheral
    bool startAdvertising();

    // Stop advertising
    void stopAdvertising();

    static bool setServiceUUID(const std::string& key);

    // Send a wakeup notification to all subscribed clients and wait for response
    enum class ClientResponseType {
        NONE,       // No response received yet
        USE_BLE,    // Client wants to use BLE for data transfer
        USE_TCP     // Client wants to use TCP for data transfer
    };

    ClientResponseType sendWakeupAndWaitForResponse(int timeoutMilliseconds = 1000);

    // Send clipboard data via GATT characteristic
    bool sendMessage(const std::vector<uint8_t>& data, MessageContentType contentType);

    // Set connection callback
    void setConnectionCallback(BLEConnectionCallback callback);

    // Set data received callback
    void setDataReceivedCallback(BLEDataReceivedCallback callback);


    // Initialize with default UUID at declaration


private:
    
    // Service and characteristic UUIDs
    static inline GUID SERVICE_UUID = { 0x6c871015, 0xd93c, 0x437b, { 0x9f, 0x13, 0x93, 0x49, 0x98, 0x7e, 0x6f, 0xb3 } };

    // WakeUp characteristic UUID
    const GUID WAKEUP_CHAR_UUID = { 0x84fb7f28, 0x93da, 0x4a5b, { 0x81, 0x72, 0x25, 0x45, 0xb3, 0x91, 0xe2, 0xc6 } };

    // Data characteristic UUID (for clipboard data)
    const GUID DATA_CHAR_UUID = { 0xd752c5fb, 0x1a50, 0x4682, { 0xb3, 0x08, 0x59, 0x3e, 0x96, 0xce, 0x1e, 0x5d } };

    static GUID BLEManager::convertStringToGUID(const std::string& uuidString);

    // Device information
    std::string deviceName;
    std::string deviceId; // Unique identifier for this device instance

    // Advertisement publisher
    BluetoothLEAdvertisementPublisher advertisementPublisher = nullptr;

    // GATT service provider (for server role)
    GattServiceProvider serviceProvider = nullptr;

    // Callback handlers
    BLEConnectionCallback connectionCallback = nullptr;
    BLEDataReceivedCallback dataCallback = nullptr;

    // Event handlers
    winrt::event_token characteristicReadRequestedToken;
    winrt::event_token characteristicWriteRequestedToken;
    winrt::event_token subscriptionChangedToken;
    winrt::event_token wakeupWriteRequestedToken;

    bool testEncodeDecodeMessage(const std::string& data);

    bool hasSubscribedClients = false;
    std::shared_ptr<GattLocalCharacteristic> wakeupCharacteristicRef;
    std::shared_ptr<GattLocalCharacteristic> dataCharacteristicRef;

    // Store clipboard content for use in characteristics
    std::string clipboardContent;

    // Store client response from wakeup
    std::atomic<ClientResponseType> lastClientResponse{ ClientResponseType::NONE };
    std::atomic<bool> waitingForResponse{ false };

    // Helper methods
    void handleCharacteristicReadRequested(GattLocalCharacteristic sender, GattReadRequestedEventArgs args);
    void handleCharacteristicWriteRequested(GattLocalCharacteristic sender, GattWriteRequestedEventArgs args);

    // Create the GATT service and characteristics
    bool createGattService();
};