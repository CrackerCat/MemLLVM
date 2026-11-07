#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include "chip8_core.h"
#include "chip8_roms.h"

#define IDM_PONG         1001
#define IDM_INVADERS     1002
#define IDM_TETRIS       1003
#define IDM_BLINKY       1004
#define IDM_MAZE         1005
#define IDM_OPEN         1006
#define IDM_RESET        1007
#define IDM_EXIT         1008

#define IDM_SPEED_SLOW   1010
#define IDM_SPEED_NORM   1011
#define IDM_SPEED_FAST   1012
#define IDM_SPEED_MAX    1013

#define IDM_COLOR_GREEN  1020
#define IDM_COLOR_CYAN   1021
#define IDM_COLOR_AMBER  1022
#define IDM_COLOR_BW     1023

#define IDM_PAUSE        1030
#define IDM_HELP         1031

static Chip8Core g_chip8;
static uint32_t  g_dib_pixels[64 * 32];
static BITMAPINFO g_bmi;

static uint32_t g_color_fg = 0x0033FF33; // Phosphor green
static uint32_t g_color_bg = 0x00101810; // Dark CRT green
static int g_cycles_per_frame = 10;      // 10 * 60 = 600 Hz
static const char *g_current_game = "Space Invaders";

static const uint8_t *g_current_rom_data = g_rom_invaders;
static size_t g_current_rom_size = sizeof(g_rom_invaders);

static uint8_t g_custom_rom[4096];
static size_t  g_custom_rom_size = 0;

static void update_pixels() {
    for (int i = 0; i < 64 * 32; ++i) {
        g_dib_pixels[i] = g_chip8.gfx[i] ? g_color_fg : g_color_bg;
    }
}

static void update_title(HWND hWnd) {
    char title[128];
    wsprintfA(title, "CHIP-8 Virtual Machine [MemLLVM] - %s (%d Hz)%s",
              g_current_game, g_cycles_per_frame * 60,
              g_chip8.paused ? " [PAUSED]" : "");
    SetWindowTextA(hWnd, title);
}

static void switch_game(HWND hWnd, const char *name, const uint8_t *data, size_t size) {
    g_current_game = name;
    g_current_rom_data = data;
    g_current_rom_size = size;
    g_chip8.load_rom(data, size);
    update_pixels();
    update_title(hWnd);
    InvalidateRect(hWnd, NULL, FALSE);
}

static int map_key(WPARAM wParam) {
    switch (wParam) {
    // 1-4
    case '1': return 0x1;
    case '2': return 0x2;
    case '3': return 0x3;
    case '4': return 0xC;
    // Q-R
    case 'Q': return 0x4;
    case 'W': return 0x5;
    case 'E': return 0x6;
    case 'R': return 0xD;
    // A-F
    case 'A': return 0x7;
    case 'S': return 0x8;
    case 'D': return 0x9;
    case 'F': return 0xE;
    // Z-V
    case 'Z': return 0xA;
    case 'X': return 0x0;
    case 'C': return 0xB;
    case 'V': return 0xF;
    // Common game control helpers
    case VK_LEFT:  return 0x4; // Left
    case VK_RIGHT: return 0x6; // Right
    case VK_UP:    return 0x5; // Up / Rotate / Pong P2 Up
    case VK_DOWN:  return 0x8; // Down / Drop / Pong P2 Down
    case VK_SPACE: return 0x5; // Fire
    default: return -1;
    }
}

