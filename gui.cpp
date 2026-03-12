#include "gui.h"
#include "api.h"
#include "ble.h"
#include <algorithm>
#include <commdlg.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#define IDC_LV_DEVICES      1001
#define IDC_BTN_SCAN        1002
#define IDC_BTN_STOP        1003
#define IDC_BTN_CONNECT     1004
#define IDC_BTN_CLEAR       1005
#define IDC_EDIT_TIMEOUT    1006
#define IDC_STATIC_TIMEOUT  1007
#define IDC_STATIC_STATUS   1008
#define IDC_STATIC_FOUND    1009
#define IDC_CHECK_CONNALL   1010
#define IDC_BTN_LOG         1011
#define IDC_BTN_THEME       1012
#define IDC_BTN_BG          1013
#define IDC_EDIT_LOG        1014
#define IDC_BTN_SCANR       1015
#define IDC_SIGNAL_PANEL    1016

static const int TB_H  = 44;
static const int SIG_W = 130;
static const int SB_H  = 22;
static const int LOG_H = 130;

static HFONT  s_fontUI    = nullptr;
static HFONT  s_fontMono  = nullptr;
static HFONT  s_fontSmall = nullptr;

static HBRUSH s_brBg   = nullptr;
static HBRUSH s_brPanel= nullptr;
static HBRUSH s_brBtn  = nullptr;
static HBRUSH s_brEdit = nullptr;
static HBRUSH s_brLog  = nullptr;

static HWND s_hwnd       = nullptr;
static HWND s_hwndLV     = nullptr;
static HWND s_hwndStatus = nullptr;
static HWND s_hwndFound  = nullptr;
static HWND s_hwndLogEdit= nullptr;
static HWND s_hwndSig    = nullptr;
static bool s_logVisible = false;

static Gdiplus::Bitmap* s_bgBmp   = nullptr;
static ULONG_PTR        s_gdipTok = 0;

static gui::Theme             s_theme;
static std::map<HWND,bool>    s_btnHover;
static std::vector<ble::Device> s_devices;
static std::mutex               s_mutex;
static std::atomic<bool>        s_autoStop{false};

static struct {
    bool  radioPresent = false;
    int   deviceCount  = 0;
    bool  scanning     = false;
    float sigQuality   = 0.f;
} s_sig;

namespace gui {

static LRESULT CALLBACK WndProc(HWND,UINT,WPARAM,LPARAM);
static LRESULT CALLBACK BtnSubProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
static LRESULT CALLBACK SigSubProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);

Theme darkTheme() {
    return {RGB(18,18,18),RGB(28,28,28),RGB(38,38,38),RGB(60,60,60),
            RGB(215,215,215),RGB(120,120,120),RGB(255,255,255),RGB(140,140,140),
            RGB(50,50,50),RGB(70,70,70),RGB(35,35,35),RGB(60,120,180),
            RGB(80,200,80),RGB(210,70,70),RGB(210,180,60),RGB(55,55,55),false};
}
Theme lightTheme() {
    return {RGB(240,240,242),RGB(255,255,255),RGB(228,228,232),RGB(190,190,195),
            RGB(30,30,35),RGB(100,100,108),RGB(0,0,0),RGB(70,70,80),
            RGB(210,210,215),RGB(185,185,195),RGB(220,220,225),RGB(40,110,200),
            RGB(30,155,30),RGB(190,40,40),RGB(160,140,20),RGB(180,210,240),false};
}
Theme blurTheme() { Theme t=darkTheme(); t.blur=true; return t; }

static void rebuildBrushes() {
    auto rep=[](HBRUSH& b,COLORREF c){if(b)DeleteObject(b);b=CreateSolidBrush(c);};
    rep(s_brBg,   s_theme.bg);
    rep(s_brPanel,s_theme.panel);
    rep(s_brBtn,  s_theme.btnNormal);
    rep(s_brEdit, s_theme.panel);
    rep(s_brLog,  RGB(8,12,8));
}

