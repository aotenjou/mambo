/**
 * Bluetooth Console for Desktop Pet Control (Enhanced with BLE GATT support)
 * Connects to HC-05/HC-06 (Classic SPP) or ECB01H2S (BLE NUS) Bluetooth modules
 *
 * Supports:
 *   - Classic Bluetooth RFCOMM/SPP connection
 *   - BLE GATT connection via Nordic UART Service (NUS)
 *
 * Build: use compile_ble.bat
 */

#define _CRT_SECURE_NO_WARNINGS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2bth.h>
#include <windows.h>
#include <bluetoothapis.h>
#pragma warning(disable: 4995)
#include <objbase.h>
#include <initguid.h>

// WinRT headers
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <conio.h>
#include <algorithm>
#include <map>
#include <cstdio>
#include <condition_variable>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bthprops.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "runtimeobject.lib")

using namespace winrt;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;

// Serial Port Profile UUID (Classic BT)
DEFINE_GUID(GUID_SERIAL_PORT_SERVICE, 0x00001101, 0x0000, 0x1000, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB);

// Nordic UART Service (NUS) UUIDs
static const winrt::guid NUS_SERVICE_UUID  = {0x6E400001, 0xB5A3, 0xF393, {0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E}};
static const winrt::guid NUS_RX_CHAR_UUID  = {0x6E400002, 0xB5A3, 0xF393, {0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E}};
static const winrt::guid NUS_TX_CHAR_UUID  = {0x6E400003, 0xB5A3, 0xF393, {0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E}};

// ECB01H2S custom UUIDs (used by guaguale desktop pet)
static const winrt::guid ECB_SERVICE_UUID  = {0x0000FFF0, 0x0000, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};
static const winrt::guid ECB_RX_CHAR_UUID  = {0x0000FFF2, 0x0000, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};  // Write
static const winrt::guid ECB_TX_CHAR_UUID  = {0x0000FFF1, 0x0000, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};  // Notify

// Device name prefixes
const wchar_t* DEVICE_PREFIX = L"HC-05";
const wchar_t* DEVICE_PREFIX_ALT = L"HC-06";
const wchar_t* DEVICE_PREFIX_PET = L"Pet";
const wchar_t* DEVICE_PREFIX_BLE  = L"guaguale";   // ECB01H2S module name prefix

// Global state
std::atomic<bool> g_running(true);
std::atomic<bool> g_connected(false);

// Connection mode
enum class ConnectionMode { None, ClassicSPP, BLEGATT };
static ConnectionMode g_connMode = ConnectionMode::None;

// Classic BT socket
static SOCKET g_socket = INVALID_SOCKET;

// BLE GATT state
static BluetoothLEDevice g_bleDevice{nullptr};
static GattDeviceService g_nusService{nullptr};
static GattCharacteristic g_rxCharacteristic{nullptr};
static GattCharacteristic g_txCharacteristic{nullptr};
static std::mutex g_bleRxMutex;
static std::condition_variable g_bleRxCv;
static std::vector<uint8_t> g_bleRxBuffer;

// BLE connection completion signal
static std::mutex g_connectMutex;
static std::condition_variable g_connectCv;
static bool g_connectComplete = false;

struct ConnectCompleteGuard {
    ~ConnectCompleteGuard() {
        { std::lock_guard<std::mutex> lk(g_connectMutex); g_connectComplete = true; }
        g_connectCv.notify_one();
    }
};

// BLE pairing completion signal
static std::mutex g_pairMutex;
static std::condition_variable g_pairCv;
static bool g_pairComplete = false;

struct PairCompleteGuard {
    ~PairCompleteGuard() {
        { std::lock_guard<std::mutex> lk(g_pairMutex); g_pairComplete = true; }
        g_pairCv.notify_one();
    }
};

// Command definitions matching hardware
struct CommandInfo {
    uint8_t code;
    const char* name;
    const char* description;
};

const CommandInfo COMMANDS[] = {
    {0x29, "relax",      "Relaxed lay down"},
    {0x30, "sit",        "Sit"},
    {0x31, "upright",    "Stand upright"},
    {0x32, "down",       "Lie down"},
    {0x33, "forward",    "Move forward"},
    {0x34, "back",       "Move backward"},
    {0x35, "left",       "Turn left"},
    {0x36, "right",      "Turn right"},
    {0x37, "swing",      "Swing"},
    {0x38, "speed",      "Change speed"},
    {0x39, "swing_spd",  "Change swing speed"},
    {0x3A, "tail",       "Toggle tail wag"},
    {0x3B, "jump_up",    "Jump up"},
    {0x3C, "jump_down",  "Jump down"},
    {0x3D, "hello",      "Wave hello"},
    {0x3E, "led_on",     "Turn LED on"},
    {0x3F, "led_off",    "Turn LED off"},
    {0x40, "breathe_on", "Turn breathe LED on"},
    {0x41, "breathe_off","Turn breathe LED off"},
    {0x42, "stretch",    "Stretch"},
    {0x43, "l_stretch",  "Left stretch"},
};

const int COMMAND_COUNT = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

// ============================================================
// Unified device info
// ============================================================
enum class DeviceType { Classic, BLE, Watcher };

struct DiscoveredDevice {
    std::wstring name;
    uint64_t address = 0;
    DeviceType type = DeviceType::Classic;
    bool paired = false;
    bool connected = false;
    int rssi = 0;
    bool isClassicSPP = false;
    hstring deviceId;  // WinRT device ID for BLE pairing
};

// ============================================================
// Helpers
// ============================================================
static std::string WToNarrow(const std::wstring& ws) {
    std::string result;
    for (wchar_t c : ws) result += (c < 128) ? (char)c : '?';
    return result;
}

static std::string FormatMACAddress(uint64_t addr) {
    char buf[18];
    sprintf_s(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
        (unsigned char)((addr >> 40) & 0xFF),
        (unsigned char)((addr >> 32) & 0xFF),
        (unsigned char)((addr >> 24) & 0xFF),
        (unsigned char)((addr >> 16) & 0xFF),
        (unsigned char)((addr >> 8) & 0xFF),
        (unsigned char)(addr & 0xFF));
    return std::string(buf);
}

static const char* DeviceTypeToString(DeviceType t) {
    switch (t) {
        case DeviceType::Classic: return "Classic";
        case DeviceType::BLE:     return "BLE";
        case DeviceType::Watcher: return "Watcher";
        default:                  return "Unknown";
    }
}

