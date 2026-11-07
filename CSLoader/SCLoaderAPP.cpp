#include "SCLoaderApp.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shldisp.h>
#include <exdisp.h>
#include <tlhelp32.h>
#include <io.h>
#include <fcntl.h>
#include <format>
#include <chrono>
#include <intrin.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comdlg32.lib")

using namespace CUI;
using namespace CUI::DSL;

namespace SCLoader {

namespace {

constexpr UINT_PTR kSCLoaderSubclassId = 0x5C10;

// 打开文件文件夹 SVG 图标（参照 Geek 的 kSvgFolder）
constexpr const char* kSvgFolder =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M20 6h-8l-2-2H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2zm0 12H4V8h16v10z\" fill=\"currentColor\"/>"
    "</svg>";

// 启动播放三角形 SVG 图标
constexpr const char* kSvgPlay =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M8 5v14l11-7z\" fill=\"currentColor\"/>"
    "</svg>";

// 停止方块 SVG 图标
constexpr const char* kSvgStop =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M6 6h12v12H6z\" fill=\"currentColor\"/>"
    "</svg>";

// 清屏垃圾桶 SVG 图标
constexpr const char* kSvgClear =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M19 4h-3.5l-1-1h-5l-1 1H5v2h14V4zM6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12z\" fill=\"currentColor\"/>"
    "</svg>";

// 终端控制台 SVG 图标（用于标题栏）
constexpr const char* kSvgTerminal =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M20 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V6c0-1.1-.9-2-2-2zm0 14H4V8h16v10zm-12-3l4-3-4-3v2H6v2h2v2zm6 1h6v-2h-6v2z\" fill=\"currentColor\"/>"
    "</svg>";

// 重启 SVG 图标 (刷新循环箭头)
constexpr const char* kSvgRestart =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M17.65 6.35A7.958 7.958 0 0012 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08A5.99 5.99 0 0112 18c-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z\" fill=\"currentColor\"/>"
    "</svg>";

// 以管理员重启 SVG 图标 (安全盾牌)
constexpr const char* kSvgAdmin =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 1L3 5v6c0 5.55 3.84 10.74 9 12 5.16-1.26 9-6.45 9-12V5l-9-4zm0 10.99h7c-.53 4.12-3.28 7.79-7 8.94V12H5V6.3l7-3.11v8.8z\" fill=\"currentColor\"/>"
    "</svg>";

// 低权限/普通用户 SVG 图标 (用户人像)
constexpr const char* kSvgUser =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 12c2.21 0 4-1.79 4-4s-1.79-4-4-4-4 1.79-4 4 1.79 4 4 4zm0 2c-2.67 0-8 1.34-8 4v2h16v-2c0-2.66-5.33-4-8-4z\" fill=\"currentColor\"/>"
    "</svg>";

static bool IsCurrentProcessAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

static bool LaunchUnelevatedViaExplorerCOM(const wchar_t* exePath, const wchar_t* params) {
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    IShellWindows* psw = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&psw));
    if (FAILED(hr) || !psw) {
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return false;
    }

    VARIANT vEmpty = {};
    VariantInit(&vEmpty);
    HWND hwnd = nullptr;
    IDispatch* pdisp = nullptr;
    hr = psw->FindWindowSW(&vEmpty, &vEmpty, SWC_DESKTOP, reinterpret_cast<long*>(&hwnd), SWFO_NEEDDISPATCH, &pdisp);
    if (FAILED(hr) || !pdisp) {
        psw->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return false;
    }

    IServiceProvider* psp = nullptr;
    hr = pdisp->QueryInterface(IID_PPV_ARGS(&psp));
    pdisp->Release();
    if (FAILED(hr) || !psp) {
        psw->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return false;
    }

    IShellBrowser* psb = nullptr;
    hr = psp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&psb));
    psp->Release();
    if (FAILED(hr) || !psb) {
        psw->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return false;
    }

    IShellView* psv = nullptr;
    hr = psb->QueryActiveShellView(&psv);
    psb->Release();
    if (FAILED(hr) || !psv) {
        psw->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return false;
    }

    IDispatch* pdispBg = nullptr;
    hr = psv->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&pdispBg));
    psv->Release();
    if (FAILED(hr) || !pdispBg) {
        psw->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return false;
    }

    IShellFolderViewDual* psfvd = nullptr;
    hr = pdispBg->QueryInterface(IID_PPV_ARGS(&psfvd));
    pdispBg->Release();
    if (FAILED(hr) || !psfvd) {
        psw->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return false;
    }

    IDispatch* pdispApp = nullptr;
    hr = psfvd->get_Application(&pdispApp);
    psfvd->Release();
    if (FAILED(hr) || !pdispApp) {
        psw->Release();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return false;
    }

    IShellDispatch2* psd = nullptr;
    hr = pdispApp->QueryInterface(IID_PPV_ARGS(&psd));
    pdispApp->Release();
    psw->Release();
    if (FAILED(hr) || !psd) {
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return false;
    }

    BSTR bstrFile = SysAllocString(exePath);
    VARIANT vArgs;
    VariantInit(&vArgs);
    vArgs.vt = VT_BSTR;
    vArgs.bstrVal = SysAllocString(params ? params : L"");

    VARIANT vDir;
    VariantInit(&vDir);
    VARIANT vOp;
    VariantInit(&vOp);
    vOp.vt = VT_BSTR;
    vOp.bstrVal = SysAllocString(L"open");

    VARIANT vShow;
    VariantInit(&vShow);
    vShow.vt = VT_I4;
    vShow.lVal = SW_SHOWNORMAL;

    hr = psd->ShellExecute(bstrFile, vArgs, vDir, vOp, vShow);

    SysFreeString(bstrFile);
    SysFreeString(vArgs.bstrVal);
    SysFreeString(vOp.bstrVal);
    psd->Release();

    if (SUCCEEDED(hrCo)) CoUninitialize();
    return SUCCEEDED(hr);
}

static bool LaunchUnelevatedViaToken(const wchar_t* exePath, const wchar_t* params) {
    HWND hShellWnd = GetShellWindow();
    if (!hShellWnd) {
        hShellWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    }
    DWORD shellPid = 0;
    if (hShellWnd) {
        GetWindowThreadProcessId(hShellWnd, &shellPid);
    }
    if (shellPid == 0) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = { sizeof(pe) };
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                        shellPid = pe.th32ProcessID;
                        break;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
    }

    if (shellPid != 0) {
        HANDLE hShellProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, shellPid);
        if (hShellProc) {
            HANDLE hShellToken = nullptr;
            if (OpenProcessToken(hShellProc, TOKEN_DUPLICATE, &hShellToken)) {
                HANDLE hNewToken = nullptr;
                if (DuplicateTokenEx(hShellToken, TOKEN_ALL_ACCESS, nullptr,
                                     SecurityImpersonation, TokenPrimary, &hNewToken)) {
                    STARTUPINFOW si = { sizeof(si) };
                    PROCESS_INFORMATION pi = {};
                    std::wstring cmd = L"\"" + std::wstring(exePath) + L"\"";
                    if (params && params[0]) {
                        cmd += L" ";
                        cmd += params;
                    }
                    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
                    cmdBuf.push_back(L'\0');

                    BOOL created = CreateProcessWithTokenW(
                        hNewToken, 0, nullptr, cmdBuf.data(), 0, nullptr, nullptr, &si, &pi);

                    if (created) {
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                        CloseHandle(hNewToken);
                        CloseHandle(hShellToken);
                        CloseHandle(hShellProc);
                        return true;
                    }
                    CloseHandle(hNewToken);
                }
                CloseHandle(hShellToken);
            }
            CloseHandle(hShellProc);
        }
    }
    return false;
}

