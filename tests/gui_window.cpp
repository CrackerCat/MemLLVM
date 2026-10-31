#include <windows.h>
#include <d3d11.h>
#include <math.h>

#include "vs_bytecode.h"
#include "ps_bytecode.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "msvcrt.lib")

int _fltused = 0x9876;

#pragma function(memcpy)
extern "C" void* memcpy(void* dest, const void* src, size_t count) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    while (count--) *d++ = *s++;
    return dest;
}

#pragma function(memset)
extern "C" void* memset(void* dest, int c, size_t count) {
    char* d = (char*)dest;
    while (count--) *d++ = (char)c;
    return dest;
}

constexpr int SCREEN_W = 800;
constexpr int SCREEN_H = 600;

// Simple RAII COM smart pointer
template<typename T>
class ComPtr {
private:
    T* m_ptr = nullptr;
public:
    ComPtr() = default;
    ComPtr(T* ptr) : m_ptr(ptr) {}
    ~ComPtr() { Release(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    void Release() {
        if (m_ptr) {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }

    T* Get() const { return m_ptr; }
    T** GetAddressOf() { Release(); return &m_ptr; }
    T** operator&() { Release(); return &m_ptr; }
    T* operator->() const { return m_ptr; }
    operator T*() const { return m_ptr; }
};

// 4x4 Transformation Matrix
struct Matrix4 {
    float m[4][4];

    static Matrix4 Identity() {
        Matrix4 r = {0};
        r.m[0][0] = 1.0f; r.m[1][1] = 1.0f; r.m[2][2] = 1.0f; r.m[3][3] = 1.0f;
        return r;
    }

    static Matrix4 RotationX(float a) {
        Matrix4 r = Identity();
        float c = cosf(a), s = sinf(a);
        r.m[1][1] = c;  r.m[1][2] = s;
        r.m[2][1] = -s; r.m[2][2] = c;
        return r;
    }

    static Matrix4 RotationY(float a) {
        Matrix4 r = Identity();
        float c = cosf(a), s = sinf(a);
        r.m[0][0] = c; r.m[0][2] = -s;
        r.m[2][0] = s; r.m[2][2] = c;
        return r;
    }

    static Matrix4 RotationZ(float a) {
        Matrix4 r = Identity();
        float c = cosf(a), s = sinf(a);
        r.m[0][0] = c;  r.m[0][1] = s;
        r.m[1][0] = -s; r.m[1][1] = c;
        return r;
    }

    static Matrix4 Translation(float x, float y, float z) {
        Matrix4 r = Identity();
        r.m[3][0] = x; r.m[3][1] = y; r.m[3][2] = z;
        return r;
    }

    static Matrix4 Perspective(float fovRad, float aspect, float nearZ, float farZ) {
        Matrix4 r = {0};
        float tanHalf = tanf(fovRad * 0.5f);
        r.m[0][0] = 1.0f / (aspect * tanHalf);
        r.m[1][1] = 1.0f / tanHalf;
        r.m[2][2] = farZ / (farZ - nearZ);
        r.m[2][3] = 1.0f;
        r.m[3][2] = -(nearZ * farZ) / (farZ - nearZ);
        return r;
    }

    Matrix4 operator*(const Matrix4& r) const {
        Matrix4 out = {0};
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                out.m[i][j] = m[i][0] * r.m[0][j] +
                              m[i][1] * r.m[1][j] +
                              m[i][2] * r.m[2][j] +
                              m[i][3] * r.m[3][j];
            }
        }
        return out;
    }
};

struct Vertex {
    float pos[3];
    float col[4];
};

struct ConstantBufferData {
    Matrix4 transform;
};

class D3DRenderer {
public:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_rtv;
    ComPtr<ID3D11DepthStencilView> m_dsv;

    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11RasterizerState> m_rasterizerState;
    ComPtr<ID3D11DepthStencilState> m_depthStencilState;

    float m_rotX = 0.0f;
    float m_rotY = 0.0f;
    float m_rotZ = 0.0f;
    bool m_isPaused = false;
    POINT m_lastMousePos = {0, 0};
    bool m_isDragging = false;