static void drawButton(HDC hdc,HWND btn,bool hover,bool pressed,bool accent){
    RECT rc; GetClientRect(btn,&rc);
    wchar_t txt[128]={}; GetWindowTextW(btn,txt,127);
    BOOL ena=IsWindowEnabled(btn);
    COLORREF bg=!ena?RGB(28,28,28):accent?(hover?RGB(80,145,200):s_theme.btnAccent):
                pressed?s_theme.btnActive:hover?s_theme.btnHover:s_theme.btnNormal;
    if(s_theme.blur&&ena&&!s_bgBmp)
        bg=RGB(GetRValue(bg)/6,GetGValue(bg)/6,GetBValue(bg)/6);
    HRGN clip=CreateRoundRectRgn(rc.left,rc.top,rc.right,rc.bottom,10,10);
    SelectClipRgn(hdc,clip);
    HBRUSH br=CreateSolidBrush(bg); FillRect(hdc,&rc,br);
    DeleteObject(br); SelectClipRgn(hdc,nullptr); DeleteObject(clip);
    COLORREF bc=ena?(hover?s_theme.accent:s_theme.border):RGB(42,42,42);
    if(accent&&ena)bc=RGB(80,150,220);
    HPEN pen=CreatePen(PS_SOLID,1,bc);
    HPEN op=(HPEN)SelectObject(hdc,pen);
    HBRUSH ob=(HBRUSH)SelectObject(hdc,GetStockObject(NULL_BRUSH));
    RoundRect(hdc,rc.left,rc.top,rc.right-1,rc.bottom-1,10,10);
    SelectObject(hdc,op); SelectObject(hdc,ob); DeleteObject(pen);
    SetTextColor(hdc,ena?s_theme.text:s_theme.textDim);
    SetBkMode(hdc,TRANSPARENT);
    HFONT of=(HFONT)SelectObject(hdc,s_fontUI);
    DrawTextW(hdc,txt,-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(hdc,of);
}

static void drawSignalPanel(HDC hdc,HWND panel){
    RECT rc; GetClientRect(panel,&rc);
    int W=rc.right,H=rc.bottom;
    HBRUSH bg=CreateSolidBrush(s_theme.panel); FillRect(hdc,&rc,bg); DeleteObject(bg);
    HPEN bp=CreatePen(PS_SOLID,1,s_theme.border);
    HPEN op=(HPEN)SelectObject(hdc,bp);
    HBRUSH ob=(HBRUSH)SelectObject(hdc,GetStockObject(NULL_BRUSH));
    Rectangle(hdc,0,0,W-1,H-1);
    SelectObject(hdc,op); SelectObject(hdc,ob); DeleteObject(bp);
    SetBkMode(hdc,TRANSPARENT);
    HFONT of=(HFONT)SelectObject(hdc,s_fontSmall);
    // Title
    SetTextColor(hdc,s_theme.textDim);
    RECT tr={4,5,W-4,19}; DrawTextW(hdc,L"Bluetooth Signal",-1,&tr,DT_CENTER|DT_TOP|DT_SINGLELINE);
    // Dot
    COLORREF dc=s_sig.radioPresent?(s_sig.scanning?RGB(60,220,120):RGB(60,180,255)):RGB(200,50,50);
    HBRUSH db=CreateSolidBrush(dc); HPEN dp=CreatePen(PS_SOLID,1,dc);
    SelectObject(hdc,db); SelectObject(hdc,dp);
    int cx=W/2,cy=32,cr=7;
    Ellipse(hdc,cx-cr,cy-cr,cx+cr,cy+cr);
    SelectObject(hdc,GetStockObject(NULL_BRUSH));
    DeleteObject(db); DeleteObject(dp);
    // Status text under dot
    RECT sr={2,43,W-2,57}; SetTextColor(hdc,dc);
    DrawTextW(hdc,s_sig.radioPresent?(s_sig.scanning?L"Scanning":L"Ready"):L"No Radio",
              -1,&sr,DT_CENTER|DT_TOP|DT_SINGLELINE);
    // Bars
    int by0=62,bW=11,bGap=4,nBars=4;
    int totalW=nBars*bW+(nBars-1)*bGap;
    int bx0=(W-totalW)/2;
    int active=s_sig.scanning?(int)(s_sig.sigQuality*4.f+0.5f):0;
    for(int i=0;i<4;i++){
        int bh=7+i*7, bx=bx0+i*(bW+bGap), byt=by0+28-bh;
        COLORREF col=(i<active)?RGB(60,210,100):RGB(45,45,45);
        HBRUSH bb=CreateSolidBrush(col);
        RECT br2={bx,byt,bx+bW,by0+28}; FillRect(hdc,&br2,bb); DeleteObject(bb);
    }
    // Divider
    HPEN dv=CreatePen(PS_SOLID,1,s_theme.border);
    op=(HPEN)SelectObject(hdc,dv);
    MoveToEx(hdc,6,by0+32,nullptr); LineTo(hdc,W-6,by0+32);
    SelectObject(hdc,op); DeleteObject(dv);
    // Count
    RECT cr2={2,by0+36,W-2,by0+50};
    wchar_t dtxt[32]; swprintf_s(dtxt,L"%d device%s",s_sig.deviceCount,s_sig.deviceCount==1?L"":L"s");
    SetTextColor(hdc,s_theme.text);
    DrawTextW(hdc,dtxt,-1,&cr2,DT_CENTER|DT_TOP|DT_SINGLELINE);
    // Scanning animation dots when active
    if(s_sig.scanning){
        static int tick=0; tick=(tick+1)%4;
        wchar_t dots[8]={}; for(int i=0;i<tick;i++) dots[i]=L'.';
        RECT ar={2,by0+54,W-2,by0+68};
        SetTextColor(hdc,RGB(60,210,100));
        DrawTextW(hdc,dots,-1,&ar,DT_CENTER|DT_TOP|DT_SINGLELINE);
    }
    SelectObject(hdc,of);
}

static void setStatus(const std::wstring& m){if(s_hwndStatus)SetWindowTextW(s_hwndStatus,m.c_str());}

static void updateFoundCount(){
    std::lock_guard<std::mutex> lk(s_mutex);
    wchar_t buf[32]; swprintf_s(buf,L"Devices: %zu",s_devices.size());
    if(s_hwndFound) SetWindowTextW(s_hwndFound,buf);
    s_sig.deviceCount=(int)s_devices.size();
    float sum=0;int cnt=0;
    for(auto& d:s_devices) if(d.rssi!=0){float q=std::min(1.f,std::max(0.f,(d.rssi+100.f)/60.f));sum+=q;cnt++;}
    s_sig.sigQuality=cnt>0?sum/cnt:0.5f;
    if(s_hwndSig) InvalidateRect(s_hwndSig,nullptr,TRUE);
}

static void lvAddCol(HWND lv,int i,const wchar_t* n,int w){
    LVCOLUMNW c={}; c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;
    c.pszText=(LPWSTR)n; c.cx=w; c.iSubItem=i;
    ListView_InsertColumn(lv,i,&c);
}
static void lvSet(HWND lv,int r,int c,const std::wstring& t){
    LVITEMW it={}; it.mask=LVIF_TEXT; it.iItem=r; it.iSubItem=c; it.pszText=(LPWSTR)t.c_str();
    if(c==0) ListView_InsertItem(lv,&it); else ListView_SetItem(lv,&it);
}
static void refreshListView(){
    if(!s_hwndLV) return;
    SendMessageW(s_hwndLV,WM_SETREDRAW,FALSE,0);
    std::lock_guard<std::mutex> lk(s_mutex);
    int cur=ListView_GetItemCount(s_hwndLV),nw=(int)s_devices.size();
    while(ListView_GetItemCount(s_hwndLV)>nw)
        ListView_DeleteItem(s_hwndLV,ListView_GetItemCount(s_hwndLV)-1);
    for(int i=0;i<nw;i++){
        const auto& d=s_devices[i];
        std::wstring nm=d.name.empty()?L"(unknown)":d.name;
        std::wstring st=d.connected?L"Connected":d.remembered?L"Paired":L"Available";
        if(i>=cur) lvSet(s_hwndLV,i,0,nm);
        else{LVITEMW it={};it.mask=LVIF_TEXT;it.iItem=i;it.iSubItem=0;it.pszText=(LPWSTR)nm.c_str();ListView_SetItem(s_hwndLV,&it);}
        lvSet(s_hwndLV,i,1,d.mac);
        lvSet(s_hwndLV,i,2,ble::rssiValue(d.rssi));
        lvSet(s_hwndLV,i,3,ble::rssiBar(d.rssi));
        lvSet(s_hwndLV,i,4,d.deviceClass);
        lvSet(s_hwndLV,i,5,d.battery);
        lvSet(s_hwndLV,i,6,st);
    }
    SendMessageW(s_hwndLV,WM_SETREDRAW,TRUE,0);
    InvalidateRect(s_hwndLV,nullptr,TRUE);
}

void logAppend(const std::wstring& line){
    if(!s_hwndLogEdit) return;
    int len=GetWindowTextLengthW(s_hwndLogEdit);
    SendMessageW(s_hwndLogEdit,EM_SETSEL,len,len);
    std::wstring ln=line+L"\r\n";
    SendMessageW(s_hwndLogEdit,EM_REPLACESEL,FALSE,(LPARAM)ln.c_str());
    SendMessageW(s_hwndLogEdit,EM_SCROLLCARET,0,0);
}

void logSetVisible(bool v){
    s_logVisible=v;
    RECT rc; GetClientRect(s_hwnd,&rc);
    PostMessageW(s_hwnd,WM_SIZE,0,MAKELPARAM(rc.right,rc.bottom));
    HWND b=GetDlgItem(s_hwnd,IDC_BTN_LOG);
    if(b){SetWindowTextW(b,v?L"Log [ON]":L"Log");InvalidateRect(b,nullptr,TRUE);}
}

static void showThemeMenu(HWND hwnd){
    HMENU m=CreatePopupMenu();
    AppendMenuW(m,MF_STRING,1,L"Dark");
    AppendMenuW(m,MF_STRING,2,L"Light");
    AppendMenuW(m,MF_STRING,3,L"Blur / Acrylic");
    RECT rc; GetWindowRect(GetDlgItem(hwnd,IDC_BTN_THEME),&rc);
    int cmd=TrackPopupMenu(m,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTBUTTON,rc.left,rc.bottom,0,hwnd,nullptr);
    DestroyMenu(m);
    if(cmd==1) applyTheme(hwnd,darkTheme());
    else if(cmd==2) applyTheme(hwnd,lightTheme());
    else if(cmd==3) applyTheme(hwnd,blurTheme());
}

static void showBgPicker(HWND hwnd){
    wchar_t path[MAX_PATH]={};
    OPENFILENAMEW ofn={};
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=hwnd;
    ofn.lpstrFile=path; ofn.nMaxFile=MAX_PATH;
    ofn.lpstrFilter=L"Images\0*.bmp;*.jpg;*.jpeg;*.png;*.gif\0All\0*.*\0";
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if(!GetOpenFileNameW(&ofn)) return;
    if(s_bgBmp){delete s_bgBmp;s_bgBmp=nullptr;}
    s_bgBmp=new Gdiplus::Bitmap(path);
    if(s_bgBmp->GetLastStatus()!=Gdiplus::Ok){
        delete s_bgBmp;s_bgBmp=nullptr;
        MessageBoxW(hwnd,L"Failed to load image.",L"Error",MB_ICONWARNING|MB_OK);
        return;
    }
    ListView_SetBkColor(s_hwndLV,CLR_NONE);
    ListView_SetTextBkColor(s_hwndLV,CLR_NONE);
    InvalidateRect(hwnd,nullptr,TRUE);
    logAppend(std::wstring(L"[BG] ")+path);
}

void applyTheme(HWND hwnd,const Theme& t){
    s_theme=t; rebuildBrushes();
    if(t.blur){
        LONG_PTR ex=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd,GWL_EXSTYLE,ex|WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd,0,235,LWA_ALPHA);
        api::setAcrylicBlur(hwnd,true,t.bg);
        if(s_hwndLV){ListView_SetBkColor(s_hwndLV,CLR_NONE);ListView_SetTextBkColor(s_hwndLV,CLR_NONE);}
    } else {
        LONG_PTR ex=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd,GWL_EXSTYLE,ex&~WS_EX_LAYERED);
        api::setAcrylicBlur(hwnd,false);
        if(s_hwndLV){ListView_SetBkColor(s_hwndLV,t.surface);ListView_SetTextBkColor(s_hwndLV,t.surface);ListView_SetTextColor(s_hwndLV,t.text);}
    }
    InvalidateRect(hwnd,nullptr,TRUE);
    HWND c=GetWindow(hwnd,GW_CHILD);
    while(c){InvalidateRect(c,nullptr,TRUE);c=GetWindow(c,GW_HWNDNEXT);}
    logAppend(t.blur?L"[Theme] Blur":(t.bg==RGB(18,18,18)?L"[Theme] Dark":L"[Theme] Light"));
}