static bool LaunchUnelevated(const wchar_t* exePath, const wchar_t* params) {
    if (LaunchUnelevatedViaExplorerCOM(exePath, params)) {
        return true;
    }
    return LaunchUnelevatedViaToken(exePath, params);
}

static void LogTrace(const std::string& msg) {
    static std::mutex s_logMutex;
    std::lock_guard<std::mutex> lock(s_logMutex);
    FILE* fp = nullptr;
    fopen_s(&fp, "E:\\C++project\\CUI\\scloader_trace.log", "a");
    if (fp) {
        fprintf(fp, "[%llu] %s\n", (unsigned long long)GetTickCount64(), msg.c_str());
        fclose(fp);
    }
}

static void FixProcessStandardHandles(HANDLE hIn, HANDLE hOut, HANDLE hErr) {
    SetStdHandle(STD_INPUT_HANDLE, hIn);
    SetStdHandle(STD_OUTPUT_HANDLE, hOut);
    SetStdHandle(STD_ERROR_HANDLE, hErr);

    // 突破 Windows 核心黑幕：
    // 在 64 位 Windows kernelbase!GetStdHandle 逆向指令中：
    //   test dword ptr [ProcessParameters + 0xa4], 0x400 -> jne return_null (针对 stdout)
    //   test dword ptr [ProcessParameters + 0xa4], 0x200 -> jne return_null (针对 stdin)
    // 当进程由 Explorer / ShellExecuteEx 启动时，ProcessParameters->WindowFlags（偏移 0xA4）
    // 会被打上 0x400 / 0x200 掩码。即使 SetStdHandle 成功将管道句柄存入了 StandardOutput，
    // GetStdHandle 也会被 0x400 掩码拦截而强制返回 NULL。原生 MSVC/UCRT 工具检测到 NULL 便会静默丢弃输出或提前退出。
    // Windows 的 SetStdHandle 只写句柄不清理掩码，此处通过 PEB 强制清除 0x200 与 0x400，
    // 并显式开启 STARTF_USESTDHANDLES (0x100)，确保 GetStdHandle 100% 能够拿到有效管道！
#if defined(_M_X64) || defined(__x86_64__)
    auto* pPeb = reinterpret_cast<uint8_t*>(__readgsqword(0x60));
    if (pPeb) {
        auto* pProcessParameters = *reinterpret_cast<uint8_t**>(pPeb + 0x20);
        if (pProcessParameters) {
            auto* pWindowFlags = reinterpret_cast<uint32_t*>(pProcessParameters + 0xA4);
            *pWindowFlags &= ~(0x200 | 0x400);
            *pWindowFlags |= 0x100; // STARTF_USESTDHANDLES
        }
    }
#elif defined(_M_IX86) || defined(__i386__)
    auto* pPeb = reinterpret_cast<uint8_t*>(__readfsdword(0x30));
    if (pPeb) {
        auto* pProcessParameters = *reinterpret_cast<uint8_t**>(pPeb + 0x10);
        if (pProcessParameters) {
            auto* pWindowFlags = reinterpret_cast<uint32_t*>(pProcessParameters + 0xA4);
            *pWindowFlags &= ~(0x200 | 0x400);
            *pWindowFlags |= 0x100;
        }
    }
#endif
}

extern "C" DWORD CallShellcodeAsm(LPVOID pMemory);

static DWORD ExecuteShellcodeSEH(LPVOID pMemory) {
    __try {
        return CallShellcodeAsm(pMemory);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

template <typename... Args>
inline std::string FormatStr(const char* fmt, Args... args) {
    char buf[1024];
    int len = snprintf(buf, sizeof(buf), fmt, args...);
    if (len < 0) return std::string();
    return std::string(buf, len);
}

} // namespace

LRESULT CALLBACK SCLoaderApp::SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    auto* app = reinterpret_cast<SCLoaderApp*>(dwRefData);
    if (app) {
        switch (msg) {
            case WM_KEYDOWN:
                // Ctrl+O 打开文件
                if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && (wParam == 'O' || wParam == 'o')) {
                    app->DoOpenFile();
                    return 0;
                }
                // F5 运行，Shift+F5 停止
                if (wParam == VK_F5) {
                    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                        app->StopSC();
                    } else {
                        app->StartSC();
                    }
                    return 0;
                }
                // Ctrl+L 清屏
                if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && (wParam == 'L' || wParam == 'l')) {
                    app->ClearConsole();
                    return 0;
                }
                // Ctrl+R 重启自身，Ctrl+Shift+R 切换权限重启（若已是管理员则低权限启动，否则提权启动）
                if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && (wParam == 'R' || wParam == 'r')) {
                    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                        if (IsCurrentProcessAdmin()) {
                            app->RestartAsUnelevated();
                        } else {
                            app->RestartSelf(true);
                        }
                    } else {
                        app->RestartSelf(false);
                    }
                    return 0;
                }
                break;

            case WM_SC_OUTPUT: {
                std::vector<std::string> batch;
                {
                    std::lock_guard<std::mutex> lock(app->m_queueMutex);
                    batch.swap(app->m_pendingQueue);
                }
                for (const auto& text : batch) {
                    app->OnOutputReceived(text);
                }
                return 0;
            }

            case WM_SC_THREAD_FINISHED:
                LogTrace("[SubclassProc] WM_SC_THREAD_FINISHED");
                app->OnThreadFinished(static_cast<DWORD>(wParam));
                return 0;

            case WM_SC_AUTOSTART:
                LogTrace("[SubclassProc] WM_SC_AUTOSTART");
                app->StartSC();
                return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

SCLoaderApp::SCLoaderApp() = default;

SCLoaderApp::~SCLoaderApp() {
    StopSC();
}