static HMENU create_app_menu() {
    HMENU hMenuBar = CreateMenu();
    HMENU hGameMenu = CreatePopupMenu();
    HMENU hSpeedMenu = CreatePopupMenu();
    HMENU hColorMenu = CreatePopupMenu();
    HMENU hHelpMenu = CreatePopupMenu();

    AppendMenuA(hGameMenu, MF_STRING, IDM_INVADERS, "1. Space Invaders");
    AppendMenuA(hGameMenu, MF_STRING, IDM_PONG,     "2. Pong (2 Players)");
    AppendMenuA(hGameMenu, MF_STRING, IDM_TETRIS,   "3. Tetris");
    AppendMenuA(hGameMenu, MF_STRING, IDM_BLINKY,   "4. Blinky (Pac-Man)");
    AppendMenuA(hGameMenu, MF_STRING, IDM_MAZE,     "5. Maze Generator");
    AppendMenuA(hGameMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hGameMenu, MF_STRING, IDM_OPEN,     "Load ROM File...");
    AppendMenuA(hGameMenu, MF_STRING, IDM_RESET,    "Reset Current Game\tF5");
    AppendMenuA(hGameMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hGameMenu, MF_STRING, IDM_EXIT,     "Exit\tAlt+F4");

    AppendMenuA(hSpeedMenu, MF_STRING, IDM_SPEED_SLOW, "Slow (300 Hz)");
    AppendMenuA(hSpeedMenu, MF_STRING, IDM_SPEED_NORM, "Normal (600 Hz)");
    AppendMenuA(hSpeedMenu, MF_STRING, IDM_SPEED_FAST, "Fast (1200 Hz)");
    AppendMenuA(hSpeedMenu, MF_STRING, IDM_SPEED_MAX,  "Ultra (2400 Hz)");

    AppendMenuA(hColorMenu, MF_STRING, IDM_COLOR_GREEN, "Retro Phosphor Green");
    AppendMenuA(hColorMenu, MF_STRING, IDM_COLOR_CYAN,  "Cyberpunk Cyan");
    AppendMenuA(hColorMenu, MF_STRING, IDM_COLOR_AMBER, "GameBoy / Amber");
    AppendMenuA(hColorMenu, MF_STRING, IDM_COLOR_BW,    "Classic Monochrome");

    AppendMenuA(hHelpMenu, MF_STRING, IDM_PAUSE, "Pause / Resume\tP");
    AppendMenuA(hHelpMenu, MF_STRING, IDM_HELP,  "Keypad Instructions...");

    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hGameMenu,  "&Game");
    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hSpeedMenu, "&Speed");
    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hColorMenu, "&Palette");
    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hHelpMenu,  "&Control");

    return hMenuBar;
}

