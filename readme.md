# MemLLVM

<div align="center">

# ⚡ MemLLVM
### Next-Generation LLVM Pass Plugin & Freestanding C/C++ Runtime for Position-Independent Flat Machine Code Generation
**Turn Full-Fledged Modern C and C++20 Projects Directly into Standalone, In-Memory Runnable Flat Machine Code (`.bin`)**

[![Build & Release](https://github.com/Laster-dev/MemLLVM/actions/workflows/release.yml/badge.svg)](https://github.com/Laster-dev/MemLLVM/actions/workflows/release.yml)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-0078D6.svg?logo=windows)](https://microsoft.com)
[![Toolchain](https://img.shields.io/badge/Toolchain-LLVM%20%2F%20Clang--cl%2022-269539.svg?logo=llvm)](https://llvm.org)
[![Standard](https://img.shields.io/badge/Language-C17%20%2F%20C%2B%2B20-f34b7d.svg?logo=c%2B%2B)](https://isocpp.org)
[![Coverage](https://img.shields.io/badge/C%2FC%2B%2B%20Coverage-%3E99.5%25-brightgreen.svg)](#-3-coverage-matrix--what-is-not-supported)
[![License](https://img.shields.io/badge/License-MIT-blueviolet.svg)](LICENSE)

[**English Documentation**](readme.md) | [**简体中文技术文档**](readme_zh.md)

</div>

---

## 🌟 What is MemLLVM?

**MemLLVM** is an industrial-grade compiler infrastructure built as an out-of-tree **LLVM New Pass Manager plugin** for the Windows `clang-cl` compiler.

Unlike traditional shellcode generators, packers, or in-memory PE loaders (such as Reflective DLL Injection), **MemLLVM does not emulate Windows or wrap binaries in heavy loaders**. Instead, it performs **deep AST and LLVM Intermediate Representation (IR) transformations during compilation**, erasing all assumptions about the operating system's PE loader (`ntdll!LdrLoadDll`, section mapping, IAT initialization, relocation directories, and OS TLS).

The result: Your high-level C or C++20 code (including templates, virtual inheritance, static constructors, multithreading, DirectX 11 graphics, and file I/O) compiles into a **pure, contiguous, self-relocating flat binary (`.bin`)** that can be injected into any executable memory page and executed directly via a single function pointer call:

```cpp
typedef void (*ShellcodeEntry)();
ShellcodeEntry entry = (ShellcodeEntry)pMemory;
entry(); // Boots up full C++20 runtime, static objects, and main() instantly!
```

---

## 🏗️ Architecture Pipeline

```
+-------------------------------------------------------------------------------------------------+
|                                    MemLLVM Compilation Pipeline                                 |
+-------------------------------------------------------------------------------------------------+
|  [Modern C / C++20 Source Code]  --->  [Clang AST / LLVM IR Engine]                             |
|                                                  |                                              |
|                                                  v                                              |
|                     +---------------------------------------------------------+                 |
|                     |             MemLLVM 8-Stage IR Pass Plugin              |                 |
|                     |  1. AutoMemoryPass     - Synthesizes Heap Allocators    |                 |
|                     |  2. StlStubPass        - Erases C++ Exception / TLS     |                 |
|                     |  3. GlobalLifecyclePass- Replaces .CRT$XCU with IR Ctor |                 |
|                     |  4. GlobalSectionPass  - Merges .text/.data/.rdata/.bss |                 |
|                     |  5. GlobalPointerPass  - Dynamic VTable / Pointer Delta |                 |
|                     |  6. AutoLoadLibraryPass- Injects On-Demand Subsystem DLL|                 |
|                     |  7. StackStringPass    - Erases .rdata String Literals  |                 |
|                     |  8. ApiResolvePass     - Inlines ROR13 PEB IAT Eraser   |                 |
|                     +---------------------------------------------------------+                 |
|                                                  |                                              |
|                                                  v                                              |
|                         [Optimized COFF Object] + [LTL Runtime (ltl.lib)]                       |
|                                                  |                                              |
|                                                  v                                              |
|                            [Flat Binary Extractor (extract_flat_bin.py)]                        |
|                                  + [Self-Rebase Bootstrap Stub]                                 |
|                                                  |                                              |
|                                                  v                                              |
|                ===================================================================              |
|                >>> Pure Contiguous Self-Relocating Flat Binary Image (.bin) <<<                |
|                ===================================================================              |
+-------------------------------------------------------------------------------------------------+
```

---

## 🎯 Industrial Coverage & In-Depth Architectural Boundaries

While MemLLVM breaks through compiler-level barriers—achieving unprecedented C++20 language completeness in pure memory—operating completely outside the operating system's native PE loader inevitably meets fundamental **environmental and OS contract boundaries**.

When designing or porting complex software into loaderless flat binaries, developers must understand and observe the following **six core architectural boundaries**:

### ✅ Fully Supported at the Compiler & Language Layer:
| Domain | Supported Capabilities |
| :--- | :--- |
| **Language Features** | Templates, concepts, lambdas, Meyers singletons, `constexpr`, type traits, function pointers, multi-level virtual inheritance & dynamic polymorphism (`vtable`). |
| **C++ Exceptions & Unwinding** | **Full `try` / `catch` / `throw` and re-throw (`throw;`) support**! Standalone table-based SEH runtime with self-registering dynamic `.pdata` function tables (`RtlAddFunctionTable`). Guaranteed deterministic reverse-order execution of local RAII destructors during stack unwinding. |
| **Object Lifecycle** | Global & static object construction (`@llvm.global_ctors`), destruction on exit (`atexit`), deterministic scoping. |
| **Memory Management** | Full freestanding `malloc`, `free`, `realloc`, `calloc`, `operator new`, `operator delete`, aligned allocation, bucketed arena allocators. |
| **Concurrency & Threads** | Full `std::thread`, `std::mutex`, `std::recursive_mutex`, `std::condition_variable`, `std::shared_mutex`, `std::async`, `std::future`, atomic operations (`std::atomic`). |
| **Standard I/O Streams** | Full `std::cout`, `std::cin`, `std::cerr`, `printf`, `wprintf`, `fgets`, pipe redirection, console interactive loops, file streams (`std::fstream`). |
| **String & Containers** | `std::string`, `std::wstring`, `std::vector`, `std::map`, `std::unordered_map`, `std::queue`, `std::deque`, `std::span`, UTF-8 / UTF-16 conversions. |
| **Filesystem & OS APIs** | Standard `std::filesystem::path`, directory iterators, Win32 I/O, Win32 GUI, GDI vector rendering, DirectX 11 / DXGI hardware acceleration, Windows Multimedia (`winmm`). |

---

### ⚠️ The 6 Real-World Architectural Boundaries & Best Practices:

#### 1. Module Identity & Filesystem Context Severance
* **The `GetModuleHandle(NULL)` and `GetModuleFileName` Impersonation Trap**:
  In loaderless memory execution, the operating system does not assign an independent `HMODULE` to the flat binary. Any library or code calling `GetModuleHandle(NULL)` or `GetModuleFileName(NULL, ...)` to locate "its own folder" for sibling assets (e.g. fonts, SQLite DBs, config files) will unexpectedly receive the **absolute path of the HOST process** (e.g., `explorer.exe` or the loader itself)!
  - **Best Practice**: Never rely on `GetModuleFileName(NULL)` to locate companion assets. Pass paths explicitly via `main` arguments, use absolute paths, or embed assets as inline byte arrays.
* **Current Working Directory (CWD) Reliance**:
  Relative paths (e.g., `fopen("config.ini", "r")`) resolve against the host process's active working directory, not the memory blob's location.

#### 2. Absence of Native PE Loader Facilities
* **Native Windows PE Resources (`.rsrc` / RC Files / Dialog Templates)**:
  Flat binaries strip all PE section headers and Resource Directory Trees. Win32 APIs like `FindResource`, `LoadIcon`, `LoadAccelerators`, and `DialogBox` cannot function.
  - **Best Practice**: Embed textures, fonts, audio, and shaders as inline C/C++ byte arrays or companion streaming containers (e.g., `doom1.wad`).
* **Windows Manifests, ComCtl32 v6 Visual Styles & High-DPI**:
  Standard PEs rely on embedded manifests (`RT_MANIFEST`) to activate Common Controls v6 (modern visual styles) and declare DPI awareness. In a flat binary, standard Win32 dialogs fallback to classic Windows 2000 flat appearance.
  - **Best Practice**: Use self-rendered UI engines (DirectX 11 / Direct2D / Dear ImGui), or programmatically mount activation contexts via `CreateActCtx` and call `SetProcessDpiAwarenessContext`.
* **Namespace-Scope `thread_local` & TLS Callbacks**:
  While thread-safe local statics are rewritten into atomic CAS, namespace-scope and class-static `thread_local` variables still require `__tls_index` slots. Furthermore, the OS loader cannot fire `PIMAGE_TLS_CALLBACK` notifications when new threads spawn.
  - **Best Practice**: Use explicit Win32 TLS APIs (`TlsAlloc`, `TlsGetValue`, `TlsSetValue`) for thread-specific state.

#### 3. Execution Morphology & Library Boundaries
* **Single-Entry Payload Model vs. Shared Library (No EAT)**:
  MemLLVM generates a standalone execution payload with a single entry point (offset 0 jumping to `main` after self-rebasing). **It cannot act as a multi-export Dynamic Link Library (DLL)** exporting dozens of named symbols for external callers.
* **Strictly x64 Architecture Exclusivity**:
  The entire pipeline (PE parsing, DIR64 fixups, ROR13 API resolution, 12-byte `.pdata` tables, and x64 assembly stubs) is hardcoded for the **Windows x86_64 ABI**. It **does not support 32-bit x86 (IA-32)** (which requires `FS:[0x30]`, HIGHLOW relocations, and linked-list SEH) or **ARM64**.

#### 4. Host Process Lifecycle Coupling
* **The `exit()` / `abort()` Host Termination Risk**:
  In a standalone EXE, calling `exit(code)` terminates normally. When running as an in-memory payload inside a host process, calling standard library `exit()` or `abort()` calls `ExitProcess`, **instantly killing the entire host process**!
  - **Best Practice**: If the payload must return cleanly to the host, it must `return` from `main()` or invoke `ExitThread`. Avoid calling `exit()` in embedded scenarios.
* **Direct Command Line (`GetCommandLine`) Query Pollution**:
  `GetCommandLineA/W` returns the host process's creation command line. While MemLLVM's stub reconstructs `main(argc, argv)`, libraries bypassing `main` and querying the PEB directly will observe host parameters.

#### 5. Dynamic API Resolution & Full Ordinal Support
* **Native & Transparent Ordinal-Only API Auto-Resolution**:
  In the Windows ecosystem, core system DLLs such as `shlwapi.dll` (551 functions), `shell32.dll` (364 functions), `ws2_32.dll`, and `comctl32.dll` export hundreds of functions solely by integer ordinal (`[NONAME]` exports like `SHAlloc`, `SHFree`, `ConnectToConnectionPoint`), omitting function name strings from PE export tables.
  MemLLVM provides full, end-to-end native support for ordinal-only resolution:
  1. **SDK Ordinal Index Database**: Pre-indexes all 2,729 Windows SDK ordinal-only exports across 80 system DLLs with O(log N) compile-time binary search, plus dynamic ordinal parsing for project-local `.lib` files.
  2. **Compile-Time IR Inlining**: `ApiResolvePass` automatically rewrites implicit calls, `invoke` exception sites, and `__imp_*` slot loads to `MemLLVM_GetApiAddressByOrdinal(dll_hash, ordinal)` with zero linker import dependencies, accompanied by proactive DLL preloading from `AutoLoadLibraryPass`.
  3. **Global Pointer & Vtable Auto-Rebasing**: `GlobalPointerPass` automatically rebases address-taken ordinal function pointers in global variables and vtables at entry time.
  4. **Multi-hop Forwarder Resolution & Synthetic Symbols**: The runtime bootstrap resolver natively resolves forwarders (`DLL.#ordinal` and `DLL.func`) and supports calling arbitrary private ordinals using `<dll>_Ordinal_<ord>` or `<dll>_<ord>`.
* **Explicit Loading Boundary for Custom Non-SDK Private DLLs**:
  If a payload depends on a completely proprietary third-party DLL that is neither in the Windows SDK database nor accompanied by an import `.lib` during compilation, the DLL must be loaded via `LoadLibraryA("custom.dll")` to populate the PEB module list before resolving functions from it.

#### 6. Modern OS Security Mitigations
* **W^X and DEP Memory Protection**:
  Flat binaries require executable memory pages (allocated with `PAGE_EXECUTE_READWRITE` or switched from RW to RX via `VirtualProtect` before execution).
* **Control Flow Guard (CFG) & Arbitrary Code Guard (ACG)**:
  If the host process enforces strict Control Flow Guard (CFG), indirect calls into non-image dynamic memory trigger fast-fail `0xC0000409` unless registered via `SetProcessValidCallTargets`. If Arbitrary Code Guard (ACG) is enabled on the host, allocating executable memory dynamically is blocked by the Windows kernel.

---

## 🕳️ Hard-Fought Engineering Pits & Technical Breakthroughs

Building MemLLVM meant fighting against subtle, undocumented Windows internals and compiler edge-cases. Here is a candid record of the hardest traps solved during development:

<details>
<summary><b>1. The Undocumented Windows <code>GetStdHandle</code> WindowFlags 0x400 / 0x200 Trap</b> (Click to expand)</summary>

#### The Trap:
When running CLI tools (like SQLite3 or diagnostic interactive stubs) inside a GUI-based pipe loader, `SetStdHandle(STD_OUTPUT_HANDLE, hPipeWrite)` returned `TRUE`, yet subsequent calls to `GetStdHandle(STD_OUTPUT_HANDLE)` in the payload returned `0x(nil)` (NULL)!
While MemLLVM-LTL had a fallback to `_get_osfhandle(1)`, native MSVC/UCRT binaries saw NULL, assumed the process was non-interactive or output was broken, and dropped all output or terminated instantly.

#### The Discovery:
We disassembled Windows 11/10 `kernelbase!GetStdHandle`:
```x86asm
GetStdHandle:
    mov rax, gs:[0x60]                   ; rax = PEB
    mov rcx, [rax + 0x20]                ; rcx = ProcessParameters
    test dword ptr [rcx + 0xa4], 0x400   ; Test WindowFlags at offset 0xA4!
    jne return_null                      ; If bit 0x400 is set, FORCIBLY RETURN NULL!
    mov rax, [rcx + 0x28]                ; Return StandardOutput
    ret
```
When a process is launched by Windows Explorer or `ShellExecuteEx`, Windows sets bit `0x400` in `WindowFlags`. Even if you call `SetStdHandle`, Windows only writes to the handle slot without clearing `0x400`! `GetStdHandle` sees the flag, assumes stdout is an icon/monitor handle, and forcibly returns NULL.

#### The Breakthrough:
MemLLVM's loader harness inspects the PEB directly via `__readgsqword(0x60)`, clears `0x400` (and stdin's `0x200`) from `ProcessParameters->WindowFlags`, and sets `STARTF_USESTDHANDLES (0x100)`, unlocking 100% reliable pipe and console I/O for all native tools!
</details>

<details>
<summary><b>2. The Dual CRT File Descriptor Collision (<code>msvcrt.dll</code> vs <code>ucrtbase.dll</code>)</b> (Click to expand)</summary>

#### The Trap:
Redirecting C runtime stdout via `_dup2(pipeFd, 1)` worked for tools linked with `msvcrt.dll`, but modern Windows payloads dynamically importing `ucrtbase.dll` wrote into a black hole—their `_write(1)` returned `-1` and `printf` output vanished.

#### The Discovery:
`msvcrt.dll` and `ucrtbase.dll` maintain completely separate, independent file descriptor tables inside the same process. Redirecting fd 1 in `msvcrt` has zero effect on `ucrtbase`!

#### The Breakthrough:
We implemented dual-table redirection in the loader harness: dynamically inspecting and duplicating pipe handles across both `msvcrt` and `ucrtbase` via `DuplicateHandle`, ensuring every CRT flavor writes directly into the visual terminal.
</details>

<details>
<summary><b>3. The Clang Pass Plugin C++ ABI Mismatch (Heap Corruption 0xC0000374)</b> (Click to expand)</summary>

#### The Trap:
Building an out-of-tree LLVM pass plugin against LLVM header files and loading it into an official LLVM release installer crashed immediately during pass registration with `STATUS_HEAP_CORRUPTION (0xC0000374)`.

#### The Discovery:
LLVM's C++ codebase makes extensive use of STL containers (`std::vector`, `llvm::StringRef`, `llvm::SmallVector`) without an ABI freeze across builds. Even minor compiler flag differences (e.g. `/MT` vs `/MD`, debug iterators, MSVC version differences) corrupt memory when passing objects across DLL boundaries.

#### The Breakthrough:
We published and committed a pinned, hermetic plugin-compatible compiler toolchain (`clang-plugin-toolchain.zip` in GitHub Releases and `release_artifacts/`) ensuring 100% binary compatibility on every developer machine and GitHub CI runner.
</details>

<details>
<summary><b>4. Thread-Safe Local Statics (Meyers Singleton) Invoking Invalid TLS</b> (Click to expand)</summary>

#### The Trap:
C++11/C++20 guarantees thread-safe initialization of local static variables. MSVC and Clang implement this by emitting calls to `_Init_thread_epoch` and accessing `__tls_index` via `gs:[0x58]`. In a flat binary, this immediately triggered an access violation (`0xC0000005`).

#### The Breakthrough:
MemLLVM's `StlStubPass` detects these compiler-generated TLS checks at the IR level and rewrites them into atomic lock-free compare-and-swap operations on non-TLS global storage, completely eliminating the OS TLS dependency while preserving thread safety!
</details>

<details>
<summary><b>5. Windows x64 SEH Exception Unwinding & The Missing .pdata Pitfall</b> (Click to expand)</summary>

#### The Trap:
After implementing `_CxxThrowException`, `__CxxFrameHandler3`, and registering function tables via `RtlAddFunctionTable`, throwing an exception (`throw 42;`) still caused the process to abruptly crash with unhandled exception code `0xE06D7363`, completely bypassing the outer `catch` handler.

#### The Discovery:
Windows x64 employs a strictly table-driven exception dispatch model. When `_CxxThrowException` calls `ntdll!RaiseException`, the kernel SEH dispatcher invokes `RtlVirtualUnwind` to walk backward through the call stack. If any non-leaf function frame (including `_CxxThrowException` itself!) lacks `.pdata` unwind metadata (e.g. because `/clang:-fno-unwind-tables` was enabled during CRT compilation), the kernel's `RtlLookupFunctionEntry` query returns `NULL`. Unable to compute the previous `RSP`, **the kernel immediately aborts the unwind search and terminates the process**!

#### The Breakthrough:
1. Compiled `ltl/src/crt_exceptions.cpp` specifically with `/EHsc` to preserve full `.pdata` and `.xdata` unwind tables.
2. Updated `extract_flat_bin.py` to preserve and extract the `.pdata` section into the flat image.
3. In the bootstrap self-rebase stub, dynamically bound the `MEMLLVME` descriptor with the runtime image base, `.pdata` pointer, and count. This seamlessly drives `RtlAddFunctionTable` and `RtlDeleteFunctionTable` for zero-overhead, flawless C++ exception handling (`try`/`catch`/`throw`/rethrow) and RAII stack unwinding in flat shellcode!
</details>

<details>
<summary><b>6. Undocumented Ordinal-Only ([NONAME]) Exports & Multi-Hop Forwarder Chains</b> (Click to expand)</summary>

#### The Trap:
Calling common Windows Shell or networking APIs (such as `SHAlloc`, `SHFree`, or `ConnectToConnectionPoint`) works normally in a standard executable, but when converted into flat shellcode, calls crash immediately with a null pointer (`0x00000000`) or linger as unresolved link-time imports.

#### The Discovery:
The PE specification allows DLLs to export functions purely by numeric index (`[NONAME]`), omitting function name strings from `AddressOfNames`. `shlwapi.dll` alone contains 551 ordinal-only exports, and `shell32.dll` contains 364. Standard ROR13 export table scanners return NULL. Furthermore, many ordinals are forwarded to other libraries (e.g. `SHUNIMPL.#429` or `NTDLL.RtlAllocateHeap`); naively jumping to the exported RVA executes ASCII forwarder string data instead of machine code!

#### The Breakthrough:
1. Implemented an $O(1)$ runtime ordinal indexer (`funcs[ordinal - Base]`) in the bootstrap resolver with multi-hop forwarder resolution supporting both string and `#ordinal` forwarded targets with cycle detection;
2. Pre-indexed all 2,729 Windows SDK ordinal exports across 80 system DLLs into a compile-time binary search table and added dynamic `.lib` archive header scanning for project-local dependencies;
3. Connected `AutoLoadLibraryPass`, `ApiResolvePass`, and `GlobalPointerPass` to transparently rewrite calls and rebase vtable/global pointer slots to `MemLLVM_GetApiAddressByOrdinal`.
</details>

---

## 🏆 Verified Showcase Projects (From `examples/`)

Every project in `examples/` compiles directly from source into a **standalone, self-relocating flat binary (`.bin`)** with zero external dependencies:

```
examples/
├── box2d/       # Box2D 3.0 2D Physics Engine (45 files, real-time GDI rendering)
├── chip8/       # Full CHIP-8 CPU / Sound Virtual Machine (C++20, plays Pong/Tetris)
├── doom/        # Classic DOOM 3D Game Engine (83 files, software rasterizer, audio)
├── imgui/       # Dear ImGui v1.91+ (Hardware-accelerated DirectX 11, multi-windows)
├── sqlite/      # Official SQLite3 CLI Shell (Full interactive REPL, SQL engine)
├── mimikatz/    # Large-scale Complex C Project (114 TUs, tentative definitions, subsystems)
└── mock_cli/    # Interactive CLI Diagnostic Stub (Testing stdio pipes & arguments)
```

| Showcase Project | Scale & Language | Flat Binary Size | Key Validated Capabilities |
| :--- | :--- | :--- | :--- |
| **[Official SQLite3 CLI](examples/sqlite/)** | C / SQLite 3.45 | **340 KB** | Full interactive SQL REPL, in-memory database creation, complex SQL queries, table formatting, pipe streaming. |
| **[Classic DOOM Engine](examples/doom/)** | Pure C (83 files) | **703 KB** | Full id Software DOOM engine, 3D software rasterization, keyboard controls, audio mixer, streaming IWAD data. |
| **[Dear ImGui v1.91+](examples/imgui/)** | C++17 Industrial UI | **1.70 MB** | Full immediate-mode GUI, multi-window docking, plots, widgets, hardware-accelerated **DirectX 11 / DXGI** pipeline. |
| **[Box2D 3.0 Engine](examples/box2d/)** | Pure C (45 files) | **605 KB** | Continuous rigid body physics, dynamic BVH trees, collision impulse solver, real-time Win32 GDI vector rendering. |
| **[CHIP-8 Virtual Machine](examples/chip8/)** | Modern C++20 | **42 KB** | 35-opcode CPU emulation, frame timers, audio buzzer, 64x32 monochrome display matrix running *Pong* and *Tetris*. |
| **[Mimikatz 2.2.0](examples/mimikatz/)** | Complex C (114 Translation Units) | **1.20 MB** | Massive real-world code base testing tentative definitions merging, dynamic subsystem resolution, multi-tier struct alignment, and dual-CRT stdio pipe redirection. |
| **[Mock CLI Diagnostic Stub](examples/mock_cli/)** | Minimal C Runtime | **36 KB** | Dedicated diagnostic tool testing full-duplex stdio pipes, command line argument forwarding, streaming output throughput, and graceful exit handling. |

---

## 🚀 Quick Start Guide

### 1. Download & One-Click Environment Setup
1. Download `MemLLVM-SDK-windows-x64.zip` from the latest [GitHub Releases](https://github.com/Laster-dev/MemLLVM/releases).
2. Extract to any directory (e.g. `C:\MemLLVM`).
3. **Double-click `setup_env.bat`**:
   - Automatically sets `MEMLLVM_DIR` in your User Environment.
   - **Auto-Binds Bundled Hermetic Toolchain**: Sets `LLVMInstallDir` to the SDK's embedded `toolchain/` directory so Visual Studio ClangCL automatically invokes the exact binary-aligned `clang-cl.exe`, eliminating host Clang ABI mismatches and plugin loading errors!
   - Mounts `toolchain\bin`, `bin`, and `Tools` into your User `PATH`.
   - **Smart Path Migration**: Moving the SDK in the future? Just run `setup_env.bat` again in the new folder—it automatically purges old stale paths.
   - **One-Click In-Place Auto-Update**: The SDK comes with `update.bat`. Run it at any time to automatically fetch the latest release from GitHub and overwrite your local SDK in-place, then re-synchronize configurations.

### 2. Enable Clang in Visual Studio
* **Option A (Recommended Built-in Component)**: Open Visual Studio Installer -> Modify -> Individual components -> Search `Clang` -> Check **C++ Clang Compiler for Windows** and **MSBuild support for LLVM toolset**.
* **Option B (VSIX Extension)**: Visual Studio Extensions -> Manage Extensions -> Search & Install **LLVM Compiler Toolchain**.

### 3. Visual Studio Project (.vcxproj) Configuration
In your project properties:
1. **General** -> **Platform Toolset**: Select **LLVM (clang-cl)**.
2. **C/C++** -> **General** -> **Additional Include Directories**:
   ```text
   $(MEMLLVM_DIR)\include;$(MEMLLVM_DIR)\ltl\include;%(AdditionalIncludeDirectories)
   ```
3. **C/C++** -> **Code Generation**:
   - Runtime Library: **Multi-threaded (/MT)**
   - Enable C++ Exceptions: **Yes (`/EHsc`)** (Fully supports exception handling & RAII stack unwinding; or **No (`/EHs-c- /GR-`)** if minimal binary size is desired)
   - Security Check: **Disable Security Check (`/GS-`)**
4. **C/C++** -> **Command Line** -> **Additional Options**:
   ```text
   /clang:-fpass-plugin="$(MEMLLVM_DIR)\bin\MemLLVM.dll" /clang:-fno-jump-tables /clang:-fno-builtin
   ```
5. **Linker** -> **General** -> **Additional Library Directories**:
   ```text
   $(MEMLLVM_DIR)\lib;$(MEMLLVM_DIR)\ltl;%(AdditionalLibraryDirectories)
   ```
6. **Linker** -> **Input**:
   - Ignore All Default Libraries: **Yes (`/NODEFAULTLIB`)**
   - Additional Dependencies: `ltl.lib;kernel32.lib;user32.lib;gdi32.lib;d3d11.lib;`
7. **Linker** -> **Advanced**:
   - Entry Point: **`main`**
8. **Build Events** -> **Post-Build Event**:
   ```bat
   python "$(MEMLLVM_DIR)\Tools\extract_flat_bin.py" "$(TargetPath)" "$(TargetDir)$(TargetName).bin"
   ```

---

## 🖥️ SCLoader / CSLoader: Open-Source Status & Standalone Tool

In the `Tools/` directory, MemLLVM provides **SCLoader** (`Tools/SCLoader.exe`), a modern GUI runner and interactive payload debugger built specifically for executing, inspecting, and testing flat machine code images.

> **Notice on CSLoader Source Code**:  
> SCLoader's visual presentation is powered by **CUI (Contemporary UI)**, an extensive proprietary Direct2D / DirectWrite Fluent Design GUI framework. Because the underlying CUI framework is a commercial enterprise component not yet cleared for open-source release, **we cannot open-source the full GUI library repository at this time**.  
> 
> However, to ensure full transparency and community utility:
> 1. We provide the **complete core execution harness source code** in [`CSLoader/`](CSLoader/) (including the x64 assembly execution bridge `CallShellcode.asm`, SEH exception harness, privilege elevation/de-elevation dispatcher, and pipe redirection engine).
> 2. We provide the **fully compiled standalone tool `Tools/SCLoader.exe`** in every release, completely free for all users to test, run, and debug flat binaries!

---

## 💖 Acknowledgements & Credits

MemLLVM stands on the shoulders of giants. We express our deepest gratitude to:

* **[VC-LTL](https://github.com/Chuyu-Team/VC-LTL5)** and the Chuyu-Team: For setting the gold standard in lightweight Windows CRT engineering and dependency-free runtime architecture. MemLLVM's freestanding LTL philosophy was deeply inspired by VC-LTL.
* **[The LLVM Project](https://llvm.org)**: For creating the most modular, powerful compiler intermediate representation and New Pass Manager ecosystem.
* **[Omar Cornut](https://github.com/ocornut)** & The Dear ImGui Community: For the legendary immediate-mode GUI framework.
* **[Erin Catto](https://github.com/erincatto)** & The Box2D Project: For the industry-standard 2D rigid body physics engine.
* **id Software & John Carmack**: For the immortal, beautifully written classic DOOM engine.
* **D. Richard Hipp & The SQLite Team**: For the most reliable, elegant, and versatile database engine in history.

---

## 📈 Star History

[![Star History Chart](https://api.star-history.com/svg?repos=Laster-dev/MemLLVM&type=Date)](https://star-history.com/#Laster-dev/MemLLVM&Date)

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).
All example projects and third-party libraries retain their respective original licenses.