std::shared_ptr<UIElement> SCLoaderApp::BuildRoot() {
    // 1. 顶部自定义磨砂标题栏（参照 GeekApp 架构与原生风格）
    m_titleBar = Widgets::WindowTitleBar("SCLoader - Shellcode / Payload 加载执行器")
        .IconText(kSvgTerminal)
        .Height(30.0f);

    // 2. 顶部 CommandBar（参照 GeekApp 的 CommandBar 写法，汇集所有操作按钮）
    m_commandBar = std::make_shared<CommandBar>();
    m_commandBar->ApplyLabelPosition(CommandBarLabelPosition::Right);
    m_commandBar->ApplyHeight(28.0f);
    m_commandBar->ApplyBackgroundToken(ThemeTokenId::Unset);
    m_commandBar->ApplyBorderThickness(0.0f);

    // 2.1 打开文件按钮 (Ctrl+O)
    m_openBtn = m_commandBar->AddButton("打开 Payload", kSvgFolder, [this]() {
        DoOpenFile();
    });

    // 2.2 启动按钮 (F5)
    m_startBtn = m_commandBar->AddButton("启动", kSvgPlay, [this]() {
        StartSC();
    });

    // 2.3 停止按钮 (Shift+F5)
    m_stopBtn = m_commandBar->AddButton("停止", kSvgStop, [this]() {
        StopSC();
    });
    m_stopBtn->ApplyIsEnabled(false);

    m_commandBar->AddSeparator();

    // 2.4 清屏按钮 (Ctrl+L)
    m_clearBtn = m_commandBar->AddButton("清屏", kSvgClear, [this]() {
        ClearConsole();
    });

    m_commandBar->AddSeparator();

    // 2.5 重启自身按钮 (Ctrl+R)
    m_restartBtn = m_commandBar->AddButton("重启", kSvgRestart, [this]() {
        RestartSelf(false);
    });

    // 2.6 权限切换重启按钮 (Ctrl+Shift+R)
    // 若当前是管理员，按钮自动改为“以低权限重启”，点击自动以低权限凭据启动；否则为“以管理员重启”
    const bool isAdmin = IsCurrentProcessAdmin();
    std::string permBtnLabel = isAdmin ? "以低权限重启" : "以管理员重启";
    const char* permBtnIcon = isAdmin ? kSvgUser : kSvgAdmin;
    m_adminRestartBtn = m_commandBar->AddButton(permBtnLabel, permBtnIcon, [this]() {
        if (IsCurrentProcessAdmin()) {
            RestartAsUnelevated();
        } else {
            RestartSelf(true);
        }
    });

    // 3. 顶部路径显示 Label (采用 TextBlock 呈现当前选中的 Payload 路径)
    m_pathLabel = Widgets::TextBlock("未选择文件（请点击【打开 Payload】选取）")
        .VerticalAlign(TextVerticalAlignment::Center)
        .Margin(Thickness(10.0f, 0.0f, 8.0f, 0.0f))
        .FlexGrow(1.0f);
    m_pathLabel->ApplyColorToken(ThemeTokenId::TextSecondary);

    // 顶部操作卡片条：左侧 CommandBar，右侧伸展的路径 Label
    auto topBar = Row({
        m_commandBar,
        m_pathLabel
    }, 8.0f)
    .Padding(Thickness(8.0f, 4.0f, 8.0f, 4.0f))
    .Align(Alignment::Stretch);

    // 4. 虚拟终端控制台控件（用于重定向 payload 输出与显示加载调试信息）
    m_console = Widgets::TerminalControl()
        .FlexGrow(1.0f)
		.CornerRadius(0.0f)
        .Margin(Thickness(8.0f, 0.0f, 8.0f, 4.0f));

    // 挂接交互式终端输入后端（实现键盘打字输入并桥接 STDIN 管道）
    m_backend = std::make_unique<SCLoaderTerminalBackend>(this);
    m_console->AttachBackend(m_backend.get());

    // 5. 底部现代化原生状态栏（参照 GeekApp 状态栏设计）
    m_statusBar = std::make_shared<StatusBar>();
    m_statusBar->ApplyHeight(24.0f);
    m_statusBar->ApplyBackgroundToken(ThemeTokenId::Unset);
    m_statusBar->ApplyBackground(Color(0.0f, 0.0f, 0.0f, 0.0f));
    m_statusBar->ApplyBorderThickness(1.0f);
    m_statusBar->ApplyBorderToken(ThemeTokenId::CardBorder);
    m_sbStatusId = m_statusBar->AddTextItem("准备就绪", StatusBarItemAlignment::Left);
    m_sbFileId = m_statusBar->AddTextItem("未选择文件", StatusBarItemAlignment::Right, 260.0f);
    m_sbThreadId = m_statusBar->AddTextItem("线程: 空闲", StatusBarItemAlignment::Right, 140.0f);

    // 6. 声明式 DSL 组装全套根视图
    auto root = Column({
        m_titleBar,
        topBar,
        m_console,
        m_statusBar
    });

    return root;
}

int SCLoaderApp::Run() {
    m_root = BuildRoot();

    auto windowBuilder = m_window.Fluent()
        .Title("SCLoader - Shellcode / Payload 加载执行器")
        .Size(960, 620)
        .Theme(ThemeManager::Instance().GetThemeMode())
        .RenderStats(false)
        .Root(m_root)
        .Build();

    if (!windowBuilder) return -1;

    HWND hwnd = m_window.GetHWND();
    SetWindowSubclass(hwnd, SubclassProc, kSCLoaderSubclassId, reinterpret_cast<DWORD_PTR>(this));

    windowBuilder.Show();

    // 打印控制台初始横幅
    AppendOutputLine("\x1b[1;36m=====================================================================\x1b[0m");
    AppendOutputLine("\x1b[1;32m         SCLoader - ShellcodeLoader           \x1b[0m");
    AppendOutputLine("\x1b[1;36m=====================================================================\x1b[0m");
    const bool curIsAdmin = IsCurrentProcessAdmin();
    std::string permBanner = curIsAdmin ? "以低权限重启" : "以管理员重启";
    std::string permTag = curIsAdmin ? "\x1b[33;1m[管理员]\x1b[0m" : "\x1b[32m[标准用户]\x1b[0m";
    AppendOutputLine("\x1b[90m* 当前权限: %s | 快捷键: [Ctrl+O] 打开 | [F5] 启动 | [Shift+F5] 终止 \x1b[0m\r\n");

    if (!m_payloadPath.empty()) {
        if (m_pathLabel) {
            m_pathLabel->ApplyText("目标: " + m_payloadPath);
            m_pathLabel->ApplyColorToken(ThemeTokenId::TextPrimary);
        }
        if (m_statusBar && m_sbFileId != -1) {
            std::string filename = m_payloadPath;
            size_t slash = filename.find_last_of("\\/");
            if (slash != std::string::npos) {
                filename = filename.substr(slash + 1);
            }
            m_statusBar->ApplyItemText(m_sbFileId, "文件: " + filename);
        }
        AppendOutputLine(FormatStr("\x1b[32m[+]\x1b[0m 预设 Payload: \x1b[36m%s\x1b[0m", m_payloadPath.c_str()));
        if (m_autoStart) {
            PostMessage(hwnd, WM_SC_AUTOSTART, 0, 0);
        }
    }

    windowBuilder.Run();
    return 0;
}

