/**
 * Bluetooth Console for Desktop Pet Control (Enhanced with BLE support)
 * Connects to HC-05 Bluetooth module and sends control commands
 *
 * Scans both Classic Bluetooth and Bluetooth Low Energy (BLE) devices
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
#pragma warning(disable: 4995)  // BluetoothAuthenticateDevice deprecated
#include <objbase.h>
#include <initguid.h>

// WinRT headers for BLE scanning
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
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

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bthprops.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "runtimeobject.lib")

using namespace winrt;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;

// Serial Port Profile UUID
DEFINE_GUID(GUID_SERIAL_PORT_SERVICE, 0x00001101, 0x0000, 0x1000, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB);

// Device name prefixes
const wchar_t* DEVICE_PREFIX = L"HC-05";
const wchar_t* DEVICE_PREFIX_ALT = L"HC-06";
const wchar_t* DEVICE_PREFIX_PET = L"Pet";

// Global state
std::atomic<bool> g_running(true);
std::atomic<bool> g_connected(false);
SOCKET g_socket = INVALID_SOCKET;

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
// Unified device info structure (covers Classic + BLE)
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
};

// ============================================================
// Helper: wide string to narrow string for logging
// ============================================================
static std::string WToNarrow(const std::wstring& ws) {
    std::string result;
    for (wchar_t c : ws) {
        result += (c < 128) ? (char)c : '?';
    }
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
// Classic Bluetooth discovery (existing API)
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
    searchParams.cTimeoutMultiplier = 10;  // ~12.8 seconds
    searchParams.hRadio = NULL;

    BLUETOOTH_DEVICE_INFO deviceInfo = {};
    deviceInfo.dwSize = sizeof(deviceInfo);

    std::cout << "[DEBUG] Classic BT: Starting BluetoothFindFirstDevice..." << std::endl;
    HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&searchParams, &deviceInfo);

    if (hFind == NULL) {
        DWORD err = GetLastError();
        std::cerr << "[WARN] Classic BT: BluetoothFindFirstDevice returned NULL. GetLastError=" << err << std::endl;
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
// BLE device discovery using WinRT
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
            std::cout << "[DEBUG] BLE: Creating BluetoothLEAdvertisementWatcher..." << std::endl;
            watcher = BluetoothLEAdvertisementWatcher();
            watcher.ScanningMode(BluetoothLEScanningMode::Active);
            std::cout << "[DEBUG] BLE: ScanningMode set to Active." << std::endl;

            watcher.Received([this](BluetoothLEAdvertisementWatcher const&,
                                     BluetoothLEAdvertisementReceivedEventArgs const& args) {
                try {
                    uint64_t addr = args.BluetoothAddress();
                    int16_t rssiVal = args.RawSignalStrengthInDBm();

                    std::wstring name;
                    auto localName = args.Advertisement().LocalName();
                    if (!localName.empty()) {
                        name = localName.c_str();
                    }

                    std::lock_guard<std::mutex> lock(mutex);

                    bool isNew = (devices.find(addr) == devices.end());

                    auto it = devices.find(addr);
                    if (it != devices.end()) {
                        if (!name.empty() && it->second.name.empty()) {
                            it->second.name = name;
                        }
                        if (rssiVal > it->second.rssi || it->second.rssi == 0) {
                            it->second.rssi = rssiVal;
                        }
                    } else {
                        DiscoveredDevice dev;
                        dev.name = name;
                        dev.address = addr;
                        dev.type = DeviceType::BLE;
                        dev.rssi = rssiVal;
                        dev.paired = false;
                        dev.connected = false;
                        devices[addr] = dev;
                        deviceCount++;
                    }

                    // Log every new device found
                    if (isNew) {
                        printf("[DEBUG] BLE: Found new device! MAC=%s Name=\"%s\" RSSI=%d\n",
                               FormatMACAddress(addr).c_str(),
                               name.empty() ? "(unnamed)" : WToNarrow(name).c_str(),
                               (int)rssiVal);
                    }
                } catch (const winrt::hresult_error& ex) {
                    printf("[WARN] BLE: Received callback error: %s\n", winrt::to_string(ex.message()).c_str());
                } catch (...) {}
            });

            watcher.Start();
            std::cout << "[DEBUG] BLE: Watcher started successfully." << std::endl;
        } catch (const winrt::hresult_error& ex) {
            printf("[ERROR] BLE: Failed to start scanner: %s (hr=0x%08X)\n",
                   winrt::to_string(ex.message()).c_str(), (unsigned int)ex.code());
            scanning = false;
        } catch (const std::exception& ex) {
            printf("[ERROR] BLE: std::exception: %s\n", ex.what());
            scanning = false;
        } catch (...) {
            std::cerr << "[ERROR] BLE: Unknown exception starting scanner." << std::endl;
            scanning = false;
        }
    }

    void Stop() {
        if (watcher) {
            try {
                auto status = watcher.Status();
                printf("[DEBUG] BLE: Stopping watcher (current status=%d).\n", (int)status);
                watcher.Stop();
                std::cout << "[DEBUG] BLE: Watcher stopped." << std::endl;
            } catch (const winrt::hresult_error& ex) {
                printf("[WARN] BLE: Error stopping watcher: %s\n", winrt::to_string(ex.message()).c_str());
            } catch (...) {}
        }
        scanning = false;
    }

    std::vector<DiscoveredDevice> GetDevices() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<DiscoveredDevice> result;
        for (auto& kv : devices) {
            result.push_back(kv.second);
        }
        return result;
    }
};

// ============================================================
// WinRT DeviceWatcher for all Bluetooth devices (Classic + BLE)
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
            // Use BluetoothDevice::GetDeviceSelector() which returns a simpler,
            // more reliable AQS string for paired+discoverable Bluetooth devices
            hstring selector = BluetoothDevice::GetDeviceSelector();
            printf("[DEBUG] Watcher: Using BluetoothDevice selector.\n");

            // Request additional properties
            std::vector<hstring> properties = {
                L"System.Devices.Aep.DeviceAddress",
                L"System.Devices.Aep.IsConnected",
                L"System.Devices.Aep.IsPaired",
                L"System.Devices.Aep.SignalStrength",
                L"System.DeviceInterface.Bluetooth.DeviceAddress",
                L"System.Devices.Aep.Bluetooth.Cod",
            };

            printf("[DEBUG] Watcher: Creating DeviceWatcher with %zu properties...\n", properties.size());

            watcher = DeviceInformation::CreateWatcher(
                selector,
                winrt::single_threaded_vector<hstring>(std::move(properties)).GetView()
            );

            if (!watcher) {
                std::cerr << "[ERROR] Watcher: CreateWatcher returned null!" << std::endl;
                return;
            }

            printf("[DEBUG] Watcher: DeviceWatcher created successfully.\n");

            watcher.Added([this](DeviceWatcher const&, DeviceInformation const& info) {
                try {
                    std::lock_guard<std::mutex> lock(mutex);
                    DiscoveredDevice dev;
                    dev.name = std::wstring(info.Name().c_str());
                    dev.type = DeviceType::Watcher;

                    auto props = info.Properties();

                    // Try multiple address properties
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

                    try {
                        auto pairedObj = props.Lookup(L"System.Devices.Aep.IsPaired");
                        if (pairedObj) dev.paired = winrt::unbox_value<bool>(pairedObj);
                    } catch (...) {}
                    // Also try info.Pairing()
                    try {
                        dev.paired = info.Pairing().IsPaired();
                    } catch (...) {}

                    try {
                        auto connObj = props.Lookup(L"System.Devices.Aep.IsConnected");
                        if (connObj) dev.connected = winrt::unbox_value<bool>(connObj);
                    } catch (...) {}

                    try {
                        auto rssiObj = props.Lookup(L"System.Devices.Aep.SignalStrength");
                        if (rssiObj) dev.rssi = (int)winrt::unbox_value<int16_t>(rssiObj);
                    } catch (...) {}

                    std::wstring id = std::wstring(info.Id().c_str());
                    devices[id] = dev;
                    addedCount++;

                    printf("[DEBUG] Watcher: Added device #%d: Name=\"%s\" MAC=%s Paired=%d Connected=%d\n",
                           (int)addedCount,
                           dev.name.empty() ? "(unnamed)" : WToNarrow(dev.name).c_str(),
                           FormatMACAddress(dev.address).c_str(),
                           dev.paired, dev.connected);
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
                        try {
                            auto pairedObj = props.Lookup(L"System.Devices.Aep.IsPaired");
                            if (pairedObj) it->second.paired = winrt::unbox_value<bool>(pairedObj);
                        } catch (...) {}
                        try {
                            auto connObj = props.Lookup(L"System.Devices.Aep.IsConnected");
                            if (connObj) it->second.connected = winrt::unbox_value<bool>(connObj);
                        } catch (...) {}
                        try {
                            auto rssiObj = props.Lookup(L"System.Devices.Aep.SignalStrength");
                            if (rssiObj) it->second.rssi = (int)winrt::unbox_value<int16_t>(rssiObj);
                        } catch (...) {}
                        updatedCount++;
                    }
                } catch (...) {}
            });

            watcher.Removed([this](DeviceWatcher const&, DeviceInformationUpdate const& update) {
                try {
                    std::lock_guard<std::mutex> lock(mutex);
                    std::wstring id = std::wstring(update.Id().c_str());
                    devices.erase(id);
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

            printf("[DEBUG] Watcher: Calling Start()...\n");
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
                printf("[DEBUG] Watcher: Stopping (current status=%d).\n", (int)status);
                if (status == DeviceWatcherStatus::Started ||
                    status == DeviceWatcherStatus::EnumerationCompleted) {
                    watcher.Stop();
                    std::cout << "[DEBUG] Watcher: Stop() called." << std::endl;
                }
            } catch (const winrt::hresult_error& ex) {
                printf("[WARN] Watcher: Error stopping: %s\n", winrt::to_string(ex.message()).c_str());
            } catch (...) {}
        }
    }

    std::vector<DiscoveredDevice> GetDevices() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<DiscoveredDevice> result;
        for (auto& kv : devices) {
            result.push_back(kv.second);
        }
        return result;
    }

    static uint64_t ParseBTAddress(const std::wstring& addrStr) {
        uint64_t addr = 0;
        std::wstring clean;
        for (wchar_t c : addrStr) {
            if (c != L':' && c != L'-') {
                clean += c;
            }
        }
        if (clean.length() == 12) {
            for (int i = 0; i < 12; i++) {
                int val = 0;
                wchar_t c = clean[i];
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
               mac.c_str(),
               DeviceTypeToString(dev.type),
               dev.connected ? "Yes" : "No",
               dev.paired ? "Yes" : "No");
        if (dev.rssi != 0) {
            printf(" | RSSI: %d dBm", dev.rssi);
        }
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
            dev.name.find(DEVICE_PREFIX_PET) != std::wstring::npos) {

            printf("\n[INFO] Auto-detected target device: %s\n", WToNarrow(dev.name).c_str());
            std::cout << "[INFO] Press Enter to use this device, or type a number to select another: ";

            std::string input;
            std::getline(std::cin, input);

            if (input.empty()) return &dev;
            int index = atoi(input.c_str());
            if (index >= 1 && index <= (int)devices.size()) {
                return &devices[index - 1];
            }
            return &dev;
        }
    }

    std::cout << "\n[INFO] Auto-detect failed. Please select a device by number: ";
    std::string input;
    std::getline(std::cin, input);

    int index = atoi(input.c_str());
    if (index >= 1 && index <= (int)devices.size()) {
        return &devices[index - 1];
    }
    return nullptr;
}

// ============================================================
// Pairing and connection (Classic Bluetooth only)
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
    if (result == ERROR_SUCCESS) {
        std::cout << "[INFO] Paired successfully!" << std::endl;
        return true;
    }

    std::cerr << "[ERROR] Pairing failed with error code: " << result << std::endl;
    return false;
}

bool ConnectToDevice(uint64_t address) {
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

    std::cout << "[INFO] Attempting connection to " << FormatMACAddress(address) << "..." << std::endl;

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
                closesocket(g_socket);
                g_socket = INVALID_SOCKET;
                return false;
            }
        } else {
            std::cerr << "[ERROR] Connection failed. Error: " << err << std::endl;
            closesocket(g_socket);
            g_socket = INVALID_SOCKET;
            return false;
        }
    }

    return true;
}

void Disconnect() {
    if (g_socket != INVALID_SOCKET) {
        shutdown(g_socket, SD_BOTH);
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
    }
    g_connected = false;
    std::cout << "[INFO] Disconnected from device." << std::endl;
}

bool SendCommand(uint8_t cmd) {
    if (!g_connected || g_socket == INVALID_SOCKET) {
        std::cerr << "[ERROR] Not connected to device." << std::endl;
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

    printf("[SENT] Command 0x%02X (%d)\n", cmd, cmd);
    return true;
}

void ReceiveThread() {
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
                buffer[received] = '\0';
                std::cout << "\n[RECV] ";
                for (int i = 0; i < received; i++) {
                    printf("0x%02X ", (unsigned char)buffer[i]);
                }
                std::cout << std::endl << "> " << std::flush;
            } else if (received == 0) {
                std::cout << "\n[INFO] Connection closed by device." << std::endl;
                g_connected = false;
                break;
            } else {
                int err = WSAGetLastError();
                if (err != WSAETIMEDOUT && err != WSAEWOULDBLOCK) {
                    std::cout << "\n[ERROR] Receive error: " << err << std::endl;
                    g_connected = false;
                    break;
                }
            }
        }
    }
}

uint8_t ParseHexCommand(const std::string& input) {
    std::string cmd = input;
    if (cmd.length() > 2 && (cmd.substr(0, 2) == "0x" || cmd.substr(0, 2) == "0X")) {
        cmd = cmd.substr(2);
    }
    unsigned int value;
    if (sscanf(cmd.c_str(), "%x", &value) == 1) {
        return (uint8_t)(value & 0xFF);
    }
    return 0xFF;
}

int FindCommandByName(const std::string& name) {
    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (_stricmp(name.c_str(), COMMANDS[i].name) == 0) {
            return i;
        }
    }
    return -1;
}

void InteractiveMode() {
    std::cout << "\n[INFO] Entering interactive mode. Type 'help' for commands." << std::endl;
    std::cout << "> " << std::flush;

    std::string input;
    while (g_running && g_connected) {
        if (!std::getline(std::cin, input)) break;

        size_t start = input.find_first_not_of(" \t\r\n");
        size_t end = input.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) {
            std::cout << "> " << std::flush;
            continue;
        }
        input = input.substr(start, end - start + 1);

        if (input.empty()) {
        } else if (input == "quit" || input == "exit" || input == "q") {
            std::cout << "[INFO] Exiting..." << std::endl;
            break;
        } else if (input == "help" || input == "h" || input == "?") {
            std::cout << "\n========== Command Reference ==========" << std::endl;
            std::cout << "Available commands (0x29 - 0x43):\n" << std::endl;
            for (int i = 0; i < COMMAND_COUNT; i++) {
                printf("  0x%02X %-12s - %s\n", COMMANDS[i].code, COMMANDS[i].name, COMMANDS[i].description);
            }
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
            for (int i = 0; i < COMMAND_COUNT; i++) {
                printf("  %-12s (0x%02X)\n", COMMANDS[i].name, COMMANDS[i].code);
            }
            std::cout << std::endl;
        } else if (input == "status") {
            std::cout << "\nConnection Status:" << std::endl;
            std::cout << "  Connected: " << (g_connected ? "Yes" : "No") << std::endl;
            std::cout << "  Socket: " << (g_socket != INVALID_SOCKET ? "Valid" : "Invalid") << std::endl;
        } else if (input == "disconnect") {
            Disconnect();
            break;
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
                    std::cout << "[INFO] Valid command range: 0x29-0x49, or use command names." << std::endl;
                    std::cout << "> " << std::flush;
                    continue;
                }
            }
            if (cmd != 0xFF) {
                SendCommand(cmd);
            }
        }

        if (g_connected) {
            std::cout << "> " << std::flush;
        }
    }
}

void PrintBanner() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Desktop Pet Bluetooth Controller" << std::endl;
    std::cout << "  Target: HC-05/HC-06 Bluetooth Module" << std::endl;
    std::cout << "  Enhanced: Classic BT + BLE Discovery" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
}

// ============================================================
// Main
// ============================================================
int main() {
    PrintBanner();

    // Initialize Winsock for Classic Bluetooth
    WSADATA wsaData;
    int wsResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsResult != 0) {
        std::cerr << "[ERROR] WSAStartup failed: " << wsResult << std::endl;
        return 1;
    }
    std::cout << "[DEBUG] Winsock initialized." << std::endl;

    // Initialize WinRT apartment for BLE scanning
    bool winrtOk = false;
    try {
        winrt::init_apartment(apartment_type::multi_threaded);
        std::cout << "[DEBUG] WinRT initialized (multi-threaded apartment)." << std::endl;
        winrtOk = true;
    } catch (const winrt::hresult_error& ex) {
        printf("[ERROR] WinRT init failed: %s (hr=0x%08X)\n",
               winrt::to_string(ex.message()).c_str(), (unsigned int)ex.code());
        std::cerr << "[WARN] BLE scanning will be unavailable. Falling back to Classic BT only." << std::endl;
    } catch (const std::exception& ex) {
        printf("[ERROR] WinRT init std::exception: %s\n", ex.what());
        std::cerr << "[WARN] BLE scanning will be unavailable. Falling back to Classic BT only." << std::endl;
    } catch (...) {
        std::cerr << "[ERROR] WinRT init unknown exception." << std::endl;
        std::cerr << "[WARN] BLE scanning will be unavailable. Falling back to Classic BT only." << std::endl;
    }

    // =====================================================
    // Phase 1: Classic Bluetooth scan (existing API)
    // =====================================================
    std::cout << "\n[INFO] === Phase 1: Classic Bluetooth scan ===" << std::endl;
    auto classicDevices = DiscoverClassicBluetoothDevices();
    printf("[INFO] Phase 1 complete: found %zu Classic BT device(s).\n", classicDevices.size());

    // =====================================================
    // Phase 2: BLE advertisement scan (WinRT)
    // =====================================================
    std::vector<DiscoveredDevice> bleDevices;
    if (winrtOk) {
        std::cout << "\n[INFO] === Phase 2: BLE advertisement scan (10 seconds) ===" << std::endl;
        BLEScanner bleScanner;
        bleScanner.Start(10);

        if (bleScanner.scanning) {
            // Show progress dots while scanning
            for (int i = 0; i < 10; i++) {
                Sleep(1000);
                printf("[DEBUG] BLE scanning... %d/%d seconds, found %d device(s)\n",
                       i + 1, 10, (int)bleScanner.deviceCount);
            }
            bleScanner.Stop();
        }

        bleDevices = bleScanner.GetDevices();
        printf("[INFO] Phase 2 complete: found %zu BLE device(s).\n", bleDevices.size());
    } else {
        std::cout << "\n[INFO] === Phase 2: SKIPPED (WinRT not available) ===" << std::endl;
    }

    // =====================================================
    // Phase 3: WinRT DeviceWatcher (all BT devices)
    // =====================================================
    std::vector<DiscoveredDevice> watcherDevices;
    if (winrtOk) {
        std::cout << "\n[INFO] === Phase 3: WinRT DeviceWatcher scan ===" << std::endl;
        AllDeviceScanner allScanner;
        allScanner.Start(12);

        if (allScanner.started) {
            // Wait up to 12 seconds, but check for enumeration completion
            for (int i = 0; i < 12; i++) {
                Sleep(1000);
                printf("[DEBUG] Watcher scanning... %d/12 seconds, added=%d updated=%d\n",
                       i + 1, (int)allScanner.addedCount, (int)allScanner.updatedCount);
                if (allScanner.enumerationCompleted) {
                    std::cout << "[DEBUG] Watcher: Enumeration completed, stopping early." << std::endl;
                    break;
                }
            }
            allScanner.Stop();
        }

        watcherDevices = allScanner.GetDevices();
        printf("[INFO] Phase 3 complete: found %zu device(s) via DeviceWatcher.\n", watcherDevices.size());
    } else {
        std::cout << "\n[INFO] === Phase 3: SKIPPED (WinRT not available) ===" << std::endl;
    }

    // =====================================================
    // Merge all discovered devices, deduplicate by address
    // =====================================================
    std::cout << "\n[INFO] === Merging results from all scan phases ===" << std::endl;
    std::map<uint64_t, DiscoveredDevice> merged;

    // Add classic devices first (highest priority)
    for (auto& dev : classicDevices) {
        if (dev.address != 0) {
            printf("[DEBUG] Merge: Classic BT -> %s MAC=%s\n",
                   dev.name.empty() ? "(unnamed)" : WToNarrow(dev.name).c_str(),
                   FormatMACAddress(dev.address).c_str());
            merged[dev.address] = dev;
        }
    }

    // Add DeviceWatcher results
    for (auto& dev : watcherDevices) {
        if (dev.address == 0) {
            printf("[DEBUG] Merge: Skipping Watcher device with no address: \"%s\"\n",
                   dev.name.empty() ? "(unnamed)" : WToNarrow(dev.name).c_str());
            continue;
        }
        auto it = merged.find(dev.address);
        if (it != merged.end()) {
            if (dev.paired && !it->second.paired) it->second.paired = true;
            if (dev.connected && !it->second.connected) it->second.connected = true;
            if (dev.rssi != 0 && it->second.rssi == 0) it->second.rssi = dev.rssi;
            if (!dev.name.empty() && it->second.name.empty()) it->second.name = dev.name;
            printf("[DEBUG] Merge: Enhanced existing entry for %s\n", FormatMACAddress(dev.address).c_str());
        } else {
            printf("[DEBUG] Merge: Watcher -> %s MAC=%s (new)\n",
                   dev.name.empty() ? "(unnamed)" : WToNarrow(dev.name).c_str(),
                   FormatMACAddress(dev.address).c_str());
            merged[dev.address] = dev;
        }
    }

    // Add BLE-only devices
    for (auto& dev : bleDevices) {
        auto it = merged.find(dev.address);
        if (it != merged.end()) {
            if (dev.rssi != 0 && it->second.rssi == 0) it->second.rssi = dev.rssi;
            if (!dev.name.empty() && it->second.name.empty()) it->second.name = dev.name;
            printf("[DEBUG] Merge: Enhanced existing entry for %s (BLE RSSI=%d)\n",
                   FormatMACAddress(dev.address).c_str(), dev.rssi);
        } else {
            printf("[DEBUG] Merge: BLE -> %s MAC=%s RSSI=%d (new)\n",
                   dev.name.empty() ? "(unnamed)" : WToNarrow(dev.name).c_str(),
                   FormatMACAddress(dev.address).c_str(), dev.rssi);
            merged[dev.address] = dev;
        }
    }

    // Build final device list
    std::vector<DiscoveredDevice> allDevices;
    for (auto& kv : merged) {
        allDevices.push_back(kv.second);
    }

    if (allDevices.empty()) {
        std::cerr << "\n[ERROR] No Bluetooth devices found at all!" << std::endl;
        std::cerr << "[TIP] Make sure Bluetooth is enabled and devices are discoverable." << std::endl;
        std::cerr << "[TIP] On Windows 11, go to Settings > Bluetooth & devices > Devices > Device settings" << std::endl;
        std::cerr << "[TIP] Set 'Bluetooth devices discovery' to 'Advanced'." << std::endl;
        std::cerr << "[TIP] Or run: reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Bluetooth\" /v AdvancedDiscoveryMode /t REG_DWORD /d 1 /f" << std::endl;
        if (winrtOk) winrt::uninit_apartment();
        WSACleanup();
        return 1;
    }

    std::cout << "\n[INFO] ======================================" << std::endl;
    printf("[INFO] Total devices found: %zu (Classic=%zu, BLE=%zu, Watcher=%zu)\n",
           allDevices.size(), classicDevices.size(), bleDevices.size(), watcherDevices.size());
    std::cout << "[INFO] ======================================" << std::endl;
    PrintDevices(allDevices);

    // =====================================================
    // Device selection
    // =====================================================
    DiscoveredDevice* selectedDevice = SelectDevice(allDevices);
    if (!selectedDevice) {
        std::cerr << "[ERROR] No device selected." << std::endl;
        if (winrtOk) winrt::uninit_apartment();
        WSACleanup();
        return 1;
    }

    printf("\n[INFO] Selected device: %s (%s)\n",
           WToNarrow(selectedDevice->name).c_str(),
           DeviceTypeToString(selectedDevice->type));

    // BLE devices can't use Classic SPP connection
    if (selectedDevice->type == DeviceType::BLE) {
        std::cerr << "[WARN] This is a BLE device. Classic SPP (Serial Port) connection is not supported for BLE." << std::endl;
        std::cerr << "[INFO] Please select a Classic Bluetooth device (HC-05/HC-06) for SPP connection." << std::endl;

        if (classicDevices.empty()) {
            std::cerr << "[ERROR] No Classic Bluetooth devices found." << std::endl;
            if (winrtOk) winrt::uninit_apartment();
            WSACleanup();
            return 1;
        }
        std::cout << "\n[INFO] Re-selecting from Classic Bluetooth devices only:" << std::endl;
        PrintDevices(classicDevices);
        std::vector<DiscoveredDevice> classicAsDiscovered = classicDevices;
        selectedDevice = SelectDevice(classicAsDiscovered);
        if (!selectedDevice) {
            if (winrtOk) winrt::uninit_apartment();
            WSACleanup();
            return 1;
        }
    }

    // Pair if needed
    if (!selectedDevice->paired) {
        std::cout << "[INFO] Device not paired. Attempting to pair (PIN: 1234)..." << std::endl;
        PairWithDevice(selectedDevice->address, L"1234");
    } else {
        std::cout << "[INFO] Device is already paired." << std::endl;
    }

    // Connect via RFCOMM/SPP
    std::cout << "\n[INFO] Connecting to device..." << std::endl;
    if (!ConnectToDevice(selectedDevice->address)) {
        std::cerr << "[ERROR] Failed to connect to device." << std::endl;
        if (winrtOk) winrt::uninit_apartment();
        WSACleanup();
        return 1;
    }

    std::cout << "[INFO] Connected successfully!" << std::endl;
    g_connected = true;

    std::thread recvThread(ReceiveThread);
    InteractiveMode();

    g_running = false;
    g_connected = false;
    if (g_socket != INVALID_SOCKET) {
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
    }
    if (recvThread.joinable()) {
        recvThread.join();
    }

    if (winrtOk) winrt::uninit_apartment();
    WSACleanup();
    std::cout << "\n[INFO] Program terminated." << std::endl;
    return 0;
}
