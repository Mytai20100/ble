#include "api.h"
#include <shlwapi.h>

namespace api {

bool RadioHandle::open() {
    BLUETOOTH_FIND_RADIO_PARAMS p = { sizeof(p) };
    hFind = BluetoothFindFirstRadio(&p, &hRadio);
    return hFind != nullptr;
}

void RadioHandle::close() {
    if (hFind)  { BluetoothFindRadioClose(hFind);  hFind  = nullptr; }
    if (hRadio) { CloseHandle(hRadio);              hRadio = nullptr; }
}

bool DeviceEnum::first(HANDLE hRadio, bool issueInquiry, UCHAR timeoutMult) {
    info.dwSize = sizeof(info);
    BLUETOOTH_DEVICE_SEARCH_PARAMS sp = {};
    sp.dwSize               = sizeof(sp);
    sp.fReturnAuthenticated = TRUE;
    sp.fReturnRemembered    = TRUE;
    sp.fReturnUnknown       = TRUE;
    sp.fReturnConnected     = TRUE;
    sp.fIssueInquiry        = issueInquiry ? TRUE : FALSE;
    sp.cTimeoutMultiplier   = timeoutMult;
    sp.hRadio               = hRadio;
    hFind = BluetoothFindFirstDevice(&sp, &info);
    return hFind != nullptr;
}

bool DeviceEnum::next() {
    return BluetoothFindNextDevice(hFind, &info) != FALSE;
}

void DeviceEnum::close() {
    if (hFind) { BluetoothFindDeviceClose(hFind); hFind = nullptr; }
}

void setDarkTitleBar(HWND hwnd, bool dark) {
    BOOL d = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &d, sizeof(d));
}

void setAcrylicBlur(HWND hwnd, bool enable, COLORREF tintColor) {
    if (!enable) {
        DWM_BLURBEHIND bb = {};
        bb.dwFlags  = DWM_BB_ENABLE;
        bb.fEnable  = FALSE;
        DwmEnableBlurBehindWindow(hwnd, &bb);
        return;
    }

    // Use undocumented SetWindowCompositionAttribute for Acrylic on Win10
    typedef enum {
        WCA_ACCENT_POLICY = 19
    } WINDOWCOMPOSITIONATTRIB;

    typedef struct {
        int    nAccentState;
        int    nFlags;
        DWORD  dwGradientColor;
        int    nAnimationId;
    } ACCENT_POLICY;

    typedef struct {
        WINDOWCOMPOSITIONATTRIB Attrib;
        PVOID                   pvData;
        SIZE_T                  cbData;
    } WINDOWCOMPOSITIONATTRIBDATA;

    typedef BOOL (WINAPI* pfnSetWCA)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    auto fn = (pfnSetWCA)GetProcAddress(hUser, "SetWindowCompositionAttribute");
    if (!fn) return;

    BYTE r = GetRValue(tintColor);
    BYTE g = GetGValue(tintColor);
    BYTE b = GetBValue(tintColor);
    DWORD gradientColor = (0xCC << 24) | (b << 16) | (g << 8) | r;

    ACCENT_POLICY accent = { 4, 0, gradientColor, 0 }; // 4 = ACCENT_ENABLE_ACRYLICBLURBEHIND
    WINDOWCOMPOSITIONATTRIBDATA data = { WCA_ACCENT_POLICY, &accent, sizeof(accent) };
    fn(hwnd, &data);
}

HBITMAP loadBitmapFromFile(const wchar_t* path) {
    return (HBITMAP)LoadImageW(nullptr, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
}

bool isGifOrVideo(const wchar_t* path) {
    const wchar_t* ext = PathFindExtensionW(path);
    if (!ext) return false;
    return (_wcsicmp(ext, L".gif") == 0 ||
            _wcsicmp(ext, L".mp4") == 0 ||
            _wcsicmp(ext, L".webm") == 0 ||
            _wcsicmp(ext, L".avi") == 0);
}

}