void SCLoaderApp::DoOpenFile() {
    WCHAR filePath[MAX_PATH] = L"";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = m_window.GetHWND();
    ofn.lpstrFilter = L"Payload 文件 (*.bin;*.raw;*.dat;*.payload)\0*.bin;*.raw;*.dat;*.payload\0可执行文件 (*.exe;*.dll;*.sys)\0*.exe;*.dll;*.sys\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) {
        return;
    }

    int len = WideCharToMultiByte(CP_UTF8, 0, filePath, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return;
    std::string utf8Path(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, filePath, -1, utf8Path.data(), len, nullptr, nullptr);

    m_payloadPath = utf8Path;

    // 更新路径标签 Label
    if (m_pathLabel) {
        m_pathLabel->ApplyText("目标: " + m_payloadPath);
        m_pathLabel->ApplyColorToken(ThemeTokenId::TextPrimary);
    }

    // 更新状态栏
    if (m_statusBar && m_sbFileId != -1) {
        std::string filename = m_payloadPath;
        size_t slash = filename.find_last_of("\\/");
        if (slash != std::string::npos) {
            filename = filename.substr(slash + 1);
        }
        m_statusBar->ApplyItemText(m_sbFileId, "文件: " + filename);
    }

    AppendOutputLine(FormatStr("\x1b[32m[+]\x1b[0m 已选取 Payload: \x1b[36m%s\x1b[0m", m_payloadPath.c_str()));
}

void SCLoaderApp::AppendOutput(const std::string& text) {
    if (m_console && !text.empty()) {
        // 规范化换行符：将未带 \r 的裸 \n 转换为 \r\n，保证光标在终端模拟器中回到第 0 列
        std::string normalized;
        normalized.reserve(text.size() * 2);
        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (c == '\n') {
                if (normalized.empty()) {
                    if (m_lastChar != '\r') {
                        normalized.push_back('\r');
                    }
                } else if (normalized.back() != '\r') {
                    normalized.push_back('\r');
                }
            }
            normalized.push_back(c);
            m_lastChar = c;
        }
        m_console->Terminal().Write(normalized.data(), normalized.size());
        m_console->Terminal().ScrollToBottom();
    }
}

void SCLoaderApp::AppendOutputLine(const std::string& line) {
    AppendOutput(line + "\r\n");
}

void SCLoaderApp::OnOutputReceived(const std::string& text) {
    AppendOutput(text);
}

void SCLoaderApp::ClearConsole() {
    if (m_console) {
        m_console->Terminal().Clear();
        m_lastChar = 0;
    }
}

void SCLoaderApp::RestartSelf(bool asAdmin) {
    if (m_isRunning) {
        StopSC();
    }

    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring params;
    if (!m_payloadPath.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, m_payloadPath.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wpath(wlen, 0);
            MultiByteToWideChar(CP_UTF8, 0, m_payloadPath.c_str(), -1, wpath.data(), wlen);
            if (!wpath.empty() && wpath.back() == L'\0') {
                wpath.pop_back();
            }
            params = L"\"" + wpath + L"\"";
        }
    }

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_DEFAULT;
    sei.lpVerb = asAdmin ? L"runas" : L"open";
    sei.lpFile = exePath;
    sei.lpParameters = params.empty() ? nullptr : params.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei)) {
        ExitProcess(0);
    } else {
        DWORD err = GetLastError();
        if (err != ERROR_CANCELLED) {
            AppendOutputLine(FormatStr("\x1b[31;1m[-] 重启进程失败 (错误代码: %lu)\x1b[0m", err));
        }
    }
}

void SCLoaderApp::RestartAsUnelevated() {
    if (m_isRunning) {
        StopSC();
    }

    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring params;
    if (!m_payloadPath.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, m_payloadPath.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wpath(wlen, 0);
            MultiByteToWideChar(CP_UTF8, 0, m_payloadPath.c_str(), -1, wpath.data(), wlen);
            if (!wpath.empty() && wpath.back() == L'\0') {
                wpath.pop_back();
            }
            params = L"\"" + wpath + L"\"";
        }
    }

    if (LaunchUnelevated(exePath, params.empty() ? nullptr : params.c_str())) {
        ExitProcess(0);
    } else {
        AppendOutputLine("\x1b[31;1m[-] 降权启动低权限进程失败，请检查 Explorer 进程是否正常运行\x1b[0m");
    }
}

void SCLoaderApp::UpdateStateUI(bool running) {
    m_isRunning = running;

    if (m_openBtn) {
        m_openBtn->ApplyIsEnabled(!running);
    }
    if (m_startBtn) {
        m_startBtn->ApplyIsEnabled(!running);
    }
    if (m_stopBtn) {
        m_stopBtn->ApplyIsEnabled(running);
    }

    if (m_statusBar) {
        if (m_sbStatusId != -1) {
            m_statusBar->ApplyItemText(m_sbStatusId, running ? "● 正在运行 Payload..." : "准备就绪");
        }
        if (m_sbThreadId != -1) {
            if (running) {
                m_statusBar->ApplyItemText(m_sbThreadId, FormatStr("TID: %lu", m_threadId));
            } else {
                m_statusBar->ApplyItemText(m_sbThreadId, "线程: 空闲");
            }
        }
    }
}

void SCLoaderTerminalBackend::Write(const char* data, size_t length) {
    if (m_app) {
        m_app->OnTerminalInput(data, length);
    }
}

void SCLoaderApp::HandleHistoryUp() {
    if (m_history.empty()) return;

    if (m_historyIndex == -1) {
        m_savedCurrentLine = m_inputLine;
        m_historyIndex = static_cast<int>(m_history.size()) - 1;
    } else if (m_historyIndex > 0) {
        m_historyIndex--;
    } else {
        return;
    }

    ReplaceInputLine(m_history[m_historyIndex]);
}

void SCLoaderApp::HandleHistoryDown() {
    if (m_historyIndex == -1) return;

    if (m_historyIndex + 1 < static_cast<int>(m_history.size())) {
        m_historyIndex++;
        ReplaceInputLine(m_history[m_historyIndex]);
    } else {
        m_historyIndex = -1;
        ReplaceInputLine(m_savedCurrentLine);
    }
}

void SCLoaderApp::ReplaceInputLine(const std::string& newLine) {
    size_t charCount = 0;
    for (unsigned char c : m_inputLine) {
        if ((c & 0xC0) != 0x80) {
            charCount++;
        }
    }
    std::string eraseSeq;
    eraseSeq.reserve(charCount * 3);
    for (size_t i = 0; i < charCount; ++i) {
        eraseSeq += "\b \b";
    }
    AppendOutput(eraseSeq);

    m_inputLine = newLine;
    AppendOutput(m_inputLine);
}

