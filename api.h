#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NTDDI_VERSION 0x0A000000
#define _WIN32_WINNT  0x0A00

#include <windows.h>
#include <bluetoothapis.h>
#include <bthsdpdef.h>
#include <ws2bth.h>
#include <setupapi.h>
#include <devguid.h>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <wingdi.h>

#include <string>
#include <vector>
#include <functional>

namespace api {

struct RadioHandle {
    HANDLE        hRadio = nullptr;
    HBLUETOOTH_RADIO_FIND hFind  = nullptr;

    bool open();
    void close();
    bool valid() const { return hRadio != nullptr; }
};

struct DeviceEnum {
    HBLUETOOTH_DEVICE_FIND hFind = nullptr;
    BLUETOOTH_DEVICE_INFO  info  = {};

    bool first(HANDLE hRadio, bool issueInquiry, UCHAR timeoutMult);
    bool next();
    void close();
};

void     setDarkTitleBar(HWND hwnd, bool dark);
void     setAcrylicBlur(HWND hwnd, bool enable, COLORREF tintColor = RGB(18,18,18));
HBITMAP  loadBitmapFromFile(const wchar_t* path);
bool     isGifOrVideo(const wchar_t* path);

}