// ============================================================
// Classic Bluetooth discovery
// ============================================================
std::vector<DiscoveredDevice> DiscoverClassicBluetoothDevices() {
    std::vector<DiscoveredDevice> devices;

    BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams = {};
    searchParams.dwSize = sizeof(searchParams);
    searchParams.fReturnAuthenticated = TRUE;
    searchParams.fReturnRemembered = TRUE;
    searchParams.fReturnUnknown = TRUE;
    searchParams.fReturnConnected = TRUE;
    searchParams.fIssueInquiry = TRUE;
    searchParams.cTimeoutMultiplier = 10;
    searchParams.hRadio = NULL;

    BLUETOOTH_DEVICE_INFO deviceInfo = {};
    deviceInfo.dwSize = sizeof(deviceInfo);

    std::cout << "[DEBUG] Classic BT: Starting BluetoothFindFirstDevice..." << std::endl;
    HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&searchParams, &deviceInfo);
    if (hFind == NULL) {
        DWORD err = GetLastError();
        printf("[WARN] Classic BT: BluetoothFindFirstDevice returned NULL. GetLastError=%lu\n", err);
        return devices;
    }

    int idx = 0;
    do {
        idx++;
        DiscoveredDevice dev;
        dev.name = deviceInfo.szName;
        dev.address = deviceInfo.Address.ullLong;
        dev.type = DeviceType::Classic;
        dev.paired = (deviceInfo.fAuthenticated != 0);
        dev.connected = (deviceInfo.fConnected != 0);
        dev.isClassicSPP = true;

        printf("[DEBUG] Classic BT: [%d] Name=\"%s\" MAC=%s Paired=%d Connected=%d\n",
               idx, WToNarrow(dev.name).c_str(), FormatMACAddress(dev.address).c_str(),
               dev.paired, dev.connected);

        devices.push_back(dev);
    } while (BluetoothFindNextDevice(hFind, &deviceInfo));

    BluetoothFindDeviceClose(hFind);
    printf("[DEBUG] Classic BT: Scan complete, found %zu device(s).\n", devices.size());
    return devices;
}

// ============================================================
// BLE scanner
// ============================================================
struct BLEScanner {
    std::mutex mutex;
    std::map<uint64_t, DiscoveredDevice> devices;
    BluetoothLEAdvertisementWatcher watcher{nullptr};
    std::atomic<bool> scanning{false};
    std::atomic<int> deviceCount{0};

    void Start(int timeoutSeconds = 10) {
        scanning = true;
        deviceCount = 0;
        try {
            watcher = BluetoothLEAdvertisementWatcher();
            watcher.ScanningMode(BluetoothLEScanningMode::Active);

            watcher.Received([this](BluetoothLEAdvertisementWatcher const&,
                                     BluetoothLEAdvertisementReceivedEventArgs const& args) {
                try {
                    uint64_t addr = args.BluetoothAddress();
                    int16_t rssiVal = args.RawSignalStrengthInDBm();
                    std::wstring name;
                    auto localName = args.Advertisement().LocalName();
                    if (!localName.empty()) name = localName.c_str();

                    std::lock_guard<std::mutex> lock(mutex);
                    if (devices.find(addr) == devices.end()) {
                        DiscoveredDevice dev;
                        dev.name = name;
                        dev.address = addr;
                        dev.type = DeviceType::BLE;
                        dev.rssi = rssiVal;
                        devices[addr] = dev;
                        deviceCount++;
                        printf("[DEBUG] BLE: Found new device! MAC=%s Name=\"%s\" RSSI=%d\n",
                               FormatMACAddress(addr).c_str(),
                               name.empty() ? "(unnamed)" : WToNarrow(name).c_str(), (int)rssiVal);
                    }
                } catch (...) {}
            });

            watcher.Start();
        } catch (const winrt::hresult_error& ex) {
            printf("[ERROR] BLE: Failed to start scanner: %s (hr=0x%08X)\n",
                   winrt::to_string(ex.message()).c_str(), (unsigned int)ex.code());
            scanning = false;
        } catch (...) {
            std::cerr << "[ERROR] BLE: Unknown exception starting scanner." << std::endl;
            scanning = false;
        }
    }

    void Stop() {
        if (watcher) {
            try { watcher.Stop(); } catch (...) {}
        }
        scanning = false;
    }

    std::vector<DiscoveredDevice> GetDevices() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<DiscoveredDevice> result;
        for (auto& kv : devices) result.push_back(kv.second);
        return result;
    }
};

// ============================================================
// All BT DeviceWatcher (WinRT)
// ============================================================
struct AllDeviceScanner {
    std::mutex mutex;
    std::map<std::wstring, DiscoveredDevice> devices;
    DeviceWatcher watcher{nullptr};
    std::atomic<bool> enumerationCompleted{false};
    std::atomic<int> addedCount{0};
    std::atomic<int> updatedCount{0};
    bool started = false;

