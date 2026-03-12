#include "ble.h"
#include "api.h"
#include <algorithm>
#include <mutex>
#include <chrono>

namespace ble {

const UINT WM_BLE_SCAN_UPDATE    = WM_USER + 1;
const UINT WM_BLE_SCAN_DONE      = WM_USER + 2;
const UINT WM_BLE_CONNECT_RESULT = WM_USER + 3;

static std::atomic<bool> g_scanning  { false };
static std::atomic<bool> g_stopScan  { false };
static std::thread       g_scanThread;

std::wstring macString(const BLUETOOTH_ADDRESS& a) {
    wchar_t buf[32];
    swprintf_s(buf, L"%02X:%02X:%02X:%02X:%02X:%02X",
        a.rgBytes[5], a.rgBytes[4], a.rgBytes[3],
        a.rgBytes[2], a.rgBytes[1], a.rgBytes[0]);
    return buf;
}

std::wstring classString(ULONG cls) {
    ULONG major = (cls >> 8) & 0x1F;
    switch (major) {
        case 0x01: return L"Computer";
        case 0x02: return L"Phone";
        case 0x03: return L"Network";
        case 0x04: return L"Audio/Video";
        case 0x05: return L"Peripheral";
        case 0x06: return L"Imaging";
        case 0x07: return L"Wearable";
        case 0x08: return L"Toy";
        case 0x09: return L"Health";
        default:   return L"Unknown";
    }
}

std::wstring rssiValue(int rssi) {
    if (rssi == 0) return L"N/A";
    wchar_t buf[16];
    swprintf_s(buf, L"%d dBm", rssi);
    return buf;
}

std::wstring rssiBar(int rssi) {
    if (rssi == 0)   return L"---";
    if (rssi >= -50) return L"[####] Excellent";
    if (rssi >= -65) return L"[### ] Good";
    if (rssi >= -75) return L"[##  ] Fair";
    if (rssi >= -85) return L"[#   ] Weak";
    return                L"[    ] Poor";
}

// RSSI: Classic BT API does not expose it via a public call.
// Windows requires HCI_Read_RSSI via DeviceIoControl on the radio handle,
// which needs elevated privileges and an undocumented IOCTL code.
// We leave it as 0 (N/A) for Classic BT; BLE GATT would use a different API.
static int queryRSSI(HANDLE /*hRadio*/, const BLUETOOTH_ADDRESS& /*addr*/) {
    return 0;
}

std::wstring queryBattery(HANDLE hRadio, const BLUETOOTH_ADDRESS& addr) {
    // Battery Level via SDP attribute 0x0311 is only available for BLE GATT
    // devices via the WinRT GATT API. Classic BT exposes battery only on some
    // HID devices via their HID descriptor, not via the BluetoothApis.h surface.
    //
    // What we can do: check fConnected + fAuthenticated as a proxy indicator,
    // and attempt to read the HID battery level via the HID device path.
    BLUETOOTH_DEVICE_INFO info = {};
    info.dwSize  = sizeof(info);
    info.Address = addr;
    if (BluetoothGetDeviceInfo(hRadio, &info) != ERROR_SUCCESS)
        return L"-";

    // For HID peripherals (mice, keyboards, headsets) that report battery:
    // Windows exposes this via SetupDi + HID_USAGE_PAGE_BATTERY_SYSTEM.
    // That path requires setupapi + hidsdi enumeration which is a separate
    // module. Return a clear status rather than a misleading value.
    if (info.fConnected && info.fAuthenticated)
        return L"Connected*";  // * = battery readable via GATT/HID if device supports it

    return L"-";
}

static Device fromInfo(const BLUETOOTH_DEVICE_INFO& info, HANDLE hRadio) {
    Device d;
    d.name          = info.szName[0] ? info.szName : L"(Unknown)";
    d.address       = info.Address;
    d.mac           = macString(info.Address);
    d.rssi          = queryRSSI(hRadio, info.Address);
    d.deviceClass   = classString(info.ulClassofDevice);
    d.connected     = info.fConnected     != 0;
    d.remembered    = info.fRemembered    != 0;
    d.authenticated = info.fAuthenticated != 0;
    d.battery       = queryBattery(hRadio, info.Address);
    return d;
}

bool isScanning() { return g_scanning.load(); }
void stopScan()   { g_stopScan = true; }

static void enumerateDevices(HANDLE hRadio, bool issueInquiry, UCHAR mult,
                             const std::atomic<bool>& stopFlag,
                             const OnDeviceFound& onDevice, HWND msgTarget)
{
    api::DeviceEnum dev;
    if (!dev.first(hRadio, issueInquiry, mult)) return;
    do {
        if (stopFlag) break;
        Device d = fromInfo(dev.info, hRadio);
        if (onDevice) onDevice(d);
        PostMessageW(msgTarget, WM_BLE_SCAN_UPDATE, 0, 0);
    } while (dev.next());
    dev.close();
}

void scan(HWND msgTarget, int timeoutSec, OnDeviceFound onDevice, OnScanDone onDone) {
    if (g_scanning) return;
    if (g_scanThread.joinable()) g_scanThread.join();

    g_scanThread = std::thread([=]() {
        g_scanning = true;
        g_stopScan = false;
        PostMessageW(msgTarget, WM_BLE_SCAN_UPDATE, 0, 0);

        api::RadioHandle radio;
        if (!radio.open()) {
            g_scanning = false;
            g_stopScan = false;
            if (onDone) onDone(true);
            PostMessageW(msgTarget, WM_BLE_SCAN_DONE, 0, (LPARAM)1);
            return;
        }

        // Pass 1: cached/remembered devices (instant)
        enumerateDevices(radio.hRadio, false, 0, g_stopScan, onDevice, msgTarget);

        // Pass 2: active inquiry (blocks for timeoutSec)
        if (!g_stopScan) {
            UCHAR mult = (timeoutSec == -1) ? 48 : (UCHAR)std::min(timeoutSec * 6, 48);
            enumerateDevices(radio.hRadio, true, mult, g_stopScan, onDevice, msgTarget);
        }

        radio.close();
        g_scanning = false;
        g_stopScan = false;
        if (onDone) onDone(false);
        PostMessageW(msgTarget, WM_BLE_SCAN_DONE, 0, 0);
    });
}

void scanRealtime(HWND msgTarget, OnDeviceFound onDevice, OnScanDone onDone) {
    if (g_scanning) return;
    if (g_scanThread.joinable()) g_scanThread.join();

    g_scanThread = std::thread([=]() {
        g_scanning = true;
        g_stopScan = false;
        PostMessageW(msgTarget, WM_BLE_SCAN_UPDATE, 0, 0);

        api::RadioHandle radio;
        if (!radio.open()) {
            g_scanning = false;
            g_stopScan = false;
            if (onDone) onDone(true);
            PostMessageW(msgTarget, WM_BLE_SCAN_DONE, 0, (LPARAM)1);
            return;
        }

        int cycle = 0;
        while (!g_stopScan) {
            cycle++;
            // Each cycle: ~6.4s inquiry (5 * 1.28s)
            enumerateDevices(radio.hRadio, true, 5, g_stopScan, onDevice, msgTarget);
            if (!g_stopScan) {
                // WPARAM = cycle number, LPARAM = 2 signals realtime cycle tick
                PostMessageW(msgTarget, WM_BLE_SCAN_UPDATE, (WPARAM)cycle, 2);
                for (int i = 0; i < 10 && !g_stopScan; i++) Sleep(100);
            }
        }

        radio.close();
        g_scanning = false;
        g_stopScan = false;
        if (onDone) onDone(false);
        PostMessageW(msgTarget, WM_BLE_SCAN_DONE, 0, 0);
    });
}

void connect(HWND msgTarget, const Device& device, OnConnectDone onDone, int maxRetries) {
    std::thread([=]() {
        api::RadioHandle radio;
        ConnectResult* res = new ConnectResult();
        res->mac = device.mac;

        if (!radio.open()) {
            res->success = false;
            res->message = L"No radio.";
            if (onDone) onDone(*res);
            PostMessageW(msgTarget, WM_BLE_CONNECT_RESULT, 0, (LPARAM)res);
            return;
        }

        DWORD ret = ERROR_GEN_FAILURE;
        for (int attempt = 1; attempt <= maxRetries; attempt++) {
            BLUETOOTH_DEVICE_INFO info = {};
            info.dwSize  = sizeof(info);
            info.Address = device.address;
            BluetoothGetDeviceInfo(radio.hRadio, &info);

            if (info.fConnected) { ret = ERROR_SUCCESS; break; }

            ret = BluetoothAuthenticateDeviceEx(
                msgTarget, radio.hRadio, &info, nullptr, MITMProtectionNotRequired);

            if (ret == ERROR_SUCCESS) break;
            if (attempt < maxRetries) Sleep(1500);
        }

        if (ret == ERROR_SUCCESS) {
            res->success = true;
            res->message = L"Connected.";
        } else if (ret == ERROR_INVALID_PARAMETER) {
            res->success = false;
            res->message = L"Device may not support Classic BT pairing. (0x57)";
        } else if (ret == ERROR_CANCELLED) {
            res->success = false;
            res->message = L"Cancelled or rejected by device.";
        } else {
            wchar_t msg[128];
            swprintf_s(msg, L"HRESULT=0x%08X", ret);
            res->success = false;
            res->message = msg;
        }

        radio.close();
        if (onDone) onDone(*res);
        PostMessageW(msgTarget, WM_BLE_CONNECT_RESULT, 0, (LPARAM)res);
    }).detach();
}

void autoConnectAll(HWND msgTarget, std::vector<Device> devices, std::atomic<bool>* stopFlag) {
    std::thread([msgTarget, devices, stopFlag]() mutable {
        std::vector<bool> done(devices.size(), false);

        while (stopFlag && !stopFlag->load()) {
            bool allDone = true;
            for (size_t i = 0; i < devices.size() && (!stopFlag || !stopFlag->load()); i++) {
                if (done[i]) continue;
                allDone = false;

                api::RadioHandle radio;
                if (!radio.open()) { Sleep(2000); break; }

                BLUETOOTH_DEVICE_INFO info = {};
                info.dwSize  = sizeof(info);
                info.Address = devices[i].address;
                BluetoothGetDeviceInfo(radio.hRadio, &info);

                DWORD ret = ERROR_GEN_FAILURE;
                if (info.fConnected) {
                    ret = ERROR_SUCCESS;
                } else {
                    ret = BluetoothAuthenticateDeviceEx(
                        msgTarget, radio.hRadio, &info, nullptr, MITMProtectionNotRequired);
                }

                ConnectResult* res = new ConnectResult();
                res->mac     = devices[i].mac;
                res->success = (ret == ERROR_SUCCESS);
                if (ret == ERROR_SUCCESS) {
                    res->message = L"[Auto] Connected.";
                    done[i] = true;
                } else {
                    wchar_t msg[128];
                    swprintf_s(msg, L"[Auto] Retry... 0x%08X", ret);
                    res->message = msg;
                }
                PostMessageW(msgTarget, WM_BLE_CONNECT_RESULT, 0, (LPARAM)res);
                radio.close();

                if (stopFlag && !stopFlag->load()) Sleep(800);
            }

            if (allDone || (stopFlag && stopFlag->load())) break;
            for (int i = 0; i < 30 && (!stopFlag || !stopFlag->load()); i++) Sleep(100);
        }
    }).detach();
}

}
