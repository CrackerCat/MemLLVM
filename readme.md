# MemLLVM

> **Next-Generation LLVM Pass Plugin & Freestanding C/C++ Runtime for Position-Independent Flat Machine Code Generation**  
> *基于 LLVM 编译管线与轻量级自举运行时的绝对位置无关纯内存扁平二进制生成框架*

[![Build & Release](https://github.com/Laster-dev/MemLLVM/actions/workflows/release.yml/badge.svg)](https://github.com/Laster-dev/MemLLVM/actions/workflows/release.yml)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-blue.svg)](https://microsoft.com)
[![Toolchain](https://img.shields.io/badge/Toolchain-LLVM%20%2F%20Clang--cl-green.svg)](https://llvm.org)
[![Standard](https://img.shields.io/badge/C%2B%2B%20Standard-C17%20%2F%20C%2B%2B20-orange.svg)](https://isocpp.org)
[![License](https://img.shields.io/badge/License-MIT-purple.svg)](LICENSE)

---

### Navigation / 语言导航
- [English Documentation](#english-documentation)
  - [1. Overview & Core Philosophy](#1-overview--core-philosophy)
  - [2. The Challenge of In-Memory Code Execution](#2-the-challenge-of-in-memory-code-execution)
  - [3. MemLLVM LLVM Pass Pipeline Architecture](#3-memllvm-llvm-pass-pipeline-architecture)
  - [4. LTL: Freestanding C/C++ Runtime Library](#4-ltl-freestanding-cc-runtime-library)
  - [5. Position-Independent Code & Dynamic Self-Rebase](#5-position-independent-code--dynamic-self-rebase)
  - [6. Dynamic PEB Traversal & ROR13 API Resolution](#6-dynamic-peb-traversal--ror13-api-resolution)
  - [7. Thread-Local Storage (TLS) Handling & Strict Boundaries](#7-thread-local-storage-tls-handling--strict-boundaries)
  - [8. Current Framework Limitations & Roadmap](#8-current-framework-limitations--roadmap)
  - [9. Showcase Applications & Verification](#9-showcase-applications--verification)
  - [10. Quick Start & Visual Studio Integration Guide](#10-quick-start--visual-studio-integration-guide)
  - [11. Acknowledgements (VC-LTL Tribute)](#11-acknowledgements-vc-ltl-tribute)
  - [12. Star History](#12-star-history)
- [中文技术文档](#中文技术文档)
  - [1. 项目概述与核心哲学](#1-项目概述与核心哲学)
  - [2. 传统 PE 结构在内存执行场景下的根本缺陷](#2-传统-pe-结构在内存执行场景下的根本缺陷)
  - [3. MemLLVM 编译管线流水线架构](#3-memllvm-编译管线流水线架构)
  - [4. LTL：自包含无依赖 C/C++ 现代化运行时](#4-ltl自包含无依赖-cc-现代化运行时)
  - [5. 绝对位置无关码（PIC）与自重定位（Self-Rebase）](#5-绝对位置无关码pic与自重定位self-rebase)
  - [6. PEB 模块链表遍历与 ROR13 哈希动态 IAT 消除](#6-peb-模块链表遍历与-ror13-哈希动态-iat-消除)
  - [7. 线程局部存储（TLS）处理策略与技术边界](#7-线程局部存储tls处理策略与技术边界)
  - [8. 框架当前局限性与后续攻坚方向](#8-框架当前局限性与后续攻坚方向)
  - [9. 旗舰级重型应用测试与实战验证](#9-旗舰级重型应用测试与实战验证)
  - [10. 极简上手与 Visual Studio 原生项目（.vcxproj）对接指南](#10-极简上手与-visual-studio-原生项目vcxproj对接指南)
  - [11. 致敬与鸣谢（VC-LTL）](#11-致敬与鸣谢vc-ltl)
  - [12. Star 历史趋势](#12-star-历史趋势)

---

<a name="english-documentation"></a>
# English Documentation

## 1. Overview & Core Philosophy

**MemLLVM** is an advanced compiler-level transformation infrastructure built as an out-of-tree **LLVM New Pass Manager plugin**. Designed specifically for the Windows `clang-cl` toolchain, MemLLVM transforms high-level C and C++20 code directly into **pure, contiguous, self-relocating flat machine code images (`.bin`)** that can be loaded into arbitrary memory pages and executed immediately without invoking the operating system's PE loader (`ntdll!LdrLoadDll`, section mapping, or import resolution).

Unlike traditional shellcode generators or payload converters that rely on dirty runtime emulation, fragile Packers, or binary post-processing hooks, MemLLVM operates directly at the **LLVM Intermediate Representation (IR)** level. It bridges the gap between modern C++ abstractions (templates, virtual tables, static constructors, multithreading, concurrency, iostreams, DirectX/Direct2D graphics) and the strict operational constraints of raw in-memory code execution.

```
+-----------------------------------------------------------------------------------------+
|                                    MemLLVM Compilation Pipeline                        |
+-----------------------------------------------------------------------------------------+
|  [C/C++20 Source]  --->  [Clang AST / LLVM IR]                                          |
|                                |                                                        |
|                                v                                                        |
|                     +---------------------------------------+                           |
|                     |       MemLLVM Pass Plugin Pipeline    |                           |
|                     |  - AutoMemoryPass (CRT Synthesis)     |                           |
|                     |  - StlStubPass (Exception Erasure)    |                           |
|                     |  - GlobalLifecyclePass (Ctor / Dtor)  |                           |
|                     |  - GlobalSectionPass (Segment Merge)  |                           |
|                     |  - GlobalPointerPass (VTable Rebase)  |                           |
|                     |  - AutoLoadLibraryPass (Dynamic Dll)  |                           |
|                     |  - StackStringPass (RoData Erasure)   |                           |
|                     |  - ApiResolvePass (PEB/ROR13 Engine)  |                           |
|                     +---------------------------------------+                           |
|                                |                                                        |
|                                v                                                        |
|                     [Optimized Object (.obj)] + [LTL Runtime (ltl.lib)]                 |
|                                |                                                        |
|                                v                                                        |
|                     [Flat Binary Extractor + Self-Rebase Stub]                          |
|                                |                                                        |
|                                v                                                        |
|          =======================================================                        |
|          >>>  Standalone Position-Independent Machine Code (.bin) <<<                   |
|          =======================================================                        |
+-----------------------------------------------------------------------------------------+
```

---

## 2. The Challenge of In-Memory Code Execution

Standard compilers target operating system loader environments. Under Windows, an executable (`.exe` or `.dll`) fundamentally relies on the Portable Executable (PE) specification:

1. **Import Address Table (IAT / `.idata`)**: Calls to external Win32 APIs (`VirtualAlloc`, `CreateWindowExW`, etc.) are compiled as indirect calls through pointers in `.rdata` that must be filled by `ntdll.dll` during process bootstrap. In a flat binary, this table does not exist.
2. **Hardcoded Image Base & Relocations**: Global pointer tables, C++ virtual method tables (`vtable`), and function pointer arrays contain absolute 64-bit virtual addresses predicated on a fixed image base (e.g., `0x140000000`). If loaded at any other base address, memory dereferences cause immediate access violations (`0xC0000005`).
3. **C++ Runtime & Global Initializers (`.CRT$XCU`)**: C++ static objects and constructors are recorded as function pointer arrays in `.CRT$XCU`. Without the MSVC CRT startup harness (`__scrt_common_main`), static instances remain uninitialized, leading to null vtable crashes.
4. **Read-Only Data (`.rdata`) Separation**: Constant strings, floating-point literals, and jump tables are relegated to `.rdata`. Standard loaders allocating a single flat block often break when instructions attempt relative addressing across non-contiguous sections.
5. **Thread-Local Storage (TLS)**: Modern C++ features like thread-safe local statics (`Meyers Singleton`) invoke `__tls_index` and `_Init_thread_epoch`, referencing `TEB->ThreadLocalStoragePointer` (`gs:[0x58]`), which fault immediately when invoked outside an OS-registered module.

---

## 3. MemLLVM LLVM Pass Pipeline Architecture

MemLLVM hooks into Clang via `-fpass-plugin=MemLLVM.dll` and registers an 8-stage optimization pass pipeline through `registerOptimizerLastEPCallback`:

### 3.1 `AutoMemoryPass`
Intercepts memory allocation operations across the IR. It automatically synthesizes freestanding definitions for `malloc`, `free`, `operator new`, `operator new[]`, `operator delete`, and `operator delete[]`, routing them directly to the native Windows Heap API (`RtlAllocateHeap` / `HeapAlloc`) obtained dynamically from `PEB->ProcessHeap`.

### 3.2 `StlStubPass`
Stubs out MSVC C++ runtime exception infrastructure. When standard library routines emit exception dispatchers (`_CxxThrowException`, `_purecall`, `?_Xlength_error@std@@...`, `?_Xout_of_range@std@@...`), this pass replaces them with deterministic error-trapping stubs (`ExitProcess(0xC0000005)`). Furthermore, it localizes `_Init_thread_epoch` and synthesizes thread-safe initialization barriers (`_Init_thread_header`/`_Init_thread_footer`), erasing all dependencies on OS TLS infrastructure.

### 3.3 `GlobalLifecyclePass`
Eliminates `.CRT$XCU` and `.CRT$XTX` sections. It inspects `@llvm.global_ctors` and inserts direct, sequential calls to all static constructors at the entry point of `@main`. Simultaneously, it analyzes `@llvm.global_dtors` and atexit-registered handlers, weaving reverse-LIFO destruction sequences immediately prior to module exit points.

### 3.4 `GlobalSectionPass`
Scans all global variables, static variables, and read-only tables. It forces code, data, bss, and rodata variables into contiguous unified section attributes, eliminating fragmentation and enabling single-segment memory deployment.

### 3.5 `GlobalPointerPass`
Analyzes every pointer table, virtual function table (`vftable`), and static pointer initialized with compile-time addresses. It generates an inline initialization routine that dynamically queries the current execution base via relative instruction pointer arithmetic (`call $+5; pop reg`) and patches every absolute pointer slot with the runtime relocation delta (`current_base - compile_base`).

### 3.6 `AutoLoadLibraryPass`
Inspects external symbol references. If a requested API belongs to a secondary subsystem (e.g., `d3d11.dll`, `dxgi.dll`, `d2d1.dll`, `dwrite.dll`, `winmm.dll`) that may not be loaded into the host process address space by default, this pass automatically injects safe dynamic loading logic (`LoadLibraryA`) before dispatching the invocation.

### 3.7 `StackStringPass`
Identifies all global string constants and string literals allocated in `.rdata`. It unfolds each string literal into a localized stack-allocated array in the referencing function's entry block, using immediate 64-bit store instructions (`mov qword ptr [rsp+...], imm64`) to construct the string entirely in registers and stack memory, leaving zero traces in data sections.

### 3.8 `ApiResolvePass`
The core dynamic resolution engine. Traverses all external function call sites (`CallInst`), calculates compile-time ROR13 hashes for both target DLL module names and API function names, and replaces direct call targets with an inlined, self-contained resolver stub (`__memllvm_resolve_api`).

---

## 4. LTL: Freestanding C/C++ Runtime Library

To support complex C++ applications without pulling in the massive, non-PIC Microsoft Visual C++ CRT (`msvcrt.dll` / `ucrt.lib`), MemLLVM includes **LTL (Lightweight Template Library)**:

* **Memory Management**: High-speed bucketed arena allocators and freestanding wrappers for `malloc`, `calloc`, `realloc`, and aligned allocations.
* **Standard Streams**: Full implementation of `std::cout`, `std::cin`, `std::cerr`, `std::fstream`, `std::stringstream`, and formatting operators, completely decoupled from OS file descriptors.
* **Concurrency & Synchronization**: Full `std::thread`, `std::mutex`, `std::condition_variable`, `std::shared_mutex`, `std::async`, and `std::future`, built directly atop Windows Slim Reader/Writer (SRW) Locks and Condition Variables.
* **Regular Expressions**: Freestanding `std::regex` engine supporting ECMAScript, basic, and extended syntax matching.
* **Filesystem**: Standard C++17 `std::filesystem::path`, directory iterators, and file status inspectors mapped to native Win32 `FindFirstFileW` / `GetFileAttributesW`.
* **String & Locale Utilities**: Self-contained character classification, UTF-8/UTF-16 wide string conversions, and ASCII collations without external code page tables.

---

## 5. Position-Independent Code & Dynamic Self-Rebase

MemLLVM binaries support true **arbitrary base execution** via a hybrid compilation and extraction scheme:

1. **Compilation**: Code is compiled with `-fno-jump-tables`, `/GS-`, `/GR-`, and `-mno-stack-arg-probe`.
2. **Extraction & Relocation Harvesting**: The extractor parses the linked PE executable, gathers all base relocations (`IMAGE_REL_BASED_DIR64`), and appends a compact relocation table to the flat binary.
3. **ABI-Compliant Self-Rebase Bootstrap Stub**: A lightweight 64-bit assembly preamble is prepended to the binary entry point. It uses only volatile scratch registers (`r8-r11`, `rdx`, `rax`), strictly conforming to the Windows x64 ABI without corrupting callee-saved registers:

```nasm
; --- MemLLVM ABI-Compliant Self-Rebase Bootstrap ---
call    $+5
pop     rdx                       ; RDX = Current runtime RIP
sub     rdx, STUB_OFFSET          ; RDX = Current base address
mov     rax, ORIGINAL_IMAGE_BASE  ; RAX = Compile-time image base
mov     r8, rdx
sub     r8, rax                   ; R8 = Relocation Delta
jz      .skip_rebase              ; If loaded at compile base, skip

lea     r9, [rdx + RELOC_TABLE]   ; R9 = Relocation RVA table
mov     r10d, NUM_RELOCS          ; R10D = Number of relocations
.rebase_loop:
mov     eax, [r9]                 ; EAX = Slot RVA
add     qword ptr [rdx + rax], r8 ; Patch slot with runtime Delta
add     r9, 4
dec     r10d
jnz     .rebase_loop

.skip_rebase:
lea     rax, [rdx + MAIN_OFFSET]
jmp     rax                       ; Jump directly to synthesized @main
```

---

## 6. Dynamic PEB Traversal & ROR13 API Resolution

MemLLVM completely eliminates the PE Import Address Table (IAT). When an external function is invoked, MemLLVM dynamically resolves its address at runtime:

```
[GS Register: gs:[0x60]] ---> [PEB: Process Environment Block]
                                     |
                                     v
                        [PEB_LDR_DATA: Ldr]
                                     |
                                     v
              [InLoadOrderModuleList / InMemoryOrderModuleList]
                                     |
                 +--------------------+--------------------+
                 |                    |                    |
                 v                    v                    v
           [ntdll.dll]       [kernel32.dll]        [user32.dll]
                 |
                 v
       [Parse Export Directory (IMAGE_EXPORT_DIRECTORY)]
                 |
         +-------+-------+
         |               |
         v               v
   [AddressOfNames]  [AddressOfFunctions]
         |               |
         v               v
   [Compute ROR13]   [Retrieve Function Pointer]
         |
         +---> [Match Hash: 0x75da1966 -> Dispatch Call]
```

### Hash Function (ROR13)
```cpp
constexpr uint32_t HashApiName(const char* name) {
    uint32_t hash = 0;
    while (*name) {
        hash = (hash >> 13) | (hash << (32 - 13));
        hash += static_cast<uint8_t>(*name++);
    }
    return hash;
}
```

---

## 7. Thread-Local Storage (TLS) Handling & Strict Boundaries

Standard Windows C++ compilers rely on OS-managed TLS infrastructure (`TEB->ThreadLocalStoragePointer` at `gs:[0x58]`), which faults immediately when invoked outside a PE-loader-registered module because no module index (`__tls_index`) is assigned.

* **What MemLLVM Solves**:
  MemLLVM's `StlStubPass` detects compiler-generated TLS variables used for thread-safe local static variables (Meyers Singleton pattern) and rewrites epoch checks into lock-free atomic compare-and-swap operations on non-TLS global storage. This completely removes dependencies on `_Init_thread_epoch` and `__tls_index`.
* **Strict Architectural Boundary (Zero Assumptions on Host)**:
  Raw flat machine code running in pure memory without an operating system PE loader cannot natively allocate or initialize TEB TLS slots. **MemLLVM does not make ambiguous claims and imposes NO requirements or modifications on the host runner.** Therefore, raw flat binaries **cannot natively support the C++ `thread_local` keyword**. Applications requiring thread-local storage in flat binaries should use explicit Win32 TLS APIs (`TlsAlloc`, `TlsGetValue`, `TlsSetValue`).

---

## 8. Current Framework Limitations & Roadmap

MemLLVM aims to provide maximum modern C/C++ capability in raw flat machine code. However, due to operating outside the Windows PE loader, several fundamental OS abstractions cannot currently be processed natively:

1. **C++ Exception Handling (`try` / `catch` / `throw`)**:
   - Windows x64 structured exception handling and C++ exception unwinding strictly depend on `.pdata` and `.xdata` function tables registered with the NT kernel (`RtlAddFunctionTable`). In a flat memory binary, executing a `throw` will crash because the kernel unwinder cannot locate the personality routine or call frame handlers.
   - *Current Requirement*: Code must be compiled with exceptions disabled (`/EHs-c-`, `/GR-`, `/clang:-fno-exceptions`).
   - *Future Roadmap*: We are actively developing a self-registering runtime unwinder that dynamically calls `RtlAddFunctionTable` at binary startup to restore full C++ exception support.
2. **Windows PE Resources (`.rsrc` / RC Files)**:
   - Flat binaries do not have PE section headers or standard resource directory tables. Windows APIs like `FindResource`, `LoadIcon`, and `LoadAccelerators` cannot resolve resources from a flat memory image without a valid `HMODULE`.
   - *Current Requirement*: Assets, textures, shaders, and data files must be embedded as inline byte arrays (e.g. C headers, bytecode arrays, or companion data files like `doom1.wad`).
   - *Future Roadmap*: Exploring in-memory virtual resource table emulation and pseudo-handle hooks.
3. **Native C++ `thread_local` Keyword**:
   - As noted in Section 7, flat binaries lack PE loader TLS allocation; use `TlsAlloc()` series APIs instead.

---

## 9. Showcase Applications & Verification

MemLLVM has been tested and verified across major industrial-grade C and C++ projects, each running as a pure standalone `.bin` without any companion DLLs:

| Application | Technology Stack | Binary Size | Verified Capabilities |
| :--- | :--- | :--- | :--- |
| **Box2D 3.0** | Pure C (45 files) | **605 KB** | Continuous rigid body physics, dynamic BVH bounding box trees, collision constraints solver, real-time Win32 GDI vector rendering. |
| **CHIP-8 Virtual Machine** | C++20 | **42 KB** | Full virtual CPU opcode interpreter, 35 instructions, sound buzzer, frame timer, 64x32 monochrome display matrix running *Pong* and *Tetris*. |
| **Classic DOOM** | Pure C (83 files) | **703 KB** | Full id Software DOOM engine (doomgeneric), BSP rendering, fixed-point math, live keyboard input, software audio mixer, IWAD file streaming. |
| **Dear ImGui (v1.91+)** | C++17 | **1.70 MB** | Full immediate-mode GUI, docking, multi-viewports, menus, plots, styling editor, rendered via hardware-accelerated DirectX 11. |

---

## 10. Quick Start & Visual Studio Integration Guide

The recommended workflow is to use the pre-packaged SDK from GitHub Releases and configure the `MEMLLVM_DIR` environment variable.

### 1. Download & One-Click Environment Setup
1. Download `MemLLVM-SDK-windows-x64.zip` (and optional `MemLLVM-Tests.zip` for representative C++ test cases) from the latest [GitHub Release](https://github.com/Laster-dev/MemLLVM/releases).
2. Extract the archive to any directory on your system (e.g., `C:\MemLLVM`).
3. **Double-click `setup_env.bat` in the extracted root folder**:
   - Automatically detects the current directory and sets `MEMLLVM_DIR` in your User Environment.
   - Automatically mounts `bin` and `Tools` into your User `PATH`.
   - **Smart Path Migration**: If you ever move the SDK folder to a different directory or machine in the future, simply re-run `setup_env.bat` in the new location; it automatically purges the old stale paths and updates to the new location without creating duplicates.
   - *(To uninstall at any time: run `setup_env.bat --uninstall`)*.

### 2. How to Install Clang in Visual Studio (Easiest Way & Extension)

To compile with `clang-cl` in Visual Studio, choose either of the two simplest methods:

* **Method 1: Visual Studio Installer (Recommended Built-in Component, Zero Configuration)**
  1. Open **Visual Studio Installer** from your Windows Start Menu.
  2. Click **Modify** on your Visual Studio installation.
  3. Go to the **Individual components** tab.
  4. In the search box, type `Clang`.
  5. Check **C++ Clang Compiler for Windows** and **MSBuild support for LLVM toolset**.
  6. Click **Modify** to install. After installation, `LLVM (clang-cl)` will immediately appear in your project's **Platform Toolset** dropdown.

* **Method 2: Visual Studio Extension (For Custom / Latest LLVM Versions)**
  1. In Visual Studio, go to top menu: **Extensions** -> **Manage Extensions**.
  2. Search for: **`LLVM Compiler Toolchain`** (Official extension by The LLVM Project).
  3. Click **Download**, close Visual Studio, and follow the VSIX prompt to install.
  4. This extension enables seamless switching and auto-detection of any external LLVM version in Visual Studio's Platform Toolset.

### 3. Native Visual Studio Project (.vcxproj) Integration
1. Open your Visual Studio project properties.
2. **General** -> **Platform Toolset**: Select **LLVM (clang-cl)**.
3. **C/C++** -> **General** -> **Additional Include Directories**:
   ```text
   $(MEMLLVM_DIR)\include;$(MEMLLVM_DIR)\ltl\include;%(AdditionalIncludeDirectories)
   ```
4. **C/C++** -> **Code Generation**:
   - **Enable C++ Exceptions**: Set to **No** (`/EHs-c- /GR-`).
   - **Security Check**: Set to **Disable Security Check (/GS-)**.
5. **C/C++** -> **Command Line** -> **Additional Options**:
   ```text
   /clang:-fpass-plugin="$(MEMLLVM_DIR)\bin\MemLLVM.dll" /clang:-fno-jump-tables /clang:-mno-stack-arg-probe /clang:-fno-builtin
   ```
6. **Linker** -> **General** -> **Additional Library Directories**:
   ```text
   $(MEMLLVM_DIR)\ltl;$(MEMLLVM_DIR)\lib;%(AdditionalLibraryDirectories)
   ```
7. **Linker** -> **Input**:
   - **Ignore All Default Libraries**: **Yes (/NODEFAULTLIB)**.
   - **Additional Dependencies**: Add `ltl.lib;` plus any required Win32 import libraries (`kernel32.lib;user32.lib;gdi32.lib;d3d11.lib`).
8. **Linker** -> **Advanced** -> **Entry Point**: Set to `main`.
9. **Build Events** -> **Post-Build Event**:
   ```bat
   python "$(MEMLLVM_DIR)\examples\imgui\extract_flat_bin.py" "$(TargetPath)" "$(TargetDir)$(TargetName).bin"
   ```

You can now use `$(MEMLLVM_DIR)\Tools\SCLoader.exe` to run, inspect, and debug the resulting `.bin` file in an isolated memory harness.

---

## 11. Acknowledgements (VC-LTL Tribute)

Special tribute and heartfelt thanks to **[VC-LTL](https://github.com/Chuyu-Team/VC-LTL5)** and the Chuyu-Team!  
VC-LTL has set an exceptional benchmark in lightweight Windows CRT engineering and dependency-free runtime architecture. MemLLVM's freestanding LTL runtime design is deeply inspired by and indebted to the pioneering open-source work of the VC-LTL project.

---

## 12. Star History

[![Star History Chart](https://api.star-history.com/svg?repos=Laster-dev/MemLLVM&type=Date)](https://star-history.com/#Laster-dev/MemLLVM&Date)

---

<a name="中文技术文档"></a>
# 中文技术文档

## 1. 项目概述与核心哲学

**MemLLVM** 是一个基于 **LLVM New Pass Manager（新版通行证管理器）** 架构的工业级编译器级插件与独立自举运行时框架。专为 Windows 平台 `clang-cl` 工具链量身定制，能够将现代高阶 C 与 C++20 代码直接编译转化为 **绝对位置无关、自包含、自重定位的纯内存扁平机器码镜像（.bin）**。

生成的二进制无需依赖 Windows 操作系统的 PE 加载器（如 `ntdll!LdrLoadDll`、节区映射、导入表绑定等），可直接被映射至任意内存页（如 `PAGE_EXECUTE_READWRITE`）并立即无缝执行。

与传统基于外壳打包器（Packer）、内存 PE 模拟器（Reflective DLL Injection）或二进制补丁（Hooking）的方案不同，MemLLVM 直接在 **LLVM 中间表示（IR）层** 进行 AST/IR 级深度重构，在现代 C++ 高级语言抽象（虚表多态、模板元编程、全局对象生命周期、并发多线程、DirectX/Direct2D 图形）与纯内存代码严苛边界之间架起坚固桥梁。

---

## 2. 传统 PE 结构在内存执行场景下的根本缺陷

在 Windows 操作系统中，标准编译器输出的 `.exe` 与 `.dll` 均遵循 PE（Portable Executable）规范，具有对操作系统环境的强依赖：

1. **导入地址表（IAT / `.idata`）**：调用外部 Win32 API 均被编译为通过导入表的间接寻址，必须由系统内核加载器完成地址填充。在纯内存扁平代码中不存在 IAT。
2. **硬编码基址与重定位依赖**：全局指针数组、虚函数表（`vtable`）以及函数指针均依赖固定基地址（如 `0x140000000`）。一旦装载于非预期基址，指针解引用直接触发非法访问崩溃（`0xC0000005`）。
3. **C++ 静态对象初始化链（`.CRT$XCU`）**：全局对象的构造函数指针数组存放在 `.CRT$XCU` 段中。脱离 MSVC CRT 启动引导程序，对象构造函数不会被执行，直接导致野指针与虚表空指针崩溃。
4. **只读数据段（`.rdata`）隔离**：常量字符串、浮点数常量和跳转表默认存放在只读段。脱离 PE 映射时，跨节区的相对地址偏移极易失效。
5. **线程局部存储（TLS）**：C++ 局部静态变量的线程安全初始化依赖于 `__tls_index` 和 `_Init_thread_epoch`，脱离操作系统注册模块时会直接引发 TEB 解引用崩溃。

---

## 3. MemLLVM 编译管线流水线架构

MemLLVM 通过 `-fpass-plugin=MemLLVM.dll` 注入 Clang 编译前端，通过 `registerOptimizerLastEPCallback` 挂载 8 大优化 Pass：

### 3.1 `AutoMemoryPass`（内存管理自举 Pass）
深度扫描 IR 级别的内存申请与释放指令，自动合成独立的 `malloc`、`free`、`operator new`、`operator new[]`、`operator delete` 和 `operator delete[]` 实现，底层直接桥接至从 `PEB->ProcessHeap` 动态查询的系统原生堆分配 API（`HeapAlloc` / `HeapFree`）。

### 3.2 `StlStubPass`（异常消除与 TLS 降级 Pass）
消除 MSVC C++ 异常与 TLS 依赖。将 STL 抛出的异常调度器替换为确定的错误捕获桩；同时将局部静态变量的线程安全检测由 TLS 变量降级为原子内存比较，彻底消除对 `_Init_thread_epoch` 和 `__tls_index` 的依赖。

### 3.3 `GlobalLifecyclePass`（全局对象生命周期 Pass）
彻底消除 `.CRT$XCU` 与 `.CRT$XTX` 段。在 `@main` 入口最前侧注入对全局静态构造函数的顺序调用链，并在所有退出分支前逆序注入对应的析构函数链。

### 3.4 `GlobalSectionPass`（段区扁平化合并 Pass）
遍历所有全局变量、静态变量和常量表，将代码段、数据段、BSS 段和只读数据段强制合并至统一的连续段中，彻底消除多节区分片导致的内存离散问题。

### 3.5 `GlobalPointerPass`（全局指针与虚表自适应重定位 Pass）
扫描模块中的全局函数指针表与虚函数表（`vtable`），在运行时动态查询当前真实加载基址，并将差值（`Delta`）自动回写修补至各个指针槽位中。

### 3.6 `AutoLoadLibraryPass`（动态依赖加载 Pass）
检测外调 API 所属的动态链接库。若属于非默认驻留 DLL（如 `d3d11.dll`、`dxgi.dll`、`d2d1.dll`、`winmm.dll`），自动注入动态 `LoadLibraryA` 探测与自愈逻辑。

### 3.7 `StackStringPass`（栈字符串混淆 Pass）
将原本存放在 `.rdata` 中的字符串常量直接降解为函数栈帧上的即时寄存器赋值指令（`mov qword ptr [rsp+...], imm64`），实现数据段字符串的物理擦除。

### 3.8 `ApiResolvePass`（PEB/ROR13 动态哈希解析 Pass）
MemLLVM 的核心 IAT 消除引擎。遍历所有外部函数调用指令，计算目标模块与 API 的 ROR13 哈希值，并将直接调用改写为内置的动态解析桩（`__memllvm_resolve_api`）。

---

## 4. LTL：自包含无依赖 C/C++ 现代化运行时

MemLLVM 附带轻量级无依赖独立运行时 **LTL（Lightweight Template Library）**：

* **高性能内存分配器**：多阶桶式 Arena 快速分配器与对齐分配接口。
* **标准 I/O 流**：脱离 OS 文件句柄的完整 `std::cout`、`std::cin`、`std::cerr`、`std::fstream` 与 `std::stringstream` 实现。
* **多线程与并发原语**：基于 Windows 原生 SRW 锁和条件变量实现的 `std::thread`、`std::mutex`、`std::condition_variable`、`std::shared_mutex`、`std::async` 与 `std::future`。
* **正则表达式引擎**：自包含的 `std::regex` 引擎。
* **文件系统**：映射至原生 Win32 文件枚举接口的 `std::filesystem::path` 与目录迭代器。
* **字符集与宽字符处理**：内置 UTF-8 与 UTF-16 宽字符串编解码，不依赖宿主代码页。

---

## 5. 绝对位置无关码（PIC）与自重定位（Self-Rebase）

MemLLVM 扁平二进制支持任意基址加载，前端采用符合 Windows x64 ABI 标准的自重定位引导桩，仅使用易失寄存器（`r8-r11`, `rdx`, `rax`），严禁破坏调用者的非易失寄存器：

```nasm
; --- MemLLVM 严格遵循 x64 ABI 的自重定位引导桩 ---
call    $+5
pop     rdx                       ; RDX = 当前执行时的 RIP
sub     rdx, STUB_OFFSET          ; RDX = 扁平镜像当前内存基地址
mov     rax, ORIGINAL_IMAGE_BASE  ; RAX = 编译时基地址
mov     r8, rdx
sub     r8, rax                   ; R8 = 重定位偏差 Delta
jz      .skip_rebase              ; 若恰好加载于编译基址，跳过重定位

lea     r9, [rdx + RELOC_TABLE]   ; R9 = 重定位 RVA 表首地址
mov     r10d, NUM_RELOCS          ; R10D = 重定位项数量
.rebase_loop:
mov     eax, [r9]                 ; EAX = 需要重定位的槽位相对偏移
add     qword ptr [rdx + rax], r8 ; 将真实偏差累加回目标槽位
add     r9, 4
dec     r10d
jnz     .rebase_loop

.skip_rebase:
lea     rax, [rdx + MAIN_OFFSET]
jmp     rax                       ; 跳入合成后的程序主入口 @main
```

---

## 6. PEB 模块链表遍历与 ROR13 哈希动态 IAT 消除

通过从 `gs:[0x60]` 访问当前进程环境块（PEB），解析导出表并计算函数名 ROR13 哈希，实现运行时动态无感绑定：

```cpp
constexpr uint32_t HashApiName(const char* name) {
    uint32_t hash = 0;
    while (*name) {
        hash = (hash >> 13) | (hash << (32 - 13));
        hash += static_cast<uint8_t>(*name++);
    }
    return hash;
}
```

---

## 7. 线程局部存储（TLS）处理策略与技术边界

在原生 Windows C++ 开发中，编译器生成的 TLS 访问依赖于操作系统 PE 加载器分配的模块索引（`__tls_index`）以及线程环境块（`TEB->ThreadLocalStoragePointer`，位于 `gs:[0x58]`）。在无 PE 加载器介入的纯内存裸二进制中，此机制会因空指针解引用立即引发崩溃。

* **MemLLVM 解决的范围**：
  MemLLVM 通过 `StlStubPass` 深度分析 IR，识别编译器为局部静态变量线程安全初始化（Meyers 单例模式）插入的 TLS 检查，并将其改写为基于非 TLS 全局变量的原子比较交换（CAS），彻底消除了对 `_Init_thread_epoch` 和 `__tls_index` 的依赖。
* **明确的技术边界（不对宿主运行器作任何隐式假设）**：
  纯内存扁平二进制在脱离 PE 引导时，任何外部宿主（无论是测试 Harness 还是真实业务宿主）均未在操作系统内部为其分配专属 TLS 槽位。**行就是行，不行就是不行——MemLLVM 坚决不对宿主环境提出任何特殊侵入要求。** 因此，纯内存裸二进制**无法原生支持 C++ `thread_local` 关键字**。若业务确实需要线程私有存储，应使用标准 Win32 API（`TlsAlloc`、`TlsGetValue`、`TlsSetValue`）进行显式管理。

---

## 8. 框架当前局限性与后续攻坚方向

MemLLVM 致力于在纯内存扁平二进制中提供尽可能强大的现代化 C/C++ 开发能力。但由于完全脱离了 Windows 操作系统的原生 PE 加载管线，以下机制目前受限于体系结构而无法直接处理：

1. **C++ 异常处理 (`try` / `catch` / `throw`)**：
   - Windows x64 异常展开（Stack Unwinding）严格依赖 PE 文件在构建时生成的 `.pdata` 和 `.xdata` 异常展开表，并由系统内核登记管理。纯内存执行的裸二进制在没有向 NT 内核注册函数表时，抛出异常会导致内核无法定位展开处理器而直接崩溃。
   - **当前要求**：工程必须显式禁用 C++ 异常与 RTTI（`/EHs-c-`、`/GR-`、`/clang:-fno-exceptions`）。
   - **后续攻坚方案**：团队正在攻坚基于微型运行时自举注册（通过自省代码段范围并在入口自注册 `RtlAddFunctionTable`）的技术方案，计划在后续版本恢复对异常安全展开的完整支持。
2. **Windows PE 资源嵌入（.rsrc / RC 资源）**：
   - 纯内存扁平二进制没有标准的 PE 头部与数据目录表，因此通过 Visual Studio 资源编译器（`.rc`）打包的窗口图标（RT_ICON）、对话框模板、版本信息（VS_VERSION_INFO）等，无法通过传统 `FindResource` / `LoadIcon` 获取。
   - **当前要求**：所有静态资源（图像、着色器字节码、字体、音频数据包）需以内嵌字节数组（如内嵌十六进制数组、或伴生数据流如 `doom1.wad`）方式集成。
   - **后续攻坚方案**：正在研发纯内存虚拟资源包拦截与伪 `HMODULE` 重定向技术。
3. **原生 C++ `thread_local` 关键字**：
   - 如第 7 节所述，脱离系统 PE 引导无法自动开辟 TLS Slot，目前请统一采用 `TlsAlloc` 系列 API 进行显式管理。

---

## 9. 旗舰级重型应用测试与实战验证

为验证 MemLLVM 对极端复杂工业级 C/C++ 项目的编译承载能力，项目提供了 4 个高难度测试示例，全部成功编译并运行为 **单文件纯内存扁平二进制（.bin）**：

| 测试项目 | 语言与规模 | 产物大小 | 验证技术点 |
| :--- | :--- | :--- | :--- |
| **Box2D 3.0** | 纯 C 语言 (45 个源文件) | **605 KB** | 连续刚体碰撞模拟、四叉树/动态 BVH 树粗筛、冲量积分约束求解器、原生 Win32 GDI 矢量线框实时渲染。 |
| **CHIP-8 虚拟机** | C++20 现代特性 | **42 KB** | 虚拟 CPU 指令解码循环（Opcode Dispatch）、35 条机器指令仿真、定时器中断、单色点阵显存渲染，流畅运行《Pong》与《俄罗斯方块》。 |
| **经典 DOOM 引擎** | 纯 C 语言 (83 个源文件) | **703 KB** | id Software 经典 DOOM（doomgeneric），纯内存 3D 软件光栅化渲染、实时键盘交互控制、音频合成器、IWAD 数据包连续流式读取。 |
| **Dear ImGui (v1.91+)** | C++17 工业级 UI | **1.70 MB** | 现代化即时模式图形界面、DirectX 11 硬件加速渲染管线、控件停靠（Docking）、色彩选择器、实时性能图表。 |

---

## 10. 极简上手与 Visual Studio 原生项目（.vcxproj）对接指南

使用 MemLLVM 的最简方案是直接下载 GitHub Release 提供的预打包 SDK，并通过环境变量无痛融入工程，无需在项目中写死任何机器绝对路径。

### 第一步：下载并一键配置 SDK 环境变量
1. 前往 [MemLLVM GitHub Releases](https://github.com/Laster-dev/MemLLVM/releases) 下载最新的 `MemLLVM-SDK-windows-x64.zip`（若需测试各类 C++20 特性样例，可一并下载 `MemLLVM-Tests.zip` 测试包）。
2. 解压到本地任意目录（例如 `C:\MemLLVM`）。
3. **直接双击运行根目录下的 `setup_env.bat`**：
   - 自动获取当前目录，并一键配置用户环境变量 `MEMLLVM_DIR`。
   - 自动将当前目录下的 `bin` 与 `Tools` 挂载到用户 `PATH`（支持终端直接运行 `SCLoader.exe` 等工具）。
   - **智能迁移防冗余**：未来若将 SDK 整体移动或解压到新的文件夹，直接在新目录下再次双击 `setup_env.bat`，脚本会自动识别并清理旧目录残留，智能无缝迁移至最新路径，绝不重复堆叠垃圾路径。
   - *(如需彻底卸载，在命令行执行 `setup_env.bat --uninstall` 即可完全擦除)*。

### 第二步：Visual Studio 安装 Clang / LLVM（最简官方组件与扩展插件）

要在 Visual Studio 中启用 `clang-cl` 工具链，以下两种方式均极其简单：

* **方式一：通过 Visual Studio Installer 一键安装官方内置组件（最推荐，开箱即用，零额外配置）**
  1. 打开 Windows 开始菜单中的 **Visual Studio Installer**。
  2. 找到当前正在使用的 Visual Studio 版本，点击 **修改 (Modify)**。
  3. 切换到 **“单个组件” (Individual components)** 标签页。
  4. 在右上角搜索框中输入：`Clang`。
  5. 勾选以下组件：
     - **适用于 Windows 的 C++ Clang 编译器** (C++ Clang Compiler for Windows)
     - **适用于 Windows 的 C++ Clang-cl** 或 **LLVM 工具集 MSBuild 支持** (MSBuild support for LLVM toolset)
  6. 点击右下角 **修改** 开始自动安装。安装完成后，VS 工程属性的“平台工具集”下拉框中会直接出现 **`LLVM (clang-cl)`** 选项！

* **方式二：通过 Visual Studio 官方扩展插件（适合对接任意独立安装的最新版 LLVM）**
  1. 打开 Visual Studio，点击顶部菜单栏：**扩展 (Extensions)** -> **管理扩展 (Manage Extensions)**。
  2. 在右上角搜索框中输入：`LLVM` 或 `LLVM Compiler Toolchain`。
  3. 找到并点击下载官方插件 **`LLVM Compiler Toolchain`**（由 The LLVM Project 官方出品）。
  4. 关闭 Visual Studio，VSIX 安装器会自动弹出并完成安装。该插件能自动检索并识别系统中安装的任何外部 LLVM 发行版，并在平台工具集中直接供您选择。

### 第三步：原生 Visual Studio 项目（.vcxproj）对接配置
在 Visual Studio 中打开已有或新建的 C++ 项目，右键打开 **项目属性**：

1. **常规** -> **平台工具集**：选择 **LLVM (clang-cl)**。
2. **C/C++** -> **常规** -> **附加包含目录**：添加
   ```text
   $(MEMLLVM_DIR)\include;$(MEMLLVM_DIR)\ltl\include;%(AdditionalIncludeDirectories)
   ```
3. **C/C++** -> **代码生成**：
   - **启用 C++ 异常**：选择 **否** (`/EHs-c- /GR-`)。
   - **安全检查**：选择 **禁用安全检查 (/GS-)**。
4. **C/C++** -> **命令行** -> **其他选项**：直接添加
   ```text
   /clang:-fpass-plugin="$(MEMLLVM_DIR)\bin\MemLLVM.dll" /clang:-fno-jump-tables /clang:-mno-stack-arg-probe /clang:-fno-builtin
   ```
5. **链接器** -> **常规** -> **附加库目录**：添加
   ```text
   $(MEMLLVM_DIR)\ltl;$(MEMLLVM_DIR)\lib;%(AdditionalLibraryDirectories)
   ```
6. **链接器** -> **输入**：
   - **忽略所有默认库**：选择 **是 (/NODEFAULTLIB)**。
   - **附加依赖项**：添加 `ltl.lib;` 以及项目需要的 Win32 系统库（如 `kernel32.lib;user32.lib;gdi32.lib;d3d11.lib`）。
7. **链接器** -> **高级** -> **入口点**：填入 `main`。
8. **生成事件** -> **生成后事件** -> **命令行**：
   ```bat
   python "$(MEMLLVM_DIR)\examples\imgui\extract_flat_bin.py" "$(TargetPath)" "$(TargetDir)$(TargetName).bin"
   ```

编译完成后，会在输出目录自动生成绝对位置无关的 `.bin` 文件。您可以使用 SDK 自带的现代化图形执行器 `$(MEMLLVM_DIR)\Tools\SCLoader.exe` 打开此 `.bin` 进行载入、执行与调试。

---

## 11. 致敬与鸣谢（VC-LTL）

特别致敬并诚挚感谢 **[VC-LTL](https://github.com/Chuyu-Team/VC-LTL5)** 项目及其作者团队（Chuyu-Team）！  
VC-LTL 在 Windows 轻量化 CRT、独立无依赖运行库、精简运行时体积领域开创了极其卓越的技术典范。MemLLVM 的自由运行时（Freestanding LTL）体系理念深受 VC-LTL 的启发与赋能，特此致以崇高的敬意与感谢！

---

## 12. Star 历史趋势

[![Star History Chart](https://api.star-history.com/svg?repos=Laster-dev/MemLLVM&type=Date)](https://star-history.com/#Laster-dev/MemLLVM&Date)

---

## 📄 License / 开源许可协议
This project is licensed under the [MIT License](LICENSE).  
本项目遵循 MIT 开源许可协议。
