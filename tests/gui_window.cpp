#include <windows.h>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// Floating point CRT contract satisfy
int _fltused = 0x9876;

constexpr int SCREEN_W = 400;
constexpr int SCREEN_H = 600;
constexpr int PIPE_COUNT = 3;
constexpr int GAP_SIZE = 150;
constexpr int PIPE_WIDTH = 60;
constexpr float GRAVITY = 0.65f;
constexpr float JUMP_FORCE = -8.5f;

// RAII: Scoped GDI Device Context
class ScopedDC {
private:
    HDC m_hdc;
public:
    explicit ScopedDC(HDC hdc) : m_hdc(hdc) {}
    ~ScopedDC() {
        if (m_hdc) DeleteDC(m_hdc);
    }
    operator HDC() const { return m_hdc; }
};

// RAII: Scoped GDI Object Selection
class ScopedSelectObject {
private:
    HDC m_hdc;
    HGDIOBJ m_oldObj;
    HGDIOBJ m_newObj;
public:
    ScopedSelectObject(HDC hdc, HGDIOBJ newObj) : m_hdc(hdc), m_newObj(newObj) {
        m_oldObj = SelectObject(hdc, newObj);
    }
    ~ScopedSelectObject() {
        if (m_hdc && m_oldObj) {
            SelectObject(m_hdc, m_oldObj);
        }
        if (m_newObj) {
            DeleteObject(m_newObj);
        }
    }
};

// RAII: Paint Scope
class ScopedPaint {
private:
    HWND m_hWnd;
    PAINTSTRUCT m_ps;
    HDC m_hdc;
public:
    explicit ScopedPaint(HWND hWnd) : m_hWnd(hWnd) {
        m_hdc = BeginPaint(hWnd, &m_ps);
    }
    ~ScopedPaint() {
        EndPaint(m_hWnd, &m_ps);
    }
    HDC getDC() const { return m_hdc; }
};

// Simple PRNG without CRT rand()
class Random {
private:
    unsigned int m_seed;
public:
    explicit Random(unsigned int seed = 123456789) : m_seed(seed) {}
    int next() {
        m_seed = (214013 * m_seed + 2531011);
        return (int)((m_seed >> 16) & 0x7FFF);
    }
};

enum class GameState {
    Ready,
    Playing,
    GameOver
};

// Abstract polymorphic GameObject
class GameObject {
public:
    virtual ~GameObject() = default;
    virtual void update() = 0;
    virtual void render(HDC hdc) = 0;
};

// Bird entity
class Bird : public GameObject {
public:
    float y = 250.0f;
    float velocity = 0.0f;

    void reset() {
        y = 250.0f;
        velocity = 0.0f;
    }

    void jump() {
        velocity = JUMP_FORCE;
    }

    void update() override {
        velocity += GRAVITY;
        y += velocity;
        if (y < 0.0f) {
            y = 0.0f;
            velocity = 0.0f;
        }
        if (y > SCREEN_H - 30) {
            y = SCREEN_H - 30;
        }
    }

    void render(HDC hdc) override {
        HBRUSH yellowBrush = CreateSolidBrush(RGB(245, 215, 60));
        HPEN darkPen = CreatePen(PS_SOLID, 2, RGB(180, 140, 20));
        ScopedSelectObject sBrush(hdc, yellowBrush);
        ScopedSelectObject sPen(hdc, darkPen);

        int by = (int)y;
        Ellipse(hdc, 80, by, 80 + 32, by + 24);

        HBRUSH orangeBrush = CreateSolidBrush(RGB(240, 100, 30));
        ScopedSelectObject sBeak(hdc, orangeBrush);
        POINT beak[3] = { {80 + 28, by + 8}, {80 + 40, by + 13}, {80 + 28, by + 18} };
        Polygon(hdc, beak, 3);
    }
};

// Single obstacle pipe
struct Pipe {
    int x;
    int topHeight;
    bool passed;
};

// Pipe Manager entity managing dynamic vector of pipes
class PipeManager : public GameObject {
public:
    std::vector<Pipe> pipes;
    Random rng;

    PipeManager() : rng(987654321) {
        reset();
    }

    void reset() {
        pipes.clear();
        for (int i = 0; i < PIPE_COUNT; i++) {
            Pipe p;
            p.x = SCREEN_W + 100 + i * 220;
            p.topHeight = 80 + (rng.next() % 220);
            p.passed = false;
            pipes.push_back(p);
        }
    }

    void update() override {
        for (auto &p : pipes) {
            p.x -= 3;
            if (p.x + PIPE_WIDTH < 0) {
                p.x = SCREEN_W + 160;
                p.topHeight = 80 + (rng.next() % 220);
                p.passed = false;
            }
        }
    }

