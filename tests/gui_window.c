#include <windows.h>
#include <math.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

int _fltused = 0x9876;

#define SCREEN_W 640
#define SCREEN_H 480

typedef struct { float x, y, z; } Vec3;
typedef struct { int x, y; float z; } ScreenVert;

// 12 个三角形构成的 3D 立方体网格（顶点表 + 索引表）
static const Vec3 g_MeshVertices[8] = {
    {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f},
    { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f},
    {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f},
    { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}
};

static const int g_MeshTriangles[12][3] = {
    {0, 2, 1}, {0, 3, 2}, // 后
    {4, 5, 6}, {4, 6, 7}, // 前
    {0, 1, 5}, {0, 5, 4}, // 底
    {3, 6, 2}, {3, 7, 6}, // 顶
    {0, 4, 7}, {0, 7, 3}, // 左
    {1, 2, 6}, {1, 6, 5}  // 右
};

// 交互视角与渲染缓冲
static float g_RotX = 0.5f, g_RotY = 0.5f;
static float g_Zoom = 2.5f;
static BOOL  g_Dragging = FALSE;
static POINT g_LastMouse;

static unsigned int* g_ColorBuffer = NULL;
static float*        g_DepthBuffer = NULL;

// 向量基础运算
static Vec3 Vec3Sub(Vec3 a, Vec3 b) { return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static float Vec3Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 Vec3Cross(Vec3 a, Vec3 b) {
    return (Vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static Vec3 Vec3Normalize(Vec3 v) {
    float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-6f) return (Vec3){0, 0, 0};
    return (Vec3){v.x / len, v.y / len, v.z / len};
}

// 旋转变换
static Vec3 Rotate(Vec3 p, float ax, float ay) {
    // 绕 X
    float y1 = p.y * cosf(ax) - p.z * sinf(ax);
    float z1 = p.y * sinf(ax) + p.z * cosf(ax);
    // 绕 Y
    float x2 = p.x * cosf(ay) + z1 * sinf(ay);
    float z2 = -p.x * sinf(ay) + z1 * cosf(ay);
    return (Vec3){x2, y1, z2};
}

// 屏幕重投影
static ScreenVert ProjectToScreen(Vec3 p) {
    float fov = 350.0f;
    float zDist = p.z + g_Zoom;
    if (zDist < 0.1f) zDist = 0.1f;

    ScreenVert v;
    v.x = (int)((p.x * fov) / zDist + (SCREEN_W / 2.0f));
    v.y = (int)((-p.y * fov) / zDist + (SCREEN_H / 2.0f));
    v.z = zDist;
    return v;
}

// 重心坐标测试与三角形栅格化填充（手写软件光栅化核心）
static void RasterizeTriangle(ScreenVert v0, ScreenVert v1, ScreenVert v2, unsigned int color) {
    int minX = v0.x < v1.x ? (v0.x < v2.x ? v0.x : v2.x) : (v1.x < v2.x ? v1.x : v2.x);
    int maxX = v0.x > v1.x ? (v0.x > v2.x ? v0.x : v2.x) : (v1.x > v2.x ? v1.x : v2.x);
    int minY = v0.y < v1.y ? (v0.y < v2.y ? v0.y : v2.y) : (v1.y < v2.y ? v1.y : v2.y);
    int maxY = v0.y > v1.y ? (v0.y > v2.y ? v0.y : v2.y) : (v1.y > v2.y ? v1.y : v2.y);

    // 屏幕包围盒边界裁剪
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= SCREEN_W) maxX = SCREEN_W - 1;
    if (maxY >= SCREEN_H) maxY = SCREEN_H - 1;

    int area = (v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x);
    if (area <= 0) return; // 背面剔除 / 退化三角形忽略

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            int w0 = (v2.x - v1.x) * (y - v1.y) - (v2.y - v1.y) * (x - v1.x);
            int w1 = (v0.x - v2.x) * (y - v2.y) - (v0.y - v2.y) * (x - v2.x);
            int w2 = (v1.x - v0.x) * (y - v0.y) - (v1.y - v0.y) * (x - v0.x);

            // 落在三角形内部
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                float l0 = (float)w0 / (float)area;
                float l1 = (float)w1 / (float)area;
                float l2 = (float)w2 / (float)area;
                float z = l0 * v0.z + l1 * v1.z + l2 * v2.z;

                int idx = y * SCREEN_W + x;
                // 深度测试 (Z-Buffer Test)
                if (z < g_DepthBuffer[idx]) {
                    g_DepthBuffer[idx] = z;
                    g_ColorBuffer[idx] = color;
                }
            }
        }
    }
}