    void Start(int timeoutSeconds = 12) {
        enumerationCompleted = false;
        addedCount = 0;
        updatedCount = 0;

        try {
            hstring selector = BluetoothDevice::GetDeviceSelector();
            printf("[DEBUG] Watcher: Using BluetoothDevice selector.\n");

            std::vector<hstring> properties = {
                L"System.Devices.Aep.DeviceAddress",
                L"System.Devices.Aep.IsConnected",
                L"System.Devices.Aep.IsPaired",
                L"System.Devices.Aep.SignalStrength",
                L"System.DeviceInterface.Bluetooth.DeviceAddress",
                L"System.Devices.Aep.Bluetooth.Cod",
            };

            watcher = DeviceInformation::CreateWatcher(
                selector,
                winrt::single_threaded_vector<hstring>(std::move(properties)).GetView()
            );

            watcher.Added([this](DeviceWatcher const&, DeviceInformation const& info) {
                try {
                    std::lock_guard<std::mutex> lock(mutex);
                    DiscoveredDevice dev;
                    dev.name = std::wstring(info.Name().c_str());
                    dev.type = DeviceType::Watcher;
                    dev.deviceId = info.Id();

                    auto props = info.Properties();
                    bool gotAddress = false;
                    try {
                        auto addrObj = props.Lookup(L"System.DeviceInterface.Bluetooth.DeviceAddress");
                        if (addrObj) {
                            auto addrStr = winrt::unbox_value<hstring>(addrObj);
                            dev.address = ParseBTAddress(std::wstring(addrStr.c_str()));
                            if (dev.address != 0) gotAddress = true;
                        }
                    } catch (...) {}
                    if (!gotAddress) {
                        try {
                            auto addrObj = props.Lookup(L"System.Devices.Aep.DeviceAddress");
                            if (addrObj) {
                                auto addrStr = winrt::unbox_value<hstring>(addrObj);
                                dev.address = ParseBTAddress(std::wstring(addrStr.c_str()));
                            }
                        } catch (...) {}
                    }
                    try { auto p = props.Lookup(L"System.Devices.Aep.IsPaired"); if (p) dev.paired = winrt::unbox_value<bool>(p); } catch (...) {}
                    try { dev.paired = info.Pairing().IsPaired(); } catch (...) {}
                    try { auto p = props.Lookup(L"System.Devices.Aep.IsConnected"); if (p) dev.connected = winrt::unbox_value<bool>(p); } catch (...) {}
                    try { auto p = props.Lookup(L"System.Devices.Aep.SignalStrength"); if (p) dev.rssi = (int)winrt::unbox_value<int16_t>(p); } catch (...) {}

                    std::wstring id = std::wstring(info.Id().c_str());
                    devices[id] = dev;
                    addedCount++;

                    printf("[DEBUG] Watcher: Added device #%d: Name=\"%s\" MAC=%s Paired=%d Connected=%d\n",
                           (int)addedCount, dev.name.empty() ? "(unnamed)" : WToNarrow(dev.name).c_str(),
                           FormatMACAddress(dev.address).c_str(), dev.paired, dev.connected);
                } catch (const winrt::hresult_error& ex) {
                    printf("[WARN] Watcher: Added callback error: %s\n", winrt::to_string(ex.message()).c_str());
                } catch (...) {}
            });

            watcher.Updated([this](DeviceWatcher const&, DeviceInformationUpdate const& update) {
                try {
                    std::lock_guard<std::mutex> lock(mutex);
                    std::wstring id = std::wstring(update.Id().c_str());
                    auto it = devices.find(id);
                    if (it != devices.end()) {
                        auto props = update.Properties();
                        try { auto p = props.Lookup(L"System.Devices.Aep.IsPaired"); if (p) it->second.paired = winrt::unbox_value<bool>(p); } catch (...) {}
                        try { auto p = props.Lookup(L"System.Devices.Aep.IsConnected"); if (p) it->second.connected = winrt::unbox_value<bool>(p); } catch (...) {}
                        try { auto p = props.Lookup(L"System.Devices.Aep.SignalStrength"); if (p) it->second.rssi = (int)winrt::unbox_value<int16_t>(p); } catch (...) {}
                        updatedCount++;
                    }
                } catch (...) {}
            });

            watcher.Removed([this](DeviceWatcher const&, DeviceInformationUpdate const& update) {
                try {
                    std::lock_guard<std::mutex> lock(mutex);
                    devices.erase(std::wstring(update.Id().c_str()));
                } catch (...) {}
            });

            watcher.EnumerationCompleted([this](DeviceWatcher const&, Windows::Foundation::IInspectable const&) {
                enumerationCompleted = true;
                printf("[DEBUG] Watcher: Enumeration completed. Total added=%d, updated=%d\n",
                       (int)addedCount, (int)updatedCount);
            });

            watcher.Stopped([this](DeviceWatcher const&, Windows::Foundation::IInspectable const&) {
                printf("[DEBUG] Watcher: Stopped event received.\n");
            });

            watcher.Start();
            started = true;
            printf("[DEBUG] Watcher: Started successfully.\n");

        } catch (const winrt::hresult_error& ex) {
            printf("[ERROR] Watcher: Failed to start: %s (hr=0x%08X)\n",
                   winrt::to_string(ex.message()).c_str(), (unsigned int)ex.code());
        } catch (const std::exception& ex) {
            printf("[ERROR] Watcher: std::exception: %s\n", ex.what());
        } catch (...) {
            std::cerr << "[ERROR] Watcher: Unknown exception starting watcher." << std::endl;
        }
    }

    void Stop() {
        if (watcher && started) {
            try {
                auto status = watcher.Status();
                if (status == DeviceWatcherStatus::Started || status == DeviceWatcherStatus::EnumerationCompleted)
                    watcher.Stop();
            } catch (...) {}
        }
    }

    std::vector<DiscoveredDevice> GetDevices() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<DiscoveredDevice> result;
        for (auto& kv : devices) result.push_back(kv.second);
        return result;
    }

    static uint64_t ParseBTAddress(const std::wstring& addrStr) {
        uint64_t addr = 0;
        std::wstring clean;
        for (wchar_t c : addrStr) if (c != L':' && c != L'-') clean += c;
        if (clean.length() == 12) {
            for (int i = 0; i < 12; i++) {
                int val = 0; wchar_t c = clean[i];
                if (c >= L'0' && c <= L'9') val = c - L'0';
                else if (c >= L'A' && c <= L'F') val = c - L'A' + 10;
                else if (c >= L'a' && c <= L'f') val = c - L'a' + 10;
                else return 0;
                addr = (addr << 4) | val;
            }
        }
        return addr;
    }
};

// ============================================================
// Print devices
// ============================================================
void PrintDevices(const std::vector<DiscoveredDevice>& devices) {
    std::cout << "\n----------------------------------------" << std::endl;
    for (size_t i = 0; i < devices.size(); i++) {
        const auto& dev = devices[i];
        std::string mac = FormatMACAddress(dev.address);
        std::string nameStr = dev.name.empty() ? "(unnamed)" : WToNarrow(dev.name);
        printf("[%zu] %s\n", i + 1, nameStr.c_str());
        printf("    MAC: %s | Type: %s | Connected: %s | Paired: %s",
               mac.c_str(), DeviceTypeToString(dev.type),
               dev.connected ? "Yes" : "No", dev.paired ? "Yes" : "No");
        if (dev.rssi != 0) printf(" | RSSI: %d dBm", dev.rssi);
        printf("\n");
    }
    std::cout << "----------------------------------------" << std::endl;
}

// ============================================================
// Device selection
// ============================================================
DiscoveredDevice* SelectDevice(std::vector<DiscoveredDevice>& devices) {
    for (auto& dev : devices) {
        if (dev.name.find(DEVICE_PREFIX) != std::wstring::npos ||
            dev.name.find(DEVICE_PREFIX_ALT) != std::wstring::npos ||
            dev.name.find(DEVICE_PREFIX_PET) != std::wstring::npos ||
            dev.name.find(DEVICE_PREFIX_BLE) != std::wstring::npos) {
            printf("\n[INFO] Auto-detected target device: %s\n", WToNarrow(dev.name).c_str());
            std::cout << "[INFO] Press Enter to use this device, or type a number to select another: ";
            std::string input;
            std::getline(std::cin, input);
            if (input.empty()) return &dev;
            int index = atoi(input.c_str());
            if (index >= 1 && index <= (int)devices.size()) return &devices[index - 1];
            return &dev;
        }
    }
    std::cout << "\n[INFO] Auto-detect failed. Please select a device by number: ";
    std::string input;
    std::getline(std::cin, input);
    int index = atoi(input.c_str());
    if (index >= 1 && index <= (int)devices.size()) return &devices[index - 1];
    return nullptr;
}

