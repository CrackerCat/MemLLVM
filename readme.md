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
  - [7. Thread-Local Storage (TLS) Emulation](#7-thread-local-storage-tls-emulation)
  - [8. Showcase Applications & Verification](#8-showcase-applications--verification)
  - [9. Quick Start & Build Instructions](#9-quick-start--build-instructions)
  - [10. Visual Studio Integration Guide](#10-visual-studio-integration-guide)
- [中文技术文档](#中文技术文档)
  - [1. 项目概述与核心哲学](#1-项目概述与核心哲学)
  - [2. 传统 PE 结构在内存执行场景下的根本缺陷](#2-传统-pe-结构在内存执行场景下的根本缺陷)
  - [3. MemLLVM 编译管线流水线架构](#3-memllvm-编译管线流水线架构)
  - [4. LTL：自包含无依赖 C/C++ 现代化运行时](#4-ltl自包含无依赖-cc-现代化运行时)
  - [5. 绝对位置无关码（PIC）与自重定位（Self-Rebase）](#5-绝对位置无关码pic与自重定位self-rebase)
  - [6. PEB 模块链表遍历与 ROR13 哈希动态 IAT 消除](#6-peb-模块链表遍历与-ror13-哈希动态-iat-消除)
  - [7. 线程局部存储（TLS）自举与异常擦除](#7-线程局部存储tls自举与异常擦除)
  - [8. 旗舰级重型应用测试与实战验证](#8-旗舰级重型应用测试与实战验证)
  - [9. 快速上手与编译指南](#9-快速上手与编译指南)
  - [10. Visual Studio 深度集成与项目对接指南](#10-visual-studio-深度集成与项目对接指南)

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
4. **Read-Only Data (`.rdata`) Separation**: Constant strings, floating-point literals, and jump tables are relegated to `.rdata`. Standard shellcode loaders allocating a single flat block often break when instructions attempt relative addressing across non-contiguous sections.
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
3. **Self-Rebase Bootstrap Stub**: A lightweight 64-bit assembly preamble is prepended to the binary entry point:

```nasm
; --- MemLLVM Self-Rebase Bootstrap ---
call    $+5
pop     rdx                       ; RDX = Current runtime RIP
sub     rdx, STUB_OFFSET          ; RDX = Current base address
mov     rax, ORIGINAL_IMAGE_BASE  ; RAX = Compile-time image base
mov     rbx, rdx
sub     rbx, rax                  ; RBX = Relocation Delta
jz      .skip_rebase              ; If loaded at compile base, skip

lea     rsi, [rdx + RELOC_TABLE]  ; RSI = Relocation RVA table
mov     ecx, NUM_RELOCS           ; ECX = Number of relocations
.rebase_loop:
mov     eax, [rsi]                ; EAX = Slot RVA
add     qword ptr [rdx + rax], rbx; Patch slot with runtime Delta
add     rsi, 4
dec     ecx
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

## 7. Thread-Local Storage (TLS) Emulation

Standard TLS mechanisms allocate slots in `TEB->ThreadLocalStoragePointer`. In a flat binary, this crashes because no OS module index is assigned. MemLLVM resolves this through two complementary strategies:

1. **IR-Level TLS Erasure**: `StlStubPass` detects local thread-safe statics and redirects epoch checks from thread-local variables to atomic global variables, eliminating `__tls_index`.
2. **Runtime TEB Injection**: For complex multi-threaded workloads, `tests/run_bin.c` initializes a valid TLS descriptor array in the host thread environment (`gs:[0x58]`), providing a compliant thread-local block for advanced runtimes.

---

## 8. Showcase Applications & Verification

MemLLVM has been tested and verified across five major industrial-grade C and C++ projects, each running as a pure standalone `.bin` without any companion DLLs:

| Application | Technology Stack | Binary Size | Verified Capabilities |
| :--- | :--- | :--- | :--- |
| **Box2D 3.0** | Pure C (45 files) | **605 KB** | Continuous rigid body physics, dynamic BVH bounding box trees, collision constraints solver, real-time Win32 GDI vector rendering. |
| **CHIP-8 Virtual Machine** | C++20 | **42 KB** | Full virtual CPU opcode interpreter, 35 instructions, sound buzzer, frame timer, 64x32 monochrome display matrix running *Pong* and *Tetris*. |
| **Classic DOOM** | Pure C (83 files) | **703 KB** | Full id Software DOOM engine (doomgeneric), BSP rendering, fixed-point math, live keyboard input, software audio mixer, IWAD file streaming. |
| **Dear ImGui (v1.91+)** | C++17 | **1.70 MB** | Full immediate-mode GUI, docking, multi-viewports, menus, plots, styling editor, rendered via hardware-accelerated DirectX 11. |
| **Scintilla Code Editor** | C++20 | **980 KB** | Full Scintilla text editing engine, Direct2D/DirectWrite hardware text rasterization, multi-level undo/redo, dynamic margins, caret engine. |

---

## 9. Quick Start & Build Instructions

### Prerequisites
* Windows 10/11 x64
* Visual Studio 2022 (MSVC v143+)
* LLVM / Clang 18+ (with `clang-cl.exe`, `lld-link.exe`, and CMake configs)
* Python 3.10+ (with `pefile`: `pip install pefile`)

### 1. Build MemLLVM Plugin
```powershell
git clone https://github.com/Laster-dev/MemLLVM.git
cd MemLLVM
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DLLVM_DIR="<path_to_llvm>/lib/cmake/llvm"
cmake --build build --config Release --target MemLLVM
```

### 2. Build LTL Runtime Library
```powershell
cmd /c ltl\build_ltl.bat
```

### 3. Build Test Loader Harness
```powershell
clang-cl /O2 /Fe:tests\run_bin.exe tests\run_bin.c
```

### 4. Build All Showcase Examples
```powershell
python examples\build_all.py
```

### 5. Execute Any Flat Binary in Memory
```powershell
.\tests\run_bin.exe .\examples\box2d\box2d.bin
.\tests\run_bin.exe .\examples\chip8\chip8.bin
.\tests\run_bin.exe .\examples\doom\doom.bin
.\tests\run_bin.exe .\examples\imgui\imgui_demo.bin
.\tests\run_bin.exe .\examples\scintilla_editor\scintilla_editor.bin
```

---

## 10. Visual Studio Integration Guide

MemLLVM integrates seamlessly into Microsoft Visual Studio 2022 / 2019, allowing you to develop, compile, and debug flat `.bin` binaries directly from the IDE using either standard MSBuild `.vcxproj` projects or CMake Open Folder.

### Method A: Native Visual Studio Project (.vcxproj)

1. **Environment Setup & Platform Toolset**:
   - Ensure **C++ Clang Compiler for Windows** and **MSBuild support for LLVM toolset** are installed via Visual Studio Installer.
   - Configure the `LLVM_PATH` environment variable pointing to your LLVM root directory (e.g., `C:\LLVM-22-dev`):
     ```powershell
     [System.Environment]::SetEnvironmentVariable("LLVM_PATH", "C:\LLVM-22-dev", "User")
     ```
   - In Solution Explorer, right-click your Project -> **Properties** -> **Configuration Properties** -> **General** -> **Platform Toolset**: Select **LLVM (clang-cl)**.

2. **C/C++ Compiler Configuration**:
   - **Configuration Properties** -> **C/C++** -> **General**:
     - **Additional Include Directories**: Add `$(SolutionDir)ltl\include` (or the path to MemLLVM `ltl\include`).
   - **Configuration Properties** -> **C/C++** -> **Code Generation**:
     - **Enable C++ Exceptions**: Set to **No** (`/EHs-c- /GR-`).
     - **Security Check**: Set to **Disable Security Check (/GS-)**.
   - **Configuration Properties** -> **C/C++** -> **Command Line**:
     - Add the following flags in **Additional Options**:
       ```text
       /clang:-fpass-plugin="<PathToMemLLVM>\build\MemLLVM\Release\MemLLVM.dll" /clang:-fno-jump-tables /clang:-mno-stack-arg-probe /clang:-fno-builtin
       ```

3. **Linker Configuration**:
   - **Configuration Properties** -> **Linker** -> **Input**:
     - **Ignore All Default Libraries**: Set to **Yes (/NODEFAULTLIB)**.
     - **Additional Dependencies**: Add `<PathToMemLLVM>\ltl\ltl.lib` along with any necessary Windows API import libraries (e.g., `user32.lib;gdi32.lib;kernel32.lib;d3d11.lib`).
   - **Configuration Properties** -> **Linker** -> **Advanced**:
     - **Entry Point**: Set to `main` (or your entry point function name).

4. **Post-Build Extraction to Flat Binary**:
   - **Configuration Properties** -> **Build Events** -> **Post-Build Event**:
     - **Command Line**:
       ```bat
       python "<PathToMemLLVM>\examples\imgui\extract_flat_bin.py" "$(TargetPath)" "$(TargetDir)$(TargetName).bin"
       ```
     - **Description**: `Extracting PE into self-relocating flat binary (.bin)...`

5. **One-Click F5 Debugging & Execution**:
   - **Configuration Properties** -> **Debugging**:
     - **Command**: `<PathToMemLLVM>\tests\run_bin.exe`
     - **Command Arguments**: `"$(TargetDir)$(TargetName).bin"`
     - **Working Directory**: `$(TargetDir)`
   - Now pressing **F5** or **Ctrl+F5** will automatically compile with the MemLLVM pass plugin, extract the `.bin` memory image, and launch it inside the memory test harness!

---

### Method B: CMake Project (Open Folder / Visual Studio CMake)

If you manage your project using CMake within Visual Studio:
1. Specify `clang-cl` as the compiler in `CMakeLists.txt` or `CMakePresets.json`:
   ```cmake
   set(CMAKE_C_COMPILER "$ENV{LLVM_PATH}/bin/clang-cl.exe")
   set(CMAKE_CXX_COMPILER "$ENV{LLVM_PATH}/bin/clang-cl.exe")
   ```
2. Append the MemLLVM pass plugin flags to compile options:
   ```cmake
   add_compile_options(
       $<$<COMPILE_LANGUAGE:C,CXX>:
           /clang:-fpass-plugin=${MEMLLVM_DIR}/build/MemLLVM/Release/MemLLVM.dll
           /GS-
           /GR-
           /clang:-fno-jump-tables
           /clang:-mno-stack-arg-probe
           /clang:-fno-builtin
       >
   )
   ```
3. Strip default CRT dependencies and register a custom post-build step to produce the `.bin`:
   ```cmake
   target_link_options(my_app PRIVATE /NODEFAULTLIB /ENTRY:main)
   target_link_libraries(my_app PRIVATE ${MEMLLVM_DIR}/ltl/ltl.lib)

   add_custom_command(TARGET my_app POST_BUILD
       COMMAND python ${MEMLLVM_DIR}/examples/imgui/extract_flat_bin.py
               $<TARGET_FILE:my_app>
               $<TARGET_FILE_DIR:my_app>/$<TARGET_FILE_BASE_NAME:my_app>.bin
       COMMENT "Generating position-independent flat binary..."
   )
   ```

---

<a name="中文技术文档"></a>
# 中文技术文档

## 1. 项目概述与核心哲学

**MemLLVM** 是一套基于 **LLVM New Pass Manager** 架构的深度编译器变换框架与轻量级自举运行时。专为 Windows 平台下的 `clang-cl` 工具链设计，MemLLVM 能够将现代化高层 C/C++20 源代码直接编译为**自包含、绝对位置无关、自重定位的纯内存扁平二进制镜像（Flat Binary / `.bin`）**。

生成的机器码完全脱离操作系统的标准可执行文件加载器（PE Loader），无需任何 `.exe`、`.dll` 格式包装或系统加载器引导，可直接被投递到任意可读写执行（RWX）的内存区域，通过原生函数指针调用 `((void(*)())buf)()` 瞬间自举拉起运行。

与业界现有的 Payload 转换工具、Shellcode 打包器或反射加载器相比，MemLLVM 具有根本性的架构创新：
- **编译期 IR 级原生干预**：在 LLVM 中间表示（IR）层重构代码生成逻辑，无任何运行时外挂壳或 Hook 注入。
- **现代化 C++ 标准全支持**：全面支持 C++ 模板、虚函数表（vtable）、全局构造与析构、异常擦除、多线程同步、标准流及 DirectX/Direct2D 原生图形管道。
- **零外部导入表依赖**：通过编译期符号哈希与运行时 PEB/EAT 动态遍历，彻底消除 PE 导入地址表（IAT）。

---

## 2. 传统 PE 结构在内存执行场景下的根本缺陷

传统的 C/C++ 编译器针对的是常规操作系统进程模型，生成的二进制严重依赖 PE 文件结构：

1. **导入表绑定（IAT / `.idata`）**：调用外部 Win32 API（如 `VirtualAlloc`、`CreateWindowExW`）时，编译器生成的是基于 `.rdata` 导入表的间接寻址指令。在内存裸跑场景下，该导入表根本不存在，导致空指针崩溃。
2. **写死基址与绝对重定位**：虚函数表（vtable）、全局指针变量、回调函数数组包含写死的 64 位绝对虚拟地址（默认基于 `0x140000000`）。一旦加载基址改变且未执行重定位，立刻触发 `0xC0000005` 内存访问越界。
3. **C++ 全局初始化缺失（`.CRT$XCU`）**：C++ 全局对象与静态实例依赖 MSVC CRT 初始化链（`__scrt_common_main`）遍历 `.CRT$XCU` 段。在裸内存代码中，静态对象未经构造即被使用，引发纯虚函数调用崩溃。
4. **多段分离与内存碎片**：代码段（`.text`）、只读数据段（`.rdata`）、全局数据段（`.data`）分散存储，无法直接以连续扁平字节流形式加载。
5. **线程局部存储（TLS）依赖**：现代化 C++ 的局部静态变量（Meyers 单例）依赖 `__tls_index` 和 `TEB->ThreadLocalStoragePointer`（`gs:[0x58]`），在无 OS 模块登记的状态下执行必将引发蓝屏或崩溃。

---

## 3. MemLLVM 编译管线流水线架构

MemLLVM 以插件形式通过 `-fpass-plugin=MemLLVM.dll` 挂载至 Clang，在 `registerOptimizerLastEPCallback` 优化终点注入了 8 大核心变换 Pass：

```
+----------------------------------------------------------------------------------------+
|                            MemLLVM 八大核心 Pass 编译流水线                              |
+----------------------------------------------------------------------------------------+
| 1. AutoMemoryPass       -> 合成原生堆分配原语 (malloc / free / operator new)            |
| 2. StlStubPass          -> 擦除 MSVC 异常处理链与 TLS 依赖，生成安全陷阱桩              |
| 3. GlobalLifecyclePass  -> 提取 .CRT$XCU 全局构造与析构，在 @main 入口/出口自动排障调用 |
| 4. GlobalSectionPass    -> 合并代码段与数据段，消除段分散与内存碎片                      |
| 5. GlobalPointerPass    -> 扫描静态虚表与指针表，注入 RIP 动态自重定位逻辑              |
| 6. AutoLoadLibraryPass  -> 静态分析未载入的子系统 DLL，注入自动动态按需加载代码          |
| 7. StackStringPass      -> 将只读字符串常量展开至函数栈帧，抹除 .rdata 字符串痕迹       |
| 8. ApiResolvePass       -> 将外部 API 调用全面重写为 PEB 遍历 + ROR13 哈希动态解析       |
+----------------------------------------------------------------------------------------+
```

### 核心 Pass 技术详解：
* **`AutoMemoryPass`**：拦截模块中所有的内存分配请求，在 LLVM IR 级就地合成无依赖的 `malloc`、`free`、`new`、`delete` 实现，底层直接绑定当前进程的 `PEB->ProcessHeap`。
* **`StlStubPass`**：自动识别并安全阻断 MSVC 标准库抛出的异常体系（`_CxxThrowException`、`?_Xlength_error@std@@...` 等），将其重定向至确定性终止陷阱；消除静态变量初始化中的 `_Init_thread_epoch` TLS 依赖。
* **`GlobalLifecyclePass`**：彻底废弃 `.CRT$XCU` 段，将所有静态构造函数按优先级有序编排，在 entry 函数起始处显式依次调用；同时在所有 `ret` 指令前织入逆序 LIFO 析构调用链。
* **`GlobalPointerPass`**：静态深度分析所有全局指针数组、虚函数表，在代码头部生成相对寻址重定位桩，运行时动态获取当前实际基址并对每一个指针槽位补正 Delta 偏差。
* **`ApiResolvePass`**：将原本针对外部 DLL 导出的直接 `call` 指令替换为基于 ROR13 算法的内存解析桩，彻底粉碎任何对 IAT 导入表的依赖。

---

## 4. LTL：自包含无依赖 C/C++ 现代化运行时

为了让高层 C++ 源码无需链接臃肿且包含位置相关代码的微软官方 CRT（`msvcrt.dll` / `ucrt.lib`），MemLLVM 配套自研了 **LTL (Lightweight Template Library)** 轻量级运行时：

- **自包含内存管理**：内置高性能 Arena 区域内存分配器与多段合并内存池。
- **现代化流式 I/O**：完整重写 `std::cout`、`std::cin`、`std::fstream` 与 `std::stringstream`，脱离标准句柄与文件描述符束缚。
- **高并发多线程系统**：完整提供 `std::thread`、`std::mutex`、`std::condition_variable`、`std::shared_mutex`、`std::async`、`std::future`，底层直接映射至 Windows 原生 SRW 读写锁与条件变量。
- **正规表达式引擎**：纯 C++ 实现的轻量级 `std::regex` 引擎，完整支持 ECMAScript 标准正则匹配。
- **标准文件系统**：支持 C++17 `std::filesystem::path` 跨路径解析与 Win32 文件枚举。

---

## 5. 绝对位置无关码（PIC）与自重定位（Self-Rebase）

MemLLVM 生成的二进制具备完美的**自愈合与动态自重定位能力**：

1. **编译期约束**：使用 `/GS-`（关闭安全 Cookie 栈保护）、`/GR-`（关闭运行时类型信息 RTTI）、`-fno-jump-tables`（消除只读跳转表）保证代码段自身绝对纯净。
2. **重定位表嵌入**：由 flat 提取脚本自动从 PE 产物中检索所有 `IMAGE_REL_BASED_DIR64` 重定位项，压缩后附带于平坦二进制尾部。
3. **自重定位引导桩（Bootstrap Stub）**：扁平二进制入口首先执行一段精简汇编：
   - 通过 `call $+5; pop rdx` 配合相对减法，动态求得当前二进制在内存中的实际起始物理地址。
   - 比较实际物理地址与编译预设基址，计算出偏差值 $\Delta = \text{ActualBase} - \text{OrigBase}$。
   - 若 $\Delta \neq 0$，则循环遍历内置重定位表，对虚表与全局指针槽位逐一加上 $\Delta$ 完成现场自修正。
   - 恢复入口机器码并无缝长跳（`jmp rax`）至主业务逻辑入口 `@main`。

---

## 6. PEB 模块链表遍历与 ROR13 哈希动态 IAT 消除

MemLLVM 内部集成超高效率的运行时 API 动态查找引擎：

1. **定位 PEB**：通过 64 位段寄存器 `gs:[0x60]` 直达当前进程的 `PEB`。
2. **遍历模块**：访问 `PEB->Ldr->InLoadOrderModuleList` 双向链表，对比已加载 DLL 的模块名称哈希。
3. **导出表匹配**：定位目标 DLL 的 `IMAGE_EXPORT_DIRECTORY`，遍历 `AddressOfNames` 名字表，通过 ROR13 哈希算法极速比对。
4. **获取地址并调用**：经 `AddressOfNameOrdinals` 与 `AddressOfFunctions` 索引求出目标函数虚拟地址，发起间接调用。

```
[GS:0x60] -> PEB -> Ldr -> InMemoryOrderModuleList
                               |
                   +-----------+-----------+
                   |                       |
             ntdll.dll               kernel32.dll
                   |                       |
             导出表解析 (EAT)         导出表解析 (EAT)
                   |                       |
             ROR13 哈希计算           ROR13 哈希计算
                   |                       |
                   +-----------> 命中目标函数指针 -> 直接调用
```

---

## 7. 线程局部存储（TLS）自举与异常擦除

在裸二进制中调用 TLS 会因为系统未分配 `__tls_index` 而导致空指针解引用。MemLLVM 采用双轨方案攻克此难题：

1. **IR 层消除 TLS 依赖**：`StlStubPass` 将局部静态变量的线程安全检测由 TLS 变量降级为原子内存比较，彻底移除对 `_Init_thread_epoch` 和 `__tls_index` 的依赖。
2. **测试载体环境仿真**：在测试执行器 `run_bin.c` 中，自动在主线程的 `gs:[0x58]` 区域开辟兼容的 TLS 指针数组，为多线程工作流提供安全的运行宿主环境。

---

## 8. 旗舰级重型应用测试与实战验证

为验证 MemLLVM 对极端复杂工业级 C/C++ 项目的编译承载能力，项目提供了 5 个高难度测试示例，全部成功编译并运行为 **单文件纯内存扁平二进制（.bin）**：

| 测试项目 | 语言与规模 | 产物大小 | 验证技术点 |
| :--- | :--- | :--- | :--- |
| **Box2D 3.0** | 纯 C 语言 (45 个源文件) | **605 KB** | 连续刚体碰撞模拟、四叉树/动态 BVH 树粗筛、冲量积分约束求解器、原生 Win32 GDI 矢量线框实时渲染。 |
| **CHIP-8 虚拟机** | C++20 现代特性 | **42 KB** | 虚拟 CPU 指令解码循环（Opcode Dispatch）、35 条机器指令仿真、定时器中断、单色点阵显存渲染，流畅运行《Pong》与《俄罗斯方块》。 |
| **经典 DOOM 引擎** | 纯 C 语言 (83 个源文件) | **703 KB** | id Software 经典 DOOM（doomgeneric），纯内存 3D 软件光栅化渲染、实时键盘交互控制、音频合成器、IWAD 数据包连续流式读取。 |
| **Dear ImGui (v1.91+)** | C++17 工业级 UI | **1.70 MB** | 现代化即时模式图形界面、DirectX 11 硬件加速渲染管线、控件停靠（Docking）、色彩选择器、实时性能图表。 |
| **Scintilla 代码编辑器** | C++20 重型项目 | **980 KB** | Scintilla 原生文本渲染引擎、Direct2D / DirectWrite 矢量字体排版、多级撤销重做栈、行号标记栏、光标呼吸引擎。 |

---

## 9. 快速上手与编译指南

### 编译环境要求
* 操作系统：Windows 10 / 11 x64
* 编译器：Visual Studio 2022 (MSVC v143+)
* LLVM 工具链：LLVM / Clang 18+ (包含 `clang-cl.exe`、`lld-link.exe`)
* 脚本环境：Python 3.10+（需安装 `pefile`：`pip install pefile`）

### 1. 编译 MemLLVM 编译器插件
```powershell
git clone https://github.com/Laster-dev/MemLLVM.git
cd MemLLVM
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DLLVM_DIR="<path_to_llvm>/lib/cmake/llvm"
cmake --build build --config Release --target MemLLVM
```

### 2. 编译 LTL 核心运行时库
```powershell
cmd /c ltl\build_ltl.bat
```

### 3. 编译纯内存二进制加载测试器
```powershell
clang-cl /O2 /Fe:tests\run_bin.exe tests\run_bin.c
```

### 4. 一键批量编译所有展示示例
```powershell
python examples\build_all.py
```

### 5. 在内存中直接运行生成的扁平二进制
```powershell
# 运行 Box2D 刚体物理动力学引擎
.\tests\run_bin.exe .\examples\box2d\box2d.bin

# 运行 CHIP-8 虚拟机与游戏
.\tests\run_bin.exe .\examples\chip8\chip8.bin

# 运行 经典 DOOM
.\tests\run_bin.exe .\examples\doom\doom.bin

# 运行 Dear ImGui DirectX 11 演示界面
.\tests\run_bin.exe .\examples\imgui\imgui_demo.bin

# 运行 Scintilla 现代化代码编辑器
.\tests\run_bin.exe .\examples\scintilla_editor\scintilla_editor.bin
```

---

## 10. Visual Studio 深度集成与项目对接指南

MemLLVM 能够完美融入 Microsoft Visual Studio 2022 / 2019 IDE 开发工作流。您可以直接在 Visual Studio 内编写现代化 C/C++ 业务逻辑，利用 IDE 强大的智能感知（IntelliSense）与代码高亮，并通过一键点击（或按下 `F5`）自动触发编译、Pass 插件变换、扁平二进制提取以及在内存沙盒中拉起运行与断点调试。

### 方案 A：原生 Visual Studio 项目（.vcxproj）对接

1. **环境变量与平台工具集准备**：
   - 打开 Visual Studio Installer，确认已勾选安装 **适用于 Windows 的 C++ Clang 编译器** 以及 **LLVM 工具集 MSBuild 支持**。
   - 配置系统环境变量 `LLVM_PATH`，指向您的 LLVM 安装根目录（例如 `C:\LLVM-22-dev`）：
     ```powershell
     [System.Environment]::SetEnvironmentVariable("LLVM_PATH", "C:\LLVM-22-dev", "User")
     ```
   - 在解决方案资源管理器中右键点击工程 -> **属性** -> **配置属性** -> **常规** -> **平台工具集**：选择 **LLVM (clang-cl)**。

2. **C/C++ 编译器属性配置**：
   - **配置属性** -> **C/C++** -> **常规**：
     - **附加包含目录**：添加 `$(SolutionDir)ltl\include`（或 MemLLVM 的 `ltl\include` 路径）。
   - **配置属性** -> **C/C++** -> **代码生成**：
     - **启用 C++ 异常**：选择 **否** (`/EHs-c- /GR-`)。
     - **安全检查**：选择 **禁用安全检查 (/GS-)**。
   - **配置属性** -> **C/C++** -> **命令行**：
     - 在 **其他选项** 中填入以下参数：
       ```text
       /clang:-fpass-plugin="<PathToMemLLVM>\build\MemLLVM\Release\MemLLVM.dll" /clang:-fno-jump-tables /clang:-mno-stack-arg-probe /clang:-fno-builtin
       ```

3. **链接器配置（纯净无依赖）**：
   - **配置属性** -> **链接器** -> **输入**：
     - **忽略所有默认库**：选择 **是 (/NODEFAULTLIB)**。
     - **附加依赖项**：添加 `<PathToMemLLVM>\ltl\ltl.lib` 以及项目所需的 Win32 导入库（如 `kernel32.lib;user32.lib;gdi32.lib;d3d11.lib` 等）。
   - **配置属性** -> **链接器** -> **高级**：
     - **入口点**：填入 `main`。

4. **配置生成后事件自动提取 `.bin` 镜像**：
   - **配置属性** -> **生成事件** -> **生成后事件**：
     - **命令行**：
       ```bat
       python "<PathToMemLLVM>\examples\imgui\extract_flat_bin.py" "$(TargetPath)" "$(TargetDir)$(TargetName).bin"
       ```
     - **说明**：`正在将 PE 二进制提取并封装为自包含绝对位置无关纯内存镜像 (.bin)...`

5. **配置 F5 一键启动与内存沙盒调试**：
   - **配置属性** -> **调试**：
     - **命令**：`<PathToMemLLVM>\tests\run_bin.exe`
     - **命令参数**：`"$(TargetDir)$(TargetName).bin"`
     - **工作目录**：`$(TargetDir)`
   - 配置完成后，直接按下 **F5** 或 **Ctrl+F5**，Visual Studio 将自动调用 `clang-cl` 挂载 `MemLLVM.dll`，完成编译与自重定位链接，提取 `.bin`，并由 `run_bin.exe` 在模拟内存中直接执行！

---

### 方案 B：CMake 工程对接（Visual Studio “打开文件夹”模式）

如果您在 Visual Studio 中使用 CMake 作为工程组织形式：
1. 在 `CMakeLists.txt` 或 `CMakePresets.json` 中绑定 `clang-cl`：
   ```cmake
   set(CMAKE_C_COMPILER "$ENV{LLVM_PATH}/bin/clang-cl.exe")
   set(CMAKE_CXX_COMPILER "$ENV{LLVM_PATH}/bin/clang-cl.exe")
   ```
2. 注入 MemLLVM 核心编译选项：
   ```cmake
   add_compile_options(
       $<$<COMPILE_LANGUAGE:C,CXX>:
           /clang:-fpass-plugin=${MEMLLVM_DIR}/build/MemLLVM/Release/MemLLVM.dll
           /GS-
           /GR-
           /clang:-fno-jump-tables
           /clang:-mno-stack-arg-probe
           /clang:-fno-builtin
       >
   )
   ```
3. 剥离 CRT 依赖并配置自动化生成 `.bin`：
   ```cmake
   target_link_options(my_app PRIVATE /NODEFAULTLIB /ENTRY:main)
   target_link_libraries(my_app PRIVATE ${MEMLLVM_DIR}/ltl/ltl.lib)

   add_custom_command(TARGET my_app POST_BUILD
       COMMAND python ${MEMLLVM_DIR}/examples/imgui/extract_flat_bin.py
               $<TARGET_FILE:my_app>
               $<TARGET_FILE_DIR:my_app>/$<TARGET_FILE_BASE_NAME:my_app>.bin
       COMMENT "Generating position-independent flat binary..."
   )
   ```

---

## 📄 License / 开源许可协议
This project is licensed under the [MIT License](LICENSE).
本项目遵循 MIT 开源许可协议。
