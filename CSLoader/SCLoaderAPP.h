#pragma once

#include <windows.h>
#include <shlobj.h>
#include <shldisp.h>
#include <exdisp.h>

#include "CUI.h"
#include "framework/controls/terminal/ITerminalBackend.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

namespace SCLoader {

class SCLoaderApp;

// 交互式终端输入后端桥接（支持键盘打字与 std::cin / ReadFile 交互）
class SCLoaderTerminalBackend : public CUI::Term::ITerminalBackend {
public:
    explicit SCLoaderTerminalBackend(SCLoaderApp* app) : m_app(app) {}

    void ApplyOutputCallback(OutputCallback callback) override {
        m_callback = std::move(callback);
    }

    bool Start(int cols, int rows) override { return true; }
    void Resize(int cols, int rows) override {}
    void Stop() override {}
    bool IsRunning() const override { return true; }

    void Write(const char* data, size_t length) override;

private:
    SCLoaderApp* m_app = nullptr;
    OutputCallback m_callback;
};

// 自定义消息定义（参照 GeekApp 自定义消息）
constexpr UINT WM_SC_OUTPUT = WM_APP + 70;
constexpr UINT WM_SC_THREAD_FINISHED = WM_APP + 71;
constexpr UINT WM_SC_AUTOSTART = WM_APP + 72;

struct ScOutputMessage {
    std::string text;
};

class SCLoaderApp {
    friend class SCLoaderTerminalBackend;
public:
    SCLoaderApp();
    ~SCLoaderApp();

    int Run();

    void SetInitialPayload(const std::string& path) { m_payloadPath = path; }
    void SetAutoStart(bool autoStart) { m_autoStart = autoStart; }

private:
    std::shared_ptr<CUI::UIElement> BuildRoot();

    // 核心执行与文件交互
    void DoOpenFile();
    BOOL StartSC();
    BOOL StopSC();
    void ClearConsole();
    void RestartSelf(bool asAdmin);
    void RestartAsUnelevated();

    // 输出与日志输出辅助
    void AppendOutput(const std::string& text);
    void AppendOutputLine(const std::string& line);
    void OnOutputReceived(const std::string& text);
    void OnThreadFinished(DWORD exitCode);

    // 管道重定向辅助
    void SetupOutputRedirection();
    void CleanupOutputRedirection();
    void CleanupPayloadWindowsAndClasses();
    static DWORD WINAPI PipeReaderThreadProc(LPVOID param);

    // 第二张 CRT 文件描述符表的重定向 (ucrtbase.dll)
    // SCLoader 自身用 VC-LTL 链接 msvcrt.dll，而 payload 可能通过 ucrtbase.dll
    // 写 stdout，两者 fd 表互不相干，必须分别重定向。
    void RedirectUcrtStreams();
    void RestoreUcrtStreams();

    // Shellcode 执行工作线程入口
    static DWORD WINAPI ShellcodeWorkerThread(LPVOID param);

    // 更新界面各控件状态
    void UpdateStateUI(bool running);

    // 窗口与核心控件（参照 GeekApp 架构与用户规范）
    CUI::Window m_window;
    std::shared_ptr<CUI::UIElement> m_root;

    // 自定义磨砂标题栏
    std::shared_ptr<CUI::Controls::WindowTitleBar> m_titleBar;

    // 顶部 CommandBar 及各类操作按钮
    std::shared_ptr<CUI::CommandBar> m_commandBar;
    std::shared_ptr<CUI::Controls::Button> m_openBtn;
    std::shared_ptr<CUI::Controls::Button> m_startBtn;
    std::shared_ptr<CUI::Controls::Button> m_stopBtn;
    std::shared_ptr<CUI::Controls::Button> m_clearBtn;
    std::shared_ptr<CUI::Controls::Button> m_restartBtn;
    std::shared_ptr<CUI::Controls::Button> m_adminRestartBtn;

    // 当前选中的 Payload 路径文本标签 (Label)
    std::shared_ptr<CUI::Controls::TextBlock> m_pathLabel;

    // 内置终端控制台（用于实时呈现 payload 输出与重定向日志）
    std::shared_ptr<CUI::TerminalControl> m_console;

    // 底部状态栏
    std::shared_ptr<CUI::StatusBar> m_statusBar;
    int m_sbStatusId = -1;
    int m_sbFileId = -1;
    int m_sbThreadId = -1;

    // 当前选中的 Payload 路径
    std::string m_payloadPath;
    bool m_autoStart = false;

    // 线程与执行状态
    HANDLE hThread = nullptr;
    DWORD m_threadId = 0;
    LPVOID m_pPayloadMemory = nullptr;
    size_t m_payloadSize = 0;
    std::atomic<bool> m_isRunning{ false };

    // 管道句柄与 CRT 文件描述符重定向 (STDOUT / STDERR)
    HANDLE m_hPipeRead = nullptr;
    HANDLE m_hPipeWrite = nullptr;
    HANDLE m_hPipeThread = nullptr;
    std::atomic<bool> m_isPipeReading{ false };
    HANDLE m_hOldStdOut = nullptr;
    HANDLE m_hOldStdErr = nullptr;
    int m_nOldFd1 = -1;
    int m_nOldFd2 = -1;
    int m_nPipeFd = -1;

    // 输入管道句柄与 CRT 文件描述符重定向 (STDIN)
    HANDLE m_hPipeInRead = nullptr;
    HANDLE m_hPipeInWrite = nullptr;
    HANDLE m_hOldStdIn = nullptr;
    int m_nOldFd0 = -1;
    int m_nPipeInFd = -1;

    // ucrtbase.dll 的 fd 表重定向状态（与上面 msvcrt.dll 那套完全独立）
    HMODULE m_hUcrt = nullptr;
    int m_nUcrtOldFd0 = -1;
    int m_nUcrtOldFd1 = -1;
    int m_nUcrtOldFd2 = -1;
    int m_nUcrtPipeFd = -1;
    int m_nUcrtPipeInFd = -1;

    // 交互式终端输入桥接与行缓冲
    void OnTerminalInput(const char* data, size_t length);
    void HandleHistoryUp();
    void HandleHistoryDown();
    void ReplaceInputLine(const std::string& newLine);
    std::unique_ptr<SCLoaderTerminalBackend> m_backend;
    std::string m_inputLine;
    std::vector<std::string> m_history;
    int m_historyIndex = -1;
    std::string m_savedCurrentLine;
    std::string m_escapeSeq;
    bool m_inEscape = false;

    // 输出消息队列与线程同步
    std::mutex m_queueMutex;
    std::vector<std::string> m_pendingQueue;
    char m_lastChar = 0;

    // 窗口子类化消息分发处理（参照 GeekApp::SubclassProc）
    static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
};

} // namespace SCLoader
