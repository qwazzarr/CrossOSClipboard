#include "BLEManager.h"
#include "BLEPayload.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <unordered_set>

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
            // Continue anyway, as we might still be able to advertise with manufacturer data
        }

        // Create our payload
        ClipboardSyncPayload payload;
        payload.deviceName = deviceName;
        payload.version = CLIPBOARD_SYNC_VERSION;
        payload.deviceId = deviceId;

        // First try manufacturer data approach
        BluetoothLEAdvertisement advertisement = BLEPayloadManager::CreateAdvertisement(payload);
        advertisementPublisher = BluetoothLEAdvertisementPublisher(advertisement);

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
            GattCharacteristicProperties::Notify|
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
        if (advertisementPublisher.Status() == BluetoothLEAdvertisementPublisherStatus::Started) {
            return true; // Already advertising
        }

        // Try to start GATT advertising
        if (serviceProvider) {
            try {
                std::cout << "Starting GATT advertising..." << std::endl;
                auto params = GattServiceProviderAdvertisingParameters();
                params.IsDiscoverable(true);
                params.IsConnectable(true);
                serviceProvider.StartAdvertising(params);
                std::cout << "GATT advertising started successfully" << std::endl;
            }
            catch (const winrt::hresult_error& ex) {
                std::cerr << "GATT advertising error: " << winrt::to_string(ex.message()) << std::endl;
                // Continue with regular advertising even if GATT fails
            }
        }

        // Create new payload
        ClipboardSyncPayload payload;
        payload.deviceName = deviceName;
        payload.version = CLIPBOARD_SYNC_VERSION;
        payload.deviceId = deviceId;

        // Try to start advertisement publisher
        try {
            // If we already have a publisher, stop it first
            if (advertisementPublisher) {
                advertisementPublisher.Stop();
            }

            // Create new advertisement
            BluetoothLEAdvertisement advertisement = BLEPayloadManager::CreateAdvertisement(payload);
            advertisementPublisher = BluetoothLEAdvertisementPublisher(advertisement);

            std::cout << "Starting advertisement publisher..." << std::endl;
            advertisementPublisher.Start();
            std::cout << "Advertisement publisher started successfully" << std::endl;
        }
        catch (const winrt::hresult_error& ex) {
            std::cerr << "Advertisement publisher error: " << winrt::to_string(ex.message()) << std::endl;
        }

        std::cout << "BLE advertising started" << std::endl;
        return true;
    }
    catch (const winrt::hresult_error& ex) {
        std::cerr << "BLE advertising error: " << winrt::to_string(ex.message()) << std::endl;
        return false;
    }
}

void BLEManager::stopAdvertising() {
    try {
        if (advertisementPublisher) {
            if (advertisementPublisher.Status() == BluetoothLEAdvertisementPublisherStatus::Started) {
                advertisementPublisher.Stop();
                std::cout << "Advertisement publisher stopped" << std::endl;
            }
        }

        if (serviceProvider) {
            serviceProvider.StopAdvertising();
            std::cout << "GATT advertising stopped" << std::endl;
        }

        std::cout << "BLE advertising stopped" << std::endl;
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
                                // Client wants to use BLE
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
                        // Try to decode using MessageProtocol
                        auto message = MessageProtocol::decodeData(rawData);

                        if (message) {
                            // If message is complete, process it
                            std::cout << "Decoded complete message from GATT write" << std::endl;

                            if (message->contentType == MessageContentType::PLAIN_TEXT) {
                                // For text data, convert to string
                                std::string text = message->getStringPayload();

                                // Process the received text data
                                if (dataCallback && !text.empty()) {
                                    dataCallback(text);
                                }
                            }
                            else {
                                std::cout << "Received non-text data via GATT, content type: "
                                    << static_cast<int>(message->contentType) << std::endl;
                            }
                        }
                        else {
                            std::cout << "Partial message received, message protocol needs more data" << std::endl;
                            // Note: For partial messages, we would need a more complex buffer management system
                            // This is a limitation of the GATT approach compared to streaming protocols
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

bool BLEManager::sendClipboardData(const std::string& data) {
    try {
        std::cout << "Sending clipboard data via GATT characteristic, length: " << data.length() << std::endl;

        // Store the clipboard content for sending to new connections
        clipboardContent = data;

        // Check if we have a valid data characteristic reference
        if (!dataCharacteristicRef) {
            std::cerr << "No data characteristic available (reference is null)" << std::endl;
            return false;
        }

        // Convert string to vector of bytes
        std::vector<uint8_t> payload(data.begin(), data.end());

        // Encode using MessageProtocol
        auto encodedChunks = MessageProtocol::encodeMessage(MessageContentType::PLAIN_TEXT, payload, TransportType::BLE);

        if (encodedChunks.empty()) {
            std::cerr << "Failed to encode message" << std::endl;
            return false;
        }

        std::cout << "Encoded into " << encodedChunks.size() << " chunks for BLE transmission" << std::endl;

        // Send each chunk as a separate notification/write
        for (size_t i = 0; i < encodedChunks.size(); i++) {
            // Create a buffer with the chunk data
            auto writer = DataWriter();
            writer.WriteBytes(encodedChunks[i]);
            auto buffer = writer.DetachBuffer();

            try {
                // Send the notification
                auto asyncOp = dataCharacteristicRef->NotifyValueAsync(buffer);

                // Wait for it to complete without blocking indefinitely
                auto status = asyncOp.wait_for(std::chrono::seconds(1));

                if (status != winrt::Windows::Foundation::AsyncStatus::Completed) {
                    std::cerr << "Chunk " << (i + 1) << "/" << encodedChunks.size() << " send operation failed or timed out" << std::endl;
                    return false;
                }

                std::cout << "Sent chunk " << (i + 1) << "/" << encodedChunks.size() << " (" << encodedChunks[i].size() << " bytes)" << std::endl;

                // Add a small delay between chunks to avoid overwhelming the receiver
                if (i < encodedChunks.size() - 1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }
            catch (const winrt::hresult_error& ex) {
                std::cerr << "Failed to send chunk " << (i + 1) << ": " << winrt::to_string(ex.message()) << std::endl;
                return false;
            }
        }

        std::cout << "Clipboard data sent successfully via GATT" << std::endl;
        return true;
    }
    catch (const std::exception& ex) {
        std::cerr << "Exception in sendClipboardData: " << ex.what() << std::endl;
        return false;
    }
    catch (...) {
        std::cerr << "Unknown error in sendClipboardData" << std::endl;
        return false;
    }
}