void SCLoaderApp::OnTerminalInput(const char* data, size_t length) {
    if (!data || length == 0) return;

    // 只有在 Payload 处于运行状态且 STDIN 管道有效时才接收输入
    if (!m_isRunning || !m_hPipeInWrite) {
        return;
    }

    for (size_t i = 0; i < length; ++i) {
        char ch = data[i];

        // ANSI / VT100 转义序列解析 (例如 \x1b[A 上箭头, \x1b[B 下箭头)
        if (m_inEscape) {
            m_escapeSeq.push_back(ch);
            if (m_escapeSeq.size() == 2 && m_escapeSeq[1] != '[' && m_escapeSeq[1] != 'O') {
                m_inEscape = false;
                m_escapeSeq.clear();
            } else if (m_escapeSeq.size() >= 3) {
                unsigned char uc = static_cast<unsigned char>(ch);
                if (uc >= 0x40 && uc <= 0x7E) {
                    if (m_escapeSeq == "\x1b[A") {
                        HandleHistoryUp();
                    } else if (m_escapeSeq == "\x1b[B") {
                        HandleHistoryDown();
                    }
                    m_inEscape = false;
                    m_escapeSeq.clear();
                }
            }
            continue;
        }

        if (ch == '\x1b') {
            m_inEscape = true;
            m_escapeSeq.clear();
            m_escapeSeq.push_back(ch);
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            // 用户按下回车：终端回显换行，并将整行写入 STDIN 管道唤醒 std::cin / ReadFile
            AppendOutput("\r\n");

            if (!m_inputLine.empty()) {
                if (m_history.empty() || m_history.back() != m_inputLine) {
                    m_history.push_back(m_inputLine);
                }
            }
            m_historyIndex = -1;
            m_savedCurrentLine.clear();

            std::string lineToSend = m_inputLine + "\r\n";
            m_inputLine.clear();
    m_historyIndex = -1;
    m_savedCurrentLine.clear();
    m_inEscape = false;
    m_escapeSeq.clear();

            DWORD written = 0;
            WriteFile(m_hPipeInWrite, lineToSend.data(), static_cast<DWORD>(lineToSend.size()), &written, nullptr);
        } else if (ch == '\b' || ch == 0x7F) {
            // 退格键：从行缓冲移除字符，并在终端擦除视觉字符
            if (!m_inputLine.empty()) {
                while (!m_inputLine.empty()) {
                    unsigned char c = static_cast<unsigned char>(m_inputLine.back());
                    m_inputLine.pop_back();
                    if ((c & 0xC0) != 0x80) { // 不是 UTF-8 续字节，说明完整移除了一个字符
                        break;
                    }
                }
                AppendOutput("\b \b");
            }
        } else if (ch == 0x03) {
            // Ctrl+C：强制终止
            AppendOutput("^C\r\n");
            m_inputLine.clear();
    m_historyIndex = -1;
    m_savedCurrentLine.clear();
    m_inEscape = false;
    m_escapeSeq.clear();
            StopSC();
            return;
        } else if (static_cast<unsigned char>(ch) >= 0x20 || ch == '\t') {
            // 可见字符或制表符：存入输入缓冲区并回显到终端
            m_inputLine.push_back(ch);
            std::string echo(1, ch);
            AppendOutput(echo);
        }
    }
}

DWORD WINAPI SCLoaderApp::PipeReaderThreadProc(LPVOID param) {
    auto* self = reinterpret_cast<SCLoaderApp*>(param);
    if (!self || !self->m_hPipeRead) return 0;

    char buffer[4096];
    DWORD bytesRead = 0;

    while (self->m_isPipeReading) {
        if (!ReadFile(self->m_hPipeRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) || bytesRead == 0) {
            LogTrace(FormatStr("[Pipe] reader stop: bytesRead=%lu err=%lu", bytesRead, GetLastError()));
            break;
        }
        buffer[bytesRead] = '\0';
        std::string text(buffer, bytesRead);

        {
            std::lock_guard<std::mutex> lock(self->m_queueMutex);
            self->m_pendingQueue.push_back(std::move(text));
        }

        if (HWND hwnd = self->m_window.GetHWND()) {
            PostMessage(hwnd, WM_SC_OUTPUT, 0, 0);
        }
    }

    return 0;
}

void SCLoaderApp::SetupOutputRedirection() {
    SECURITY_ATTRIBUTES saOut{};
    saOut.nLength = sizeof(saOut);
    saOut.bInheritHandle = TRUE;

    if (!CreatePipe(&m_hPipeRead, &m_hPipeWrite, &saOut, 0)) {
        return;
    }
    SetHandleInformation(m_hPipeRead, HANDLE_FLAG_INHERIT, 0);

    // 备份原有标准输出/错误句柄
    m_hOldStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    m_hOldStdErr = GetStdHandle(STD_ERROR_HANDLE);

    // 重定向进程标准输出与标准错误
    SetStdHandle(STD_OUTPUT_HANDLE, m_hPipeWrite);
    SetStdHandle(STD_ERROR_HANDLE, m_hPipeWrite);

    // 备份并重定向 C 运行时 stdout / stderr (fd 1 和 fd 2)
    m_nOldFd1 = _dup(1);
    m_nOldFd2 = _dup(2);

    m_nPipeFd = _open_osfhandle(reinterpret_cast<intptr_t>(m_hPipeWrite), _O_TEXT);
    if (m_nPipeFd != -1) {
        _dup2(m_nPipeFd, 1);
        _dup2(m_nPipeFd, 2);
    }
    LogTrace(FormatStr("[Setup] pipeRead=%p pipeWrite=%p msvcrtFd=%d msvcrtFd1=%lld",
                       m_hPipeRead, m_hPipeWrite, m_nPipeFd,
                       static_cast<long long>(_get_osfhandle(1))));

    // 创建标准输入管道 (STDIN)，支持 std::cin / ReadFile 交互式键盘打字
    SECURITY_ATTRIBUTES saIn{};
    saIn.nLength = sizeof(saIn);
    saIn.bInheritHandle = TRUE;

    if (CreatePipe(&m_hPipeInRead, &m_hPipeInWrite, &saIn, 0)) {
        SetHandleInformation(m_hPipeInWrite, HANDLE_FLAG_INHERIT, 0);

        m_hOldStdIn = GetStdHandle(STD_INPUT_HANDLE);
        SetStdHandle(STD_INPUT_HANDLE, m_hPipeInRead);

        m_nOldFd0 = _dup(0);
        m_nPipeInFd = _open_osfhandle(reinterpret_cast<intptr_t>(m_hPipeInRead), _O_TEXT);
        if (m_nPipeInFd != -1) {
            _dup2(m_nPipeInFd, 0);
        }
    }

    m_inputLine.clear();
    m_historyIndex = -1;
    m_savedCurrentLine.clear();
    m_inEscape = false;
    m_escapeSeq.clear();

    // 上面那套 _dup2 只作用于 msvcrt.dll 的 fd 表。以 ucrtbase.dll 为运行时的
    // payload 走的是另一张表，必须单独重定向，否则它的 printf/wprintf 全部
    // 静默丢弃。详见 RedirectUcrtStreams 的注释。
    RedirectUcrtStreams();

    // 关键修复：清除 Explorer 注入的 0x400 / 0x200 拦截标记，开启 STARTF_USESTDHANDLES，
    // 强制将 Win32 标准句柄确凿地绑定到管道，保证依赖 GetStdHandle 的所有 CLI 工具拿到有效的 FILE_TYPE_PIPE。
    FixProcessStandardHandles(m_hPipeInRead, m_hPipeWrite, m_hPipeWrite);

    // 启动管道读取后台线程
    m_isPipeReading = true;
    m_hPipeThread = CreateThread(nullptr, 0, PipeReaderThreadProc, this, 0, nullptr);
}

