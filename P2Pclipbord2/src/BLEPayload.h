#pragma once

#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <string>

using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;

// Define a custom data type for our clipboard sync service
// Choose a value not in the reserved list (0x01-0x3D)
const uint8_t CLIPBOARD_SYNC_DATA_TYPE = 0xA0;

// Version of the clipboard sync protocol
const uint8_t CLIPBOARD_SYNC_VERSION = 1;

// Structure to represent our payload
struct ClipboardSyncPayload {
    std::string deviceName;
    uint8_t version;
    std::string deviceId; // Unique identifier for this device
};

class BLEPayloadManager {
public:
    // Create a Bluetooth LE advertisement with our custom payload
    static BluetoothLEAdvertisement CreateAdvertisement(const ClipboardSyncPayload& payload) {
        BluetoothLEAdvertisement advertisement;

        // Use manufacturer data since it's explicitly allowed
        BluetoothLEManufacturerData manufacturerData;

        // Use Microsoft's company ID (0x0006) or another appropriate ID
        // See https://www.bluetooth.com/specifications/assigned-numbers/company-identifiers/
        manufacturerData.CompanyId(0x0006);

        // Create a data writer to build payload
        DataWriter dataWriter;

        // Format:
        // [1 byte] Magic number to identify our app (0xCS for ClipboardSync)
        // [1 byte] Protocol version
        // [1 byte] Device name length
        // [n bytes] Device name
        // [1 byte] Device ID length
        // [m bytes] Device ID

        // Magic number
        dataWriter.WriteByte(0xC5);

        // Protocol version
        dataWriter.WriteByte(payload.version);

        // Device name
        dataWriter.WriteByte(static_cast<uint8_t>(payload.deviceName.length()));
        dataWriter.WriteString(winrt::to_hstring(payload.deviceName));

        // Device ID
        dataWriter.WriteByte(static_cast<uint8_t>(payload.deviceId.length()));
        dataWriter.WriteString(winrt::to_hstring(payload.deviceId));

        // Set the manufacturer data
        manufacturerData.Data(dataWriter.DetachBuffer());

        // Add the manufacturer data to the advertisement
        advertisement.ManufacturerData().Append(manufacturerData);

        return advertisement;
    }

    // Parse a received advertisement to extract our payload
    static bool TryParseAdvertisement(BluetoothLEAdvertisement advertisement, ClipboardSyncPayload& outPayload) {
        // Look for manufacturer data
        for (auto manufacturerData : advertisement.ManufacturerData()) {
            // Check if this is our app's data (Microsoft company ID)
            if (manufacturerData.CompanyId() == 0x0006) {
                auto data = manufacturerData.Data();

                if (data.Length() < 3) {
                    return false; // Too short to be our format
                }

                // Read the data
                auto reader = DataReader::FromBuffer(data);

                // Check magic number
                uint8_t magicNumber = reader.ReadByte();
                if (magicNumber != 0xC5) {
                    return false; // Not our app
                }

                // Get protocol version
                outPayload.version = reader.ReadByte();

                // Get device name
                uint8_t nameLength = reader.ReadByte();
                auto name = reader.ReadString(nameLength);
                outPayload.deviceName = winrt::to_string(name);

                // Get device ID
                uint8_t idLength = reader.ReadByte();
                auto id = reader.ReadString(idLength);
                outPayload.deviceId = winrt::to_string(id);

                return true;
            }
        }

        // Also try our custom data type approach as a fallback
        for (auto section : advertisement.DataSections()) {
            if (section.DataType() == CLIPBOARD_SYNC_DATA_TYPE) {
                auto data = section.Data();

                if (data.Length() < 3) {
                    return false; // Too short to be our format
                }

                // Read the data
                auto reader = DataReader::FromBuffer(data);

                // Get protocol version
                outPayload.version = reader.ReadByte();

                // Get device name
                uint8_t nameLength = reader.ReadByte();
                auto name = reader.ReadString(nameLength);
                outPayload.deviceName = winrt::to_string(name);

                // Get device ID
                uint8_t idLength = reader.ReadByte();
                auto id = reader.ReadString(idLength);
                outPayload.deviceId = winrt::to_string(id);

                return true;
            }
        }

        return false;
    }

    // Alternative method to create an advertisement using custom data type
    // This is a fallback in case the manufacturer data approach doesn't work
    static BluetoothLEAdvertisement CreateCustomTypeAdvertisement(const ClipboardSyncPayload& payload) {
        BluetoothLEAdvertisement advertisement;

        // Create a data writer to build payload
        DataWriter dataWriter;

        // Format:
        // [1 byte] Protocol version
        // [1 byte] Device name length
        // [n bytes] Device name
        // [1 byte] Device ID length
        // [m bytes] Device ID

        // Protocol version
        dataWriter.WriteByte(payload.version);

        // Device name
        dataWriter.WriteByte(static_cast<uint8_t>(payload.deviceName.length()));
        dataWriter.WriteString(winrt::to_hstring(payload.deviceName));

        // Device ID
        dataWriter.WriteByte(static_cast<uint8_t>(payload.deviceId.length()));
        dataWriter.WriteString(winrt::to_hstring(payload.deviceId));

        // Create the data section with our custom type
        BluetoothLEAdvertisementDataSection customSection;
        customSection.DataType(CLIPBOARD_SYNC_DATA_TYPE);
        customSection.Data(dataWriter.DetachBuffer());

        // Add the custom section to the advertisement
        advertisement.DataSections().Append(customSection);

        return advertisement;
    }
};