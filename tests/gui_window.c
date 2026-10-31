#include <windows.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// 消除脱离 CRT 时的浮点符号依赖（如果开启了浮点）
int _fltused = 0x9876;

#define SCREEN_W 400
#define SCREEN_H 600
#define PIPE_COUNT 3
#define GAP_SIZE 150
#define PIPE_WIDTH 60

// 游戏状态枚举
typedef enum {
    STATE_READY = 0,
    STATE_PLAYING,
    STATE_GAMEOVER
} GameState;

// 障碍物管道结构体
typedef struct {
    int x;
    int topHeight;
    BOOL passed;
} Pipe;

// 游戏核心数据状态
static GameState g_State = STATE_READY;
static float g_BirdY = 250.0f;
static float g_BirdVelocity = 0.0f;
static const float GRAVITY = 0.65f;
static const float JUMP_FORCE = -8.5f;

static Pipe g_Pipes[PIPE_COUNT];
static int g_Score = 0;

// 极简伪随机数生成器（彻底避免引用 CRT 的 rand()）
static unsigned int g_RandSeed = 123456789;
static int FastRand(void) {
    g_RandSeed = (214013 * g_RandSeed + 2531011);
    return (int)((g_RandSeed >> 16) & 0x7FFF);
}

// 重置游戏
static void ResetGame(void) {
    g_BirdY = 250.0f;
    g_BirdVelocity = 0.0f;
    g_Score = 0;
    g_State = STATE_READY;

    for (int i = 0; i < PIPE_COUNT; i++) {
        g_Pipes[i].x = SCREEN_W + 100 + i * 220;
        g_Pipes[i].topHeight = 80 + (FastRand() % 220);
        g_Pipes[i].passed = FALSE;
    }
}

// 物理与状态更新 (每帧执行一次)
static void UpdatePhysics(void) {
    if (g_State != STATE_PLAYING) return;

    // 1. 鸟类重力模拟
    g_BirdVelocity += GRAVITY;
    g_BirdY += g_BirdVelocity;

    // 地面与天花板碰撞
    if (g_BirdY > SCREEN_H - 40 - 20) {
        g_BirdY = SCREEN_H - 40 - 20;
        g_State = STATE_GAMEOVER;
    }
    if (g_BirdY < 0) {
        g_BirdY = 0;
        g_BirdVelocity = 0;
    }

    // 2. 管道移动与循环
    for (int i = 0; i < PIPE_COUNT; i++) {
        g_Pipes[i].x -= 4; // 管道向左平移

        // 移出屏幕后重新排到最右侧
        if (g_Pipes[i].x + PIPE_WIDTH < 0) {
            g_Pipes[i].x = SCREEN_W + 100;
            g_Pipes[i].topHeight = 80 + (FastRand() % 220);
            g_Pipes[i].passed = FALSE;
        }

        // 计分检测
        if (!g_Pipes[i].passed && g_Pipes[i].x < 80) {
            g_Pipes[i].passed = TRUE;
            g_Score++;
        }

        // 3. AABB 矩形碰撞检测
        // 鸟的包围盒：X: 80~104, Y: g_BirdY ~ g_BirdY + 24
        int birdL = 80, birdR = 104;
        int birdT = (int)g_BirdY, birdB = (int)g_BirdY + 24;

        int pipeL = g_Pipes[i].x;
        int pipeR = g_Pipes[i].x + PIPE_WIDTH;

        // 与上方管道碰撞
        if (birdR > pipeL && birdL < pipeR && birdT < g_Pipes[i].topHeight) {
            g_State = STATE_GAMEOVER;
        }
        // 与下方管道碰撞
        int bottomPipeY = g_Pipes[i].topHeight + GAP_SIZE;
        if (birdR > pipeL && birdL < pipeR && birdB > bottomPipeY) {
            g_State = STATE_GAMEOVER;
        }
    }
}