// ============================================================
// Classic BT pairing & connection
// ============================================================
bool PairWithDevice(uint64_t address, const wchar_t* passkey = L"1234") {
    BLUETOOTH_DEVICE_INFO deviceInfo = {};
    deviceInfo.dwSize = sizeof(deviceInfo);
    deviceInfo.Address.ullLong = address;
    DWORD result = BluetoothGetDeviceInfo(NULL, &deviceInfo);
    if (result != ERROR_SUCCESS) {
        std::cerr << "[ERROR] Could not get device info for pairing. Error: " << result << std::endl;
        return false;
    }
    if (deviceInfo.fAuthenticated) {
        std::cout << "[INFO] Device is already paired." << std::endl;
        return true;
    }
    result = BluetoothAuthenticateDevice(NULL, NULL, &deviceInfo, (LPWSTR)passkey, (DWORD)wcslen(passkey));
    if (result == ERROR_SUCCESS) { std::cout << "[INFO] Paired successfully!" << std::endl; return true; }
    std::cerr << "[ERROR] Pairing failed with error code: " << result << std::endl;
    return false;
}

bool ConnectClassicSPP(uint64_t address) {
    g_socket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (g_socket == INVALID_SOCKET) {
        std::cerr << "[ERROR] Failed to create socket. Error: " << WSAGetLastError() << std::endl;
        return false;
    }
    SOCKADDR_BTH sockAddr = {};
    sockAddr.addressFamily = AF_BTH;
    sockAddr.btAddr = address;
    sockAddr.serviceClassId = GUID_SERIAL_PORT_SERVICE;
    sockAddr.port = 0;
    std::cout << "[INFO] Attempting Classic SPP connection to " << FormatMACAddress(address) << "..." << std::endl;
    int result = connect(g_socket, (SOCKADDR*)&sockAddr, sizeof(sockAddr));
    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEADDRNOTAVAIL || err == WSAETIMEDOUT) {
            std::cout << "[INFO] SDP lookup failed, trying RFCOMM channel 1..." << std::endl;
            sockAddr.port = 1;
            sockAddr.serviceClassId = GUID_NULL;
            result = connect(g_socket, (SOCKADDR*)&sockAddr, sizeof(sockAddr));
            if (result == SOCKET_ERROR) {
                std::cerr << "[ERROR] Connection failed. Error: " << WSAGetLastError() << std::endl;
                closesocket(g_socket); g_socket = INVALID_SOCKET; return false;
            }
        } else {
            std::cerr << "[ERROR] Connection failed. Error: " << err << std::endl;
            closesocket(g_socket); g_socket = INVALID_SOCKET; return false;
        }
    }
    return true;
}