    void render(HDC hdc) override {
        HBRUSH greenBrush = CreateSolidBrush(RGB(110, 190, 50));
        HPEN greenPen = CreatePen(PS_SOLID, 2, RGB(60, 120, 20));
        ScopedSelectObject sBrush(hdc, greenBrush);
        ScopedSelectObject sPen(hdc, greenPen);

        for (const auto &p : pipes) {
            // Upper pipe
            Rectangle(hdc, p.x, 0, p.x + PIPE_WIDTH, p.topHeight);
            Rectangle(hdc, p.x - 4, p.topHeight - 20, p.x + PIPE_WIDTH + 4, p.topHeight);

            // Lower pipe
            int bottomY = p.topHeight + GAP_SIZE;
            Rectangle(hdc, p.x, bottomY, p.x + PIPE_WIDTH, SCREEN_H);
            Rectangle(hdc, p.x - 4, bottomY, p.x + PIPE_WIDTH + 4, bottomY + 20);
        }
    }
};

// Main Game Controller Singleton / Engine
class FlappyGame {
public:
    GameState state = GameState::Ready;
    int score = 0;
    Bird bird;
    PipeManager pipeMgr;

    void reset() {
        state = GameState::Ready;
        score = 0;
        bird.reset();
        pipeMgr.reset();
    }

    void onAction() {
        if (state == GameState::Ready) {
            state = GameState::Playing;
            bird.jump();
        } else if (state == GameState::Playing) {
            bird.jump();
        } else if (state == GameState::GameOver) {
            reset();
        }
    }

    void updatePhysics() {
        if (state != GameState::Playing) return;

        bird.update();
        pipeMgr.update();

        // Floor collision
        if (bird.y >= SCREEN_H - 30) {
            state = GameState::GameOver;
            return;
        }

        // Pipe collisions and scoring
        for (auto &p : pipeMgr.pipes) {
            if (!p.passed && p.x + PIPE_WIDTH < 80) {
                p.passed = true;
                score++;
            }

            if (80 + 30 > p.x && 80 < p.x + PIPE_WIDTH) {
                if (bird.y < p.topHeight || bird.y + 22 > p.topHeight + GAP_SIZE) {
                    state = GameState::GameOver;
                    return;
                }
            }
        }
    }

    void renderScene(HDC hdc) {
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, SCREEN_W, SCREEN_H);
        HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

        // Sky background
        HBRUSH skyBrush = CreateSolidBrush(RGB(115, 200, 220));
        RECT clientRc = { 0, 0, SCREEN_W, SCREEN_H };
        FillRect(memDC, &clientRc, skyBrush);
        DeleteObject(skyBrush);

        // Ground
        HBRUSH groundBrush = CreateSolidBrush(RGB(220, 215, 140));
        RECT groundRc = { 0, SCREEN_H - 30, SCREEN_W, SCREEN_H };
        FillRect(memDC, &groundRc, groundBrush);
        DeleteObject(groundBrush);

        // Polymorphic rendering of game objects
        pipeMgr.render(memDC);
        bird.render(memDC);

        // Text & UI
        SetBkMode(memDC, TRANSPARENT);
        if (state == GameState::Ready) {
            SetTextColor(memDC, RGB(255, 255, 255));
            const char* title = "C++ FLAPPY BIRD";
            TextOutA(memDC, 135, 180, title, 15);
            const char* tip = "Press SPACE / Click to Jump";
            TextOutA(memDC, 110, 220, tip, 27);
        } else if (state == GameState::Playing) {
            SetTextColor(memDC, RGB(255, 255, 255));
            char buf[16] = { 'S', 'c', 'o', 'r', 'e', ':', ' ', '0', 0 };
            buf[7] = '0' + (score % 10);
            TextOutA(memDC, 20, 20, buf, 8);
        } else if (state == GameState::GameOver) {
            SetTextColor(memDC, RGB(220, 40, 40));
            const char* go = "GAME OVER!";
            TextOutA(memDC, 150, 240, go, 10);
            const char* retry = "Press SPACE to Restart";
            TextOutA(memDC, 120, 280, retry, 22);
        }

        // Swap buffer
        BitBlt(hdc, 0, 0, SCREEN_W, SCREEN_H, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
    }
};

static FlappyGame g_Game;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_Game.reset();
        SetTimer(hWnd, 1, 16, NULL);
        break;

    case WM_TIMER:
        g_Game.updatePhysics();
        InvalidateRect(hWnd, NULL, FALSE);
        break;

    case WM_KEYDOWN:
        if (wParam == VK_SPACE) g_Game.onAction();
        break;

    case WM_LBUTTONDOWN:
        g_Game.onAction();
        break;

    case WM_PAINT: {
        ScopedPaint sp(hWnd);
        g_Game.renderScene(sp.getDC());
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int main() {
    HINSTANCE hInst = GetModuleHandleA(NULL);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "FlappyBirdCpp_Win32";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    RECT rc = {0, 0, SCREEN_W, SCREEN_H};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    HWND hWnd = CreateWindowExA(
        0, wc.lpszClassName, "MemLLVM Pure C++ GDI - Flappy Bird",
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
