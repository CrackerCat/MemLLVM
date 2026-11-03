# MemLLVM

<div align="center">

# ⚡ MemLLVM
### Next-Generation LLVM Pass Plugin & Freestanding C/C++ Runtime for Position-Independent Flat Machine Code Generation
**Turn Full-Fledged Modern C and C++20 Projects Directly into Standalone, In-Memory Runnable Flat Machine Code (`.bin`)**

[![Build & Release](https://github.com/Laster-dev/MemLLVM/actions/workflows/release.yml/badge.svg)](https://github.com/Laster-dev/MemLLVM/actions/workflows/release.yml)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-0078D6.svg?logo=windows)](https://microsoft.com)
[![Toolchain](https://img.shields.io/badge/Toolchain-LLVM%20%2F%20Clang--cl%2022-269539.svg?logo=llvm)](https://llvm.org)
[![Standard](https://img.shields.io/badge/Language-C17%20%2F%20C%2B%2B20-f34b7d.svg?logo=c%2B%2B)](https://isocpp.org)
[![Coverage](https://img.shields.io/badge/C%2FC%2B%2B%20Coverage-~99%25-brightgreen.svg)](#-3-coverage-matrix--what-is-not-supported)
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

## 🎯 99% Coverage Matrix & What is NOT Supported

MemLLVM covers **nearly 99% of all real-world C and C++ development scenarios**, allowing off-the-shelf industrial libraries and game engines to compile into flat binaries without source modification.

### ✅ What is Fully Supported (~99% Scenarios):
| Domain | Supported Capabilities |
| :--- | :--- |
| **Language Features** | Templates, concepts, lambdas, Meyers singletons, `constexpr`, type traits, function pointers, multi-level virtual inheritance & dynamic polymorphism (`vtable`). |
| **Object Lifecycle** | Global & static object construction (`@llvm.global_ctors`), destruction on exit (`atexit`), deterministic scoping. |
| **Memory Management** | Full freestanding `malloc`, `free`, `realloc`, `calloc`, `operator new`, `operator delete`, aligned allocation, bucketed arena allocators. |
| **Concurrency & Threads** | Full `std::thread`, `std::mutex`, `std::recursive_mutex`, `std::condition_variable`, `std::shared_mutex`, `std::async`, `std::future`, atomic operations (`std::atomic`). |
| **Standard I/O Streams** | Full `std::cout`, `std::cin`, `std::cerr`, `printf`, `wprintf`, `fgets`, pipe redirection, console interactive loops, file streams (`std::fstream`). |
| **String & Containers** | `std::string`, `std::wstring`, `std::vector`, `std::map`, `std::unordered_map`, `std::queue`, `std::deque`, `std::span`, UTF-8 / UTF-16 conversions. |
| **Filesystem & OS APIs** | Standard `std::filesystem::path`, directory iterators, Win32 I/O, Win32 GUI, GDI vector rendering, DirectX 11 / DXGI hardware acceleration, Windows Multimedia (`winmm`). |

---

### ⚠️ What is Currently NOT Supported (The Remaining ~1%):

While MemLLVM pushes the frontier of freestanding execution, operating outside the OS PE loader imposes three fundamental technical boundaries:

1. **C++ Exception Handling (`try` / `catch` / `throw`)**:
   - **Why**: Windows x64 Structured Exception Handling (SEH) unwinding strictly relies on `.pdata` and `.xdata` function tables registered in the NT kernel (`RtlAddFunctionTable`). In a flat memory image without OS loader involvement, an uncaught `throw` causes the kernel unwinder to fail locating frame handlers, resulting in process termination.
   - **Current Requirement**: Compile with exceptions disabled (`/EHs-c-`, `/GR-`, `/clang:-fno-exceptions`).
   - **Roadmap**: We are engineering a self-registering in-memory unwinder that invokes `RtlAddFunctionTable` dynamically at the self-rebase stage to restore zero-cost exception handling.

2. **Native Windows PE Resource Files (`.rsrc` / RC Files)**:
   - **Why**: Flat binaries lack PE headers, section headers, and standard Resource Directory Trees. Win32 APIs like `FindResource`, `LoadIcon`, and `LoadAccelerators` require an OS `HMODULE` pointing to a mapped PE image.
   - **Current Requirement**: Embed binary assets (textures, shaders, audio, fonts) as inline byte arrays or companion streaming containers (e.g., `doom1.wad`).
   - **Roadmap**: Developing an in-memory virtual resource table emulator and pseudo-`HMODULE` dispatcher.

3. **The C++ `thread_local` Keyword**:
   - **Why**: Native `thread_local` requires OS PE loader thread attach notifications and slot assignment via `__tls_index` (`TEB->ThreadLocalStoragePointer` at `gs:[0x58]`).
   - **Current Requirement**: Use explicit Win32 TLS APIs (`TlsAlloc`, `TlsGetValue`, `TlsSetValue`). (Note: Meyers local statics are 100% supported via atomic CAS rewrites).

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
   - Enable C++ Exceptions: **No (`/EHs-c- /GR-`)**
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