bool registerWindowClass(HINSTANCE hInst){
    WNDCLASSEXW wc={};
    wc.cbSize=sizeof(wc); wc.lpfnWndProc=WndProc; wc.hInstance=hInst;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=nullptr;
    wc.lpszClassName=L"BleGuiClass";
    wc.hIcon=(HICON)LoadImageW(hInst,L"app.ico",IMAGE_ICON,0,0,LR_LOADFROMFILE|LR_DEFAULTSIZE);
    if(!wc.hIcon) wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
    wc.hIconSm=wc.hIcon;
    return RegisterClassExW(&wc)!=0;
}

HWND createMainWindow(HINSTANCE hInst,int nShow){
    s_theme=darkTheme(); rebuildBrushes();
    HWND hwnd=CreateWindowExW(WS_EX_APPWINDOW,L"BleGuiClass",L"BLE v0.1",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1060,660,nullptr,nullptr,hInst,nullptr);
    if(!hwnd) return nullptr;
    api::setDarkTitleBar(hwnd,true);
    ShowWindow(hwnd,nShow); UpdateWindow(hwnd);
    return hwnd;
}

static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){

    case WM_CREATE:{
        s_hwnd=hwnd;
        s_fontUI   =CreateFontW(-13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        s_fontMono =CreateFontW(-12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,L"Consolas");
        s_fontSmall=CreateFontW(-11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");

        auto lbl=[&](const wchar_t* t,int x,int y,int w,int h,int id){
            HWND h2=CreateWindowExW(0,L"STATIC",t,WS_CHILD|WS_VISIBLE|SS_LEFT,x,y,w,h,hwnd,(HMENU)(UINT_PTR)id,nullptr,nullptr);
            SendMessageW(h2,WM_SETFONT,(WPARAM)s_fontUI,TRUE); return h2;};
        auto mkbtn=[&](const wchar_t* t,int x,int y,int w,int h,int id){
            HWND b=CreateWindowExW(0,L"BUTTON",t,WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,x,y,w,h,hwnd,(HMENU)(UINT_PTR)id,nullptr,nullptr);
            SendMessageW(b,WM_SETFONT,(WPARAM)s_fontUI,TRUE);
            SetWindowSubclass(b,BtnSubProc,0,0); return b;};

        // Toolbar: fixed positions, no overlap
        lbl(L"Timeout:",6,14,64,18,IDC_STATIC_TIMEOUT);
        HWND hEdit=CreateWindowExW(0,L"EDIT",L"30",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_CENTER,
            72,11,44,22,hwnd,(HMENU)IDC_EDIT_TIMEOUT,nullptr,nullptr);
        SendMessageW(hEdit,WM_SETFONT,(WPARAM)s_fontMono,TRUE);
        HWND hHint=CreateWindowExW(0,L"STATIC",L"sec (-1=∞)",WS_CHILD|WS_VISIBLE|SS_LEFT,
            120,14,76,18,hwnd,(HMENU)(IDC_STATIC_TIMEOUT+1),nullptr,nullptr);
        SendMessageW(hHint,WM_SETFONT,(WPARAM)s_fontSmall,TRUE);

        // All buttons in a row with consistent spacing
        int bx=202, bh=28, by=8;
        mkbtn(L"Scan",      bx,by, 78,bh,IDC_BTN_SCAN);    bx+=84;
        HWND hStop=
        mkbtn(L"Stop",      bx,by, 60,bh,IDC_BTN_STOP);    bx+=66;
        mkbtn(L"Scan-R",    bx,by, 66,bh,IDC_BTN_SCANR);   bx+=72;
        mkbtn(L"Clear",     bx,by, 60,bh,IDC_BTN_CLEAR);   bx+=66;
        mkbtn(L"Connect",   bx,by, 80,bh,IDC_BTN_CONNECT); bx+=86;
        mkbtn(L"Log",       bx,by, 50,bh,IDC_BTN_LOG);     bx+=56;
        mkbtn(L"Themes",    bx,by, 66,bh,IDC_BTN_THEME);   bx+=72;
        mkbtn(L"BG",        bx,by, 42,bh,IDC_BTN_BG);      bx+=48;

        EnableWindow(hStop,FALSE);

        HWND hChk=CreateWindowExW(0,L"BUTTON",L"Auto-connect",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            bx+2,13,108,18,hwnd,(HMENU)IDC_CHECK_CONNALL,nullptr,nullptr);
        SendMessageW(hChk,WM_SETFONT,(WPARAM)s_fontSmall,TRUE);

        // Separator
        CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,
            0,TB_H,2000,2,hwnd,nullptr,nullptr,nullptr);

        // Signal panel (left of listview)
        s_hwndSig=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,
            0,TB_H+2,SIG_W,400,hwnd,(HMENU)IDC_SIGNAL_PANEL,nullptr,nullptr);
        SetWindowSubclass(s_hwndSig,SigSubProc,0,0);

        // ListView (right of signal panel)
        HWND lv=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",
            WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SHOWSELALWAYS|LVS_SINGLESEL|LVS_NOSORTHEADER,
            SIG_W,TB_H+2,900,500,hwnd,(HMENU)IDC_LV_DEVICES,nullptr,nullptr);
        s_hwndLV=lv;
        ListView_SetExtendedListViewStyle(lv,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        SendMessageW(lv,WM_SETFONT,(WPARAM)s_fontMono,TRUE);
        lvAddCol(lv,0,L"Name",   200);
        lvAddCol(lv,1,L"MAC",    140);
        lvAddCol(lv,2,L"RSSI",    58);
        lvAddCol(lv,3,L"Signal", 148);
        lvAddCol(lv,4,L"Type",    88);
        lvAddCol(lv,5,L"Battery", 64);
        lvAddCol(lv,6,L"Status",  88);

        // Status bar
        s_hwndStatus=CreateWindowExW(0,L"STATIC",L"Ready.",WS_CHILD|WS_VISIBLE|SS_LEFT,
            4,0,600,18,hwnd,(HMENU)IDC_STATIC_STATUS,nullptr,nullptr);
        SendMessageW(s_hwndStatus,WM_SETFONT,(WPARAM)s_fontSmall,TRUE);
        s_hwndFound=CreateWindowExW(0,L"STATIC",L"Devices: 0",WS_CHILD|WS_VISIBLE|SS_RIGHT,
            600,0,300,18,hwnd,(HMENU)IDC_STATIC_FOUND,nullptr,nullptr);
        SendMessageW(s_hwndFound,WM_SETFONT,(WPARAM)s_fontSmall,TRUE);

        // Log edit (hidden initially)
        s_hwndLogEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
            WS_CHILD|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,
            0,0,900,LOG_H,hwnd,(HMENU)IDC_EDIT_LOG,nullptr,nullptr);
        SendMessageW(s_hwndLogEdit,WM_SETFONT,(WPARAM)s_fontMono,TRUE);

        // Start timer to animate signal panel while scanning
        SetTimer(hwnd, 1, 500, nullptr);

        Gdiplus::GdiplusStartupInput gi;
        Gdiplus::GdiplusStartup(&s_gdipTok,&gi,nullptr);

        logAppend(L"[BLE v0.1] Ready.");
        logAppend(L"[Info] Scan = one-shot  |  Scan-R = realtime continuous (~6s cycles)");
        logAppend(L"[Info] Auto-connect = retry all found devices until connected");
        return 0;
    }

    case WM_TIMER:{
        if(s_hwndSig&&s_sig.scanning) InvalidateRect(s_hwndSig,nullptr,TRUE);
        break;
    }

    case WM_SIZE:{
        int W=LOWORD(lp),H=HIWORD(lp);
        int logH=s_logVisible?LOG_H:0;
        int lvTop=TB_H+2;
        int lvH=H-lvTop-SB_H-logH;
        if(lvH<0) lvH=0;
        int sbY=lvTop+lvH;
        if(s_hwndSig)    MoveWindow(s_hwndSig,   0,      lvTop,  SIG_W,       lvH,    TRUE);
        if(s_hwndLV)     MoveWindow(s_hwndLV,    SIG_W,  lvTop,  W-SIG_W,     lvH,    TRUE);
        if(s_hwndStatus) MoveWindow(s_hwndStatus,4,      sbY+2,  W/2,         SB_H-4, TRUE);
        if(s_hwndFound)  MoveWindow(s_hwndFound, W/2,    sbY+2,  W/2-4,       SB_H-4, TRUE);
        if(s_hwndLogEdit&&s_logVisible)
            MoveWindow(s_hwndLogEdit,0,sbY+SB_H,W,logH,TRUE);
        return 0;
    }

    case WM_ERASEBKGND:{
        HDC hdc=(HDC)wp; RECT rc; GetClientRect(hwnd,&rc);
        if(s_bgBmp){
            Gdiplus::Graphics g(hdc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.DrawImage(s_bgBmp,0,0,rc.right,rc.bottom);
        } else {
            FillRect(hdc,&rc,s_brBg);
        }
        return 1;
    }

    case WM_CTLCOLORSTATIC:{
        HDC hdc=(HDC)wp;
        SetBkMode(hdc,TRANSPARENT);
        SetTextColor(hdc,s_theme.textDim);
        return (LRESULT)(s_bgBmp?(HBRUSH)GetStockObject(NULL_BRUSH):s_brBg);
    }

    case WM_CTLCOLOREDIT:{
        HDC hdc=(HDC)wp; HWND ctl=(HWND)lp;
        if(GetDlgCtrlID(ctl)==IDC_EDIT_LOG){
            SetTextColor(hdc,RGB(155,225,155)); SetBkColor(hdc,RGB(8,12,8));
            return (LRESULT)s_brLog;
        }
        SetTextColor(hdc,s_theme.text); SetBkColor(hdc,s_theme.panel);
        return (LRESULT)s_brEdit;
    }

    case WM_CTLCOLORBTN: return (LRESULT)s_brBg;

    case WM_DRAWITEM:{
        auto* dis=(DRAWITEMSTRUCT*)lp;
        if(dis->CtlType==ODT_BUTTON){
            bool hov=s_btnHover.count(dis->hwndItem)&&s_btnHover[dis->hwndItem];
            bool prs=(dis->itemState&ODS_SELECTED)!=0;
            bool acc=(dis->CtlID==IDC_BTN_SCAN||dis->CtlID==IDC_BTN_SCANR);
            drawButton(dis->hDC,dis->hwndItem,hov,prs,acc);
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY:{
        auto* nm=(NMHDR*)lp;
        if(nm->idFrom==IDC_LV_DEVICES&&nm->code==NM_CUSTOMDRAW){
            auto* cd=(NMLVCUSTOMDRAW*)lp;
            switch(cd->nmcd.dwDrawStage){
            case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT:{
                int row=(int)cd->nmcd.dwItemSpec;
                bool sel=(ListView_GetItemState(s_hwndLV,row,LVIS_SELECTED)&LVIS_SELECTED)!=0;
                cd->clrTextBk=sel?s_theme.lvSel:(s_bgBmp||s_theme.blur)?CLR_NONE:(row%2==0?s_theme.surface:s_theme.bg);
                cd->clrText=s_theme.text;
                return CDRF_NEWFONT|CDRF_NOTIFYSUBITEMDRAW;
            }
            case CDDS_ITEMPREPAINT|CDDS_SUBITEM:{
                if(cd->iSubItem==6){
                    wchar_t buf[32]={};
                    LVITEMW it={};it.mask=LVIF_TEXT;it.iItem=(int)cd->nmcd.dwItemSpec;
                    it.iSubItem=6;it.pszText=buf;it.cchTextMax=31;
                    ListView_GetItem(s_hwndLV,&it);
                    std::wstring s(buf);
                    cd->clrText=s==L"Connected"?s_theme.green:s==L"Paired"?s_theme.yellow:s_theme.textDim;
                }
                return CDRF_NEWFONT;
            }
            default: break;
            }
        }
        break;
    }

    case WM_COMMAND:{
        int id=LOWORD(wp);

        if(id==IDC_BTN_SCAN&&!ble::isScanning()){
            wchar_t buf[16]={}; GetWindowTextW(GetDlgItem(hwnd,IDC_EDIT_TIMEOUT),buf,15);
            int timeout=_wtoi(buf); if(wcslen(buf)==0) timeout=30;
            {std::lock_guard<std::mutex> lk(s_mutex);s_devices.clear();}
            refreshListView(); updateFoundCount();
            EnableWindow(GetDlgItem(hwnd,IDC_BTN_SCAN), FALSE);
            EnableWindow(GetDlgItem(hwnd,IDC_BTN_SCANR),FALSE);
            EnableWindow(GetDlgItem(hwnd,IDC_BTN_STOP), TRUE);
            SetWindowTextW(GetDlgItem(hwnd,IDC_BTN_SCAN),L"Scanning...");
            InvalidateRect(GetDlgItem(hwnd,IDC_BTN_SCAN),nullptr,TRUE);
            setStatus(L"Scanning..."); s_sig.scanning=true;
            logAppend(L"[Scan] Start, timeout="+std::wstring(timeout==-1?L"-1":std::to_wstring(timeout))+L"s");
            ble::scan(hwnd,timeout,
                [](const ble::Device& d){
                    std::lock_guard<std::mutex> lk(s_mutex);
                    for(auto& e:s_devices){if(e.mac==d.mac){e=d;return;}}
                    s_devices.push_back(d);
                    logAppend(L"[+] "+d.name+L"  "+d.mac+L"  "+d.deviceClass+
                        (d.authenticated?L"  [Paired]":L"")+(d.connected?L"  [Connected]":L""));
                },nullptr);
        }

        if(id==IDC_BTN_SCANR&&!ble::isScanning()){
            {std::lock_guard<std::mutex> lk(s_mutex);s_devices.clear();}
            refreshListView(); updateFoundCount();
            EnableWindow(GetDlgItem(hwnd,IDC_BTN_SCAN), FALSE);
            EnableWindow(GetDlgItem(hwnd,IDC_BTN_SCANR),FALSE);
            EnableWindow(GetDlgItem(hwnd,IDC_BTN_STOP), TRUE);
            SetWindowTextW(GetDlgItem(hwnd,IDC_BTN_SCANR),L"Running...");
            InvalidateRect(GetDlgItem(hwnd,IDC_BTN_SCANR),nullptr,TRUE);
            setStatus(L"Realtime scan..."); s_sig.scanning=true;
            logAppend(L"[Scan-R] Realtime scan started (6s inquiry cycles).");
            ble::scanRealtime(hwnd,
                [](const ble::Device& d){
                    std::lock_guard<std::mutex> lk(s_mutex);
                    for(auto& e:s_devices){
                        if(e.mac==d.mac){
                            bool chg=(e.connected!=d.connected||e.battery!=d.battery);
                            e=d;
                            if(chg) logAppend(L"[~] "+d.name+L"  "+d.mac+(d.connected?L"  [Connected]":L"  [Lost]"));
                            return;
                        }
                    }
                    s_devices.push_back(d);
                    logAppend(L"[+] "+d.name+L"  "+d.mac+L"  "+d.deviceClass+L"  Bat="+d.battery+(d.authenticated?L"  [Paired]":L""));
                },nullptr);
        }

        if(id==IDC_BTN_STOP){
            ble::stopScan(); s_autoStop=true; s_sig.scanning=false;
            setStatus(L"Stopping...");
            EnableWindow(GetDlgItem(hwnd,IDC_BTN_STOP),FALSE);
            logAppend(L"[Scan] Stop requested.");
        }

        if(id==IDC_BTN_CLEAR&&!ble::isScanning()){
            std::lock_guard<std::mutex> lk(s_mutex);
            s_devices.clear(); ListView_DeleteAllItems(s_hwndLV);
            updateFoundCount(); setStatus(L"Cleared."); logAppend(L"[List] Cleared.");
        }

        if(id==IDC_BTN_CONNECT){
            int sel=ListView_GetNextItem(s_hwndLV,-1,LVNI_SELECTED);
            if(sel<0){setStatus(L"Select a device first.");break;}
            ble::Device dev;
            {std::lock_guard<std::mutex> lk(s_mutex);if(sel>=(int)s_devices.size())break;dev=s_devices[sel];}
            setStatus(L"Connecting to "+dev.mac+L"...");
            logAppend(L"[Connect] "+dev.name+L"  "+dev.mac);
            ble::connect(hwnd,dev,nullptr,3);
        }

        if(id==IDC_BTN_LOG)   logSetVisible(!s_logVisible);
        if(id==IDC_BTN_THEME) showThemeMenu(hwnd);
        if(id==IDC_BTN_BG)    showBgPicker(hwnd);
        break;
    }

    case WM_USER+1:{ // WM_BLE_SCAN_UPDATE
        refreshListView(); updateFoundCount();
        s_sig.radioPresent=true;
        if((LPARAM)lp==2){
            wchar_t buf[64]; std::lock_guard<std::mutex> lk(s_mutex);
            swprintf_s(buf,L"[Scan-R] Cycle #%d  %zu device(s)",(int)(WPARAM)wp,s_devices.size());
            setStatus(buf); logAppend(buf);
        }
        break;
    }

    case WM_USER+2:{ // WM_BLE_SCAN_DONE
        s_sig.scanning=false;
        EnableWindow(GetDlgItem(hwnd,IDC_BTN_SCAN), TRUE);
        EnableWindow(GetDlgItem(hwnd,IDC_BTN_SCANR),TRUE);
        EnableWindow(GetDlgItem(hwnd,IDC_BTN_STOP), FALSE);
        SetWindowTextW(GetDlgItem(hwnd,IDC_BTN_SCAN), L"Scan");
        SetWindowTextW(GetDlgItem(hwnd,IDC_BTN_SCANR),L"Scan-R");
        InvalidateRect(GetDlgItem(hwnd,IDC_BTN_SCAN), nullptr,TRUE);
        InvalidateRect(GetDlgItem(hwnd,IDC_BTN_SCANR),nullptr,TRUE);
        if((LPARAM)lp==1){
            s_sig.radioPresent=false;
            setStatus(L"Error: No Bluetooth radio.");
            logAppend(L"[ERROR] No radio found.");
        } else {
            std::lock_guard<std::mutex> lk(s_mutex);
            wchar_t buf[64]; swprintf_s(buf,L"Done. %zu device(s).",s_devices.size());
            setStatus(buf);
            logAppend(std::wstring(L"[Scan] Done. ")+std::to_wstring(s_devices.size())+L" device(s):");
            for(auto& d:s_devices)
                logAppend(L"  "+d.name+L"  "+d.mac+L"  "+d.deviceClass+L"  Bat="+d.battery+
                    (d.connected?L"  [Conn]":L"")+(d.authenticated?L"  [Auth]":L"")+(d.remembered?L"  [Rem]":L""));
        }
        refreshListView(); updateFoundCount();
        if(IsDlgButtonChecked(hwnd,IDC_CHECK_CONNALL)==BST_CHECKED){
            std::vector<ble::Device> copy;
            {std::lock_guard<std::mutex> lk(s_mutex);copy=s_devices;}
            if(!copy.empty()){
                s_autoStop=false;
                logAppend(L"[Auto] Starting loop for "+std::to_wstring(copy.size())+L" device(s)...");
                setStatus(L"Auto-connecting...");
                ble::autoConnectAll(hwnd,copy,&s_autoStop);
            }
        }
        break;
    }

    case WM_USER+3:{ // WM_BLE_CONNECT_RESULT
        auto* res=(ble::ConnectResult*)lp;
        if(res){
            std::wstring m=(res->success?L"[OK] ":L"[FAIL] ")+res->mac+L"  "+res->message;
            setStatus(m); logAppend(m);
            if(res->success){
                std::lock_guard<std::mutex> lk(s_mutex);
                for(auto& d:s_devices) if(d.mac==res->mac){d.connected=true;break;}
                refreshListView();
            }
            delete res;
        }
        break;
    }

    case WM_GETMINMAXINFO:{
        auto* m=(MINMAXINFO*)lp; m->ptMinTrackSize={800,520}; return 0;
    }

    case WM_DESTROY:{
        KillTimer(hwnd,1);
        ble::stopScan(); s_autoStop=true;
        if(s_bgBmp){delete s_bgBmp;s_bgBmp=nullptr;}
        if(s_fontUI)    DeleteObject(s_fontUI);
        if(s_fontMono)  DeleteObject(s_fontMono);
        if(s_fontSmall) DeleteObject(s_fontSmall);
        if(s_brBg)      DeleteObject(s_brBg);
        if(s_brPanel)   DeleteObject(s_brPanel);
        if(s_brBtn)     DeleteObject(s_brBtn);
        if(s_brEdit)    DeleteObject(s_brEdit);
        if(s_brLog)     DeleteObject(s_brLog);
        if(s_gdipTok)   Gdiplus::GdiplusShutdown(s_gdipTok);
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

static LRESULT CALLBACK BtnSubProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    switch(msg){
    case WM_MOUSEMOVE:
        if(!s_btnHover[hwnd]){
            s_btnHover[hwnd]=true;
            TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,hwnd,0};
            TrackMouseEvent(&tme); InvalidateRect(hwnd,nullptr,TRUE);}
        break;
    case WM_MOUSELEAVE:
        s_btnHover[hwnd]=false; InvalidateRect(hwnd,nullptr,TRUE); break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd,BtnSubProc,id); break;
    }
    return DefSubclassProc(hwnd,msg,wp,lp);
}

static LRESULT CALLBACK SigSubProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    if(msg==WM_PAINT){
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
        drawSignalPanel(hdc,hwnd);
        EndPaint(hwnd,&ps); return 0;
    }
    if(msg==WM_ERASEBKGND) return 1;
    if(msg==WM_NCDESTROY) RemoveWindowSubclass(hwnd,SigSubProc,id);
    return DefSubclassProc(hwnd,msg,wp,lp);
}

} // namespace gui
