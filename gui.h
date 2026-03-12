#pragma once
#include "api.h"
#include "ble.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace gui {

struct Theme {
    COLORREF bg;
    COLORREF surface;
    COLORREF panel;
    COLORREF border;
    COLORREF text;
    COLORREF textDim;
    COLORREF textHead;
    COLORREF accent;
    COLORREF btnNormal;
    COLORREF btnHover;
    COLORREF btnActive;
    COLORREF btnAccent;
    COLORREF green;
    COLORREF red;
    COLORREF yellow;
    COLORREF lvSel;
    bool     blur;
};

Theme darkTheme();
Theme lightTheme();
Theme blurTheme();

bool  registerWindowClass(HINSTANCE hInst);
HWND  createMainWindow(HINSTANCE hInst, int nShow);
void  applyTheme(HWND hwnd, const Theme& theme);

void  logAppend(const std::wstring& line);
void  logSetVisible(bool visible);

}
