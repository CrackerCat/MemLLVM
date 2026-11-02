#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* GetFileTypeName(DWORD type) {
    switch (type) {
        case FILE_TYPE_CHAR: return "CHAR (Console)";
        case FILE_TYPE_PIPE: return "PIPE (Anonymous/Named Pipe)";
        case FILE_TYPE_DISK: return "DISK (File)";
        case FILE_TYPE_REMOTE: return "REMOTE";
        default: return "UNKNOWN";
    }
}

int main(int argc, char* argv[]) {
    // 1. 模拟底层句柄与环境检测 (Console vs Pipe 检查)
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD typeIn = (hIn && hIn != INVALID_HANDLE_VALUE) ? GetFileType(hIn) : FILE_TYPE_UNKNOWN;
    DWORD typeOut = (hOut && hOut != INVALID_HANDLE_VALUE) ? GetFileType(hOut) : FILE_TYPE_UNKNOWN;

    // 2. 宿主参数检测与隔离
    // 在加载器加载 Flat Binary 运行线程时，宿主参数可能会被传递进来。
    // 如果命令行解析器未剥离非选项参数，某些 CLI 工具会误将 argv[1] 当成批处理命令并立即退出。
    int originalArgc = argc;

    printf("\n=====================================================================\n");
    printf("         MemLLVM - Interactive CLI Diagnostic Stub (Mock CLI)        \n");
    printf("=====================================================================\n");
    printf("[*] Process ID   : %lu | Thread ID: %lu\n", GetCurrentProcessId(), GetCurrentThreadId());
    printf("[*] STDIN Handle : 0x%p (Type: %s)\n", hIn, GetFileTypeName(typeIn));
    printf("[*] STDOUT Handle: 0x%p (Type: %s)\n", hOut, GetFileTypeName(typeOut));
    printf("[*] Host Argc    : %d\n", originalArgc);
    if (argc > 1) {
        printf("[!] 检测到宿主命令行参数 (argv[1] = \"%s\")\n", argv[1]);
        printf("[!] 提示: 已成功隔离参数，避免被当成批处理命令而导致程序非预期提前退出。\n");
    }
    printf("---------------------------------------------------------------------\n");
    printf("输入 'help' 查看支持的命令，输入 'exit' 或 'quit' 退出。\n\n");
    fflush(stdout);

    char line[1024];
    while (1) {
        printf("mock_cli > ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n[*] STDIN 收到 EOF 或输入通道关闭，交互结束退出。\n");
            fflush(stdout);
            break;
        }

        // 移除末尾换行符
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
            line[--len] = '\0';
        }

        if (len == 0) {
            continue;
        }

        // 命令分发处理
        if (strcmp(line, "help") == 0) {
            printf("可用命令列表:\n");
            printf("  help            - 显示本命令帮助信息\n");
            printf("  info            - 显示当前环境、PID/TID 以及管道句柄详情\n");
            printf("  args            - 打印启动时接收到的原始命令行参数\n");
            printf("  echo <内容>     - 测试标准输出回显与管道流转\n");
            printf("  stream <数量>   - 快速连续输出测试管道高频读取稳定性\n");
            printf("  ping            - 测试往返响应\n");
            printf("  exit / quit     - 正常退出交互循环 (退出码: 0)\n");
        } else if (strcmp(line, "info") == 0) {
            printf("[诊断信息]\n");
            printf("  PID          : %lu\n", GetCurrentProcessId());
            printf("  TID          : %lu\n", GetCurrentThreadId());
            printf("  STDIN 句柄   : 0x%p [%s]\n", hIn, GetFileTypeName(typeIn));
            printf("  STDOUT 句柄  : 0x%p [%s]\n", hOut, GetFileTypeName(typeOut));
            printf("  交互模式     : 激活 (Interactive Loop Running)\n");
        } else if (strcmp(line, "args") == 0) {
            printf("[命令行参数列表 (argc=%d)]\n", originalArgc);
            for (int i = 0; i < originalArgc; ++i) {
                printf("  argv[%d] = \"%s\"\n", i, argv[i] ? argv[i] : "(null)");
            }
        } else if (strncmp(line, "echo ", 5) == 0) {
            printf("[ECHO]: %s\n", line + 5);
        } else if (strncmp(line, "stream ", 7) == 0) {
            int count = atoi(line + 7);
            if (count <= 0) count = 10;
            if (count > 500) count = 500;
            printf("[*] 正在连续向管道输出 %d 行测试数据...\n", count);
            for (int i = 1; i <= count; ++i) {
                printf("  [Stream #%03d] Test packet data from mock_cli -> SCLoader\n", i);
            }
            printf("[*] 连续流输出完毕。\n");
        } else if (strcmp(line, "ping") == 0) {
            printf("pong (响应正常)\n");
        } else if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            printf("[*] 收到退出命令，正常退出，退出码: 0\n");
            fflush(stdout);
            break;
        } else {
            printf("未知命令: '%s'，输入 'help' 查看支持的命令。\n", line);
        }
        fflush(stdout);
    }

    return 0;
}
