#include "BLEManager.h"
#include "UUIDGenerator.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <queue>
#include <iomanip>
#include <vector>
#include <chrono>

// Generate a simple device ID based on machine name and timestamp
std::string GenerateDeviceId() {
    char computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(computerName) / sizeof(computerName[0]);

    if (!GetComputerNameA(computerName, &size)) {
        // Fallback if we can't get computer name
        strcpy_s(computerName, "Unknown");
    }

    // Add timestamp for uniqueness
    std::stringstream ss;
    ss << computerName << "-" << time(nullptr);
    return ss.str();
}

BLEManager::BLEManager(const std::string& deviceName)
    : deviceName(deviceName), deviceId(GenerateDeviceId()) {
    // Initialize WinRT
    winrt::init_apartment();
}

BLEManager::~BLEManager() {
    stopAdvertising();

    // Uninitialize WinRT
    winrt::uninit_apartment();
}

bool BLEManager::setServiceUUID(const std::string& key) {
    // Generate a UUID from the key
    std::string uuidString = UUIDGenerator::uuidFromString(key);

    // Set the service UUID
    SERVICE_UUID = convertStringToGUID(uuidString);

    std::cout << "Service UUID set to: " << uuidString << " (from key: " << key << ")" << std::endl;

    return true;
}

