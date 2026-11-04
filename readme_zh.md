# MemLLVM

<div align="center">

# ⚡ MemLLVM
### 基于 LLVM 编译管线与轻量级自举运行时的绝对位置无关纯内存扁平二进制生成框架
**直接将现代完整高阶 C 与 C++20 工程，原汁原味编译转化为绝对位置无关、即开即用、可纯内存运行的机器码镜像（`.bin`）**

[![Build & Release](https://github.com/Laster-dev/MemLLVM/actions/workflows/release.yml/badge.svg)](https://github.com/Laster-dev/MemLLVM/actions/workflows/release.yml)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-0078D6.svg?logo=windows)](https://microsoft.com)
[![Toolchain](https://img.shields.io/badge/Toolchain-LLVM%20%2F%20Clang--cl%2022-269539.svg?logo=llvm)](https://llvm.org)
[![Standard](https://img.shields.io/badge/Language-C17%20%2F%20C%2B%2B20-f34b7d.svg?logo=c%2B%2B)](https://isocpp.org)
[![Coverage](https://img.shields.io/badge/C%2FC%2B%2B%20%E6%A0%B8%E5%BF%83%E8%A6%86%E7%9B%96%E7%8E%87-%3E99.5%25-brightgreen.svg)](#-3-995-开发场景覆盖矩阵与局限性)
[![License](https://img.shields.io/badge/License-MIT-blueviolet.svg)](LICENSE)

[**English Documentation**](readme.md) | [**简体中文技术文档**](readme_zh.md)

</div>

---

## 🌟 什么是 MemLLVM？

**MemLLVM** 是一个基于 **LLVM New Pass Manager（新版通行证管理器）** 架构的工业级编译器扩展插件与自举运行时框架，专为 Windows 平台 `clang-cl` 工具链深度定制。

与传统的“内存 PE 加载器（Reflective DLL）”、“二进制加壳器（Packer）”或“运行时模拟器”截然不同：**MemLLVM 不搞层层包裹的外壳，更不在宿主中模拟操作系统**。它直接扎根于 **LLVM 中间表示（IR）编译层**，在源码编译阶段彻底剥离对操作系统 PE 加载器（`ntdll!LdrLoadDll`、节区映射对齐、IAT 静态填充、重定位目录与 OS TLS 分配）的一切依赖假设。

最终编译产物是一段 **绝对位置无关、自包含、自重定位的纯连续扁平机器码（`.bin`）**。它可以被拷贝到任意具有执行权限的内存页中，仅需一个无参数函数指针即可直接瞬间启动：

```cpp
typedef void (*ShellcodeEntry)();
ShellcodeEntry entry = (ShellcodeEntry)pMemory;
entry(); // 瞬间自举完成 C++20 运行时、静态对象初始化并进入 main()！
```

---

## 🏗️ 编译管线流水线架构

```
+-------------------------------------------------------------------------------------------------+
|                                    MemLLVM 编译转换全流程流水线                                  |
+-------------------------------------------------------------------------------------------------+
|  [现代 C / C++20 业务源码]  --->  [Clang AST 前端 / LLVM IR 优化引擎]                            |
|                                                  |                                              |
|                                                  v                                              |
|                     +---------------------------------------------------------+                 |
|                     |             MemLLVM 8 大核心 IR Pass 插件处理流          |                 |
|                     |  1. AutoMemoryPass     - 自动合成无依赖 Heap 堆分配器   |                 |
|                     |  2. StlStubPass        - 消除 C++ 异常与 TLS 崩溃陷阱   |                 |
|                     |  3. GlobalLifecyclePass- 消除 .CRT$XCU，织入构造析构链  |                 |
|                     |  4. GlobalSectionPass  - 强制合并不连续节区为平坦段     |                 |
|                     |  5. GlobalPointerPass  - 动态虚表(vtable)基址差值重定位 |                 |
|                     |  6. AutoLoadLibraryPass- 自动检测并动态注入子系统 DLL   |                 |
|                     |  7. StackStringPass    - 栈上寄存器展开消除只读字符串   |                 |
|                     |  8. ApiResolvePass     - 抹除 IAT，内联 ROR13 PEB 寻址  |                 |
|                     +---------------------------------------------------------+                 |
|                                                  |                                              |
|                                                  v                                              |
|                        [优化后的 COFF 目标文件] + [LTL 独立运行时 (ltl.lib)]                     |
|                                                  |                                              |
|                                                  v                                              |
|                           [扁平二进制提取器 (extract_flat_bin.py)]                               |
|                                 + [ABI 严格合规的自重定位引导桩]                                |
|                                                  |                                              |
|                                                  v                                              |
|                ===================================================================              |
|                >>>  最终产物：纯内存自重定位绝对位置无关二进制镜像 (.bin)  <<<                  |
|                ===================================================================              |
+-------------------------------------------------------------------------------------------------+
```

---

## 🎯 工业级技术能力覆盖与现实架构边界深度剖析

MemLLVM 攻克了 C++20 语言层、虚表重定位、原子初始化、内存堆管理与 C++ 异常展开的核心难关，实现了纯内存环境下前所未有的 C++ 语言完整度。然而，**从底层系统架构与操作系统契约的角度审视**，完全脱离操作系统 PE 加载器（Loaderless Execution）在现实开发中必然面临一系列客观存在的**环境割裂与系统契约边界**。

开发者在设计或移植大型项目时，必须充分认知并遵循以下六大核心维度的技术约束与最佳实践：

### ✅ 编译器与语言层已全面突破的场景：

| 开发维度 | 全面支持的技术能力 |
| :--- | :--- |
| **现代语言特性** | 模板元编程、概念（Concepts）、Lambda 闭包、Meyers 线程安全单例、`constexpr`、多层多重虚拟继承与多态虚表（`vtable`）、函数指针回调。 |
| **C++ 异常与栈回溯** | **完整支持 `try` / `catch` / `throw` 与重新抛出（`throw;`）**！自包含表驱动 SEH 机制，引导桩自动向内核注册动态 `.pdata` 展开表（`RtlAddFunctionTable`），保证栈回溯（Stack Unwinding）过程中局部变量 RAII 析构函数 100% 可靠执行，退出自动注销。 |
| **生命周期管理** | 全局对象与静态变量自动调用构造函数（`@llvm.global_ctors`）、进程退出时自动逆序调用析构函数（`atexit`）、作用域 RAII。 |
| **内存分配体系** | 完全自举的 `malloc`、`free`、`realloc`、`calloc`、`operator new`、`operator delete`、内存对齐分配、高速分桶 Arena 分配器。 |
| **并发与多线程** | 完整的 `std::thread`、`std::mutex`、`std::recursive_mutex`、`std::condition_variable`、`std::shared_mutex`、`std::async`、`std::future` 以及原子操作（`std::atomic`）。 |
| **标准 I/O 流体系** | 完整的 `std::cout`、`std::cin`、`std::cerr`、`printf`、`wprintf`、`fgets`，支持控制台交互式打字循环、标准管道流转及文件流（`std::fstream`）。 |
| **字符串与容器** | `std::string`、`std::wstring`、`std::vector`、`std::map`、`std::unordered_map`、`std::queue`、`std::deque`、`std::span`，UTF-8 与 UTF-16 宽字符集转换。 |
| **图形与系统生态** | 原生 Win32 窗口消息循环、GDI 矢量图形、**DirectX 11 / DXGI 硬件加速管线**、Windows 多媒体音频（`winmm`）、标准文件系统（`std::filesystem`）。 |

---

### ⚠️ 纯内存无加载器环境下的 6 大现实技术边界与避坑准则：

#### 1. 模块身份与文件系统路径割裂（Module Identity & Filesystem Context）
* **`GetModuleHandle(NULL)` 与 `GetModuleFileName` 身份伪造陷阱**：
  在裸内存运行模式下，操作系统并没有为当前扁平二进制分配独立的 `HMODULE`。任何代码或第三方库若调用 `GetModuleHandle(NULL)` 或 `GetModuleFileName(NULL, ...)` 试图定位“自身所在目录”以加载同级数据（如游戏资源、字体、SQLite 数据库、配置文件），实际拿到的都会是**宿主宿主进程（如 `explorer.exe` 或加载器）的绝对路径**！
  - **避坑准则**：严禁依赖 `GetModuleFileName(NULL)` 寻找相对资产，统一通过显式绝对路径、内存内联资产（静态字节数组）或由入口函数传参指定。
* **当前工作目录（CWD）依赖风险**：
  相对路径（如 `fopen("config.ini", "r")`）完全取决于宿主进程的当前工作目录（Current Directory）。若宿主工作目录与预期不符会导致文件打开失败。

#### 2. 操作系统加载器与 PE 结构级特性缺失（Absence of OS Loader Facilities）
* **原生 PE 资源系统（`.rsrc` / RC 资源 / 图标 / 对话框模板）**：
  扁平二进制剥离了所有 PE 节头与 Resource Directory 树。原生 Win32 资源 API（如 `FindResource`、`LoadIcon`、`LoadAccelerators`、`DialogBox`）无法使用。
  - **避坑准则**：贴图、字体、音效、着色器等静态资产请以内联二进制数组（如十六进制 C 数组）或伴生数据包文件（如 `doom1.wad`、独立 sqlite 库）方式加载。
* **Windows 清单与现代控件视觉样式（Manifest, ComCtl32 v6 & High-DPI）**：
  传统 PE 依靠内嵌清单（`RT_MANIFEST`）启用 Common Controls v6 现代视觉样式及高 DPI 感知。在裸二进制下，Win32 原生控件默认回退到 Windows 2000 经典扁平灰色风格。
  - **避坑准则**：如需现代视觉外观，建议采用 Direct2D / DirectX 11 / Dear ImGui 自绘渲染管线，或在代码中显式调用 `CreateActCtx` 挂载清单并调用 `SetProcessDpiAwarenessContext`。
* **原生命名空间级 `thread_local` 与 TLS 回调（TLS Callbacks）**：
  局部静态单例虽然已被 IR 改写为原子 CAS 解决，但全局命名空间或类静态的 `thread_local` 变量仍旧依赖操作系统 PE 引导分配的 `__tls_index` 与 TEB 槽位。此外，操作系统在创建/销毁新线程时，无法主动触发扁平二进制内的 TLS 回调（`PIMAGE_TLS_CALLBACK`）。
  - **避坑准则**：跨线程私有数据推荐直接调用 Win32 原生 TLS API（`TlsAlloc`、`TlsGetValue`、`TlsSetValue`）。

#### 3. 执行形态与导出接口约束（Execution Morphology & Library Boundary）
* **单入口执行体模型（Single Entry Point），无动态符号导出表（No EAT）**：
  MemLLVM 输出的是单一扁平入口（Offset 0 自重定位后跳转到 `main`）的执行体，**无法作为多函数共享动态链接库（DLL）导出数十个具名符号供外部第三方程序按名动态调用**。它是一个纯粹的任务载荷/可执行体模型，而非动态共享库导出模型。
* **纯 64 位单架构生态（Strictly x64 Architecture Only）**：
  全套编译管线（PE 解析、DIR64 修复、ROR13 API 寻址引擎、`.pdata` 12 字节展开表、自举汇编 Stub）深度绑定于 **Windows x86_64 ABI**。目前**不兼容 32 位 x86（IA-32）**（其使用 FS 段寄存器、HIGHLOW 重定位及链表式 SEH 栈帧）与 **ARM64**。

#### 4. 进程生命周期与宿主共生耦合（Host Process Lifecycle Coupling）
* **`exit()` / `abort()` 宿主连带杀死风险**：
  在独立 EXE 中，调用标准库 `exit(code)` 正常终结进程是标准行为；但在将扁平二进制嵌入宿主进程运行的模式下，调用 `exit()` 或 `abort()` 会直接触发 `ExitProcess`，**导致宿主进程被瞬间连带杀死**！
  - **避坑准则**：若希望载荷像子程序一样平稳退出并将控制权还给宿主，必须通过 `main()` 正常 `return` 或调用 `ExitThread`，严禁在嵌入模式下随意调用 `exit()`。
* **直接命令行查询污染（`GetCommandLine`）**：
  Win32 API `GetCommandLineA/W` 查询的是宿主进程在操作系统创建时的全局命令行字符串。虽然 MemLLVM 的引导桩能够重构 `main(argc, argv)`，但若第三方库绕过 `main` 的参数直接调用 `GetCommandLineW()`，将读到宿主进程的命令参数。

#### 5. 符号动态解析的边界与全序数支持（API Dynamic Resolution & Ordinal Support）
* **全量支持按序数导出（Ordinal-Only）API 自动重定向与静态索引**：
  在 Windows 生态中，诸如 `shlwapi.dll`（多达 551 个）、`shell32.dll`（多达 364 个）、`ws2_32.dll`、`comctl32.dll` 等核心动态库存在大量不包含函数名字符串、仅能通过序号引用的隐藏/无名 API（`[NONAME]` 导出，如 `SHAlloc`、`SHFree`、`ConnectToConnectionPoint`）。
  MemLLVM 全面攻克了序数导出解析难题：
  1. **SDK 全量序数数据库**：内置收录 Windows SDK 全部 80 个核心系统 DLL 的 2,729 个无名序数导出符号映射，并支持项目私有 `.lib` 库动态序数提取；
  2. **编译期全透明内联改写**：`ApiResolvePass` 自动将序数 API 的直接调用、`invoke` 异常调用及 `__imp_*` 导入槽改写为 `MemLLVM_GetApiAddressByOrdinal(dll_hash, ordinal)`，并由 `AutoLoadLibraryPass` 自动补齐所属 DLL 的预加载；
  3. **全局函数指针与虚表自动重定位**：`GlobalPointerPass` 支持在载荷入口自动将包含序数 API 地址的全局结构体与虚表指针重构为运行时真实函数地址；
  4. **原生多级转导链（Forwarder Chain）与合成符号**：自举解析器全面支持 `DLL.#ordinal` 转导解析，并支持 `<dll>_Ordinal_<ord>`、`Ordinal<ord>` 语法直接调用任意无名序数接口。
* **极度冷门非 SDK 第三方库的显式加载边界**：
  若载荷依赖某个未在 Windows SDK 中登记、且未提供导入库 `.lib` 供扫描的完全专有/第三方私有 DLL，则需在代码中先调用 `LoadLibraryA("custom.dll")` 将其加载进宿主 PEB 模块链表后方可解析。

#### 6. 现代操作系统安全防护策略对抗（OS Security Mitigations）
* **W^X 与 DEP 内存执行权限**：
  载荷必须放置在具有执行权限的内存页（RWX 或在执行前通过 `VirtualProtect` 将 RW 切换为 RX）。
* **控制流防护（Control Flow Guard / CFG）与 ACG 限制**：
  若宿主进程开启了强制控制流防护（CFG），在未调用 `SetProcessValidCallTargets` 登记内存函数入口的情况下，间接跳转可能会触发 `0xC0000409`（FAST_FAIL_CONTROL_FLOW_GUARD_CHECK_FAILURE）；若宿主开启了 Arbitrary Code Guard (ACG)，则操作系统内核直接禁止在进程内创建或修改可执行代码页。

---

## 🕳️ 开发中的血泪避坑实录（硬核底层填坑记）

在打造 MemLLVM 的过程中，我们踩平了无数 Windows 内核与编译器未公开机制的大坑。以下是部分具有重大参考价值的实战填坑纪实：

### 坑点 1：Windows 内核 `GetStdHandle` 的未公开 0x400 / 0x200 拦截黑幕
* **诡异现象**：使用带有管道重定向的 GUI 宿主运行 CLI 交互式工具时，宿主明明成功调用了 `SetStdHandle(STD_OUTPUT_HANDLE, hPipeWrite)` 且返回值为 1，但在 Payload 内部一读 `GetStdHandle(STD_OUTPUT_HANDLE)` 竟然离奇返回 `0x(nil)` (NULL)！导致原生 MSVC 编译的 CLI 工具认为没有标准输出设备，所有输出全部被静默吞掉，或者检测到非交互直接退出。
* **逆向抓出真凶**：我们直接对 Windows 11/10 的 `kernelbase!GetStdHandle` 进行了深度汇编逆向：
  ```x86asm
  GetStdHandle:
      mov rax, gs:[0x60]                   ; rax = 当前进程 PEB
      mov rcx, [rax + 0x20]                ; rcx = ProcessParameters
      test dword ptr [rcx + 0xa4], 0x400   ; 检查 WindowFlags 偏移 0xA4！
      jne return_null                      ; 命中 0x400 标记？强行直接返回 NULL！
      mov rax, [rcx + 0x28]                ; 否则才读取 StandardOutput 句柄
      ret
  ```
  原来，当进程通过资源管理器（Explorer）或 `ShellExecuteEx` 启动时，系统会在 `WindowFlags` 中写入内部标记 `0x400`。微软官方的 `SetStdHandle` 只写句柄，根本不会帮你清掉 `0x400`！
* **终极攻克**：在加载器与自举代码中直接穿透 PEB，强制抹平 `ProcessParameters->WindowFlags` 中的 `0x400` 与 `0x200` 拦截位，并设置 `STARTF_USESTDHANDLES (0x100)`，彻底解封 Windows 标准句柄！

---

### 坑点 2：双 CRT 文件描述符表割裂冲突（`msvcrt.dll` vs `ucrtbase.dll`）
* **诡异现象**：加载器使用 `_dup2` 将 stdout 重定向到了输出管道，部分老版本或自编译代码能打印，但只要第三方库一调用 `ucrtbase.dll` 的 `printf`，内容依然神秘失踪。
* **真相大白**：`msvcrt.dll` 与 `ucrtbase.dll` 在同一个进程内部拥有**两套完全彼此独立、互不知晓的文件描述符（fd）表**！只重定向 `msvcrt.dll` 的 fd 1，`ucrtbase.dll` 的 fd 1 依旧是 `-2`（`_NO_CONSOLE_FILENO`）。
* **终极攻克**：在加载器底层同时检测并接管两张表，通过 `DuplicateHandle` 复制出独立的管道系统句柄，分别填入两套 CRT，实现全运行时无死角拦截。

---

### 坑点 3：LLVM 插件跨版本 C++ ABI 堆破坏崩溃（0xC0000374）
* **诡异现象**：在本地编译好的 LLVM Pass 插件，放到官方发布的正式版 Clang-cl 中一挂载，在 Pass 注册瞬间直接触发 `STATUS_HEAP_CORRUPTION` 崩溃。
* **真相大白**：LLVM 的 C++ 源码大量使用非 ABI 冻结的复杂模板与 STL 容器，编译器微小的优化选项差异（如 `/MD` vs `/MT`、迭代器调试等）就会导致插件与宿主 `clang-cl.exe` 析构时产生内存冲突。
* **终极攻克**：我们在 GitHub Releases 资源发布列表（`clang-plugin-toolchain.zip`）与仓库（`release_artifacts/clang-plugin-toolchain.zip`）中提供了一套完全二进制对齐的轻量编译器工具链，保证无论在任何开发者机器还是 GitHub CI 上都能 100% 稳定运行。

---

### 坑点 4：局部静态变量（Meyers 单例）的隐式 TLS 空指针崩溃
* **诡异现象**：C++ 中极为经典的单例写法 `static MyClass& inst = ...`，在编译为裸 Shellcode 后，一执行就必定在 `gs:[0x58]` 发生 `0xC0000005` 崩溃。
* **真相大白**：编译器为了保证静态变量多线程初始化的线程安全，偷偷插入了对 MSVC 内部函数 `_Init_thread_epoch` 的调用，并访问了模块 TLS 索引。裸内存代码根本没有由系统分配的 TLS 槽位。
* **终极攻克**：MemLLVM 的 `StlStubPass` 在 IR 层面将这些隐式检查全部拦截改写为基于常规内存的无锁原子 CAS 指令，不仅保全了多线程初始化安全，更彻底斩断了对 TLS 的依赖。

---

### 坑点 5：裸扁平机器码中的 Windows x64 SEH 异常展开与 .pdata 缺失陷阱
* **诡异现象**：在实现了 `_CxxThrowException`、`__CxxFrameHandler3` 并调用 `RtlAddFunctionTable` 注册动态异常表后，代码中的 `throw` 依然直接引发进程无声崩溃终止（异常代码 `0xE06D7363` 未捕获），外层的 `catch` 块完全无法拦截。
* **真相大白**：Windows x64 采用纯表驱动异常分发模型。当 `_CxxThrowException` 调用 `ntdll!RaiseException` 时，内核 SEH 分发器必须从 `RaiseException` 开始沿着栈帧向上逐级调用 `RtlVirtualUnwind` 回溯。如果其中任何一个非叶子函数（包括 `_CxxThrowException` 本身！）在编译时被禁用了 Unwind Info（如传递了 `/clang:-fno-unwind-tables`），内核在查询 `RUNTIME_FUNCTION` 时会直接返回 `NULL`。内核由于无法得知如何还原 `RSP`，**会立即判定异常无法继续展开，直接当场杀掉整个进程**！
* **终极攻克**：
  1. 为 `ltl/src/crt_exceptions.cpp` 单独开启 `/EHsc` 编译，生成完整的 `.pdata` 与 `.xdata` 异常元数据；
  2. 扁平打包工具 `extract_flat_bin.py` 自动保留并提取 `.pdata` 节区；
  3. 在二进制自举引导桩中，通过动态 `MEMLLVME` 描述符在内存中自动写入真实的加载基址、`.pdata` 运行时指针与条目数，无缝驱动 `RtlAddFunctionTable` 向 Windows 内核完成自注册与反注册。彻底实现纯内存裸二进制环境下完美的 C++ 异常捕获与 RAII 局部对象析构展开！

---

### 坑点 6：Windows 隐藏深渊——仅按序数导出（Ordinal-Only）API 的解析陷阱与转导链（Export Forwarder）攻克
* **诡异现象**：在调用 Windows Shell 或老旧网络 API（例如 `SHAlloc`、`SHFree`、`ConnectToConnectionPoint` 等）时，代码在常规 EXE 下链接运行一切正常，但放入扁平化内存代码后直接崩在空指针调用（`0x0`）或引发静态未定义导入符号。
* **真相大白**：Windows PE 规范中，DLL 导出函数并不强制包含名称。`shlwapi.dll` 拥有 551 个 `[NONAME]` 导出，`shell32.dll` 拥有 364 个。PE 导出表的 `AddressOfNames` 中压根没有它们的字符串，常规 ROR13 名称哈希遍历自然会返回 NULL。更有甚者，部分序数导出还是跨 DLL 转导（Forwarder），例如转导为 `SHUNIMPL.#429` 甚至是带名称的转导，单纯读偏移会直接踩中转导字符串所在的非可执行内存地址！
* **终极攻克**：
  1. 自举解析器实现纯 PE 导出表 $O(1)$ 序数寻址（`funcs[ordinal - Base]`）与递归转导器（深度限制防止环路，自动解析并支持转发到目标库的序数或符号名）；
  2. 离线全量提取 Windows 10/11 SDK 所有 80 个系统 `.lib` 库中多达 2,729 项序数条目，生成编译期二分哈希表；
  3. LLVM 编译管线全链路联动：`AutoLoadLibraryPass` 自动感知并预加载所属 DLL、`ApiResolvePass` 将无名调用下沉为双哈希序数寻址、`GlobalPointerPass` 自动完成虚表重构。彻底粉碎 Windows 序数黑盒壁垒！

---

## 🏆 已得到验证的旗舰级实战项目（`examples/`）

在 `examples/` 目录下，所有项目均已成功实现**从源代码直接全量编译为纯内存运行的单个 `.bin` 镜像**：

```
examples/
├── box2d/       # Box2D 3.0 顶级 2D 刚体物理引擎（45 个源码文件，实时 GDI 渲染）
├── chip8/       # 完整 CHIP-8 虚拟 CPU / 声效机（现代 C++20，流畅运行 Pong/俄罗斯方块）
├── doom/        # 经典 DOOM 3D 游戏引擎（83 个源码文件，纯 CPU 软件光栅化，实时音频流）
├── imgui/       # Dear ImGui v1.91+ 工业级 GUI（DirectX 11 硬件加速渲染管线）
├── sqlite/      # 官方 SQLite3 命令行终端（完整交互式 REPL、SQL 执行引擎）
├── mimikatz/    # 大规模复杂 C 原生工程（114 个编译单元，复杂结构体与跨模块全局符号）
└── mock_cli/    # 诊断型交互式 CLI 测试桩（测试标准管道、参数透传与流传输稳定性）
```

| 旗舰验证项目 | 语言体系与规模 | 扁平镜像体积 | 实战验证的技术能力与亮点 |
| :--- | :--- | :--- | :--- |
| **[官方 SQLite3 CLI](examples/sqlite/)** | C / SQLite 3.45 | **340 KB** | 官方完整交互式 SQL 终端，内存中建表、查询、索引构建、表格格式化回显、完全支持标准管道读写与交互式键盘录入。 |
| **[经典 DOOM 引擎](examples/doom/)** | 纯 C (83 个源码文件) | **703 KB** | 经典 DOOM 3D 引擎纯内存光栅化渲染，实时键盘方向控制，纯软件多声道音频混音流，从伴生 `doom1.wad` 实时流式读取游戏资产。 |
| **[Dear ImGui v1.91+](examples/imgui/)** | C++17 工业级 UI | **1.70 MB** | 完整即时模式图形界面、多窗口停靠系统（Docking）、图表、样式编辑器，完全通过**硬件加速的 DirectX 11 / DXGI** 管线渲染。 |
| **[Box2D 3.0 物理引擎](examples/box2d/)** | 纯 C (45 个源码文件) | **605 KB** | 连续刚体碰撞模拟、四叉树/动态 BVH 粗筛、冲量积分约束求解器、原生 Win32 GDI 矢量线框实时矢量渲染。 |
| **[CHIP-8 虚拟机](examples/chip8/)** | 现代 C++20 | **42 KB** | 35 条 CPU 机器指令仿真器、帧定时器、蜂鸣音效、64x32 单色点阵显存，稳定流畅运行经典游戏。 |
| **[Mimikatz 2.2.0](examples/mimikatz/)** | 复杂工程级 C (114 个编译单元) | **1.20 MB** | 百级 TU 大规模复杂 C 工程，验证跨模块全局符号 Tentative Definition 合并、动态子系统导入消除、多层结构体内存排布及双 CRT 管道接管。 |
| **[Mock CLI 诊断桩](examples/mock_cli/)** | 精炼 C 运行时 | **36 KB** | 专为诊断和验证控制台管道设计，覆盖全双工标准输入输出流转、命令行参数透传、高频回显吞吐与平稳优雅退出。 |

---

## 🖥️ 关于 SCLoader / CSLoader 开源状态的重要说明

在 `Tools/` 目录下，MemLLVM 为大家提供了一款功能强大、现代美观的图形化测试工具 **SCLoader**（`Tools/SCLoader.exe`）。它支持加载、执行、终止任何扁平机器码，并内置了虚拟控制台交互管道与多级调试状态栏。

> **关于 CSLoader 源码开放的说明**：  
> SCLoader 的前端视觉呈现基于我们团队自研的 **CUI (Contemporary UI)** 高性能 Direct2D / DirectWrite Fluent Design 现代化图形引擎。由于 CUI 框架底层包含大量商业级专有组件，目前尚不具备全量开源的合规条件。  
> 
> 但为了确保开源社区的纯粹性与透明度，我们采取了如下开源策略：
> 1. 在 [`CSLoader/`](CSLoader/) 目录中**无保留公开全部底层核心代码**：包括 x64 汇编调用桥（`CallShellcode.asm`）、SEH 异常捕获机制、管理员与低权限自适应重启调度器、以及底层双 CRT 管道接管流控源码；
> 2. 在每个 GitHub Release 中直接提供**编译好的免安装单文件绿色版 `Tools/SCLoader.exe`**，所有开发者均可完全免费使用它来载入、执行与调试任意 `.bin` 产物！

---

## 🚀 极简上手使用指南

### 第一步：一键配置 SDK 环境（开箱即用）
1. 前往 [GitHub Releases](https://github.com/Laster-dev/MemLLVM/releases) 下载最新的 `MemLLVM-SDK-windows-x64.zip`。
2. 解压到本地任意目录（例如 `C:\MemLLVM`）。
3. **直接双击运行目录下的 `setup_env.bat`**：
   - 自动检测并写入环境变量 `MEMLLVM_DIR`；
   - **自动绑定对齐的轻量编译器**：自动将 `LLVMInstallDir` 环境变量绑定至 SDK 内置的 `toolchain` 目录，使 Visual Studio 的 `ClangCL` 工具集自动调用完全二进制对齐的 `clang-cl.exe`，彻底避免第三方 Clang 造成的 ABI 崩溃或插件加载失败；
   - 自动将 `toolchain\bin`、`bin` 和 `Tools` 挂载到用户 `PATH`，命令行直接可用 `clang-cl`；
   - **智能防冗余**：未来移动 SDK 到新目录，直接在新目录下再次双击即可自动迁移，绝不残留旧路径。
   - **一键平滑升级**：SDK 目录下内置了 `update.bat`。后续任何时候想要获取更新，直接双击运行 `update.bat` 即可自动拉取 GitHub 最新版本并热更新覆盖本地，同时自动刷新配置！

### 第二步：Visual Studio 启用 Clang
* **方式 A（最推荐内置组件）**：打开 Visual Studio Installer -> 修改 -> 单个组件 -> 搜索 `Clang` -> 勾选 **适用于 Windows 的 C++ Clang 编译器** 与 **LLVM 工具集 MSBuild 支持**。
* **方式 B（插件方式）**：Visual Studio 菜单 -> 扩展 -> 管理扩展 -> 搜索并安装 **LLVM Compiler Toolchain** 插件。

### 第三步：Visual Studio 原生工程（.vcxproj）配置
打开项目属性：
1. **常规** -> **平台工具集**：选择 **LLVM (clang-cl)**。
2. **C/C++** -> **常规** -> **附加包含目录**：
   ```text
   $(MEMLLVM_DIR)\include;$(MEMLLVM_DIR)\ltl\include;%(AdditionalIncludeDirectories)
   ```
3. **C/C++** -> **代码生成**：
   - 运行库：**多线程 (/MT)**
   - 启用 C++ 异常：**是 (`/EHsc`)**（完全支持异常捕获与 RAII 析构；若追求极致微型体积亦可选择 **否 (`/EHs-c- /GR-`)**）
   - 安全检查：**禁用安全检查 (`/GS-`)**
4. **C/C++** -> **命令行** -> **其他选项**：
   ```text
   /clang:-fpass-plugin="$(MEMLLVM_DIR)\bin\MemLLVM.dll" /clang:-fno-jump-tables /clang:-fno-builtin
   ```
5. **链接器** -> **常规** -> **附加库目录**：
   ```text
   $(MEMLLVM_DIR)\lib;$(MEMLLVM_DIR)\ltl;%(AdditionalLibraryDirectories)
   ```
6. **链接器** -> **输入**：
   - 忽略所有默认库：**是 (`/NODEFAULTLIB`)**
   - 附加依赖项：`ltl.lib;kernel32.lib;user32.lib;gdi32.lib;d3d11.lib;`
7. **链接器** -> **高级**：
   - 入口点：**`main`**
8. **生成事件** -> **生成后事件**：
   ```bat
   python "$(MEMLLVM_DIR)\Tools\extract_flat_bin.py" "$(TargetPath)" "$(TargetDir)$(TargetName).bin"
   ```

编译完成后，同级目录会直接生成绝对位置无关的 `.bin`，使用自带的 `SCLoader.exe` 即可一键运行！

---

## 💖 致敬与鸣谢

站在巨人的肩膀上，MemLLVM 的诞生离不开开源界众多杰出先驱的启发与贡献：

* **[VC-LTL](https://github.com/Chuyu-Team/VC-LTL5)** 与初雨团队（Chuyu-Team）：在 Windows 平台轻量级 CRT、精简无依赖运行时领域开创了极其卓越的工程范式。MemLLVM 的自由运行时（LTL）理念深深受益于 VC-LTL 的开拓与启发！
* **[LLVM / Clang 项目组](https://llvm.org)**：提供了世界上最具模块化、最强大、最具可塑性的中间表示架构与新通行证管理器体系。
* **[Omar Cornut](https://github.com/ocornut)** 与 Dear ImGui 社区：开创了即时模式图形界面的划时代标杆。
* **[Erin Catto](https://github.com/erincatto)** 与 Box2D 项目：缔造了工业级 2D 刚体动力学模拟的标准范式。
* **id Software 与 John Carmack**：为世界留下了历久弥新、纯粹而优雅的经典 DOOM 引擎。
* **D. Richard Hipp 与 SQLite 开发团队**：构建了全人类软件史上最坚固、最通用、最完美的嵌入式数据库引擎。

---

## 📈 Star 历史趋势

[![Star History Chart](https://api.star-history.com/svg?repos=Laster-dev/MemLLVM&type=Date)](https://star-history.com/#Laster-dev/MemLLVM&Date)

---

## 📄 开源许可证

本项目基于 [MIT License](LICENSE) 开源许可协议发布。  
样例目录中的第三方开源工程与技术原型均保留各自的原生开源许可证。
