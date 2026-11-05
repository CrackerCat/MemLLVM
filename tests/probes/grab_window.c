/* grab_window -- capture a DWM-composited window to a BMP.
 *
 * The obvious approach, GetWindowDC + BitBlt, returns solid black for any
 * window Windows composites through DWM, which is every window drawn with a
 * modern framework. The pixels are not in the window DC at all; they live in a
 * redirection surface that only PrintWindow knows how to reach. With
 * PW_RENDERFULLCONTENT (0x2) PrintWindow asks DWM for that surface, which is
 * the only way to get the real contents of a window whose UI is drawn by hand
 * and has no child controls to read text from.
 *
 * Usage: grab_window.exe <title-prefix> <out.bmp>
 */

#include <windows.h>
#include <stdio.h>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

static const wchar_t *g_prefix;
static HWND g_found;

static BOOL CALLBACK FindByTitle(HWND h, LPARAM param)
{
    wchar_t title[512] = {0};
    (void)param;
    GetWindowTextW(h, title, 512);
    if (title[0] && wcsncmp(title, g_prefix, wcslen(g_prefix)) == 0) {
        g_found = h;
        return FALSE;
    }
    return TRUE;
}

static BOOL SaveBmp(const char *path, HDC hdc, HBITMAP hbm, int w, int h)
{
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    BITMAP bm;
    DWORD stride, size, wrote = 0;
    HANDLE f;
    unsigned char *bits;

    GetObjectA(hbm, sizeof(bm), &bm);
    stride = ((w * 32 + 31) / 32) * 4;
    size = stride * h;
    bits = (unsigned char *)VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE,
                                         PAGE_READWRITE);
    if (!bits)
        return FALSE;

    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = size;
    ih.biXPelsPerMeter = 0;
    ih.biYPelsPerMeter = 0;
    ih.biClrUsed = 0;
    ih.biClrImportant = 0;
    if (!GetDIBits(hdc, hbm, 0, (UINT)h, bits, (BITMAPINFO *)&ih, DIB_RGB_COLORS)) {
        VirtualFree(bits, 0, MEM_RELEASE);
        return FALSE;
    }

    fh.bfType = 0x4D42;
    fh.bfSize = sizeof(fh) + sizeof(ih) + size;
    fh.bfReserved1 = 0;
    fh.bfReserved2 = 0;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);

    f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        VirtualFree(bits, 0, MEM_RELEASE);
        return FALSE;
    }
    WriteFile(f, &fh, sizeof(fh), &wrote, NULL);
    WriteFile(f, &ih, sizeof(ih), &wrote, NULL);
    WriteFile(f, bits, size, &wrote, NULL);
    CloseHandle(f);
    VirtualFree(bits, 0, MEM_RELEASE);
    return TRUE;
}

int main(int argc, char **argv)
{
    HWND hwnd;
    RECT rc;
    int w, h;
    HDC screen, mem;
    HBITMAP hbm;
    BOOL ok;
    wchar_t prefix[256] = {0};
    char msg[512];
    int n;

    if (argc < 3) {
        return 2;
    }
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, prefix, 255);
    g_prefix = prefix;

    EnumWindows(FindByTitle, 0);
    if (!g_found) {
        n = wsprintfA(msg, "window not found: %s\r\n", argv[1]);
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, n, (DWORD[]){0}, NULL);
        return 1;
    }
    hwnd = g_found;

    GetClientRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) {
        n = wsprintfA(msg, "empty client rect %dx%d\r\n", w, h);
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, n, (DWORD[]){0}, NULL);
        return 1;
    }

    screen = GetDC(NULL);
    mem = CreateCompatibleDC(screen);
    hbm = CreateCompatibleBitmap(screen, w, h);
    SelectObject(mem, hbm);
    SetBkMode(mem, OPAQUE);
    SetBkColor(mem, RGB(255, 255, 255));

    ok = PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT);
    if (!ok)
        ok = PrintWindow(hwnd, mem, 0);

    n = wsprintfA(msg, "hwnd=%p client=%dx%d PrintWindow=%d\r\n",
                  (void *)hwnd, w, h, (int)ok);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), msg, n, (DWORD[]){0}, NULL);

    if (!SaveBmp(argv[2], mem, hbm, w, h)) {
        n = wsprintfA(msg, "save failed: %s\r\n", argv[2]);
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, n, (DWORD[]){0}, NULL);
        return 1;
    }

    n = wsprintfA(msg, "saved %s\r\n", argv[2]);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), msg, n, (DWORD[]){0}, NULL);

    DeleteObject(hbm);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    return ok ? 0 : 1;
}