// ---------------------------------------------------------------------------
// 第二张 CRT 文件描述符表 (ucrtbase.dll)
//
// SCLoader 由 VC-LTL 链接，_dup/_dup2/_open_osfhandle 落到 msvcrt.dll 上；而
// payload 的 _write/_get_osfhandle 很可能解析到 ucrtbase.dll。两个 CRT 各有一张
// 进程级 fd 表，重定向其中一张对另一张毫无影响。
//
// 实测（真实 SCLoader 内运行探针 payload）：
//     msvcrt.dll    fd1 = 0x0A80   filetype=3 (管道)      <- 上面的 _dup2 生效
//     ucrtbase.dll  fd1 = -2 (_NO_CONSOLE_FILENO)          <- payload 实际用的表
// 结果 payload 里 _write(1) 返回 -1，而 printf/wprintf 依旧返回成功、数据凭空
// 消失，加载器侧看到的就是「线程正常退出、退出码 0、输出区一片空白」。
//
// 所以这里对 ucrtbase 再做一遍同样的重定向。句柄必须用 DuplicateHandle 复制：
// _open_osfhandle 会把句柄所有权交给该 CRT，若两个 CRT 共用同一个句柄，各自的
// _close 会互相把管道关掉。
// ---------------------------------------------------------------------------
namespace {

using CrtOpenOsHandleFn = int(__cdecl*)(intptr_t, int);
using CrtDupFn = int(__cdecl*)(int);
using CrtDup2Fn = int(__cdecl*)(int, int);
using CrtCloseFn = int(__cdecl*)(int);
using CrtGetOsHandleFn = intptr_t(__cdecl*)(int);
using CrtFflushFn = int(__cdecl*)(FILE*);

} // namespace

void SCLoaderApp::RedirectUcrtStreams() {
    // 用 LoadLibrary 而不是 GetModuleHandle：payload 是靠 api-ms-win-crt-*
    // 转发到 ucrtbase 的，ucrtbase 很可能在 SetupOutputRedirection 之后才被拉
    // 进进程。用 GetModuleHandle 会拿到 NULL，于是重定向被静默跳过，又回到
    // 「输出凭空消失」的老路上。这里主动加载，保证它一定在场。
    // 故意不 FreeLibrary：保持引用，避免 payload 用到一半被卸载。
    m_hUcrt = LoadLibraryW(L"ucrtbase.dll");
    if (!m_hUcrt) {
        return; // 进程里确实没有这张 fd 表，无需处理
    }

    auto openf = reinterpret_cast<CrtOpenOsHandleFn>(GetProcAddress(m_hUcrt, "_open_osfhandle"));
    auto dupf = reinterpret_cast<CrtDupFn>(GetProcAddress(m_hUcrt, "_dup"));
    auto dup2f = reinterpret_cast<CrtDup2Fn>(GetProcAddress(m_hUcrt, "_dup2"));
    auto getf = reinterpret_cast<CrtGetOsHandleFn>(GetProcAddress(m_hUcrt, "_get_osfhandle"));
    if (!openf || !dupf || !dup2f) {
        m_hUcrt = nullptr;
        return;
    }

    // 标准输出 / 标准错误
    if (m_hPipeWrite) {
        HANDLE hCopy = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), m_hPipeWrite, GetCurrentProcess(), &hCopy,
                            0, FALSE, DUPLICATE_SAME_ACCESS)) {
            // 备份必须在 _dup2 之前做，否则备份到的已经是管道了
            m_nUcrtOldFd1 = dupf(1);
            m_nUcrtOldFd2 = dupf(2);
            m_nUcrtPipeFd = openf(reinterpret_cast<intptr_t>(hCopy), _O_TEXT);
            if (m_nUcrtPipeFd == -1) {
                CloseHandle(hCopy);
            } else {
                dup2f(m_nUcrtPipeFd, 1);
                dup2f(m_nUcrtPipeFd, 2);
            }
            LogTrace(FormatStr("[Ucrt] pipeWrite=%p copy=%p oldFd1=%d oldFd2=%d pipeFd=%d fd1now=%lld",
                               m_hPipeWrite, hCopy, m_nUcrtOldFd1, m_nUcrtOldFd2, m_nUcrtPipeFd,
                               getf ? static_cast<long long>(getf(1)) : -999LL));
        } else {
            LogTrace(FormatStr("[Ucrt] DuplicateHandle failed err=%lu", GetLastError()));
        }
    }

    // 标准输入
    if (m_hPipeInRead) {
        HANDLE hCopy = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), m_hPipeInRead, GetCurrentProcess(), &hCopy,
                            0, FALSE, DUPLICATE_SAME_ACCESS)) {
            m_nUcrtOldFd0 = dupf(0);
            m_nUcrtPipeInFd = openf(reinterpret_cast<intptr_t>(hCopy), _O_TEXT);
            if (m_nUcrtPipeInFd == -1) {
                CloseHandle(hCopy);
            } else {
                dup2f(m_nUcrtPipeInFd, 0);
            }
        }
    }
}

void SCLoaderApp::RestoreUcrtStreams() {
    if (!m_hUcrt) {
        return;
    }

    auto dup2f = reinterpret_cast<CrtDup2Fn>(GetProcAddress(m_hUcrt, "_dup2"));
    auto closef = reinterpret_cast<CrtCloseFn>(GetProcAddress(m_hUcrt, "_close"));
    auto flushf = reinterpret_cast<CrtFflushFn>(GetProcAddress(m_hUcrt, "fflush"));

    // 先把该 CRT 缓冲区里的输出冲出去；恢复 fd 之后再冲就来不及了
    if (flushf) {
        flushf(nullptr); // fflush(NULL) 刷新该 CRT 的所有流
    }

    if (dup2f && closef) {
        if (m_nUcrtOldFd0 != -1) {
            dup2f(m_nUcrtOldFd0, 0);
            closef(m_nUcrtOldFd0);
            m_nUcrtOldFd0 = -1;
        }
        if (m_nUcrtOldFd1 != -1) {
            dup2f(m_nUcrtOldFd1, 1);
            closef(m_nUcrtOldFd1);
            m_nUcrtOldFd1 = -1;
        }
        if (m_nUcrtOldFd2 != -1) {
            dup2f(m_nUcrtOldFd2, 2);
            closef(m_nUcrtOldFd2);
            m_nUcrtOldFd2 = -1;
        }
        // fd 1/2 已改回旧句柄，此时才能释放管道那份副本
        if (m_nUcrtPipeFd != -1) {
            closef(m_nUcrtPipeFd);
            m_nUcrtPipeFd = -1;
        }
        if (m_nUcrtPipeInFd != -1) {
            closef(m_nUcrtPipeInFd);
            m_nUcrtPipeInFd = -1;
        }
    }

    m_hUcrt = nullptr;
}