static void open_rom_dialog(HWND hWnd) {
    char szFile[MAX_PATH] = { 0 };
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = "CHIP-8 ROM Files (*.ch8;*.*)\0*.ch8;*.*\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        HANDLE hFile = CreateFileA(szFile, GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD bytesRead = 0;
            DWORD fileSize = GetFileSize(hFile, NULL);
            if (fileSize > 0 && fileSize <= sizeof(g_custom_rom)) {
                ReadFile(hFile, g_custom_rom, fileSize, &bytesRead, NULL);
                g_custom_rom_size = bytesRead;
                CloseHandle(hFile);

                const char *pSlash = szFile;
                for (const char *p = szFile; *p; ++p) {
                    if (*p == '\\' || *p == '/') pSlash = p + 1;
                }
                switch_game(hWnd, pSlash, g_custom_rom, g_custom_rom_size);
                return;
            }
            CloseHandle(hFile);
        }
        MessageBoxA(hWnd, "Failed to read the selected ROM file.", "Error", MB_ICONERROR);
    }
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        SetTimer(hWnd, 1, 16, NULL); // ~60 Hz timer
        switch_game(hWnd, "Space Invaders", g_rom_invaders, sizeof(g_rom_invaders));
        return 0;
    }

    case WM_TIMER: {
        if (!g_chip8.paused) {
            for (int i = 0; i < g_cycles_per_frame; ++i) {
                g_chip8.emulate_cycle();
            }
            g_chip8.tick_timers();
            if (g_chip8.sound_timer > 0) {
                MessageBeep(0xFFFFFFFF);
            }
        }
        if (g_chip8.draw_flag) {
            update_pixels();
            g_chip8.draw_flag = false;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int clientW = rc.right - rc.left;
        int clientH = rc.bottom - rc.top;

        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchDIBits(hdc,
                      0, 0, clientW, clientH,
                      0, 0, 64, 32,
                      g_dib_pixels,
                      &g_bmi,
                      DIB_RGB_COLORS,
                      SRCCOPY);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_F5) {
            switch_game(hWnd, g_current_game, g_current_rom_data, g_current_rom_size);
            return 0;
        }
        if (wParam == 'P') {
            g_chip8.paused = !g_chip8.paused;
            update_title(hWnd);
            return 0;
        }
        int k = map_key(wParam);
        if (k >= 0) {
            g_chip8.set_key(k, true);
        }
        return 0;
    }

    case WM_KEYUP: {
        int k = map_key(wParam);
        if (k >= 0) {
            g_chip8.set_key(k, false);
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case IDM_INVADERS:
            switch_game(hWnd, "Space Invaders", g_rom_invaders, sizeof(g_rom_invaders));
            break;
        case IDM_PONG:
            switch_game(hWnd, "Pong", g_rom_pong, sizeof(g_rom_pong));
            break;
        case IDM_TETRIS:
            switch_game(hWnd, "Tetris", g_rom_tetris, sizeof(g_rom_tetris));
            break;
        case IDM_BLINKY:
            switch_game(hWnd, "Blinky", g_rom_blinky, sizeof(g_rom_blinky));
            break;
        case IDM_MAZE:
            switch_game(hWnd, "Maze Generator", g_rom_maze, sizeof(g_rom_maze));
            break;
        case IDM_OPEN:
            open_rom_dialog(hWnd);
            break;
        case IDM_RESET:
            switch_game(hWnd, g_current_game, g_current_rom_data, g_current_rom_size);
            break;
        case IDM_EXIT:
            PostQuitMessage(0);
            break;

        case IDM_SPEED_SLOW:
            g_cycles_per_frame = 5; // 300 Hz
            update_title(hWnd);
            break;
        case IDM_SPEED_NORM:
            g_cycles_per_frame = 10; // 600 Hz
            update_title(hWnd);
            break;
        case IDM_SPEED_FAST:
            g_cycles_per_frame = 20; // 1200 Hz
            update_title(hWnd);
            break;
        case IDM_SPEED_MAX:
            g_cycles_per_frame = 40; // 2400 Hz
            update_title(hWnd);
            break;

        case IDM_COLOR_GREEN:
            g_color_fg = 0x0033FF33;
            g_color_bg = 0x00101810;
            update_pixels();
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        case IDM_COLOR_CYAN:
            g_color_fg = 0x0000F0FF;
            g_color_bg = 0x00081018;
            update_pixels();
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        case IDM_COLOR_AMBER:
            g_color_fg = 0x00FFAA00;
            g_color_bg = 0x00201400;
            update_pixels();
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        case IDM_COLOR_BW:
            g_color_fg = 0x00FFFFFF;
            g_color_bg = 0x00000000;
            update_pixels();
            InvalidateRect(hWnd, NULL, FALSE);
            break;

        case IDM_PAUSE:
            g_chip8.paused = !g_chip8.paused;
            update_title(hWnd);
            break;

        case IDM_HELP:
            MessageBoxA(hWnd,
                "CHIP-8 Virtual Keypad Mapping:\n\n"
                "  1 2 3 4   ->  Keypad 1 2 3 C\n"
                "  Q W E R   ->  Keypad 4 5 6 D\n"
                "  A S D F   ->  Keypad 7 8 9 E\n"
                "  Z X C V   ->  Keypad A 0 B F\n\n"
                "Arrow Keys & Space are also mapped:\n"
                "  Left/Right  : Left/Right (Keys 4, 6)\n"
                "  Up / Space  : Fire / Action (Key 5)\n"
                "  Down        : Down / Drop (Key 8)\n\n"
                "Shortcuts:\n"
                "  F5 : Reset ROM\n"
                "  P  : Pause / Resume",
                "CHIP-8 Controls", MB_OK | MB_ICONINFORMATION);
            break;
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Prevent flicker

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int main() {
    HINSTANCE hInstance = GetModuleHandleA(NULL);

    // Initialize Bitmap Header (top-down 32-bit DIB, 64x32)
    ZeroMemory(&g_bmi, sizeof(g_bmi));
    g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth = 64;
    g_bmi.bmiHeader.biHeight = -32; // Negative for top-down
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;

    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = "C8_WndClass";

    if (!RegisterClassExA(&wc)) {
        return 1;
    }

    // Desired client area: 640x320 (10x scaling)
    RECT rc = { 0, 0, 640, 320 };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, TRUE);

    HMENU hMenu = create_app_menu();

    HWND hWnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "CHIP-8 Virtual Machine [MemLLVM]",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL,
        hMenu,
        hInstance,
        NULL
    );

    if (!hWnd) {
        return 2;
    }

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
