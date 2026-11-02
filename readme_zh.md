# MemLLVM

<div align="center">

# ⚡ MemLLVM
### 基于 LLVM 编译管线与轻量级自举运行时的绝对位置无关纯内存扁平二进制生成框架
**直接将现代完整高阶 C 与 C++20 工程，原汁原味编译转化为绝对位置无关、即开即用、可纯内存运行的机器码镜像（`.bin`）**

[![Build & Release](https://github.com/Laster-dev/MemLLVM/actions/workflows/release.yml/badge.svg)](https://github.com/Laster-dev/MemLLVM/actions/workflows/release.yml)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-0078D6.svg?logo=windows)](https://microsoft.com)
[![Toolchain](https://img.shields.io/badge/Toolchain-LLVM%20%2F%20Clang--cl%2022-269539.svg?logo=llvm)](https://llvm.org)
[![Standard](https://img.shields.io/badge/Language-C17%20%2F%20C%2B%2B20-f34b7d.svg?logo=c%2B%2B)](https://isocpp.org)
[![Coverage](https://img.shields.io/badge/C%2FC%2B%2B%20%E6%A0%B8%E5%BF%83%E8%A6%86%E7%9B%96%E7%8E%87-~99%25-brightgreen.svg)](#-3-99-开发场景覆盖矩阵与局限性)
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

## 🎯 99% 开发场景覆盖矩阵与局限性

经过大量严苛的工业级项目实测，MemLLVM 已能够覆盖 **几乎 99% 的常见现代 C 与 C++ 开发场景**，几乎所有成熟的第三方开源开源库均可不改一行源码直接编译为纯内存运行镜像。

### ✅ 已经全面支持的开发场景（覆盖约 99%）：

| 开发维度 | 全面支持的技术能力 |
| :--- | :--- |
| **现代语言特性** | 模板元编程、概念（Concepts）、Lambda 闭包、Meyers 线程安全单例、`constexpr`、多层多重虚拟继承与多态虚表（`vtable`）、函数指针回调。 |
| **生命周期管理** | 全局对象与静态变量自动调用构造函数（`@llvm.global_ctors`）、进程退出时自动逆序调用析构函数（`atexit`）、作用域 RAII。 |
| **内存分配体系** | 完全自举的 `malloc`、`free`、`realloc`、`calloc`、`operator new`、`operator delete`、内存对齐分配、高速分桶 Arena 分配器。 |
| **并发与多线程** | 完整的 `std::thread`、`std::mutex`、`std::recursive_mutex`、`std::condition_variable`、`std::shared_mutex`、`std::async`、`std::future` 以及原子操作（`std::atomic`）。 |
| **标准 I/O 流体系** | 完整的 `std::cout`、`std::cin`、`std::cerr`、`printf`、`wprintf`、`fgets`，支持控制台交互式打字循环、标准管道流转及文件流（`std::fstream`）。 |
| **字符串与容器** | `std::string`、`std::wstring`、`std::vector`、`std::map`、`std::unordered_map`、`std::queue`、`std::deque`、`std::span`，UTF-8 与 UTF-16 宽字符集转换。 |
| **图形与系统生态** | 原生 Win32 窗口消息循环、GDI 矢量图形、**DirectX 11 / DXGI 硬件加速管线**、Windows 多媒体音频（`winmm`）、标准文件系统（`std::filesystem`）。 |

---

### ⚠️ 目前明确不支持的边缘场景（剩余约 1%）及攻坚路线：

在完全脱离操作系统 PE 加载器的前提下，由于软硬件底层的天然壁垒，目前有且仅有以下三项技术边界暂时无法直接使用：

1. **C++ 异常处理 (`try` / `catch` / `throw`)**：
   - **根本原因**：Windows x64 平台的结构化异常展开（Stack Unwinding）强依赖构建时生成的 `.pdata` 与 `.xdata` 函数展开表，并必须经由操作系统内核注册登记（`RtlAddFunctionTable`）。纯内存裸二进制若未向内核登记，抛出异常时内核将无法回溯调用栈，直接引发进程崩溃终止。
   - **当前要求**：必须在编译选项中关闭异常处理（`/EHs-c-`、`/GR-`、`/clang:-fno-exceptions`）。
   - **后续攻坚方案**：团队正在攻坚基于微型运行时在自重定位引导阶段自省代码段范围，并自动调用 `RtlAddFunctionTable` 向内核自注册展开表的方案，力争在后续大版本中彻底解除异常限制。

2. **Windows PE 资源编译器文件 (`.rsrc` / RC 资源)**：
   - **根本原因**：扁平二进制不存在 PE 节头与资源目录树（Resource Directory Tree）。传统 Win32 API（如 `FindResource`、`LoadIcon`、`LoadAccelerators`）依赖由系统分配的 `HMODULE` 句柄寻址 PE 资源。
   - **当前要求**：贴图、字体、音频、着色器等静态资产需以内联二进制数组（如十六进制 C 数组）或伴生数据包文件（如 `doom1.wad`）方式载入。
   - **后续攻坚方案**：计划引入内存虚拟 PE 资源包挂载与伪 `HMODULE` 拦截器。

3. **原生 C++ `thread_local` 关键字**：
   - **根本原因**：原生 `thread_local` 依赖操作系统 PE 引导分配 `__tls_index` 并由系统在线程创建时分配 TEB 槽位。
   - **当前要求**：若业务强依赖线程私有存储，请直接调用 Win32 原生 TLS API（`TlsAlloc`、`TlsGetValue`、`TlsSetValue`）。（注：局部静态变量的 Meyers 单例已通过原子 CAS 改写获得 100% 支持）。

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

## 🏆 已得到验证的旗舰级实战项目（`examples/`）

在 `examples/` 目录下，所有项目均已成功实现**从源代码直接全量编译为纯内存运行的单个 `.bin` 镜像**：

```
examples/
├── box2d/       # Box2D 3.0 顶级 2D 刚体物理引擎（45 个源码文件，实时 GDI 渲染）
├── chip8/       # 完整 CHIP-8 虚拟 CPU / 声效机（现代 C++20，流畅运行 Pong/俄罗斯方块）
├── doom/        # 经典 DOOM 3D 游戏引擎（83 个源码文件，纯 CPU 软件光栅化，实时音频流）
├── imgui/       # Dear ImGui v1.91+ 工业级 GUI（DirectX 11 硬件加速渲染管线）
├── sqlite/      # 官方 SQLite3 命令行终端（完整交互式 REPL、SQL 执行引擎）
└── mock_cli/    # 诊断型交互式 CLI 测试桩（测试标准管道、参数透传与流传输稳定性）
```

| 旗舰验证项目 | 语言体系与规模 | 扁平镜像体积 | 实战验证的技术能力与亮点 |
| :--- | :--- | :--- | :--- |
| **[官方 SQLite3 CLI](examples/sqlite/)** | C / SQLite 3.45 | **340 KB** | 官方完整交互式 SQL 终端，内存中建表、查询、索引构建、表格格式化回显、完全支持标准管道读写与交互式键盘录入。 |
| **[经典 DOOM 引擎](examples/doom/)** | 纯 C (83 个源码文件) | **703 KB** | 经典 DOOM 3D 引擎纯内存光栅化渲染，实时键盘方向控制，纯软件多声道音频混音流，从伴生 `doom1.wad` 实时流式读取游戏资产。 |
| **[Dear ImGui v1.91+](examples/imgui/)** | C++17 工业级 UI | **1.70 MB** | 完整即时模式图形界面、多窗口停靠系统（Docking）、图表、样式编辑器，完全通过**硬件加速的 DirectX 11 / DXGI** 管线渲染。 |
| **[Box2D 3.0 物理引擎](examples/box2d/)** | 纯 C (45 个源码文件) | **605 KB** | 连续刚体碰撞模拟、四叉树/动态 BVH 粗筛、冲量积分约束求解器、原生 Win32 GDI 矢量线框实时矢量渲染。 |
| **[CHIP-8 虚拟机](examples/chip8/)** | 现代 C++20 | **42 KB** | 35 条 CPU 机器指令仿真器、帧定时器、蜂鸣音效、64x32 单色点阵显存，稳定流畅运行经典游戏。 |

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
   - 启用 C++ 异常：**否 (`/EHs-c- /GR-`)**
   - 安全检查：**禁用安全检查 (`/GS-`)**
4. **C/C++** -> **命令行** -> **其他选项**：
   ```text
   /clang:-fpass-plugin="$(MEMLLVM_DIR)\bin\MemLLVM.dll" /clang:-fno-jump-tables /clang:-fno-builtin
   ```
5. **链接器** -> **输入**：
   - 忽略所有默认库：**是 (`/NODEFAULTLIB`)**
   - 附加依赖项：`ltl.lib;kernel32.lib;user32.lib;gdi32.lib;d3d11.lib;`
6. **生成事件** -> **生成后事件**：
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