// ============================================================
// BLE GATT connection via Nordic UART Service (NUS)
// ============================================================
fire_and_forget ConnectBLEGatt(uint64_t address) {
    ConnectCompleteGuard connectGuard;  // signals completion on any exit
    printf("[INFO] Connecting to BLE device at %s via GATT...\n", FormatMACAddress(address).c_str());

    try {
        // Step 1: Get BLE device from address
        printf("[DEBUG] BLE GATT: Calling FromBluetoothAddressAsync...\n");
        g_bleDevice = co_await BluetoothLEDevice::FromBluetoothAddressAsync(address);
        if (!g_bleDevice) {
            std::cerr << "[ERROR] BLE GATT: FromBluetoothAddressAsync returned null." << std::endl;
            g_connected = false;
            co_return;
        }
        printf("[DEBUG] BLE GATT: Got device: %ls\n", g_bleDevice.Name().c_str());
        printf("[DEBUG] BLE GATT: DeviceId: %ls\n", g_bleDevice.DeviceId().c_str());

        // Check connection status
        auto connStatus = g_bleDevice.ConnectionStatus();
        printf("[DEBUG] BLE GATT: Connection status = %d\n", (int)connStatus);

        // Step 2: Get GATT services
        printf("[DEBUG] BLE GATT: Getting GATT services...\n");
        auto servicesResult = co_await g_bleDevice.GetGattServicesAsync(BluetoothCacheMode::Uncached);
        if (servicesResult.Status() != GattCommunicationStatus::Success) {
            printf("[ERROR] BLE GATT: GetGattServicesAsync failed with status %d\n", (int)servicesResult.Status());
            g_connected = false;
            co_return;
        }

        auto services = servicesResult.Services();
        printf("[DEBUG] BLE GATT: Found %d service(s).\n", (int)services.Size());

        // Step 3: Find target service (NUS or ECB01H2S custom)
        for (uint32_t i = 0; i < services.Size(); i++) {
            auto svc = services.GetAt(i);
            auto uuid = svc.Uuid();
            printf("[DEBUG] BLE GATT: Service[%u] UUID = %08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\n",
                   i, uuid.Data1, uuid.Data2, uuid.Data3,
                   uuid.Data4[0], uuid.Data4[1], uuid.Data4[2], uuid.Data4[3],
                   uuid.Data4[4], uuid.Data4[5], uuid.Data4[6], uuid.Data4[7]);

            if (uuid == NUS_SERVICE_UUID) {
                printf("[INFO] BLE GATT: *** Found Nordic UART Service (NUS)! ***\n");
                g_nusService = svc;
            } else if (uuid == ECB_SERVICE_UUID) {
                printf("[INFO] BLE GATT: *** Found ECB01H2S custom service (FFF0)! ***\n");
                g_nusService = svc;
            }
        }

        // If no known service found, try UUID lookups
        if (!g_nusService) {
            printf("[DEBUG] BLE GATT: Known service not found, trying UUID lookups...\n");

            auto nusResult = co_await g_bleDevice.GetGattServicesForUuidAsync(NUS_SERVICE_UUID, BluetoothCacheMode::Uncached);
            if (nusResult.Status() == GattCommunicationStatus::Success && nusResult.Services().Size() > 0) {
                g_nusService = nusResult.Services().GetAt(0);
                printf("[INFO] BLE GATT: Found NUS service via UUID lookup!\n");
            }
        }
        if (!g_nusService) {
            auto ecbResult = co_await g_bleDevice.GetGattServicesForUuidAsync(ECB_SERVICE_UUID, BluetoothCacheMode::Uncached);
            if (ecbResult.Status() == GattCommunicationStatus::Success && ecbResult.Services().Size() > 0) {
                g_nusService = ecbResult.Services().GetAt(0);
                printf("[INFO] BLE GATT: Found ECB01H2S service via UUID lookup!\n");
            }
        }

        // If still no service found, try the first non-GAP/non-GATT service as fallback
        if (!g_nusService) {
            printf("[WARN] BLE GATT: No known service UUID found. Trying first usable service as fallback...\n");
            for (uint32_t i = 0; i < services.Size(); i++) {
                auto svc = services.GetAt(i);
                auto uuid = svc.Uuid();
                // Skip GAP (1800) and GATT (1801) and other standard services
                if (uuid.Data1 != 0x00001800 && uuid.Data1 != 0x00001801 &&
                    uuid.Data1 != 0x0000180A && uuid.Data1 != 0x0000180F) {
                    g_nusService = svc;
                    printf("[INFO] BLE GATT: Using fallback service UUID=%08X-%04X-%04X\n",
                           uuid.Data1, uuid.Data2, uuid.Data3);
                    break;
                }
            }
        }

        if (!g_nusService) {
            printf("[ERROR] BLE GATT: No usable GATT service found!\n");
            g_connected = false;
            co_return;
        }

        // Step 4: Get characteristics
        printf("[DEBUG] BLE GATT: Getting characteristics for service...\n");
        auto charsResult = co_await g_nusService.GetCharacteristicsAsync(BluetoothCacheMode::Uncached);
        if (charsResult.Status() != GattCommunicationStatus::Success) {
            printf("[ERROR] BLE GATT: GetCharacteristicsAsync failed with status %d\n", (int)charsResult.Status());
            g_connected = false;
            co_return;
        }

        auto characteristics = charsResult.Characteristics();
        printf("[DEBUG] BLE GATT: Service has %d characteristic(s).\n", (int)characteristics.Size());

        // First pass: match by known UUIDs
        for (uint32_t i = 0; i < characteristics.Size(); i++) {
            auto ch = characteristics.GetAt(i);
            auto uuid = ch.Uuid();
            auto props = ch.CharacteristicProperties();
            printf("[DEBUG] BLE GATT:   Char[%u] UUID = %08X-%04X-%04X  Properties=0x%02X\n",
                   i, uuid.Data1, uuid.Data2, uuid.Data3, (unsigned)props);

            // NUS UUIDs
            if (uuid == NUS_RX_CHAR_UUID) {
                g_rxCharacteristic = ch;
                printf("[INFO] BLE GATT: Found RX characteristic (NUS, write to device)\n");
            } else if (uuid == NUS_TX_CHAR_UUID) {
                g_txCharacteristic = ch;
                printf("[INFO] BLE GATT: Found TX characteristic (NUS, receive from device)\n");
            }
            // ECB01H2S UUIDs
            else if (uuid == ECB_RX_CHAR_UUID) {
                g_rxCharacteristic = ch;
                printf("[INFO] BLE GATT: Found RX characteristic (ECB01H2S FFF2, write to device)\n");
            } else if (uuid == ECB_TX_CHAR_UUID) {
                g_txCharacteristic = ch;
                printf("[INFO] BLE GATT: Found TX characteristic (ECB01H2S FFF1, receive from device)\n");
            }
        }

        // Fallback: identify RX/TX by properties if UUIDs didn't match
        if (!g_rxCharacteristic || !g_txCharacteristic) {
            printf("[INFO] BLE GATT: Identifying RX/TX by characteristic properties...\n");
            for (uint32_t i = 0; i < characteristics.Size(); i++) {
                auto ch = characteristics.GetAt(i);
                auto uuid = ch.Uuid();
                auto props = ch.CharacteristicProperties();

                uint32_t propsVal = (uint32_t)props;
                bool canWrite = (propsVal & 0x08) || (propsVal & 0x04);   // Write=0x08, WriteWithoutResponse=0x04
                bool canNotify = (propsVal & 0x10) || (propsVal & 0x20);  // Notify=0x10, Indicate=0x20

                if (!g_rxCharacteristic && canWrite && !canNotify) {
                    g_rxCharacteristic = ch;
                    printf("[INFO] BLE GATT: Auto-detected RX (write) char: %08X-%04X-%04X Properties=0x%02X\n",
                           uuid.Data1, uuid.Data2, uuid.Data3, (unsigned)props);
                }
                if (!g_txCharacteristic && canNotify) {
                    g_txCharacteristic = ch;
                    printf("[INFO] BLE GATT: Auto-detected TX (notify) char: %08X-%04X-%04X Properties=0x%02X\n",
                           uuid.Data1, uuid.Data2, uuid.Data3, (unsigned)props);
                }
            }
        }

        // Step 5: Subscribe to TX notifications
        if (g_txCharacteristic) {
            printf("[DEBUG] BLE GATT: Subscribing to TX notifications...\n");

            g_txCharacteristic.ValueChanged([](GattCharacteristic const&, GattValueChangedEventArgs const& args) {
                auto data = args.CharacteristicValue();
                auto len = data.Length();
                auto reader = DataReader::FromBuffer(data);
                std::vector<uint8_t> bytes(len);
                reader.ReadBytes(winrt::array_view<uint8_t>(bytes));

                {
                    std::lock_guard<std::mutex> lock(g_bleRxMutex);
                    for (auto b : bytes) g_bleRxBuffer.push_back(b);
                }
                g_bleRxCv.notify_one();

                // Also print to console
                std::cout << "\n[RECV] ";
                for (auto b : bytes) printf("0x%02X ", b);
                std::cout << std::endl << "> " << std::flush;
            });

            auto cccdResult = co_await g_txCharacteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify);

            if (cccdResult != GattCommunicationStatus::Success) {
                printf("[WARN] BLE GATT: Failed to enable TX notifications (status=%d). Trying Indicate...\n", (int)cccdResult);
                cccdResult = co_await g_txCharacteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue::Indicate);
                if (cccdResult != GattCommunicationStatus::Success) {
                    printf("[ERROR] BLE GATT: Failed to enable TX indications too (status=%d).\n", (int)cccdResult);
                }
            } else {
                printf("[INFO] BLE GATT: TX notifications enabled successfully.\n");
            }
        } else {
            std::cerr << "[WARN] BLE GATT: TX characteristic not found. Will not receive data from device." << std::endl;
        }

        if (g_rxCharacteristic) {
            printf("[INFO] BLE GATT: Ready to send commands via RX characteristic.\n");
        } else {
            std::cerr << "[ERROR] BLE GATT: RX characteristic not found! Cannot send commands." << std::endl;
            g_connected = false;
            co_return;
        }

        g_connMode = ConnectionMode::BLEGATT;
        g_connected = true;
        printf("[INFO] BLE GATT: Connection established successfully!\n");

    } catch (const winrt::hresult_error& ex) {
        printf("[ERROR] BLE GATT: %s (hr=0x%08X)\n", winrt::to_string(ex.message()).c_str(), (unsigned int)ex.code());
        g_connected = false;
    } catch (const std::exception& ex) {
        printf("[ERROR] BLE GATT: std::exception: %s\n", ex.what());
        g_connected = false;
    } catch (...) {
        std::cerr << "[ERROR] BLE GATT: Unknown exception." << std::endl;
        g_connected = false;
    }
}

