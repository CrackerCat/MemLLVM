#include "SCLoaderApp.h"
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <exception>


int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR pCmdLine, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    int result = 0;

    SCLoader::SCLoaderApp app;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--auto-start") {
                app.SetAutoStart(true);
            } else if (!arg.empty() && arg[0] != L'-') {
                int len = WideCharToMultiByte(CP_UTF8, 0, arg.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (len > 0) {
                    std::string utf8Path(len - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, arg.c_str(), -1, utf8Path.data(), len, nullptr, nullptr);
                    app.SetInitialPayload(utf8Path);
                }
            }
        }
        LocalFree(argv);
    }

    result = app.Run();

    CoUninitialize();
    return result;
}
