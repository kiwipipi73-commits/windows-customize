// taskbar_ui.cpp
// Build (clang):
//   clang++ taskbar_ui.cpp -o taskbar_ui.exe -mwindows -ladvapi32 -luser32 -lgdi32 -lcomdlg32
// Build (clang-cl):
//   clang-cl /EHsc taskbar_ui.cpp user32.lib gdi32.lib advapi32.lib comdlg32.lib /link /SUBSYSTEM:WINDOWS

#include <windows.h>
#include <commdlg.h>
#include <cstdio>
#include <cstdlib>

enum { ID_BAR = 100, ID_PICK = 110, ID_APPLY, ID_RESET, ID_RESTART };

static HWND g_bar[3], g_val[3], g_restart;
static int  g_rgb[3] = {0, 120, 215};
static const RECT g_preview = {20, 130, 320, 170};

static bool SetDword(const char* path, const char* name, DWORD v) {
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExA(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static bool SetBinary(const char* path, const char* name, const BYTE* d, DWORD n) {
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExA(k, name, 0, REG_BINARY, d, n);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static void Broadcast() {
    DWORD_PTR res;
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        (LPARAM)"ImmersiveColorSet", SMTO_ABORTIFHUNG, 5000, &res);
}

static void RestartExplorer() {
    system("taskkill /f /im explorer.exe >nul 2>&1 & start explorer.exe");
}

static const char* PERS = "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
static const char* ACC  = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent";
static const char* DWMK = "Software\\Microsoft\\Windows\\DWM";
static const char* DESK = "Control Panel\\Desktop";

static void ApplyColor(HWND hwnd) {
    BYTE r = (BYTE)g_rgb[0], g = (BYTE)g_rgb[1], b = (BYTE)g_rgb[2];
    DWORD abgr = (0xFFu << 24) | ((DWORD)b << 16) | ((DWORD)g << 8) | r;

    // 8-entry accent palette (R,G,B,A each); the taskbar reads from this
    BYTE palette[32];
    for (int i = 0; i < 8; i++) {
        palette[i * 4 + 0] = r; palette[i * 4 + 1] = g;
        palette[i * 4 + 2] = b; palette[i * 4 + 3] = 0xFF;
    }

    bool ok = true;
    ok &= SetDword(DESK, "AutoColorization", 0);
    ok &= SetDword(PERS, "SystemUsesLightTheme", 0);   // taskbar accent needs dark mode
    ok &= SetDword(PERS, "ColorPrevalence", 1);        // show accent on Start + taskbar
    ok &= SetDword(ACC, "AccentColorMenu", abgr);
    ok &= SetDword(ACC, "StartColorMenu", abgr);
    ok &= SetBinary(ACC, "AccentPalette", palette, sizeof(palette));
    ok &= SetDword(DWMK, "AccentColor", abgr);
    ok &= SetDword(DWMK, "ColorizationColor", abgr);
    ok &= SetDword(DWMK, "ColorizationAfterglow", abgr);
    Broadcast();

    if (!ok) MessageBoxA(hwnd, "Some registry writes failed.", "Taskbar Color", MB_ICONWARNING);
    if (SendMessageA(g_restart, BM_GETCHECK, 0, 0) == BST_CHECKED) RestartExplorer();
}

static void UpdateLabels(HWND hwnd) {
    char buf[16];
    for (int i = 0; i < 3; i++) {
        snprintf(buf, sizeof(buf), "%d", g_rgb[i]);
        SetWindowTextA(g_val[i], buf);
        SetScrollPos(g_bar[i], SB_CTL, g_rgb[i], TRUE);
    }
    InvalidateRect(hwnd, &g_preview, FALSE);
}

static HWND Mk(HWND p, const char* cls, const char* txt, DWORD style,
               int x, int y, int w, int h, int id) {
    HWND c = CreateWindowA(cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                           p, (HMENU)(INT_PTR)id, GetModuleHandleA(nullptr), nullptr);
    SendMessageA(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return c;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        const char* names[3] = {"R", "G", "B"};
        for (int i = 0; i < 3; i++) {
            int y = 20 + i * 32;
            Mk(hwnd, "STATIC", names[i], 0, 20, y + 2, 16, 20, 0);
            g_bar[i] = Mk(hwnd, "SCROLLBAR", "", SBS_HORZ, 45, y, 220, 20, ID_BAR + i);
            SetScrollRange(g_bar[i], SB_CTL, 0, 255, FALSE);
            g_val[i] = Mk(hwnd, "STATIC", "0", 0, 280, y + 2, 40, 20, 0);
        }
        Mk(hwnd, "BUTTON", "Pick color...", BS_PUSHBUTTON, 20, 185, 100, 28, ID_PICK);
        Mk(hwnd, "BUTTON", "Apply", BS_DEFPUSHBUTTON, 130, 185, 90, 28, ID_APPLY);
        Mk(hwnd, "BUTTON", "Reset", BS_PUSHBUTTON, 230, 185, 90, 28, ID_RESET);
        g_restart = Mk(hwnd, "BUTTON", "Restart Explorer after applying (Unrecommended)",
                       BS_AUTOCHECKBOX, 20, 225, 300, 20, ID_RESTART);
        SendMessageA(g_restart, BM_SETCHECK, BST_CHECKED, 0);
        UpdateLabels(hwnd);
        return 0;
    }
    case WM_HSCROLL: {
        for (int i = 0; i < 3; i++) {
            if ((HWND)lp != g_bar[i]) continue;
            int pos = GetScrollPos(g_bar[i], SB_CTL);
            switch (LOWORD(wp)) {
                case SB_LINELEFT:  pos -= 1;  break;
                case SB_LINERIGHT: pos += 1;  break;
                case SB_PAGELEFT:  pos -= 16; break;
                case SB_PAGERIGHT: pos += 16; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: pos = HIWORD(wp); break;
            }
            g_rgb[i] = pos < 0 ? 0 : (pos > 255 ? 255 : pos);
            UpdateLabels(hwnd);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_PICK) {
            static COLORREF custom[16];
            CHOOSECOLORA cc = {};
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = hwnd;
            cc.rgbResult = RGB(g_rgb[0], g_rgb[1], g_rgb[2]);
            cc.lpCustColors = custom;
            cc.Flags = CC_RGBINIT | CC_FULLOPEN;
            if (ChooseColorA(&cc)) {
                g_rgb[0] = GetRValue(cc.rgbResult);
                g_rgb[1] = GetGValue(cc.rgbResult);
                g_rgb[2] = GetBValue(cc.rgbResult);
                UpdateLabels(hwnd);
            }
        } else if (LOWORD(wp) == ID_APPLY) {
            ApplyColor(hwnd);
        } else if (LOWORD(wp) == ID_RESET) {
            SetDword(PERS, "ColorPrevalence", 0);
            Broadcast();
            if (SendMessageA(g_restart, BM_GETCHECK, 0, 0) == BST_CHECKED) RestartExplorer();
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        HBRUSH br = CreateSolidBrush(RGB(g_rgb[0], g_rgb[1], g_rgb[2]));
        FillRect(dc, &g_preview, br);
        FrameRect(dc, &g_preview, (HBRUSH)GetStockObject(BLACK_BRUSH));
        DeleteObject(br);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "TaskbarColorUI";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA("TaskbarColorUI", "Taskbar Color",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 355, 305, nullptr, nullptr, inst, nullptr);
    ShowWindow(hwnd, show);

    MSG m;
    while (GetMessageA(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}
