/**
 * Bluetooth Console for Desktop Pet Control
 * Connects to HC-05 Bluetooth module and sends control commands
 *
 * Build: g++ -o BluetoothConsole.exe BluetoothConsole.cpp -lws2_32 -lbthprops
 * Or with MSVC: cl BluetoothConsole.cpp /link ws2_32.lib bthprops.lib
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2bth.h>
#include <windows.h>
#include <bluetoothapis.h>
#include <objbase.h>
#include <initguid.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <conio.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bthprops.lib")

// Serial Port Profile UUID
DEFINE_GUID(GUID_SERIAL_PORT_SERVICE, 0x00001101, 0x0000, 0x1000, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB);

// Device name prefix to search for
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

// Function declarations
void PrintBanner();
void PrintHelp();
bool InitializeWinsock();
void CleanupWinsock();
std::vector<BLUETOOTH_DEVICE_INFO> DiscoverBluetoothDevices();
BLUETOOTH_DEVICE_INFO* SelectDevice(std::vector<BLUETOOTH_DEVICE_INFO>& devices);
bool PairWithDevice(BLUETOOTH_DEVICE_INFO& deviceInfo, const wchar_t* passkey = L"1234");
bool ConnectToDevice(const BLUETOOTH_DEVICE_INFO& deviceInfo);
void Disconnect();
bool SendCommand(uint8_t cmd);
void ReceiveThread();
void InteractiveMode();
void PrintDevices(const std::vector<BLUETOOTH_DEVICE_INFO>& devices);
std::string FormatMACAddress(BTH_ADDR addr);

int main() {
    PrintBanner();

    // Initialize Winsock
    if (!InitializeWinsock()) {
        std::cerr << "[ERROR] Failed to initialize Winsock" << std::endl;
        return 1;
    }

    std::cout << "[INFO] Initializing Bluetooth..." << std::endl;

    // Discover devices
    std::cout << "\n[INFO] Scanning for Bluetooth devices (this may take 5-10 seconds)..." << std::endl;
    auto devices = DiscoverBluetoothDevices();

    if (devices.empty()) {
        std::cerr << "[ERROR] No Bluetooth devices found. Make sure your device is powered on and discoverable." << std::endl;
        CleanupWinsock();
        return 1;
    }

    std::cout << "[INFO] Found " << devices.size() << " Bluetooth device(s):" << std::endl;
    PrintDevices(devices);

    // Select device
    BLUETOOTH_DEVICE_INFO* selectedDevice = SelectDevice(devices);
    if (!selectedDevice) {
        std::cerr << "[ERROR] No device selected." << std::endl;
        CleanupWinsock();
        return 1;
    }

    printf("\n[INFO] Selected device: %ls\n", selectedDevice->szName);

    // Pair if not already paired
    if (!selectedDevice->fAuthenticated) {
        std::cout << "[INFO] Device not paired. Attempting to pair (PIN: 1234)..." << std::endl;
        if (!PairWithDevice(*selectedDevice, L"1234")) {
            std::cout << "[WARN] Pairing failed. Trying to connect anyway..." << std::endl;
        } else {
            std::cout << "[INFO] Paired successfully!" << std::endl;
        }
    } else {
        std::cout << "[INFO] Device is already paired." << std::endl;
    }

    // Connect to device
    std::cout << "\n[INFO] Connecting to device..." << std::endl;
    if (!ConnectToDevice(*selectedDevice)) {
        std::cerr << "[ERROR] Failed to connect to device." << std::endl;
        CleanupWinsock();
        return 1;
    }

    std::cout << "[INFO] Connected successfully!" << std::endl;
    g_connected = true;

    // Start receive thread
    std::thread recvThread(ReceiveThread);

    // Enter interactive mode
    InteractiveMode();

    // Cleanup
    g_running = false;
    g_connected = false;

    if (g_socket != INVALID_SOCKET) {
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
    }

    if (recvThread.joinable()) {
        recvThread.join();
    }

    CleanupWinsock();
    std::cout << "\n[INFO] Program terminated." << std::endl;
    return 0;
}

void PrintBanner() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Desktop Pet Bluetooth Controller" << std::endl;
    std::cout << "  Target: HC-05/HC-06 Bluetooth Module" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
}

void PrintHelp() {
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
}

bool InitializeWinsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "[ERROR] WSAStartup failed: " << result << std::endl;
        return false;
    }
    return true;
}

void CleanupWinsock() {
    WSACleanup();
}

std::vector<BLUETOOTH_DEVICE_INFO> DiscoverBluetoothDevices() {
    std::vector<BLUETOOTH_DEVICE_INFO> devices;

    BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams;
    BLUETOOTH_DEVICE_INFO deviceInfo;
    HBLUETOOTH_DEVICE_FIND hFind;

    ZeroMemory(&searchParams, sizeof(searchParams));
    searchParams.dwSize = sizeof(searchParams);
    searchParams.fReturnAuthenticated = TRUE;
    searchParams.fReturnRemembered = TRUE;
    searchParams.fReturnUnknown = TRUE;
    searchParams.fReturnConnected = TRUE;
    searchParams.fIssueInquiry = TRUE;
    searchParams.cTimeoutMultiplier = 8;  // ~10 seconds
    searchParams.hRadio = NULL;

    deviceInfo.dwSize = sizeof(deviceInfo);

    hFind = BluetoothFindFirstDevice(&searchParams, &deviceInfo);

    if (hFind != NULL) {
        do {
            devices.push_back(deviceInfo);
        } while (BluetoothFindNextDevice(hFind, &deviceInfo));

        BluetoothFindDeviceClose(hFind);
    }

    return devices;
}

void PrintDevices(const std::vector<BLUETOOTH_DEVICE_INFO>& devices) {
    std::cout << "\n----------------------------------------" << std::endl;
    for (size_t i = 0; i < devices.size(); i++) {
        const auto& dev = devices[i];
        std::string mac = FormatMACAddress(dev.Address.ullLong);

        // Use printf for consistent output formatting
        printf("[%zu] ", i + 1);
        wprintf(L"%ls\n", dev.szName);
        printf("    MAC: %s | Connected: %s | Paired: %s\n",
               mac.c_str(),
               dev.fConnected ? "Yes" : "No",
               dev.fAuthenticated ? "Yes" : "No");
    }
    std::cout << "----------------------------------------" << std::endl;
}

std::string FormatMACAddress(BTH_ADDR addr) {
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

BLUETOOTH_DEVICE_INFO* SelectDevice(std::vector<BLUETOOTH_DEVICE_INFO>& devices) {
    // First, try to auto-find HC-05/HC-06/Pet device
    for (auto& dev : devices) {
        std::wstring name(dev.szName);
        if (name.find(DEVICE_PREFIX) != std::wstring::npos ||
            name.find(DEVICE_PREFIX_ALT) != std::wstring::npos ||
            name.find(DEVICE_PREFIX_PET) != std::wstring::npos) {
            printf("\n[INFO] Auto-detected target device: %ls\n", dev.szName);
            std::cout << "[INFO] Press Enter to use this device, or type a number to select another: ";

            std::string input;
            std::getline(std::cin, input);

            if (input.empty()) {
                return &dev;
            }
            // User entered a number
            int index = atoi(input.c_str());
            if (index >= 1 && index <= (int)devices.size()) {
                return &devices[index - 1];
            }
            return &dev;
        }
    }

    // Manual selection
    std::cout << "\n[INFO] Auto-detect failed. Please select a device by number: ";
    std::string input;
    std::getline(std::cin, input);

    int index = atoi(input.c_str());
    if (index >= 1 && index <= (int)devices.size()) {
        return &devices[index - 1];
    }

    return nullptr;
}

bool PairWithDevice(BLUETOOTH_DEVICE_INFO& deviceInfo, const wchar_t* passkey) {
    DWORD result = BluetoothAuthenticateDevice(
        NULL,
        NULL,
        &deviceInfo,
        (LPWSTR)passkey,
        (DWORD)wcslen(passkey)
    );

    if (result == ERROR_SUCCESS) {
        // Refresh device info
        DWORD dwSize = sizeof(deviceInfo);
        deviceInfo.dwSize = dwSize;
        BluetoothGetDeviceInfo(NULL, &deviceInfo);
        return true;
    }

    std::cerr << "[ERROR] Pairing failed with error code: " << result << std::endl;
    return false;
}

bool ConnectToDevice(const BLUETOOTH_DEVICE_INFO& deviceInfo) {
    // Create Bluetooth socket
    g_socket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);

    if (g_socket == INVALID_SOCKET) {
        std::cerr << "[ERROR] Failed to create socket. Error: " << WSAGetLastError() << std::endl;
        return false;
    }

    // Set up Bluetooth address
    SOCKADDR_BTH sockAddr;
    ZeroMemory(&sockAddr, sizeof(sockAddr));
    sockAddr.addressFamily = AF_BTH;
    sockAddr.btAddr = deviceInfo.Address.ullLong;
    sockAddr.serviceClassId = GUID_SERIAL_PORT_SERVICE;
    sockAddr.port = 0;  // Use SDP to find port

    std::cout << "[INFO] Attempting connection to " << FormatMACAddress(deviceInfo.Address.ullLong) << "..." << std::endl;

    // Connect
    int result = connect(g_socket, (SOCKADDR*)&sockAddr, sizeof(sockAddr));

    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();

        // Try with port 1 if SDP lookup failed
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
        timeout.tv_usec = 100000;  // 100ms

        int sel = select(0, &readSet, NULL, NULL, &timeout);

        if (sel > 0 && FD_ISSET(g_socket, &readSet)) {
            int received = recv(g_socket, buffer, sizeof(buffer) - 1, 0);

            if (received > 0) {
                buffer[received] = '\0';
                std::cout << "\n[RECV] ";
                for (int i = 0; i < received; i++) {
                    printf("0x%02X ", (unsigned char)buffer[i]);
                }
                std::cout << std::endl;
                std::cout << "> " << std::flush;
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

    // Remove 0x prefix if present
    if (cmd.length() > 2 && (cmd.substr(0, 2) == "0x" || cmd.substr(0, 2) == "0X")) {
        cmd = cmd.substr(2);
    }

    // Parse as hex
    unsigned int value;
    if (sscanf(cmd.c_str(), "%x", &value) == 1) {
        return (uint8_t)(value & 0xFF);
    }

    return 0xFF;  // Invalid
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
        if (!std::getline(std::cin, input)) {
            break;
        }

        // Trim whitespace
        size_t start = input.find_first_not_of(" \t\r\n");
        size_t end = input.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) {
            std::cout << "> " << std::flush;
            continue;
        }
        input = input.substr(start, end - start + 1);

        // Process commands
        if (input.empty()) {
            // Ignore empty input
        } else if (input == "quit" || input == "exit" || input == "q") {
            std::cout << "[INFO] Exiting..." << std::endl;
            break;
        } else if (input == "help" || input == "h" || input == "?") {
            PrintHelp();
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
            // Try to parse as command
            uint8_t cmd = 0xFF;
            int cmdIndex = FindCommandByName(input);

            if (cmdIndex >= 0) {
                cmd = COMMANDS[cmdIndex].code;
                printf("[INFO] Sending command '%s' (0x%02X)\n", COMMANDS[cmdIndex].name, cmd);
            } else {
                // Try hex parsing
                cmd = ParseHexCommand(input);

                // Check if command is in valid range
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