// 整数转字符串（不调用 CRT sprintf，便于直接内联）
static void IntToStr(int val, char* buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char temp[16];
    int p = 0;
    while (val > 0) {
        temp[p++] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (int i = 0; i < p; i++) {
        buf[i] = temp[p - 1 - i];
    }
    buf[p] = '\0';
}

// 纯 GDI 渲染引擎（双缓冲）
static void RenderScene(HDC hdc) {
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, SCREEN_W, SCREEN_H);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // 1. 绘制天空背景 (浅天蓝色)
    RECT rcSky = {0, 0, SCREEN_W, SCREEN_H - 40};
    HBRUSH skyBrush = CreateSolidBrush(RGB(112, 197, 206));
    FillRect(memDC, &rcSky, skyBrush);
    DeleteObject(skyBrush);

    // 2. 绘制草地与地面 (黄绿底色)
    RECT rcGround = {0, SCREEN_H - 40, SCREEN_W, SCREEN_H};
    HBRUSH groundBrush = CreateSolidBrush(RGB(222, 216, 149));
    FillRect(memDC, &rcGround, groundBrush);
    DeleteObject(groundBrush);

    // 绘制草地绿边条
    RECT rcGrass = {0, SCREEN_H - 40, SCREEN_W, SCREEN_H - 32};
    HBRUSH grassBrush = CreateSolidBrush(RGB(115, 191, 46));
    FillRect(memDC, &rcGrass, grassBrush);
    DeleteObject(grassBrush);

    // 3. 绘制障碍物管道 (翡翠绿)
    HBRUSH pipeBrush = CreateSolidBrush(RGB(115, 191, 46));
    HPEN pipeBorder = CreatePen(PS_SOLID, 2, RGB(83, 56, 71));
    SelectObject(memDC, pipeBrush);
    SelectObject(memDC, pipeBorder);

    for (int i = 0; i < PIPE_COUNT; i++) {
        // 上管道
        Rectangle(memDC, g_Pipes[i].x, -2, g_Pipes[i].x + PIPE_WIDTH, g_Pipes[i].topHeight);
        // 下管道
        Rectangle(memDC, g_Pipes[i].x, g_Pipes[i].topHeight + GAP_SIZE,
                  g_Pipes[i].x + PIPE_WIDTH, SCREEN_H - 40);
    }
    DeleteObject(pipeBrush);
    DeleteObject(pipeBorder);

    // 4. 绘制小鸟 (明黄色圆角矩形 + 眼睛)
    HBRUSH birdBrush = CreateSolidBrush(RGB(250, 205, 52));
    HPEN birdPen = CreatePen(PS_SOLID, 2, RGB(83, 56, 71));
    SelectObject(memDC, birdBrush);
    SelectObject(memDC, birdPen);
    RoundRect(memDC, 80, (int)g_BirdY, 80 + 24, (int)g_BirdY + 24, 8, 8);
    DeleteObject(birdBrush);
    DeleteObject(birdPen);

    // 鸟眼
    HBRUSH eyeBrush = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(memDC, eyeBrush);
    Ellipse(memDC, 94, (int)g_BirdY + 3, 102, (int)g_BirdY + 11);
    DeleteObject(eyeBrush);

    // 5. 绘制文字信息 (分数 / 提示)
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));

    char scoreStr[32] = "Score: ";
    IntToStr(g_Score, scoreStr + 7);
    TextOutA(memDC, 20, 20, scoreStr, (int)strlen(scoreStr));

    if (g_State == STATE_READY) {
        const char* tip = "Press SPACE or Click to Jump!";
        TextOutA(memDC, 90, 320, tip, (int)strlen(tip));
    } else if (g_State == STATE_GAMEOVER) {
        SetTextColor(memDC, RGB(220, 40, 40));
        const char* go = "GAME OVER!";
        TextOutA(memDC, 150, 260, go, (int)strlen(go));
        const char* retry = "Press SPACE to Restart";
        TextOutA(memDC, 120, 290, retry, (int)strlen(retry));
    }

    // 6. 前后台缓冲区置换
    BitBlt(hdc, 0, 0, SCREEN_W, SCREEN_H, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

// 窗口过程处理
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        ResetGame();
        // 启动 ~60 FPS 定时器进行帧驱动 (16ms)
        SetTimer(hWnd, 1, 16, NULL);
        break;

    case WM_TIMER:
        UpdatePhysics();
        InvalidateRect(hWnd, NULL, FALSE);
        break;

    case WM_KEYDOWN:
        if (wParam == VK_SPACE) {
            if (g_State == STATE_READY) {
                g_State = STATE_PLAYING;
                g_BirdVelocity = JUMP_FORCE;
            } else if (g_State == STATE_PLAYING) {
                g_BirdVelocity = JUMP_FORCE;
            } else if (g_State == STATE_GAMEOVER) {
                ResetGame();
            }
        }
        break;

    case WM_LBUTTONDOWN:
        // 支持鼠标左键点击跳跃
        if (g_State == STATE_READY) {
            g_State = STATE_PLAYING;
            g_BirdVelocity = JUMP_FORCE;
        } else if (g_State == STATE_PLAYING) {
            g_BirdVelocity = JUMP_FORCE;
        } else if (g_State == STATE_GAMEOVER) {
            ResetGame();
        }
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RenderScene(hdc);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_ERASEBKGND:
        return 1; // 阻止清除背景，消灭闪烁

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// 入口点
int main(void) {
    HINSTANCE hInst = GetModuleHandleA(NULL);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "FlappyBird_Win32";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    // 窗口尺寸自适应窗口客户区 (400x600)
    RECT rc = {0, 0, SCREEN_W, SCREEN_H};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    HWND hWnd = CreateWindowExA(
        0, wc.lpszClassName, "Win32 Pure GDI - Flappy Bird",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInst, NULL
    );

    if (!hWnd) return 0;

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}