// ============================================================
// Send command (unified)
// ============================================================
bool SendCommand(uint8_t cmd) {
    if (!g_connected) {
        std::cerr << "[ERROR] Not connected to device." << std::endl;
        return false;
    }

    if (g_connMode == ConnectionMode::ClassicSPP) {
        if (g_socket == INVALID_SOCKET) {
            std::cerr << "[ERROR] Socket is invalid." << std::endl;
            return false;
        }
        int sent = send(g_socket, (const char*)&cmd, 1, 0);
        if (sent == SOCKET_ERROR) {
            int err = WSAGetLastError();
            std::cerr << "[ERROR] Send failed. Error: " << err << std::endl;
            if (err == WSAECONNRESET || err == WSAENOTCONN) {
                g_connected = false;
                std::cerr << "[ERROR] Connection lost." << std::endl;
            }
            return false;
        }
    } else if (g_connMode == ConnectionMode::BLEGATT) {
        if (!g_rxCharacteristic) {
            std::cerr << "[ERROR] BLE RX characteristic not available." << std::endl;
            return false;
        }
        try {
            DataWriter writer;
            writer.WriteByte(cmd);
            auto buffer = writer.DetachBuffer();

            auto writeResult = g_rxCharacteristic.WriteValueAsync(buffer).get();
            if (writeResult != GattCommunicationStatus::Success) {
                printf("[ERROR] BLE GATT: Write failed with status %d\n", (int)writeResult);
                return false;
            }
        } catch (const winrt::hresult_error& ex) {
            printf("[ERROR] BLE GATT: Write exception: %s\n", winrt::to_string(ex.message()).c_str());
            return false;
        }
    }

    printf("[SENT] Command 0x%02X (%d)\n", cmd, cmd);
    return true;
}

// ============================================================
// Disconnect
// ============================================================
void Disconnect() {
    if (g_connMode == ConnectionMode::ClassicSPP && g_socket != INVALID_SOCKET) {
        shutdown(g_socket, SD_BOTH);
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
    }
    if (g_connMode == ConnectionMode::BLEGATT) {
        try {
            if (g_txCharacteristic) {
                g_txCharacteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue::None).get();
            }
        } catch (...) {}
        if (g_bleDevice) {
            g_bleDevice.Close();
            g_bleDevice = nullptr;
        }
        g_nusService = nullptr;
        g_rxCharacteristic = nullptr;
        g_txCharacteristic = nullptr;
    }
    g_connMode = ConnectionMode::None;
    g_connected = false;
    std::cout << "[INFO] Disconnected from device." << std::endl;
}

// ============================================================
// Classic BT receive thread
// ============================================================
void ReceiveThreadClassic() {
    char buffer[256];
    fd_set readSet;
    timeval timeout;
    while (g_running && g_connected) {
        FD_ZERO(&readSet);
        FD_SET(g_socket, &readSet);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        int sel = select(0, &readSet, NULL, NULL, &timeout);
        if (sel > 0 && FD_ISSET(g_socket, &readSet)) {
            int received = recv(g_socket, buffer, sizeof(buffer) - 1, 0);
            if (received > 0) {
                std::cout << "\n[RECV] ";
                for (int i = 0; i < received; i++) printf("0x%02X ", (unsigned char)buffer[i]);
                std::cout << std::endl << "> " << std::flush;
            } else if (received == 0) {
                std::cout << "\n[INFO] Connection closed by device." << std::endl;
                g_connected = false; break;
            } else {
                int err = WSAGetLastError();
                if (err != WSAETIMEDOUT && err != WSAEWOULDBLOCK) {
                    std::cout << "\n[ERROR] Receive error: " << err << std::endl;
                    g_connected = false; break;
                }
            }
        }
    }
}

// ============================================================
// Command parsing
// ============================================================
uint8_t ParseHexCommand(const std::string& input) {
    std::string cmd = input;
    if (cmd.length() > 2 && (cmd.substr(0, 2) == "0x" || cmd.substr(0, 2) == "0X"))
        cmd = cmd.substr(2);
    unsigned int value;
    if (sscanf(cmd.c_str(), "%x", &value) == 1) return (uint8_t)(value & 0xFF);
    return 0xFF;
}

int FindCommandByName(const std::string& name) {
    for (int i = 0; i < COMMAND_COUNT; i++)
        if (_stricmp(name.c_str(), COMMANDS[i].name) == 0) return i;
    return -1;
}

// ============================================================
// Interactive mode
// ============================================================
void InteractiveMode() {
    std::cout << "\n[INFO] Entering interactive mode. Type 'help' for commands." << std::endl;
    std::cout << "[INFO] Connection mode: " <<
        (g_connMode == ConnectionMode::ClassicSPP ? "Classic SPP" :
         g_connMode == ConnectionMode::BLEGATT ? "BLE GATT (NUS)" : "None") << std::endl;
    std::cout << "> " << std::flush;

    std::string input;
    while (g_running && g_connected) {
        if (!std::getline(std::cin, input)) break;
        size_t start = input.find_first_not_of(" \t\r\n");
        size_t end = input.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) { std::cout << "> " << std::flush; continue; }
        input = input.substr(start, end - start + 1);

        if (input.empty()) {
        } else if (input == "quit" || input == "exit" || input == "q") {
            std::cout << "[INFO] Exiting..." << std::endl; break;
        } else if (input == "help" || input == "h" || input == "?") {
            std::cout << "\n========== Command Reference ==========" << std::endl;
            std::cout << "Available commands (0x29 - 0x43):\n" << std::endl;
            for (int i = 0; i < COMMAND_COUNT; i++)
                printf("  0x%02X %-12s - %s\n", COMMANDS[i].code, COMMANDS[i].name, COMMANDS[i].description);
            std::cout << "\nSpecial commands:" << std::endl;
            std::cout << "  help     - Show this help" << std::endl;
            std::cout << "  list     - List available commands" << std::endl;
            std::cout << "  quit     - Disconnect and exit" << std::endl;
            std::cout << "  status   - Show connection status" << std::endl;
            std::cout << "  <hex>    - Send hex command (e.g., 0x29, 29)" << std::endl;
            std::cout << "  <name>   - Send command by name (e.g., sit, forward)" << std::endl;
            std::cout << "========================================\n" << std::endl;
        } else if (input == "list" || input == "commands") {
            std::cout << "\nAvailable commands:" << std::endl;
            for (int i = 0; i < COMMAND_COUNT; i++)
                printf("  %-12s (0x%02X)\n", COMMANDS[i].name, COMMANDS[i].code);
            std::cout << std::endl;
        } else if (input == "status") {
            std::cout << "\nConnection Status:" << std::endl;
            std::cout << "  Mode: " << (g_connMode == ConnectionMode::ClassicSPP ? "Classic SPP" :
                                          g_connMode == ConnectionMode::BLEGATT ? "BLE GATT (NUS)" : "None") << std::endl;
            std::cout << "  Connected: " << (g_connected ? "Yes" : "No") << std::endl;
            if (g_connMode == ConnectionMode::ClassicSPP)
                std::cout << "  Socket: " << (g_socket != INVALID_SOCKET ? "Valid" : "Invalid") << std::endl;
            if (g_connMode == ConnectionMode::BLEGATT) {
                std::cout << "  BLE RX Char: " << (g_rxCharacteristic ? "Available" : "Missing") << std::endl;
                std::cout << "  BLE TX Char: " << (g_txCharacteristic ? "Available" : "Missing") << std::endl;
            }
        } else if (input == "disconnect") {
            Disconnect(); break;
        } else {
            uint8_t cmd = 0xFF;
            int cmdIndex = FindCommandByName(input);
            if (cmdIndex >= 0) {
                cmd = COMMANDS[cmdIndex].code;
                printf("[INFO] Sending command '%s' (0x%02X)\n", COMMANDS[cmdIndex].name, cmd);
            } else {
                cmd = ParseHexCommand(input);
                if (cmd >= 0x29 && cmd <= 0x49) {
                    printf("[INFO] Sending hex command 0x%02X\n", cmd);
                } else {
                    std::cout << "[WARN] Unknown command: " << input << std::endl;
                    std::cout << "[INFO] Valid range: 0x29-0x49, or use command names." << std::endl;
                    std::cout << "> " << std::flush; continue;
                }
            }
            if (cmd != 0xFF) SendCommand(cmd);
        }
        if (g_connected) std::cout << "> " << std::flush;
    }
}