// 渲染管线执行
static void RenderPipeline(void) {
    // 1. 清除缓冲区
    for (int i = 0; i < SCREEN_W * SCREEN_H; ++i) {
        g_ColorBuffer[i] = 0xFF12141A; // 背景底色
        g_DepthBuffer[i] = 1e9f;       // 重置深度缓冲为无穷远
    }

    // 2. 变换全部顶点
    Vec3 transVerts[8];
    ScreenVert screenVerts[8];
    for (int i = 0; i < 8; ++i) {
        transVerts[i] = Rotate(g_MeshVertices[i], g_RotX, g_RotY);
        screenVerts[i] = ProjectToScreen(transVerts[i]);
    }

    // 平行光方向 (从斜上方照向屏幕深处)
    Vec3 lightDir = Vec3Normalize((Vec3){0.5f, 1.0f, -0.8f});

    // 3. 逐三角面着色与光栅化
    for (int i = 0; i < 12; ++i) {
        int i0 = g_MeshTriangles[i][0];
        int i1 = g_MeshTriangles[i][1];
        int i2 = g_MeshTriangles[i][2];

        // 计算世界空间表面法线
        Vec3 edge1 = Vec3Sub(transVerts[i1], transVerts[i0]);
        Vec3 edge2 = Vec3Sub(transVerts[i2], transVerts[i0]);
        Vec3 normal = Vec3Normalize(Vec3Cross(edge1, edge2));

        // 漫反射光照强度 (0.1 基础环境光 + 兰伯特余弦)
        float diff = Vec3Dot(normal, lightDir);
        if (diff < 0.0f) diff = 0.0f;
        float intensity = 0.15f + 0.85f * diff;

        // 根据强度调制青蓝色
        unsigned char r = (unsigned char)(0.0f * intensity);
        unsigned char g = (unsigned char)(180.0f * intensity);
        unsigned char b = (unsigned char)(255.0f * intensity);
        unsigned int faceColor = (r << 16) | (g << 8) | b;

        RasterizeTriangle(screenVerts[i0], screenVerts[i1], screenVerts[i2], faceColor);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HDC memDC = NULL;
    static HBITMAP hBmp = NULL;

    switch (msg) {
    case WM_CREATE: {
        HDC hdc = GetDC(hWnd);
        memDC = CreateCompatibleDC(hdc);

        // 创建直接可写内存的 32 位 DIB 位图
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = SCREEN_W;
        bmi.bmiHeader.biHeight = -SCREEN_H; // Top-Down 坐标系
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, (void**)&g_ColorBuffer, NULL, 0);
        g_DepthBuffer = (float*)VirtualAlloc(NULL, sizeof(float) * SCREEN_W * SCREEN_H, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        SelectObject(memDC, hBmp);
        ReleaseDC(hWnd, hdc);
        break;
    }
    case WM_LBUTTONDOWN:
        g_Dragging = TRUE;
        SetCapture(hWnd);
        g_LastMouse.x = (short)(lParam & 0xFFFF);
        g_LastMouse.y = (short)((lParam >> 16) & 0xFFFF);
        break;

    case WM_LBUTTONUP:
        if (g_Dragging) {
            g_Dragging = FALSE;
            ReleaseCapture();
        }
        break;

    case WM_MOUSEMOVE:
        if (g_Dragging) {
            short curX = (short)(lParam & 0xFFFF);
            short curY = (short)((lParam >> 16) & 0xFFFF);
            g_RotY += (curX - g_LastMouse.x) * 0.008f;
            g_RotX += (curY - g_LastMouse.y) * 0.008f;
            g_LastMouse.x = curX;
            g_LastMouse.y = curY;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;

    case WM_MOUSEWHEEL: {
        short delta = (short)((wParam >> 16) & 0xFFFF);
        g_Zoom -= (delta > 0 ? 0.25f : -0.25f);
        if (g_Zoom < 1.2f) g_Zoom = 1.2f;
        if (g_Zoom > 6.0f) g_Zoom = 6.0f;
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RenderPipeline();
        // 一次性无闪烁拷贝至屏幕
        BitBlt(hdc, 0, 0, SCREEN_W, SCREEN_H, memDC, 0, 0, SRCCOPY);
        EndPaint(hWnd, &ps);
        break;
    }
    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        if (g_DepthBuffer) {
            VirtualFree(g_DepthBuffer, 0, MEM_RELEASE);
            g_DepthBuffer = NULL;
        }
        DeleteDC(memDC);
        DeleteObject(hBmp);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int main(void) {
    HINSTANCE hInst = GetModuleHandleA(NULL);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "RasterizerCanvas";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    HWND hWnd = CreateWindowExA(
        0, wc.lpszClassName, 
        "Win32 GDI - Software Rasterizer (Z-Buffer + Diffuse Shading)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 
        SCREEN_W + 16, SCREEN_H + 39, NULL, NULL, hInst, NULL
    );

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}