// Helper function to convert string UUID to GUID
GUID BLEManager::convertStringToGUID(const std::string& uuidString) {
    GUID guid;
    unsigned long p0;
    unsigned int p1, p2, p3, p4, p5, p6, p7, p8, p9, p10;

    sscanf_s(uuidString.c_str(),
        "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        &p0, &p1, &p2, &p3, &p4, &p5, &p6, &p7, &p8, &p9, &p10);

    guid.Data1 = p0;
    guid.Data2 = p1;
    guid.Data3 = p2;
    guid.Data4[0] = p3;
    guid.Data4[1] = p4;
    guid.Data4[2] = p5;
    guid.Data4[3] = p6;
    guid.Data4[4] = p7;
    guid.Data4[5] = p8;
    guid.Data4[6] = p9;
    guid.Data4[7] = p10;

    return guid;
}

bool BLEManager::initialize() {
    try {
        // Get the Bluetooth adapter
        auto bluetoothAdapterOperation = BluetoothAdapter::GetDefaultAsync();
        auto bluetoothAdapter = bluetoothAdapterOperation.get();

        if (!bluetoothAdapter) {
            std::cerr << "No Bluetooth adapter found" << std::endl;
            return false;
        }

        // Check if BLE is supported
        if (!bluetoothAdapter.IsLowEnergySupported()) {
            std::cerr << "Bluetooth adapter does not support BLE" << std::endl;
            return false;
        }

        // Check if adapter is turned on
        if (bluetoothAdapter.BluetoothAddress() == 0) {
            std::cerr << "Bluetooth adapter appears to be disabled" << std::endl;
            return false;
        }

        // Check peripheral role support
        bool isPeripheralSupported = bluetoothAdapter.IsPeripheralRoleSupported();
        std::cout << "Peripheral role supported: " << (isPeripheralSupported ? "Yes" : "No") << std::endl;

        if (!isPeripheralSupported) {
            std::cout << "Warning: Peripheral role not supported, device may not be able to advertise" << std::endl;
            // Continue anyway, as some adapters report false but can still advertise
        }

        // Maximum advertisement data length
        uint32_t maxAdvDataLength = bluetoothAdapter.MaxAdvertisementDataLength();
        std::cout << "Maximum advertisement data length: " << maxAdvDataLength << " bytes" << std::endl;

        if (!createGattService()) {
            std::cerr << "Failed to create GATT service" << std::endl;
            return false;
        }

        std::cout << "BLE Manager initialized successfully" << std::endl;
        return true;
    }
    catch (const winrt::hresult_error& ex) {
        std::cerr << "BLE initialization error: "
            << "HRESULT: " << std::hex << ex.code()
            << " Message: " << winrt::to_string(ex.message()) << std::endl;
        return false;
    }
    catch (const std::exception& stdex) {
        std::cerr << "Standard exception during BLE initialization: "
            << stdex.what() << std::endl;
        return false;
    }
    catch (...) {
        std::cerr << "Unknown error during BLE initialization" << std::endl;
        return false;
    }
}

bool BLEManager::createGattService() {
    try {
        std::cout << "Creating GATT service..." << std::endl;

        // Convert GUID to winrt format
        winrt::guid serviceUuid(SERVICE_UUID);

        // Create service provider
        auto serviceResult = GattServiceProvider::CreateAsync(serviceUuid).get();

        if (!serviceResult) {
            std::cerr << "Failed to create service provider (null result)" << std::endl;
            return false;
        }

        // Get service provider
        serviceProvider = serviceResult.ServiceProvider();

        if (!serviceProvider) {
            std::cerr << "ServiceProvider is null after CreateAsync" << std::endl;
            return false;
        }

        // Get service reference
        GattLocalService service = serviceProvider.Service();
        if (!service) {
            std::cerr << "Service is null" << std::endl;
            return false;
        }

        // Create the WakeUp characteristic
        GattLocalCharacteristicParameters wakeupParams;
        wakeupParams.CharacteristicProperties(
            GattCharacteristicProperties::Read |
            GattCharacteristicProperties::Write |
            GattCharacteristicProperties::WriteWithoutResponse |
            GattCharacteristicProperties::Notify);
        wakeupParams.ReadProtectionLevel(GattProtectionLevel::Plain);

        // Convert UUID to winrt format for the WakeUp characteristic
        winrt::guid wakeupCharUuid(WAKEUP_CHAR_UUID);

        // Create the characteristic
        auto wakeupResult = service.CreateCharacteristicAsync(wakeupCharUuid, wakeupParams).get();

        if (!wakeupResult) {
            std::cerr << "Failed to create wakeup characteristic" << std::endl;
            return false;
        }

        // Get the characteristic reference
        GattLocalCharacteristic wakeupChar = wakeupResult.Characteristic();
        if (!wakeupChar) {
            std::cerr << "Wakeup characteristic is null" << std::endl;
            return false;
        }

        // Set up read event handler
        characteristicReadRequestedToken = wakeupChar.ReadRequested(
            [this](GattLocalCharacteristic sender, GattReadRequestedEventArgs args) {
                handleCharacteristicReadRequested(sender, args);
            });

        subscriptionChangedToken = wakeupChar.SubscribedClientsChanged(
            [this](GattLocalCharacteristic sender, winrt::Windows::Foundation::IInspectable args) {
                auto clients = sender.SubscribedClients();
                std::cout << "Notification subscription changed, clients: " << clients.Size() << std::endl;

                // Update our tracking flag
                hasSubscribedClients = (clients.Size() > 0);

                // Store a proper reference to the notification characteristic
                wakeupCharacteristicRef = std::make_shared<GattLocalCharacteristic>(sender);

                // If we have clients, determine the MTU of each one
                for (uint32_t i = 0; i < clients.Size(); i++) {
                    try {
                        auto session = clients.GetAt(i).Session();
                        std::cout << "Checking MTU for client " << i + 1 << ":" << std::endl;
                        determineClientMTU(session);
                    }
                    catch (const winrt::hresult_error& ex) {
                        std::cerr << "Error accessing client session: " << winrt::to_string(ex.message()) << std::endl;
                    }
                }
            });

        wakeupWriteRequestedToken = wakeupChar.WriteRequested(
            [this](GattLocalCharacteristic sender, GattWriteRequestedEventArgs args) {
                handleCharacteristicWriteRequested(sender, args);
            });

        // Create the Data characteristic for clipboard content
        GattLocalCharacteristicParameters dataParams;
        dataParams.CharacteristicProperties(
            GattCharacteristicProperties::Read |
            GattCharacteristicProperties::Write |
            GattCharacteristicProperties::Notify |
            GattCharacteristicProperties::WriteWithoutResponse);
        dataParams.ReadProtectionLevel(GattProtectionLevel::Plain);
        dataParams.WriteProtectionLevel(GattProtectionLevel::Plain);

        // Convert UUID to winrt format for the Data characteristic
        winrt::guid dataCharUuid(DATA_CHAR_UUID);

        // Create the characteristic
        auto dataResult = service.CreateCharacteristicAsync(dataCharUuid, dataParams).get();

        if (!dataResult) {
            std::cerr << "Failed to create data characteristic" << std::endl;
            return false;
        }

        // Get the characteristic reference
        GattLocalCharacteristic dataChar = dataResult.Characteristic();
        if (!dataChar) {
            std::cerr << "Data characteristic is null" << std::endl;
            return false;
        }

        // Set up write event handler
        characteristicWriteRequestedToken = dataChar.WriteRequested(
            [this](GattLocalCharacteristic sender, GattWriteRequestedEventArgs args) {
                handleCharacteristicWriteRequested(sender, args);
            });

        // Store a reference to the data characteristic
        dataCharacteristicRef = std::make_shared<GattLocalCharacteristic>(dataChar);

        std::cout << "GATT service created successfully" << std::endl;
        return true;
    }
    catch (const winrt::hresult_error& ex) {
        std::cerr << "GATT service creation error: "
            << "HRESULT: " << std::hex << ex.code()
            << " Message: " << winrt::to_string(ex.message()) << std::endl;
        return false;
    }
    catch (const std::exception& stdex) {
        std::cerr << "Standard exception during GATT service creation: "
            << stdex.what() << std::endl;
        return false;
    }
    catch (...) {
        std::cerr << "Unknown error during GATT service creation" << std::endl;
        return false;
    }
}

bool BLEManager::startAdvertising() {
    try {
        // Try to start GATT advertising
        if (serviceProvider) {
            try {
                std::cout << "Starting GATT advertising..." << std::endl;
                auto params = GattServiceProviderAdvertisingParameters();
                params.IsDiscoverable(true);
                params.IsConnectable(true);
                serviceProvider.StartAdvertising(params);
                std::cout << "GATT advertising started successfully" << std::endl;
                return true;
            }
            catch (const winrt::hresult_error& ex) {
                std::cerr << "GATT advertising error: " << winrt::to_string(ex.message()) << std::endl;
                return false;
            }
        }
        else {
            std::cerr << "Cannot start advertising - no service provider available" << std::endl;
            return false;
        }
    }
    catch (const winrt::hresult_error& ex) {
        std::cerr << "BLE advertising error: " << winrt::to_string(ex.message()) << std::endl;
        return false;
    }
}

void BLEManager::stopAdvertising() {
    try {
        if (serviceProvider) {
            serviceProvider.StopAdvertising();
            std::cout << "GATT advertising stopped" << std::endl;
        }
    }
    catch (const winrt::hresult_error& ex) {
        std::cerr << "BLE stop advertising error: " << winrt::to_string(ex.message()) << std::endl;
    }
}

BLEManager::ClientResponseType BLEManager::sendWakeupAndWaitForResponse(int timeoutMilliseconds) {
    try {
        std::cout << "\n=== BLE sendWakeupAndWaitForResponse Started ===\n" << std::endl;

        // Use our stored reference to the wakeup characteristic
        std::shared_ptr<GattLocalCharacteristic> wakeupCharRef = wakeupCharacteristicRef;

        if (!wakeupCharRef) {
            std::cerr << "No wakeup characteristic available (reference is null)" << std::endl;
            return ClientResponseType::NONE;
        }

        if (!hasSubscribedClients) {
            std::cout << "No subscribed clients to notify" << std::endl;
            return ClientResponseType::NONE;
        }

        std::cout << "Valid wakeup characteristic reference found, sending notification..." << std::endl;

        try {
            // Reset response state
            lastClientResponse.store(ClientResponseType::NONE);
            waitingForResponse.store(true);

            // Create a simple buffer with a counter value that changes each time
            static uint8_t counter = 0;
            counter = (counter + 1) % 256;  // Simple 8-bit counter

            auto writer = DataWriter();
            writer.WriteByte(counter);  // Just write the counter value
            auto buffer = writer.DetachBuffer();

            // Send the notification
            auto asyncOp = wakeupCharRef->NotifyValueAsync(buffer);

            // Wait for it to complete without blocking indefinitely
            auto status = asyncOp.wait_for(std::chrono::seconds(5));

            if (status == winrt::Windows::Foundation::AsyncStatus::Completed) {
                std::cout << "Wakeup notification sent successfully (value: " << (int)counter << ")" << std::endl;

                // Now wait for client response
                auto startTime = std::chrono::steady_clock::now();

                while (lastClientResponse.load() == ClientResponseType::NONE) {
                    // Check if we've timed out
                    auto currentTime = std::chrono::steady_clock::now();
                    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        currentTime - startTime).count();

                    if (elapsedMs > timeoutMilliseconds) {
                        std::cout << "Timed out waiting for client response after "
                            << timeoutMilliseconds << "ms" << std::endl;
                        break;
                    }

                    // Sleep a bit to avoid spinning the CPU
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                // Done waiting, return the result
                waitingForResponse = false;
                auto response = lastClientResponse.load();
                std::cout << "Client response: " <<
                    (response == ClientResponseType::USE_BLE ? "USE_BLE" :
                        response == ClientResponseType::USE_TCP ? "USE_TCP" : "NONE") << std::endl;

                return response;
            }
            else {
                std::cerr << "Wakeup notification operation timed out or failed" << std::endl;
                waitingForResponse.store(false);
                return ClientResponseType::NONE;
            }
        }
        catch (const winrt::hresult_error& ex) {
            std::cerr << "Failed to send wakeup notification: " << winrt::to_string(ex.message()) << std::endl;
            waitingForResponse.store(false);
            return ClientResponseType::NONE;
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "Exception in sendWakeupAndWaitForResponse: " << ex.what() << std::endl;
        waitingForResponse.store(false);
        return ClientResponseType::NONE;
    }
    catch (...) {
        std::cerr << "Unknown error in sendWakeupAndWaitForResponse" << std::endl;
        waitingForResponse.store(false);
        return ClientResponseType::NONE;
    }
}

void BLEManager::setConnectionCallback(BLEConnectionCallback callback) {
    connectionCallback = callback;
}

void BLEManager::setDataReceivedCallback(BLEDataReceivedCallback callback) {
    dataCallback = callback;
}

void BLEManager::handleCharacteristicWriteRequested(GattLocalCharacteristic sender, GattWriteRequestedEventArgs args) {
    try {
        auto deferral = args.GetDeferral();

        // Use GetRequestAsync instead of GetRequest
        auto requestOperation = args.GetRequestAsync();

        // Register completion handler
        requestOperation.Completed([this, deferral, sender](auto&& reqSender, auto&& args) {
            try {
                // Get the request from the completed operation
                auto request = reqSender.GetResults();

                if (request.Value().Length() > 0) {
                    // Read the data into a byte vector
                    auto reader = DataReader::FromBuffer(request.Value());
                    std::vector<uint8_t> rawData(request.Value().Length());
                    reader.ReadBytes(rawData);

                    std::cout << "Received " << rawData.size() << " bytes via GATT write" << std::endl;

                    // Check if this is a WAKEUP characteristic or DATA characteristic
                    winrt::guid characteristicUuid = sender.Uuid();
                    winrt::guid wakeupUuid(WAKEUP_CHAR_UUID);

                    // If this is the WAKEUP characteristic and we're waiting for a response
                    if (characteristicUuid == wakeupUuid && waitingForResponse.load()) {
                        // Process the response to wakeup
                        if (rawData.size() >= 1) {
                            uint8_t responseCode = rawData[0];

                            if (responseCode == 0x01) {
                                // Client wants
                                // to use BLE
                                lastClientResponse.store(ClientResponseType::USE_BLE);
                                std::cout << "Client responded: Use BLE for data transfer" << std::endl;
                            }
                            else if (responseCode == 0x02) {
                                // Client wants to use TCP
                                lastClientResponse.store(ClientResponseType::USE_TCP);
                                std::cout << "Client responded: Use TCP for data transfer" << std::endl;
                            }
                            else {
                                std::cout << "Unknown client response code: " << (int)responseCode << std::endl;
                            }
                        }
                    }
                    else {
                        // Normal data processing for DATA characteristic
                        // Use a mutex to protect concurrent access during message processing
                        static std::mutex decodeMutex;
                        std::lock_guard<std::mutex> lock(decodeMutex);

                        try {
                            // Try to decode using MessageProtocol
                            auto message = MessageProtocol::decodeData(rawData);
                            if (message) {
                                // If message is complete, process it
                                std::cout << "Decoded complete message from GATT write, content type: "
                                    << static_cast<int>(message->contentType) << std::endl;

                                // Get the binary payload
                                const std::vector<uint8_t>& payload = message->getBinaryPayload();

                                // Make a local copy of the callback to avoid race conditions
                                auto callbackCopy = dataCallback;

                                // Call the callback with both payload and content type
                                if (callbackCopy && !payload.empty()) {
                                    // Consider using a separate thread for callback if it might be long-running
                                    // This prevents blocking the BLE message handler
                                    std::thread([callbackCopy, payload, contentType = message->contentType]() {
                                        try {
                                            callbackCopy(payload, contentType);
                                        }
                                        catch (const std::exception& e) {
                                            std::cerr << "Exception in data callback: " << e.what() << std::endl;
                                        }
                                        catch (...) {
                                            std::cerr << "Unknown exception in data callback" << std::endl;
                                        }
                                        }).detach();

                                    std::cout << "Dispatched callback for received message" << std::endl;
                                }
                                else {
                                    std::cout << "No callback registered or empty payload" << std::endl;
                                }
                            }
                            else {
                                std::cout << "Partial message received, message protocol needs more data" << std::endl;
                                // Note: For partial messages, we would need a more complex buffer management system
                                // This is a limitation of the GATT approach compared to streaming protocols
                            }
                        }
                        catch (const std::exception& e) {
                            std::cerr << "Exception during message decoding: " << e.what() << std::endl;
                        }
                        catch (...) {
                            std::cerr << "Unknown exception during message decoding" << std::endl;
                        }
                    }
                }

                // Respond to confirm receipt if needed
                if (request.Option() == GattWriteOption::WriteWithResponse) {
                    request.Respond();
                }

                deferral.Complete();
            }
            catch (const winrt::hresult_error& ex) {
                std::cerr << "Write request completion error: " << winrt::to_string(ex.message()) << std::endl;
                deferral.Complete();
            }
            });
    }
    catch (const winrt::hresult_error& ex) {
        std::cerr << "Write request handling error: " << winrt::to_string(ex.message()) << std::endl;
    }
}

void BLEManager::handleCharacteristicReadRequested(GattLocalCharacteristic sender, GattReadRequestedEventArgs args) {
    try {
        auto deferral = args.GetDeferral();

        // Use GetRequestAsync instead of GetRequest
        auto requestOperation = args.GetRequestAsync();

        // Register completion handler
        requestOperation.Completed([this, deferral, sender](auto&& sender, auto&& args) {
            try {
                // Get the request from the completed operation
                auto request = sender.GetResults();

                // For read requests, return a simple value
                auto writer = DataWriter();
                writer.WriteByte(0); // Value doesn't matter much for wakeup characteristic

                request.RespondWithValue(writer.DetachBuffer());
                deferral.Complete();
            }
            catch (const winrt::hresult_error& ex) {
                std::cerr << "Read request completion error: " << winrt::to_string(ex.message()) << std::endl;
                deferral.Complete();
            }
            });
    }
    catch (const winrt::hresult_error& ex) {
        std::cerr << "Read request handling error: " << winrt::to_string(ex.message()) << std::endl;
    }
}

bool BLEManager::testEncodeDecodeMessage(const std::string& data) {
    try {
        std::cout << "\n=== Testing Encode/Decode Process ===\n" << std::endl;
        std::cout << "Original data: \"" << data << "\"" << std::endl;
        std::cout << "Length: " << data.length() << " bytes" << std::endl;

        // Convert string to vector of bytes
        std::vector<uint8_t> payload(data.begin(), data.end());

        // Encode using MessageProtocol
        auto encodedChunks = MessageProtocol::encodeMessage(
            MessageContentType::PLAIN_TEXT, payload, TransportType::BLE);

        if (encodedChunks.empty()) {
            std::cerr << "Failed to encode message" << std::endl;
            return false;
        }

        std::cout << "Successfully encoded into " << encodedChunks.size() << " chunks" << std::endl;

        // Print hex representation of the first few bytes of the first chunk
        if (!encodedChunks.empty() && encodedChunks[0].size() > 0) {
            std::cout << "First chunk header bytes: ";
            size_t bytesToPrint = (std::min)(encodedChunks[0].size(), static_cast<size_t>(20));
            for (size_t i = 0; i < bytesToPrint; i++) {
                printf("%02X ", encodedChunks[0][i]);
            }
            std::cout << std::endl;
        }

        // Now test the decoding process
        std::shared_ptr<MessageProtocol::Message> decodedMessage = nullptr;

        // Process each chunk through the decoder
        for (size_t i = 0; i < encodedChunks.size(); i++) {
            std::cout << "Processing chunk " << (i + 1) << "/" << encodedChunks.size()
                << " (" << encodedChunks[i].size() << " bytes)" << std::endl;

            // Try to decode this chunk
            decodedMessage = MessageProtocol::decodeData(encodedChunks[i]);

            if (decodedMessage) {
                std::cout << "Decoding complete after chunk " << (i + 1) << std::endl;
                break;
            }
            else if (i < encodedChunks.size() - 1) {
                std::cout << "Partial message, continuing to next chunk..." << std::endl;
            }
        }

        // Verify decoding succeeded
        if (!decodedMessage) {
            std::cerr << "Failed to decode message" << std::endl;
            return false;
        }

        // Check content type
        if (decodedMessage->contentType != MessageContentType::PLAIN_TEXT) {
            std::cerr << "Decoded message has incorrect content type: "
                << static_cast<int>(decodedMessage->contentType) << std::endl;
            return false;
        }

        // Convert payload back to string and verify contents
        std::string decodedString = decodedMessage->getStringPayload();

        // Print the decoded content with clear markers
        std::cout << "\n======== DECODED MESSAGE CONTENT ========" << std::endl;
        std::cout << decodedString << std::endl;
        std::cout << "========= END DECODED CONTENT ==========" << std::endl;

        std::cout << "Decoded length: " << decodedString.length() << " bytes" << std::endl;

        // Check if original and decoded strings match
        bool matches = (data == decodedString);
        std::cout << "Original and decoded data "
            << (matches ? "MATCH" : "DO NOT MATCH") << std::endl;

        if (!matches) {
            // Print first mismatch location for debugging
            for (size_t i = 0; i < (std::min)(data.length(), decodedString.length()); i++) {
                if (data[i] != decodedString[i]) {
                    std::cout << "First mismatch at position " << i << ": "
                        << "Original '" << data[i] << "' vs Decoded '" << decodedString[i] << "'"
                        << std::endl;
                    break;
                }
            }

            // If lengths differ, report that
            if (data.length() != decodedString.length()) {
                std::cout << "Length mismatch: Original = " << data.length()
                    << ", Decoded = " << decodedString.length() << std::endl;
            }
        }

        std::cout << "\n=== Encode/Decode Test " << (matches ? "PASSED" : "FAILED") << " ===\n" << std::endl;
        return matches;
    }
    catch (const std::exception& ex) {
        std::cerr << "Exception in testEncodeDecodeMessage: " << ex.what() << std::endl;
        return false;
    }
    catch (...) {
        std::cerr << "Unknown error in testEncodeDecodeMessage" << std::endl;
        return false;
    }
}

void BLEManager::determineClientMTU(const GattSession& session) {
    try {
        // Get the max protocol payload size - this is what we can use
        uint16_t maxPayloadSize = session.MaxPduSize();

        // MTU = maxPayloadSize - 3 (ATT header)
        // The ATT protocol overhead is 3 bytes
        auto clientMTU = maxPayloadSize - 3;

        std::cout << "Client connected with MTU: " << clientMTU << " bytes" << std::endl;
        std::cout << "Max PDU Size: " << maxPayloadSize << " bytes" << std::endl;

    }
    catch (const winrt::hresult_error& ex) {
        std::cerr << "Error determining client MTU: " << winrt::to_string(ex.message()) << std::endl;
    }
}

bool BLEManager::sendMessage(const std::vector<uint8_t>& data, MessageContentType contentType) {
    try {
        std::cout << "Sending data via GATT characteristic, type: " << static_cast<int>(contentType)
            << ", length: " << data.size() << " bytes" << std::endl;

        // Store the content for sending to new connections if it's text
        if (contentType == MessageContentType::PLAIN_TEXT) {
            clipboardContent = std::string(data.begin(), data.end());
        }

        // Check if we have a valid data characteristic reference
        if (!dataCharacteristicRef) {
            std::cerr << "No data characteristic available (reference is null)" << std::endl;
            return false;
        }

        // Client capability check - only proceed if hasSubscribedClients is true
        if (!hasSubscribedClients) {
            std::cerr << "No clients subscribed to receive notifications" << std::endl;
            return false;
        }

        // Encode using MessageProtocol - data is already a vector<uint8_t>
        auto encodedChunks = MessageProtocol::encodeMessage(contentType, data, TransportType::BLE);
        if (encodedChunks.empty()) {
            std::cerr << "Failed to encode message" << std::endl;
            return false;
        }

        std::cout << "Encoded into " << encodedChunks.size() << " chunks for BLE transmission" << std::endl;

        // Timer variables
        auto startTime = std::chrono::high_resolution_clock::now();
        double bytesPerSecond = 0;
        size_t totalBytesSent = 0;
        int delayBetweenChunks = 20; // Default delay in ms

        // Flow control - store pending operations
        const int MAX_PENDING_OPS = 3; // Maximum number of pending operations
        std::vector<winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Foundation::Collections::IVectorView<winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattClientNotificationResult>>> pendingOps;
        pendingOps.reserve(MAX_PENDING_OPS);

        // Send each chunk as a separate notification/write
        for (size_t i = 0; i < encodedChunks.size(); i++) {
            // Create a buffer with the chunk data
            auto writer = DataWriter();
            writer.WriteBytes(encodedChunks[i]);
            auto buffer = writer.DetachBuffer();

            // Wait if we've reached max pending operations
            while (pendingOps.size() >= MAX_PENDING_OPS) {
                std::cout << "Flow control: waiting for pending operations to complete..." << std::endl;

                // Check all pending operations
                for (auto it = pendingOps.begin(); it != pendingOps.end(); ) {
                    auto status = it->Status();

                    if (status == winrt::Windows::Foundation::AsyncStatus::Completed ||
                        status == winrt::Windows::Foundation::AsyncStatus::Error ||
                        status == winrt::Windows::Foundation::AsyncStatus::Canceled) {

                        // Operation completed (success or failure)
                        if (status == winrt::Windows::Foundation::AsyncStatus::Completed) {
                            auto results = it->GetResults();
                            bool allSuccess = true;

                            // Check all notification results
                            for (auto result : results) {
                                if (result.Status() != GattCommunicationStatus::Success) {
                                    allSuccess = false;
                                    std::cerr << "Notification failed for client" << std::endl;
                                    break;
                                }
                            }

                            if (!allSuccess) {
                                return false;
                            }
                        }
                        else {
                            std::cerr << "Async operation failed with status: " << static_cast<int>(status) << std::endl;
                            return false;
                        }

                        // Remove this operation from pending list
                        it = pendingOps.erase(it);
                    }
                    else {
                        ++it;
                    }
                }

                // If we still have max pending ops, wait a bit
                if (pendingOps.size() >= MAX_PENDING_OPS) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }

            try {
                // Send the notification
                auto asyncOp = dataCharacteristicRef->NotifyValueAsync(buffer);

                // Add to pending operations list
                pendingOps.push_back(asyncOp);

                // Update metrics
                totalBytesSent += encodedChunks[i].size();

                // Log progress
                std::cout << "Sent chunk " << (i + 1) << "/" << encodedChunks.size()
                    << " (" << encodedChunks[i].size() << " bytes)"
                    << " - " << pendingOps.size() << " pending operations"
                    << std::endl;

                // Calculate transfer speed periodically
                if (i > 0 && i % 5 == 0) {
                    auto currentTime = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        currentTime - startTime).count();

                    if (elapsed > 0) {
                        bytesPerSecond = totalBytesSent * 1000.0 / elapsed;

                        // Adjust delay based on transfer speed
                        if (bytesPerSecond < 5000) {
                            delayBetweenChunks = 50; // Slower for poor connections
                        }
                        else if (bytesPerSecond > 20000) {
                            delayBetweenChunks = 1; // Faster for good connections
                        }
                        else {
                            delayBetweenChunks = 20; // Default
                        }

                        std::cout << "Transfer speed: " << std::fixed << std::setprecision(2)
                            << bytesPerSecond << " bytes/sec | Delay: " << delayBetweenChunks << "ms"
                            << std::endl;
                    }
                }

                // Check if client is still connected
                if (!hasSubscribedClients) {
                    std::cerr << "Client disconnected during transmission" << std::endl;
                    return false;
                }

                // Add delay between chunks
                if (i < encodedChunks.size() - 1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayBetweenChunks));
                }
            }
            catch (const winrt::hresult_error& ex) {
                std::cerr << "Failed to send chunk " << (i + 1) << ": " << winrt::to_string(ex.message()) << std::endl;
                return false;
            }
        }

        // Wait for all remaining operations to complete
        std::cout << "Waiting for " << pendingOps.size() << " remaining operations to complete..." << std::endl;

        const auto waitStartTime = std::chrono::high_resolution_clock::now();
        const auto waitTimeout = std::chrono::seconds(5); // 5-second timeout for final operations

        while (!pendingOps.empty()) {
            // Check if we've exceeded the timeout
            auto currentTime = std::chrono::high_resolution_clock::now();
            if (currentTime - waitStartTime > waitTimeout) {
                std::cerr << "Timed out waiting for final operations to complete" << std::endl;
                break;
            }

            // Process completed operations
            for (auto it = pendingOps.begin(); it != pendingOps.end(); ) {
                auto status = it->Status();

                if (status == winrt::Windows::Foundation::AsyncStatus::Completed ||
                    status == winrt::Windows::Foundation::AsyncStatus::Error ||
                    status == winrt::Windows::Foundation::AsyncStatus::Canceled) {

                    if (status == winrt::Windows::Foundation::AsyncStatus::Completed) {
                        auto results = it->GetResults();
                        bool allSuccess = true;

                        // Check all notification results
                        for (auto result : results) {
                            if (result.Status() != GattCommunicationStatus::Success) {
                                allSuccess = false;
                                std::cerr << "Notification failed for client" << std::endl;
                                break;
                            }
                        }

                        if (!allSuccess) {
                            std::cerr << "Final operation had failed notifications" << std::endl;
                        }
                    }
                    else {
                        std::cerr << "Final operation failed with status: " << static_cast<int>(status) << std::endl;
                    }

                    it = pendingOps.erase(it);
                }
                else {
                    ++it;
                }
            }

            // Short sleep to prevent CPU spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Calculate overall transfer statistics
        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        // Calculate total bytes sent
        size_t totalBytes = 0;
        for (const auto& chunk : encodedChunks) {
            totalBytes += chunk.size();
        }

        double overallBytesPerSecond = (totalDuration > 0) ? (totalBytes * 1000.0 / totalDuration) : 0;

        std::cout << "Data sent successfully via GATT | Total: " << totalBytes << " bytes"
            << " in " << totalDuration << "ms"
            << " (" << std::fixed << std::setprecision(2) << overallBytesPerSecond << " B/s)"
            << std::endl;

        return true;
    }
    catch (const std::exception& ex) {
        std::cerr << "Exception in sendMessage: " << ex.what() << std::endl;
        return false;
    }
    catch (...) {
        std::cerr << "Unknown error in sendMessage" << std::endl;
        return false;
    }
}