// ============================================================
// Banner
// ============================================================
void PrintBanner() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Desktop Pet Bluetooth Controller" << std::endl;
    std::cout << "  Target: HC-05/HC-06 (Classic SPP)" << std::endl;
    std::cout << "           ECB01H2S (BLE NUS)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
}

// BLE pairing helper (async)
fire_and_forget PairBLEDevice(uint64_t address, bool alreadyPaired) {
    PairCompleteGuard pairGuard;  // signals completion on any exit
    if (alreadyPaired) {
        printf("[INFO] Device is already paired.\n");
        co_return;
    }
    printf("[INFO] Device is not paired. Attempting to pair via WinRT...\n");
    try {
        // Use BluetoothLEDevice to get DeviceId, then pair
        printf("[DEBUG] Pairing: Creating BLE device from address to get DeviceId...\n");
        auto bleDev = co_await BluetoothLEDevice::FromBluetoothAddressAsync(address);
        if (!bleDev) {
            printf("[WARN] Pairing: Could not create BLE device for pairing. Trying connection anyway...\n");
            co_return;
        }
        hstring deviceId = bleDev.DeviceId();
        printf("[DEBUG] Pairing: DeviceId = %ls\n", deviceId.c_str());

        auto devInfo = co_await DeviceInformation::CreateFromIdAsync(deviceId);
        if (devInfo) {
            // Register a custom pairing handler to accept pairing automatically
            printf("[DEBUG] Pairing: Starting PairAsync...\n");
            auto pairResult = co_await devInfo.Pairing().PairAsync();
            printf("[INFO] Pairing result: status=%d\n", (int)pairResult.Status());
            if (pairResult.Status() == DevicePairingResultStatus::Paired ||
                pairResult.Status() == DevicePairingResultStatus::AlreadyPaired) {
                printf("[INFO] Device paired successfully!\n");
            } else {
                printf("[WARN] Pairing status: %d. Trying connection anyway...\n", (int)pairResult.Status());
            }
        } else {
            printf("[WARN] Pairing: Could not get DeviceInformation. Trying connection without pairing...\n");
        }
        // Close the temporary BLE device (we'll create a new one in ConnectBLEGatt)
        bleDev.Close();
    } catch (const winrt::hresult_error& ex) {
        printf("[WARN] Pairing failed: %s (hr=0x%08X). Trying connection anyway...\n",
               winrt::to_string(ex.message()).c_str(), (unsigned int)ex.code());
    } catch (...) {
        printf("[WARN] Pairing failed with unknown exception. Trying connection anyway...\n");
    }
}