    bool Init(HWND hWnd, int width, int height) {
        DXGI_SWAP_CHAIN_DESC scd = {0};
        scd.BufferCount = 1;
        scd.BufferDesc.Width = width;
        scd.BufferDesc.Height = height;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferDesc.RefreshRate.Numerator = 60;
        scd.BufferDesc.RefreshRate.Denominator = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = hWnd;
        scd.SampleDesc.Count = 1;
        scd.SampleDesc.Quality = 0;
        scd.Windowed = TRUE;
        scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
            D3D_FEATURE_LEVEL_9_3,
            D3D_FEATURE_LEVEL_9_1
        };
        D3D_FEATURE_LEVEL featureLevel;

        // Try hardware acceleration first, fallback to WARP software renderer
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            featureLevels,
            5,
            D3D11_SDK_VERSION,
            &scd,
            &m_swapChain,
            &m_device,
            &featureLevel,
            &m_context
        );

        if (FAILED(hr)) {
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                featureLevels,
                5,
                D3D11_SDK_VERSION,
                &scd,
                &m_swapChain,
                &m_device,
                &featureLevel,
                &m_context
            );
            if (FAILED(hr)) return false;
        }

        // Create Render Target View
        ComPtr<ID3D11Texture2D> pBackBuffer;
        hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
        if (FAILED(hr)) return false;

        hr = m_device->CreateRenderTargetView(pBackBuffer.Get(), nullptr, &m_rtv);
        if (FAILED(hr)) return false;

        // Create Depth Stencil Texture & View
        D3D11_TEXTURE2D_DESC descDepth = {0};
        descDepth.Width = width;
        descDepth.Height = height;
        descDepth.MipLevels = 1;
        descDepth.ArraySize = 1;
        descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        descDepth.SampleDesc.Count = 1;
        descDepth.SampleDesc.Quality = 0;
        descDepth.Usage = D3D11_USAGE_DEFAULT;
        descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        ComPtr<ID3D11Texture2D> pDepthStencil;
        hr = m_device->CreateTexture2D(&descDepth, nullptr, &pDepthStencil);
        if (FAILED(hr)) return false;

        hr = m_device->CreateDepthStencilView(pDepthStencil.Get(), nullptr, &m_dsv);
        if (FAILED(hr)) return false;

        m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), m_dsv.Get());

        // Setup Viewport
        D3D11_VIEWPORT vp = {0};
        vp.Width = (float)width;
        vp.Height = (float)height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        // Create Shaders
        hr = m_device->CreateVertexShader(g_vs_main, sizeof(g_vs_main), nullptr, &m_vs);
        if (FAILED(hr)) return false;

        hr = m_device->CreatePixelShader(g_ps_main, sizeof(g_ps_main), nullptr, &m_ps);
        if (FAILED(hr)) return false;

        // Create Input Layout
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        hr = m_device->CreateInputLayout(layout, 2, g_vs_main, sizeof(g_vs_main), &m_inputLayout);
        if (FAILED(hr)) return false;

        // Setup 3D Colored Cube Geometry
        Vertex vertices[] = {
            { {-1.0f, -1.0f, -1.0f}, {1.0f, 0.1f, 0.1f, 1.0f} }, // 0: Red
            { {-1.0f,  1.0f, -1.0f}, {0.1f, 1.0f, 0.1f, 1.0f} }, // 1: Green
            { { 1.0f,  1.0f, -1.0f}, {0.1f, 0.3f, 1.0f, 1.0f} }, // 2: Blue
            { { 1.0f, -1.0f, -1.0f}, {1.0f, 0.9f, 0.1f, 1.0f} }, // 3: Yellow
            { {-1.0f, -1.0f,  1.0f}, {1.0f, 0.1f, 0.9f, 1.0f} }, // 4: Magenta
            { {-1.0f,  1.0f,  1.0f}, {0.1f, 0.9f, 1.0f, 1.0f} }, // 5: Cyan
            { { 1.0f,  1.0f,  1.0f}, {1.0f, 0.5f, 0.1f, 1.0f} }, // 6: Orange
            { { 1.0f, -1.0f,  1.0f}, {0.9f, 0.9f, 0.9f, 1.0f} }  // 7: Light Gray
        };

        D3D11_BUFFER_DESC bd = {0};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(vertices);
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA initData = {0};
        initData.pSysMem = vertices;
        hr = m_device->CreateBuffer(&bd, &initData, &m_vertexBuffer);
        if (FAILED(hr)) return false;

        // Cube Index Buffer
        unsigned short indices[] = {
            0, 1, 2,  0, 2, 3, // Front
            4, 6, 5,  4, 7, 6, // Back
            4, 5, 1,  4, 1, 0, // Left
            3, 2, 6,  3, 6, 7, // Right
            1, 5, 6,  1, 6, 2, // Top
            4, 0, 3,  4, 3, 7  // Bottom
        };

        bd.ByteWidth = sizeof(indices);
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        initData.pSysMem = indices;
        hr = m_device->CreateBuffer(&bd, &initData, &m_indexBuffer);
        if (FAILED(hr)) return false;

        // Constant Buffer
        bd.ByteWidth = sizeof(ConstantBufferData);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = m_device->CreateBuffer(&bd, nullptr, &m_constantBuffer);
        if (FAILED(hr)) return false;

        // Rasterizer State
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_BACK;
        rd.DepthClipEnable = TRUE;
        m_device->CreateRasterizerState(&rd, &m_rasterizerState);

        // Depth Stencil State
        D3D11_DEPTH_STENCIL_DESC dsd = {};
        dsd.DepthEnable = TRUE;
        dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dsd.DepthFunc = D3D11_COMPARISON_LESS;
        m_device->CreateDepthStencilState(&dsd, &m_depthStencilState);

        return true;
    }

    void Render() {
        if (!m_isPaused) {
            m_rotX += 0.012f;
            m_rotY += 0.020f;
            m_rotZ += 0.008f;
        }

        // Deep slate-blue background clear
        const float clearColor[4] = { 0.08f, 0.10f, 0.16f, 1.0f };
        m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
        m_context->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

        // Build MVP Matrix
        Matrix4 world = Matrix4::RotationX(m_rotX) * Matrix4::RotationY(m_rotY) * Matrix4::RotationZ(m_rotZ);
        Matrix4 view = Matrix4::Translation(0.0f, 0.0f, 4.2f);
        Matrix4 proj = Matrix4::Perspective(0.85f, (float)SCREEN_W / (float)SCREEN_H, 0.1f, 100.0f);
        Matrix4 mvp = world * view * proj;

        ConstantBufferData cb;
        cb.transform = mvp;
        m_context->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);

        // Pipeline binding
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_inputLayout.Get());

        m_context->VSSetShader(m_vs.Get(), nullptr, 0);
        m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

        m_context->PSSetShader(m_ps.Get(), nullptr, 0);

        m_context->RSSetState(m_rasterizerState.Get());
        m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);

        // Draw 36 indices (12 triangles)
        m_context->DrawIndexed(36, 0, 0);

        // Present with VSync
        m_swapChain->Present(1, 0);
    }
};

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    D3DRenderer* pRenderer = (D3DRenderer*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);
    if (pRenderer) {
        switch (msg) {
        case WM_KEYDOWN:
            if (wParam == VK_SPACE) {
                pRenderer->m_isPaused = !pRenderer->m_isPaused;
            } else if (wParam == VK_ESCAPE) {
                DestroyWindow(hWnd);
            }
            break;

        case WM_LBUTTONDOWN:
            pRenderer->m_isDragging = true;
            pRenderer->m_lastMousePos.x = LOWORD(lParam);
            pRenderer->m_lastMousePos.y = HIWORD(lParam);
            SetCapture(hWnd);
            break;

        case WM_LBUTTONUP:
            pRenderer->m_isDragging = false;
            ReleaseCapture();
            break;

        case WM_MOUSEMOVE:
            if (pRenderer->m_isDragging) {
                int curX = LOWORD(lParam);
                int curY = HIWORD(lParam);
                int dx = curX - pRenderer->m_lastMousePos.x;
                int dy = curY - pRenderer->m_lastMousePos.y;
                pRenderer->m_rotY += dx * 0.01f;
                pRenderer->m_rotX += dy * 0.01f;
                pRenderer->m_lastMousePos.x = curX;
                pRenderer->m_lastMousePos.y = curY;
            }
            break;
        }
    }

    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int main() {
    HINSTANCE hInst = GetModuleHandleA(NULL);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "MemLLVM_D3D11_Window";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    RECT rc = {0, 0, SCREEN_W, SCREEN_H};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    HWND hWnd = CreateWindowExA(
        0, wc.lpszClassName, "MemLLVM DirectX 11 - Pure Flat Binary 3D",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInst, NULL
    );

    if (!hWnd) return 1;

    D3DRenderer renderer;
    SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)&renderer);

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    if (!renderer.Init(hWnd, SCREEN_W, SCREEN_H)) {
        return 2;
    }

    // 60 FPS Real-time Render & Message Loop
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!running) break;

        renderer.Render();
    }

    return 0;
}