void SCLoaderApp::CleanupOutputRedirection() {
    m_isPipeReading = false;

    // 必须赶在下面关闭管道之前恢复：ucrtbase 的 fd 1/2 还引用着管道副本，
    // 先关管道会让它变成死句柄。
    RestoreUcrtStreams();

    // 恢复标准句柄
    FixProcessStandardHandles(m_hOldStdIn, m_hOldStdOut, m_hOldStdErr);
    m_hOldStdOut = nullptr;
    m_hOldStdErr = nullptr;

    // 刷新并恢复 CRT 输出描述符
    fflush(stdout);
    fflush(stderr);
    if (m_nOldFd1 != -1) {
        _dup2(m_nOldFd1, 1);
        _close(m_nOldFd1);
        m_nOldFd1 = -1;
    }
    if (m_nOldFd2 != -1) {
        _dup2(m_nOldFd2, 2);
        _close(m_nOldFd2);
        m_nOldFd2 = -1;
    }
    if (m_nPipeFd != -1) {
        _close(m_nPipeFd);
        m_nPipeFd = -1;
        m_hPipeWrite = nullptr; // _close 已关闭了底层 OS 句柄
    }

    // 彻底关闭输入管道
    m_hOldStdIn = nullptr;
    if (m_nOldFd0 != -1) {
        _dup2(m_nOldFd0, 0);
        _close(m_nOldFd0);
        m_nOldFd0 = -1;
    }
    if (m_nPipeInFd != -1) {
        _close(m_nPipeInFd);
        m_nPipeInFd = -1;
        m_hPipeInRead = nullptr;
    }
    if (m_hPipeInWrite) {
        CloseHandle(m_hPipeInWrite);
        m_hPipeInWrite = nullptr;
    }
    if (m_hPipeInRead) {
        CloseHandle(m_hPipeInRead);
        m_hPipeInRead = nullptr;
    }
    m_inputLine.clear();
    m_historyIndex = -1;
    m_savedCurrentLine.clear();
    m_inEscape = false;
    m_escapeSeq.clear();

    // 关闭写入端使读取线程退出 ReadFile
    if (m_hPipeWrite) {
        CloseHandle(m_hPipeWrite);
        m_hPipeWrite = nullptr;
    }

    // 取消后台读取线程上可能阻塞的同步 ReadFile I/O 操作
    if (m_hPipeThread) {
        CancelSynchronousIo(m_hPipeThread);
    }

    if (m_hPipeThread) {
        WaitForSingleObject(m_hPipeThread, 1000);
        CloseHandle(m_hPipeThread);
        m_hPipeThread = nullptr;
    }

    // 排空管道中可能残留的未决输出数据
    if (m_hPipeRead) {
        DWORD avail = 0;
        char buf[4096];
        while (PeekNamedPipe(m_hPipeRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD toRead = (std::min)(avail, static_cast<DWORD>(sizeof(buf) - 1));
            DWORD readBytes = 0;
            if (ReadFile(m_hPipeRead, buf, toRead, &readBytes, nullptr) && readBytes > 0) {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_pendingQueue.push_back(std::string(buf, readBytes));
            } else {
                break;
            }
        }
        CloseHandle(m_hPipeRead);
        m_hPipeRead = nullptr;
    }

    // 将后台管道读取线程收集的所有残余输出在结束前全部同步刷新至终端
    std::vector<std::string> remaining;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        remaining.swap(m_pendingQueue);
    }
    for (const auto& text : remaining) {
        OnOutputReceived(text);
    }

    // 若 Payload 输出末尾没有换行，自动补充换行，确保后续提示文本不会被粘在同一行
    if (m_lastChar != '\n' && m_lastChar != 0) {
        AppendOutput("\r\n");
    }
}

DWORD WINAPI SCLoaderApp::ShellcodeWorkerThread(LPVOID param) {
    auto* self = reinterpret_cast<SCLoaderApp*>(param);
    if (!self || !self->m_pPayloadMemory) return 0;

    // 工作线程内部执行前，再次绑定标准句柄并清除 Explorer 拦截掩码，确保线程执行期间 100% 获得有效管道
    FixProcessStandardHandles(self->m_hPipeInRead, self->m_hPipeWrite, self->m_hPipeWrite);

    LogTrace(FormatStr("[WorkerThread] out=%p in=%p pipeWrite=%p typeOut=%lu",
                       GetStdHandle(STD_OUTPUT_HANDLE),
                       GetStdHandle(STD_INPUT_HANDLE),
                       self->m_hPipeWrite,
                       GetFileType(self->m_hPipeWrite)));

    DWORD exitCode = ExecuteShellcodeSEH(self->m_pPayloadMemory);

    // 确保把可能的缓冲区数据刷出来
    fflush(stdout);
    fflush(stderr);

    // 必须在此先还原 ucrt 重定向，释放 ucrtbase 持有的管道写端副本句柄，
    // 否则 ucrtbase 的句柄不关，管道写端计数不为 0，读取线程将无法收到 EOF。
    self->RestoreUcrtStreams();

    // 关闭管道写端，触发读取线程读尽剩余数据后正常退出 ReadFile (EOF)
    if (self->m_nPipeFd != -1) {
        _close(self->m_nPipeFd);
        self->m_nPipeFd = -1;
        self->m_hPipeWrite = nullptr;
    } else if (self->m_hPipeWrite) {
        CloseHandle(self->m_hPipeWrite);
        self->m_hPipeWrite = nullptr;
    }

    // 等待后台管道读取线程把管道中全部排队数据读空并投递到 UI 消息队列
    if (self->m_hPipeThread) {
        WaitForSingleObject(self->m_hPipeThread, 1000);
    }

    if (HWND hwnd = self->m_window.GetHWND()) {
        PostMessage(hwnd, WM_SC_THREAD_FINISHED, static_cast<WPARAM>(exitCode), 0);
    }

    return exitCode;
}

void SCLoaderApp::CleanupPayloadWindowsAndClasses() {
    // 1. 若当前或上一个工作线程存在，遍历属于该线程的全部窗口，收集其注册的窗口类并请求销毁
    if (m_threadId != 0) {
        std::vector<std::string> registeredClasses;

        EnumThreadWindows(m_threadId, [](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* classes = reinterpret_cast<std::vector<std::string>*>(lParam);
            char className[256] = { 0 };
            if (GetClassNameA(hwnd, className, sizeof(className))) {
                classes->push_back(className);
            }
            // 隐藏并投递 WM_CLOSE 请求窗口在自身线程消息循环内优雅调用 DestroyWindow
            ShowWindow(hwnd, SW_HIDE);
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&registeredClasses));

        // 投递 WM_QUIT 唤醒可能阻塞在 GetMessage 上的 Win32 消息循环
        PostThreadMessage(m_threadId, WM_QUIT, 0, 0);

        // 注销收集到的该窗口专有类名
        HINSTANCE hInst = GetModuleHandleA(nullptr);
        for (const auto& cls : registeredClasses) {
            UnregisterClassA(cls.c_str(), hInst);
        }
    }

    // 2. 清理所有常见图形 Payload 的预设窗口类，彻底防止类冲突与 WndProc 悬挂
    HINSTANCE hInst = GetModuleHandleA(nullptr);
    UnregisterClassA("Box2D_Sandbox_Class", hInst);
    UnregisterClassA("MemLLVM_CHIP8_Class", hInst);
    UnregisterClassA("C8_WndClass", hInst);
    UnregisterClassA("ImGui Example", hInst);
    UnregisterClassW(L"ImGui Example", hInst);
    UnregisterClassA("Scintilla", hInst);
    UnregisterClassW(L"Scintilla", hInst);
    UnregisterClassA("ScintillaEditorClass", hInst);
    UnregisterClassW(L"ScintillaEditorClass", hInst);
    UnregisterClassA("DoomWindowClass", hInst);
}

