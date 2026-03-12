#pragma once
#include "api.h"
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <map>

namespace ble {

struct Device {
    std::wstring      name;
    std::wstring      mac;
    int               rssi;
    std::wstring      deviceClass;
    std::wstring      battery;
    BLUETOOTH_ADDRESS address;
    bool              connected;
    bool              remembered;
    bool              authenticated;
};

struct ConnectResult {
    std::wstring mac;
    bool         success;
    std::wstring message;
};

using OnDeviceFound = std::function<void(const Device&)>;
using OnScanDone    = std::function<void(bool noRadio)>;
using OnConnectDone = std::function<void(const ConnectResult&)>;

std::wstring rssiValue(int rssi);
std::wstring rssiBar(int rssi);
std::wstring classString(ULONG cls);
std::wstring macString(const BLUETOOTH_ADDRESS& addr);

// Try to read battery level via SDP service record. Returns L"-" if not available.
std::wstring queryBattery(HANDLE hRadio, const BLUETOOTH_ADDRESS& addr);

// One-shot scan (cached pass then inquiry pass).
void scan(HWND msgTarget, int timeoutSec, OnDeviceFound onDevice, OnScanDone onDone);

// Continuous realtime scan: re-runs inquiry in a loop until stopScan() is called.
void scanRealtime(HWND msgTarget, OnDeviceFound onDevice, OnScanDone onDone);

void stopScan();
bool isScanning();

// Connect / pair a single device. Retries up to maxRetries times.
void connect(HWND msgTarget, const Device& device, OnConnectDone onDone, int maxRetries = 1);

// Auto-connect loop: keeps trying all devices until connected or stopped.
// Set stopFlag to true to abort. Runs detached.
void autoConnectAll(HWND msgTarget, std::vector<Device> devices, std::atomic<bool>* stopFlag);

extern const UINT WM_BLE_SCAN_UPDATE;
extern const UINT WM_BLE_SCAN_DONE;
extern const UINT WM_BLE_CONNECT_RESULT;

}