// ============================================================
// Main
// ============================================================
int main() {
    PrintBanner();

    WSADATA wsaData;
    int wsResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsResult != 0) {
        std::cerr << "[ERROR] WSAStartup failed: " << wsResult << std::endl;
        return 1;
    }
    std::cout << "[DEBUG] Winsock initialized." << std::endl;

    bool winrtOk = false;
    try {
        winrt::init_apartment(apartment_type::multi_threaded);
        std::cout << "[DEBUG] WinRT initialized (MTA)." << std::endl;
        winrtOk = true;
    } catch (const winrt::hresult_error& ex) {
        printf("[ERROR] WinRT init failed: %s (hr=0x%08X)\n",
               winrt::to_string(ex.message()).c_str(), (unsigned int)ex.code());
    } catch (...) {
        std::cerr << "[ERROR] WinRT init unknown exception." << std::endl;
    }

    // === Phase 1: Classic Bluetooth scan ===
    std::cout << "\n[INFO] === Phase 1: Classic Bluetooth scan ===" << std::endl;
    auto classicDevices = DiscoverClassicBluetoothDevices();
    printf("[INFO] Phase 1 complete: %zu Classic BT device(s).\n", classicDevices.size());

    // === Phase 2: BLE advertisement scan ===
    std::vector<DiscoveredDevice> bleDevices;
    if (winrtOk) {
        std::cout << "\n[INFO] === Phase 2: BLE advertisement scan (10 seconds) ===" << std::endl;
        BLEScanner bleScanner;
        bleScanner.Start(10);
        if (bleScanner.scanning) {
            for (int i = 0; i < 10; i++) {
                Sleep(1000);
                printf("[DEBUG] BLE scanning... %d/10 seconds, found %d device(s)\n", i + 1, (int)bleScanner.deviceCount);
            }
            bleScanner.Stop();
        }
        bleDevices = bleScanner.GetDevices();
        printf("[INFO] Phase 2 complete: %zu BLE device(s).\n", bleDevices.size());
    }

    // === Phase 3: WinRT DeviceWatcher ===
    std::vector<DiscoveredDevice> watcherDevices;
    if (winrtOk) {
        std::cout << "\n[INFO] === Phase 3: WinRT DeviceWatcher scan ===" << std::endl;
        AllDeviceScanner allScanner;
        allScanner.Start(12);
        if (allScanner.started) {
            for (int i = 0; i < 12; i++) {
                Sleep(1000);
                printf("[DEBUG] Watcher scanning... %d/12 seconds, added=%d\n", i + 1, (int)allScanner.addedCount);
                if (allScanner.enumerationCompleted) {
                    std::cout << "[DEBUG] Watcher: Enumeration completed." << std::endl;
                    break;
                }
            }
            allScanner.Stop();
        }
        watcherDevices = allScanner.GetDevices();
        printf("[INFO] Phase 3 complete: %zu device(s) via DeviceWatcher.\n", watcherDevices.size());
    }

    // === Merge all discovered devices ===
    std::cout << "\n[INFO] === Merging scan results ===" << std::endl;
    std::map<uint64_t, DiscoveredDevice> merged;

    for (auto& dev : classicDevices) {
        if (dev.address != 0) {
            printf("[DEBUG] Merge: Classic -> %s MAC=%s\n",
                   dev.name.empty() ? "(unnamed)" : WToNarrow(dev.name).c_str(),
                   FormatMACAddress(dev.address).c_str());
            merged[dev.address] = dev;
        }
    }
    for (auto& dev : watcherDevices) {
        if (dev.address == 0) continue;
        auto it = merged.find(dev.address);
        if (it != merged.end()) {
            if (dev.paired && !it->second.paired) it->second.paired = true;
            if (dev.connected && !it->second.connected) it->second.connected = true;
            if (dev.rssi != 0 && it->second.rssi == 0) it->second.rssi = dev.rssi;
            if (!dev.name.empty() && it->second.name.empty()) it->second.name = dev.name;
            if (!dev.deviceId.empty() && it->second.deviceId.empty()) it->second.deviceId = dev.deviceId;
        } else {
            merged[dev.address] = dev;
        }
    }
    for (auto& dev : bleDevices) {
        auto it = merged.find(dev.address);
        if (it != merged.end()) {
            if (dev.rssi != 0 && it->second.rssi == 0) it->second.rssi = dev.rssi;
            if (!dev.name.empty() && it->second.name.empty()) it->second.name = dev.name;
            // Mark as BLE type if not already Classic
            if (it->second.type == DeviceType::Watcher) it->second.type = DeviceType::BLE;
        } else {
            merged[dev.address] = dev;
        }
    }

    std::vector<DiscoveredDevice> allDevices;
    for (auto& kv : merged) allDevices.push_back(kv.second);

    if (allDevices.empty()) {
        std::cerr << "\n[ERROR] No Bluetooth devices found!" << std::endl;
        std::cerr << "[TIP] Make sure Bluetooth is enabled and devices are discoverable." << std::endl;
        std::cerr << "[TIP] On Windows 11: Settings > Bluetooth & devices > Devices > Device settings > Advanced" << std::endl;
        if (winrtOk) winrt::uninit_apartment();
        WSACleanup();
        return 1;
    }

    printf("\n[INFO] Total devices: %zu (Classic=%zu, BLE=%zu, Watcher=%zu)\n",
           allDevices.size(), classicDevices.size(), bleDevices.size(), watcherDevices.size());
    PrintDevices(allDevices);

    // === Device selection ===
    DiscoveredDevice* selectedDevice = SelectDevice(allDevices);
    if (!selectedDevice) {
        std::cerr << "[ERROR] No device selected." << std::endl;
        if (winrtOk) winrt::uninit_apartment();
        WSACleanup();
        return 1;
    }

    printf("\n[INFO] Selected device: %s (%s)\n",
           WToNarrow(selectedDevice->name).c_str(), DeviceTypeToString(selectedDevice->type));

    // === Connect based on device type ===
    if (selectedDevice->type == DeviceType::BLE || selectedDevice->type == DeviceType::Watcher) {
        // BLE device - use GATT connection
        printf("[INFO] This is a BLE device. Connecting via GATT (Nordic UART Service)...\n");

        // Check if already paired - BLE GATT on Windows requires pairing for most operations
        g_pairComplete = false;
        PairBLEDevice(selectedDevice->address, selectedDevice->paired);
        // Wait for pairing to finish (up to 60 seconds - user may need to confirm on Windows)
        {
            std::unique_lock<std::mutex> lk(g_pairMutex);
            g_pairCv.wait_for(lk, std::chrono::seconds(60), [] { return g_pairComplete; });
        }
        if (!g_pairComplete) {
            printf("[WARN] Pairing timed out. Trying connection anyway...\n");
        }

        // Connect via BLE GATT (async, need to wait for completion)
        g_connectComplete = false;
        ConnectBLEGatt(selectedDevice->address);

        // Wait for connection to complete
        printf("[INFO] Waiting for BLE GATT connection to complete...\n");
        {
            std::unique_lock<std::mutex> lk(g_connectMutex);
            g_connectCv.wait_for(lk, std::chrono::seconds(30), [] { return g_connectComplete; });
        }
        if (!g_connectComplete) {
            printf("[WARN] BLE GATT connection timed out after 30 seconds.\n");
        }
    } else {
        // Classic Bluetooth - use SPP connection
        if (!selectedDevice->paired) {
            std::cout << "[INFO] Device not paired. Attempting to pair (PIN: 1234)..." << std::endl;
            PairWithDevice(selectedDevice->address, L"1234");
        } else {
            std::cout << "[INFO] Device is already paired." << std::endl;
        }

        std::cout << "\n[INFO] Connecting via Classic SPP..." << std::endl;
        if (!ConnectClassicSPP(selectedDevice->address)) {
            std::cerr << "[ERROR] Failed to connect to device." << std::endl;
            if (winrtOk) winrt::uninit_apartment();
            WSACleanup();
            return 1;
        }
        g_connMode = ConnectionMode::ClassicSPP;
        g_connected = true;
        std::cout << "[INFO] Connected successfully!" << std::endl;
    }

    if (!g_connected) {
        std::cerr << "[ERROR] Connection failed." << std::endl;
        if (winrtOk) winrt::uninit_apartment();
        WSACleanup();
        return 1;
    }

    // Start receive thread for Classic SPP
    std::thread recvThread;
    if (g_connMode == ConnectionMode::ClassicSPP) {
        recvThread = std::thread(ReceiveThreadClassic);
    }
    // BLE GATT uses ValueChanged callback, no thread needed

    InteractiveMode();

    g_running = false;
    g_connected = false;
    Disconnect();
    if (recvThread.joinable()) recvThread.join();
    if (winrtOk) winrt::uninit_apartment();
    WSACleanup();
    std::cout << "\n[INFO] Program terminated." << std::endl;
    return 0;
}