BOOL SCLoaderApp::StartSC() {
    if (m_isRunning) {
        AppendOutputLine("\x1b[33m[!] Payload 已处于运行状态中，请勿重复启动。\x1b[0m");
        return FALSE;
    }

    // 启动前重置输出状态并彻底清理上一次可能残留的窗口与窗口类
    m_lastChar = 0;
    CleanupPayloadWindowsAndClasses();

    if (m_payloadPath.empty()) {
        AppendOutputLine("\x1b[33m[!] 尚未选取目标文件，正在打开文件选择框...\x1b[0m");
        DoOpenFile();
        if (m_payloadPath.empty()) {
            AppendOutputLine("\x1b[31;1m[-] 操作取消，未选择 Payload 文件。\x1b[0m");
            return FALSE;
        }
    }

    // 打开目标文件
    HANDLE hFile = CreateFileA(m_payloadPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        AppendOutputLine(FormatStr("\x1b[31;1m[-] 打开文件失败: \"%s\" (错误代码: %lu)\x1b[0m", m_payloadPath.c_str(), err));
        return FALSE;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart == 0) {
        CloseHandle(hFile);
        AppendOutputLine("\x1b[31;1m[-] 文件大小无效或为空文件。\x1b[0m");
        return FALSE;
    }

    m_payloadSize = static_cast<size_t>(fileSize.QuadPart);

    // 分配 RWX 内存空间
    m_pPayloadMemory = VirtualAlloc(nullptr, m_payloadSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!m_pPayloadMemory) {
        CloseHandle(hFile);
        AppendOutputLine("\x1b[31;1m[-] VirtualAlloc 申请可执行内存失败。\x1b[0m");
        return FALSE;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(hFile, m_pPayloadMemory, static_cast<DWORD>(m_payloadSize), &bytesRead, nullptr) || bytesRead == 0) {
        CloseHandle(hFile);
        VirtualFree(m_pPayloadMemory, 0, MEM_RELEASE);
        m_pPayloadMemory = nullptr;
        AppendOutputLine("\x1b[31;1m[-] 读取文件内容失败。\x1b[0m");
        return FALSE;
    }
    CloseHandle(hFile);

    // 建立标准输出/错误/输入管道重定向
    SetupOutputRedirection();

    // 打印加载成功详情
    AppendOutputLine(FormatStr("\x1b[1;32m[+] 成功加载 Payload: \x1b[36m%s\x1b[0m", m_payloadPath.c_str()));
    AppendOutputLine(FormatStr("\x1b[1;32m[+] 载入大小: \x1b[33m%zu 字节\x1b[0m (%.2f KB)", m_payloadSize, m_payloadSize / 1024.0));
    AppendOutputLine(FormatStr("\x1b[1;32m[+] 内存地址: \x1b[35m0x%p\x1b[0m (PAGE_EXECUTE_READWRITE)", m_pPayloadMemory));

    // 启动执行线程
    hThread = CreateThread(nullptr, 0, ShellcodeWorkerThread, this, 0, &m_threadId);
    if (!hThread) {
        DWORD err = GetLastError();
        AppendOutputLine(FormatStr("\x1b[31;1m[-] 创建执行线程失败 (错误代码: %lu)\x1b[0m", err));
        CleanupOutputRedirection();
        VirtualFree(m_pPayloadMemory, 0, MEM_RELEASE);
        m_pPayloadMemory = nullptr;
        return FALSE;
    }

    AppendOutputLine(FormatStr("\x1b[1;32m[+] 执行线程创建成功，TID: \x1b[35m%lu\x1b[0m，开始执行...\x1b[0m", m_threadId));
    AppendOutputLine("\x1b[90m------------------------ [ PAYLOAD OUTPUT BEGIN ] ------------------------\x1b[0m");

    UpdateStateUI(true);
    return TRUE;
}

BOOL SCLoaderApp::StopSC() {
    if (!m_isRunning && !hThread) {
        return TRUE;
    }

    if (hThread) {
        // 先行触发窗口关闭与退出事件
        CleanupPayloadWindowsAndClasses();

        // 等待工作线程自行退出 (500ms)
        if (WaitForSingleObject(hThread, 500) != WAIT_OBJECT_0) {
            // 若 500ms 内未能退出（死循环等情况），再强制终止线程
            TerminateThread(hThread, static_cast<DWORD>(-1));
            WaitForSingleObject(hThread, 200);
        }
        CloseHandle(hThread);
        hThread = nullptr;
    }

    // 再次彻底注销所有窗口类，确保在释放代码内存前切断 User32 对 WndProc 的所有引用
    CleanupPayloadWindowsAndClasses();

    CleanupOutputRedirection();

    if (m_pPayloadMemory) {
        VirtualFree(m_pPayloadMemory, 0, MEM_RELEASE);
        m_pPayloadMemory = nullptr;
    }

    AppendOutputLine("\x1b[90m------------------------- [ PAYLOAD TERMINATED ] -------------------------\x1b[0m");
    AppendOutputLine("\x1b[31;1m[!] Payload 线程已安全终止并完成窗口清理\x1b[0m\r\n");

    UpdateStateUI(false);
    return TRUE;
}

void SCLoaderApp::OnThreadFinished(DWORD exitCode) {
    if (hThread) {
        WaitForSingleObject(hThread, 1000);
        CloseHandle(hThread);
        hThread = nullptr;
    }

    // 线程正常退出后清理窗口类
    CleanupPayloadWindowsAndClasses();

    CleanupOutputRedirection();

    if (m_pPayloadMemory) {
        VirtualFree(m_pPayloadMemory, 0, MEM_RELEASE);
        m_pPayloadMemory = nullptr;
    }

    AppendOutputLine("\x1b[90m------------------------- [ PAYLOAD OUTPUT END ] -------------------------\x1b[0m");

    if (exitCode >= 0x80000000) {
        AppendOutputLine(FormatStr("\x1b[31;1m[!] 捕获到异常终止退出码: 0x%08X\x1b[0m", exitCode));
    } else {
        AppendOutputLine(FormatStr("\x1b[1;32m[+] Payload 执行完毕，线程正常退出，退出码: 0x%08X (%d)\x1b[0m\r\n", exitCode, static_cast<int>(exitCode)));
    }

    UpdateStateUI(false);
}

} // namespace SCLoader
