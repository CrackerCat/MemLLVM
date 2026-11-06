# MemLLVM

<div align="center">

# ⚡ MemLLVM
### Next-Generation LLVM Pass Plugin & Freestanding C/C++ Runtime for Position-Independent Flat Machine Code Generation
**Turn Full-Fledged Modern C and C++20 Projects Directly into Standalone, In-Memory Runnable Flat Machine Code (`.bin`)**

[![Continuous Integration](https://github.com/Laster-dev/MemLLVM/actions/workflows/ci.yml/badge.svg)](https://github.com/Laster-dev/MemLLVM/actions/workflows/ci.yml)
[![Build & Release](https://github.com/Laster-dev/MemLLVM/actions/workflows/release.yml/badge.svg)](https://github.com/Laster-dev/MemLLVM/actions/workflows/release.yml)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-0078D6.svg?logo=windows)](https://microsoft.com)
[![Toolchain](https://img.shields.io/badge/Toolchain-LLVM%20%2F%20Clang--cl%2022-269539.svg?logo=llvm)](https://llvm.org)
[![Standard](https://img.shields.io/badge/Language-C17%20%2F%20C%2B%2B20%20%2F%20C%2B%2B23-f34b7d.svg?logo=c%2B%2B)](https://isocpp.org)
[![Tests](https://img.shields.io/badge/Tests-27%2F27%20Passed%20(100%25)-brightgreen.svg)](#-full-modern-c-regression-test-suite-27-tests)
[![Coverage](https://img.shields.io/badge/C%2FC%2B%2B%20Coverage-%3E99.5%25-brightgreen.svg)](#-industrial-coverage--in-depth-architectural-boundaries)
[![License](https://img.shields.io/badge/License-MIT-blueviolet.svg)](LICENSE)

[**English Documentation**](readme.md) | [**简体中文技术文档**](readme_zh.md)

</div>

---

## 🌟 What is MemLLVM?

**MemLLVM** is an industrial-grade compiler infrastructure built as an out-of-tree **LLVM New Pass Manager plugin** paired with a freestanding runtime library, specifically tailored for the Windows `clang-cl` toolchain.

Unlike traditional shellcode generators, binary packers, or in-memory PE loaders (such as Reflective DLL Injection), **MemLLVM does not emulate Windows or wrap binaries in heavy multi-stage loaders**. Instead, it performs **deep LLVM Intermediate Representation (IR) transformations during compilation**, systematically stripping all assumptions about the operating system's native PE loader (`ntdll!LdrLoadDll`, section mapping and alignment, IAT resolution, relocation directories, and OS TLS slot allocation).

The result: Your high-level C or C++20 code (including templates, virtual inheritance, static constructors, multithreading, DirectX 11 graphics, and file I/O) compiles into a **pure, contiguous, self-relocating flat binary image (`.bin`)**. It can be mapped into any executable memory page and executed directly via a single parameterless function pointer:

```cpp
typedef void (*ShellcodeEntry)();
ShellcodeEntry entry = (ShellcodeEntry)pMemory;
entry(); // Boots up the C++20 runtime, static initializers, and enters main() instantly!
```

---

## 🏗️ Architecture Pipeline

```
+-------------------------------------------------------------------------------------------------+
|                                    MemLLVM Compilation Pipeline                                 |
+-------------------------------------------------------------------------------------------------+
|  [Modern C / C++20 Source Code]  --->  [Clang AST Frontend / LLVM IR Optimizer Engine]           |
|                                                  |                                              |
|                                                  v                                              |
|                     +---------------------------------------------------------+                 |
|                     |             MemLLVM 8-Stage IR Pass Plugin              |                 |
|                     |  1. AutoMemoryPass     - Synthesizes Freestanding Heap  |                 |
|                     |  2. StlStubPass        - Erases C++ Exception / TLS     |                 |
|                     |  3. GlobalLifecyclePass- Replaces .CRT$XCU with IR Ctor |                 |
|                     |  4. GlobalSectionPass  - Merges .text/.data/.rdata/.bss |                 |
|                     |  5. GlobalPointerPass  - Dynamic VTable / Pointer Delta |                 |
|                     |  6. AutoLoadLibraryPass- Injects On-Demand Subsystem DLL|                 |
|                     |  7. StackStringPass    - Function Compound Stack Pool JIT Decryption & Ephemeral Wipe     |
|                     |  8. ApiResolvePass     - Strip IAT, Session-Polymorphic Parameterized PEB Hash Resolve    |
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

Across numerous systemic refactorings and production iterations, MemLLVM has solved the core challenges of modern C++ in loaderless environments: virtual table rebasing, Windows resource directory parsing, ordinal-only imports, dual-CRT stdio conflicts, atomic initialization, freestanding memory management, and table-driven SEH exception unwinding.

### ✅ Fully Supported at the Compiler & Runtime Layer (Conquered Capabilities):

| Domain | Supported Capabilities & Engineering Principles |
| :--- | :--- |
| **Modern C++20/C++23 Language Core** | Template metaprogramming, concepts, lambdas, `constexpr`, type traits, function pointers, multi-level virtual inheritance & dynamic polymorphism (`vtable`). **Meyers Thread-Safe Singletons Fully Supported**: `StlStubPass` rewrites implicit TLS checks and `_Init_thread_epoch` calls into atomic CAS instructions on flat memory, completely severing OS TLS dependencies. **C++20 Coroutines** (`co_await`, `co_return`, `std::coroutine_handle`) compiled down to 222 bytes flat binary. |
| **Session-Polymorphic API Hash Randomization** | **Permanently eliminates fixed ROR13 static signature patterns**! Every build session derives or accepts an explicit `MEMLLVM_HASH_SEED` generating dynamic 4-parameter hashing (`Seed`, `ROT`, `MUL`, `XOR`); `Resolver.c` and `ApiResolvePass` dynamically inject parameters at compile time without any hardcoded hashes (including `LoadLibraryA`); `ltl.lib` decouples from user project seeds via `MemLLVM_GetApiAddressByName`, allowing a single static runtime archive to serve any user project. |
| **Strings & Constants: Compound Stack Pool JIT Decryption & Ephemeral Wipe** | **Function Compound Stack Pool Architecture & Nanosecond Physical Zeroization**! `StackStringPass` merges all constants referenced by each function into an exclusive, compact read-only ciphertext pool (`@func.str_pool`) at compile time, and **completely erases the original plaintext global variables from the LLVM IR**; on function entry, allocates a single compound buffer on the stack and emits a single call to `__memllvm_jit_decrypt`. Constants within the function are indexed via single `lea` instructions with 0 byte instruction bloat; upon function exit (`ret`/`resume`), emits a single `__memllvm_jit_wipe` call to physically zero the buffer. Binary size inflation across all targets is slashed from +84% down to **only +5.2%**, leaving 0 plaintext bytes in static binaries or persistent memory. |
| **Native PE Resources (`.rsrc`) Fine-Grained Leaf Encryption** | **Per-Leaf Independent Random-Key Encryption & Ephemeral Erasure**! Extractor (`extract_flat_bin.py`) keeps the 3-level resource directory skeleton intact for $O(\log N)$ traversal, while encrypting each leaf payload (bitmaps, icon bitstreams, dialog templates, menus, string tables) with unique random keys; LTL runtime (`crt_resources.cpp`) decrypts on demand, delegates to OS GDI/kernel objects (`CreateDIBitmap`, `CreateIconFromResourceEx`, `DialogBoxIndirectParamW`), and immediately executes `SecureZeroMemory` on ephemeral buffers before caching handles. |
| **Native PE Resource Subsystem (`.rsrc`)** | **Full Win32 resource hierarchy without PE headers**! The extractor preserves the `.rsrc` physical section, while the bootstrap stub dynamically rebinds base addresses. The freestanding LTL runtime embeds an in-memory 3-level directory parser (Type -> Name/ID -> Language), transparently shimming `FindResource(Ex)A/W`, `LoadResource`, `LockResource`, `SizeofResource`, `LoadStringA/W` (decoding 16-entry Pascal strings), `DialogBoxParamA/W`, `CreateDialogParamA/W`, `LoadMenuA/W`, `LoadAcceleratorsA/W`, `LoadIconA/W`, `LoadCursorA/W`, `LoadBitmapA/W`, and directory enumerators, with automatic fallback for system DLL resources. |
| **Ordinal-Only (`[NONAME]`) Exports** | **Complete coverage of Windows un-named exports**! Pre-indexes all 2,729 ordinal exports across 80 system DLLs in Windows 10/11 SDK with $O(\log N)$ compile-time binary search; `ApiResolvePass` rewrites calls to `MemLLVM_GetApiAddressByOrdinal`; the bootstrap resolver natively handles recursive multi-hop forwarders (`DLL.#ordinal` and `DLL.func`) and `<dll>_Ordinal_<ord>` calls. |
| **C++ Exceptions & Stack Unwinding** | **Full `try` / `catch` / `throw` and rethrow (`throw;`) support**! Standalone table-driven SEH engine: the bootstrap stub dynamically registers `.pdata` function tables (`RtlAddFunctionTable`) with the Windows kernel using the `MEMLLVME` descriptor, guaranteeing 100% deterministic reverse-order execution of local RAII destructors during stack unwinding. |
| **C++20 Synchronization & Concurrency** | Full synchronization suite: `std::thread`, `std::jthread`, `std::mutex`, `std::recursive_mutex`, `std::condition_variable`, `std::shared_mutex`, `std::async`, `std::future`, lock-free `std::atomic<T>`. **C++20 Concurrency Primitives**: `<semaphore>` (`std::binary_semaphore`, `std::counting_semaphore`), `<latch>`, `<barrier>`, and `std::atomic<T>::wait/notify` dynamically bound to `WaitOnAddress`/`WakeByAddress*` in `kernelbase.dll` with SRWLOCK hash wait tables. |
| **C++17 Parallel Algorithms** | `std::execution::par`, `std::execution::seq` (e.g., `std::sort(std::execution::par, v.begin(), v.end())`), seamlessly scheduled onto the Windows NT Threadpool (`PTP_WORK`) with aligned allocator support. |
| **C++20/C++23 Formatting & Print** | **On-Demand `std::format` Auto-Linking**: `StlStubPass` inspects translation unit symbols for `std::format` / floating-point `std::to_chars` and automatically injects Ryu table dependencies (`ltl_format.lib`); strictly introduces **0 extra bytes** when unused. **C++23 `std::print` & `std::println`**: Native Unicode console bridge with UTF-8 to UTF-16 transcoding via `WriteConsoleW` and stream fallback. |
| **C++23 Stacktrace & Diagnostics** | `<stacktrace>` (`std::stacktrace::current()`, `std::to_string(st)`, frame address formatting) without external debuggers or PDB dependencies; `<source_location>` (`std::source_location::current()`). |
| **Time, Entropy & Localization** | C++20 `<chrono>` time zone database (`std::chrono::current_zone()`, `zoned_time`, `tzdb`) dynamically querying Win32 timezone info; `std::random_device` hardware entropy via `RtlGenRandom` (`SystemFunction036`) with `RDTSC` fallback; named locales `std::locale("zh-CN")`, `std::locale("")` with safe `_Locimp` heap lifetime management. |
| **Dual-CRT Stdio & Console I/O** | Directly disassembles and pierces the PEB, clearing the undocumented `WindowFlags` `0x400` and `0x200` intercept bits; simultaneously intercepts file descriptor tables in both `msvcrt.dll` and `ucrtbase.dll`, providing full support for `std::cout`, `std::cin`, `std::cerr`, `printf`, `wprintf`, `fgets`, pipe redirection, and interactive console REPLs. |
| **Object Lifecycle Management** | Global and static constructors invoked automatically at startup (`@llvm.global_ctors`), registered destructors executed in reverse order on exit (`atexit`), precise RAII deterministic scoping. |
| **Memory Allocation Subsystem** | Fully freestanding `malloc`, `free`, `realloc`, `calloc`, `operator new`, `operator delete`, aligned allocations, and high-performance bucketed arena allocators. |
| **Graphics & System Ecosystem** | Native Win32 window message pumps, GDI vector rendering, **DirectX 11 / DXGI hardware acceleration pipeline** (Dear ImGui), Windows multimedia audio (`winmm`), standard file streams (`std::fstream`), and filesystem paths (`std::filesystem`). |

---

### ⚠️ The 4 Real-World Architectural Boundaries & Best Practices:

Operating completely outside the operating system's native PE loader inevitably meets fundamental system contracts and host process co-existence boundaries:

#### 1. Module Identity & Filesystem Context Severance
* **The `GetModuleHandle(NULL)` and `GetModuleFileName` Host Path**:
  In loaderless execution, the operating system never assigns an independent `HMODULE` to the flat binary. Calling `GetModuleHandle(NULL)` or `GetModuleFileName(NULL, ...)` returns the **absolute path of the host process** (e.g., `explorer.exe` or the test loader).
  - **Best Practice**: Never rely on `GetModuleFileName(NULL)` to locate companion assets. Pass paths explicitly via `main` arguments, use absolute paths, or embed assets as inline byte arrays.
* **Current Working Directory (CWD) Reliance**:
  Relative paths (e.g., `fopen("config.ini", "r")`) resolve against the host process's active working directory.

#### 2. Execution Morphology & Library Boundaries
* **Single-Entry Payload Model vs. Shared Library (No EAT)**:
  MemLLVM generates a standalone execution payload with a single entry point (offset 0 jumping to `main` after self-rebasing). **It cannot act as a multi-export Dynamic Link Library (DLL)** exporting dozens of named symbols for external callers.
* **Strictly x64 Architecture Exclusivity**:
  The entire pipeline (PE parsing, DIR64 fixups, ROR13 API resolution, 12-byte `.pdata` tables, and x64 assembly stubs) is hardcoded for the **Windows x86_64 ABI**. It does not support 32-bit x86 (IA-32) or ARM64.

#### 3. Host Process Lifecycle Coupling
* **The `exit()` / `abort()` Host Termination Risk**:
  In a standalone EXE, calling `exit(code)` terminates normally. When running as an in-memory payload inside a host process, calling `exit()` calls `ExitProcess`, **instantly killing the entire host process**!
  - **Best Practice**: If the payload must return cleanly to the host, it must `return` from `main()` or invoke `ExitThread`. Avoid calling `exit()` in embedded scenarios.
* **Direct Command Line (`GetCommandLine`) Query Pollution**:
  `GetCommandLineA/W` returns the host process's creation command line. While MemLLVM's stub reconstructs `main(argc, argv)`, libraries bypassing `main` and querying the PEB directly will observe host parameters.

#### 4. Modern OS Security Mitigations
* **W^X and DEP Memory Protection**:
  Flat binaries require executable memory pages (allocated with `PAGE_EXECUTE_READWRITE` or switched from RW to RX via `VirtualProtect` before execution).
* **Control Flow Guard (CFG) & Arbitrary Code Guard (ACG)**:
  If the host process enforces strict Control Flow Guard (CFG), indirect calls into non-image dynamic memory trigger fast-fail `0xC0000409` unless registered via `SetProcessValidCallTargets`. If Arbitrary Code Guard (ACG) is enabled on the host, allocating executable memory dynamically is blocked by the Windows kernel.

---

## 🧪 Full Modern C++ Regression Test Suite (27/27 Tests)

MemLLVM maintains an exhaustive, continuously verified regression test suite located in [`tests/run_cpp_tests.bat`](tests/run_cpp_tests.bat). Every test undergoes the complete lifecycle: **Clang Compilation with MemLLVM Pass -> LLD Linking with LTL -> Flat Binary (.bin) Extraction -> In-Memory Execution via Python ctypes**:

| Test ID | Validated Standard Library Feature | Test File | Flat Binary Validation |
| :---: | :--- | :--- | :---: |
| **01** | STL `std::vector` Dynamic Container | `test_cpp_stl_basic.cpp` | ✅ Status 0 (Returns 600) |
| **02** | Global Objects & Dynamic Static Constructors | `test_cpp_global_obj.cpp` | ✅ Status 0 (Returns 1125) |
| **03** | Virtual Inheritance, Polymorphism & VTables | `test_cpp_polymorphism.cpp` | ✅ Status 0 (Returns 275) |
| **04** | Meyers Thread-Safe Local Statics (No TLS) | `test_cpp_thread_safe_static.cpp` | ✅ Status 0 (Returns 175) |
| **05** | Complex Structs, Pointer Math & Constexpr | `test_cpp_struct_ptr_const.cpp` | ✅ Status 0 (Returns 777) |
| **06** | Global Variables, `.data` & BSS Initialization | `test_cpp_globals_data_bss.cpp` | ✅ Status 0 (Returns 888) |
| **07** | `std::mutex` Multi-Thread Lock Synchronization | `test_cpp_mutex.cpp` | ✅ Status 0 (Returns 350) |
| **08** | `std::thread` Thread Creation & Joining | `test_cpp_thread.cpp` | ✅ Status 0 (Returns 400) |
| **09** | Emulated Thread-Local Storage (`thread_local`) | `test_cpp_thread_local.cpp` | ✅ Status 0 (Returns 130) |
| **10** | `std::filesystem` Directory & File Operations | `test_cpp_filesystem.cpp` | ✅ Status 0 (Returns 200) |
| **11** | `std::cout` Stream Formatting & Redirection | `test_cpp_iostream.cpp` | ✅ Status 0 (Returns 105) |
| **12** | `std::fstream` Disk File Reading & Writing | `test_cpp_fstream.cpp` | ✅ Status 0 (Returns 42) |
| **13** | `std::cin` Interactive Console Stream Input | `test_cpp_cin.cpp` | ✅ Status 0 |
| **14** | `std::regex` Regular Expression Matching | `test_cpp_regex.cpp` | ✅ Status 0 (Returns 88) |
| **15** | `std::async` & `std::future` Promise/Future | `test_cpp_async.cpp` | ✅ Status 0 (Returns 100) |
| **16** | C++20 Standard Modules (`import std;`) | `test_cpp_module_std.cpp` | ✅ Status 0 (Returns 77) |
| **17** | Kernel Fast Direct Stdio (`kprintf`) | `test_stdio_kprintf.cpp` | ✅ Status 0 |
| **18** | Multi-Translation-Unit Global Cross-Linking | `test_multi_tu_main.cpp` | ✅ Status 0 |
| **19** | Command-Line Arguments Forwarding (`argc/argv`) | `test_argv_ansi.cpp` / `_wide.cpp`| ✅ Status 0 |
| **20** | Full C++ Exception Handling & RAII Unwind | `test_cpp_exceptions.cpp` | ✅ Status 0 |
| **21** | Hardware Entropy (`std::random_device`) | `test_random.cpp` | ✅ Status 0 |
| **22** | C++20 Chrono Timezone Database (`current_zone`)| `test_tz.cpp` | ✅ Status 0 |
| **23** | C++17 Parallel Algorithms (`std::execution::par`)| `test_execution.cpp` | ✅ Status 0 |
| **24** | C++20 `std::format` & Floating-Point Charconv | `test_format.cpp` | ✅ Status 0 |
| **25** | C++20 `<semaphore>`, `<latch>`, `<barrier>`, wait | `test_sync.cpp` | ✅ Status 0 |
| **26** | C++23 Stacktrace (`std::stacktrace::current()`) | `test_stacktrace.cpp` | ✅ Status 0 |
| **27** | Named Locales (`std::locale("zh-CN")`) | `test_locale_named.cpp` | ✅ Status 0 |

> 🚀 **Run the complete test suite locally**: Execute `tests\run_cpp_tests.bat` to build, extract, and execute all 27 modern C++ flat binaries in ~30 seconds!

---

## 🕳️ Engineering Pitfalls & Battle-Tested Breakthroughs

Building MemLLVM required navigating subtle, undocumented Windows kernel behaviors and compiler edge-cases. Here is a record of the hardest challenges solved during development:

<details>
<summary><b>1. The Undocumented Windows <code>GetStdHandle</code> WindowFlags 0x400 / 0x200 Trap</b> (Click to expand)</summary>

#### The Trap:
When running CLI tools inside a GUI-based pipe loader, `SetStdHandle(STD_OUTPUT_HANDLE, hPipeWrite)` returned `TRUE`, yet subsequent calls to `GetStdHandle(STD_OUTPUT_HANDLE)` in the payload returned `0x(nil)` (NULL)! Native MSVC/UCRT binaries saw NULL, assumed the process was non-interactive, and dropped all output or terminated instantly.

#### The Discovery:
We disassembled Windows 11/10 `kernelbase!GetStdHandle`:
```x86asm
GetStdHandle:
    mov rax, gs:[0x60]                   ; rax = Current Process PEB
    mov rcx, [rax + 0x20]                ; rcx = ProcessParameters
    test dword ptr [rcx + 0xa4], 0x400   ; Test WindowFlags at offset 0xA4!
    jne return_null                      ; If bit 0x400 is set, FORCIBLY RETURN NULL!
    mov rax, [rcx + 0x28]                ; Otherwise return StandardOutput
    ret
```
When a process is launched by Windows Explorer or `ShellExecuteEx`, Windows sets bit `0x400` in `WindowFlags`. Standard calls to `SetStdHandle` only write to the handle slot without clearing `0x400`! `GetStdHandle` sees the flag and forcibly returns NULL.

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
<summary><b>3. The Clang Pass Plugin C++ ABI Mismatch (Heap Corruption <code>0xC0000374</code>)</b> (Click to expand)</summary>

#### The Trap:
Building an out-of-tree LLVM pass plugin against LLVM header files and loading it into an official LLVM release installer crashed immediately during pass registration with `STATUS_HEAP_CORRUPTION (0xC0000374)`.

#### The Discovery:
LLVM's C++ codebase makes extensive use of STL containers (`std::vector`, `llvm::StringRef`, `llvm::SmallVector`) without an ABI freeze across builds. Minor compiler flag differences (e.g. `/MT` vs `/MD`, debug iterators, MSVC version differences) corrupt memory when passing objects across DLL boundaries.

#### The Breakthrough:
We publish and commit a pinned, hermetic plugin-compatible compiler toolchain (`clang-plugin-toolchain.zip` in GitHub Releases and `release_artifacts/`) ensuring 100% binary compatibility on every developer machine and GitHub CI runner.
</details>

<details>
<summary><b>4. Thread-Safe Local Statics (Meyers Singleton) Invoking Invalid TLS</b> (Click to expand)</summary>

#### The Trap:
C++ guarantees thread-safe initialization of local static variables. MSVC and Clang implement this by emitting calls to `_Init_thread_epoch` and accessing `__tls_index` via `gs:[0x58]`. In a flat binary, this immediately triggered an access violation (`0xC0000005`).

#### The Breakthrough:
MemLLVM's `StlStubPass` detects these compiler-generated TLS checks at the IR level and rewrites them into atomic lock-free compare-and-swap operations on non-TLS global storage, completely eliminating the OS TLS dependency while preserving thread safety!
</details>

<details>
<summary><b>5. Windows x64 SEH Exception Unwinding & The Missing <code>.pdata</code> Pitfall</b> (Click to expand)</summary>

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
<summary><b>6. Undocumented Ordinal-Only (<code>[NONAME]</code>) Exports & Multi-Hop Forwarder Chains</b> (Click to expand)</summary>

#### The Trap:
Calling common Windows Shell or networking APIs (such as `SHAlloc`, `SHFree`, or `ConnectToConnectionPoint`) works normally in a standard executable, but when converted into flat shellcode, calls crash immediately with a null pointer (`0x00000000`) or linger as unresolved link-time imports.

#### The Discovery:
The PE specification allows DLLs to export functions purely by numeric index (`[NONAME]`), omitting function name strings from `AddressOfNames`. `shlwapi.dll` alone contains 551 ordinal-only exports, and `shell32.dll` contains 364. Standard ROR13 export table scanners return NULL. Furthermore, many ordinals are forwarded to other libraries (e.g. `SHUNIMPL.#429` or `NTDLL.RtlAllocateHeap`); naively jumping to the exported RVA executes ASCII forwarder string data instead of machine code!

#### The Breakthrough:
1. Implemented an $O(1)$ runtime ordinal indexer (`funcs[ordinal - Base]`) in the bootstrap resolver with multi-hop forwarder resolution supporting both string and `#ordinal` forwarded targets with cycle detection;
2. Pre-indexed all 2,729 Windows SDK ordinal exports across 80 system DLLs into a compile-time binary search table and added dynamic `.lib` archive header scanning for project-local dependencies;
3. Connected `AutoLoadLibraryPass`, `ApiResolvePass`, and `GlobalPointerPass` to transparently rewrite calls and rebase vtable/global pointer slots to `MemLLVM_GetApiAddressByOrdinal`.
</details>

<details>
<summary><b>7. Reconstructing Native PE Resource Subsystem (<code>.rsrc</code>) in Headless Flat Binaries</b> (Click to expand)</summary>

#### The Trap:
Windows developers extensively use `.rc` resource scripts to bundle icons, string tables (`STRINGTABLE`), dialog templates (`DIALOGEX`), menus, accelerators, and raw binary assets (`RCDATA`). In flat binaries without PE headers, calling `FindResource`, `LoadString`, or `DialogBoxParam` fails with NULL or triggers crashes.

#### The Discovery:
Win32 resource functions under `kernel32` / `user32` traverse the PE resource tree by interpreting `hModule` as the base address of a valid PE image and scanning DOS/NT headers. Flat binaries have no PE headers, so passing `GetModuleHandle(NULL)` reads the host process's resources, while passing the payload's own address returns `ERROR_RESOURCE_DATA_NOT_FOUND (1812)`. Furthermore, Clang generates `__imp_*` slot loads that normally resolve to `kernel32.lib` / `user32.lib`, re-introducing dangerous import dependencies into the flat binary.

#### The Breakthrough:
1. **Preserving and Packing `.rsrc`**: `extract_flat_bin.py` preserves the `.rsrc` section in the flat image and injects a `MemLLVM_ResourceInfo` descriptor that dynamically binds the resource base address and RVA offset at runtime startup.
2. **In-Memory 3-Level Resource Traversal Engine**: Implemented an in-memory `IMAGE_RESOURCE_DIRECTORY` parser in `ltl/src/crt_resources.cpp` supporting both integer IDs (`MAKEINTRESOURCE`) and Unicode name strings across all three directory levels (Type -> Name/ID -> Language).
3. **Comprehensive Win32 Resource API Shims**:
   - Navigation & Access: `FindResource(Ex)A/W`, `LoadResource`, `LockResource`, `SizeofResource`.
   - String Tables: `LoadStringA/W` (parsing 16-entry Pascal counted string blocks).
   - Dialogs & Menus: `DialogBoxParamA/W` & `CreateDialogParamA/W` (via indirect dialog templates), `LoadMenuA/W`, `LoadAcceleratorsA/W`.
   - Graphics & Icons: `LoadIconA/W`, `LoadCursorA/W`, `LoadBitmapA/W`.
   - Enumeration: `EnumResourceTypesW`, `EnumResourceNamesW`, `EnumResourceLanguagesW`.
   - Fallback: Transparently delegates to OS APIs when `hModule` points to external system DLLs (e.g. `IDI_APPLICATION`, `IDC_ARROW` in `user32.dll`).
4. **Compile-Time Import Interception**: `ApiResolvePass` marks all 26 resource APIs as static shims, rewriting `__imp_*` loads directly to `ltl.lib` functions in IR, completely eliminating OS import stubs.
</details>

---

## 🏆 Verified Showcase Projects (From `examples/`)

Every project in `examples/` compiles directly from source into a **standalone, self-relocating flat binary (`.bin`)** and is continuously compiled and verified on GitHub Actions CI:

```
examples/
├── box2d/       # Box2D 3.0 2D Physics Engine (45 files, real-time GDI rendering)
├── chip8/       # Full CHIP-8 CPU / Sound Virtual Machine (C++20, plays Pong/Tetris)
├── doom/        # Classic DOOM 3D Game Engine (83 files, software rasterizer, audio)
├── imgui/       # Dear ImGui v1.91+ (Hardware-accelerated DirectX 11, multi-windows)
├── sqlite/      # Official SQLite3 CLI Shell (Full interactive REPL, SQL engine)
├── winmine/     # Classic Win32 Minesweeper (Full PE resource tree, DialogBoxParam, LoadMenu, Bitmaps)
├── mimikatz/    # Large-scale Complex C Project (114 TUs, tentative definitions, subsystems)
└── mock_cli/    # Interactive CLI Diagnostic Stub (Testing stdio pipes & arguments)
```

| Showcase Project | Scale & Language | Flat Binary Size | Key Validated Capabilities |
| :--- | :--- | :--- | :--- |
| **[Classic Win32 Minesweeper](examples/winmine/)** | Pure C / Win32 (ReactOS Port) | **1.01 MB** | **Ultimate Native Resource Subsystem Showcase**: Bundles `.rsrc` directly into flat memory. Seamlessly parses `DialogBoxParamW` (Custom Game & Fastest Times modal dialogs), `LoadMenuW` menus, `LoadBitmapW` (smiley face, LED numbers, mine tiles), `LoadIconW`, `LoadAcceleratorsW`, and `LoadStringW` directly from memory without PE headers. |
| **[Official SQLite3 CLI](examples/sqlite/)** | C / SQLite 3.45 | **208 KB** | Full interactive SQL REPL, in-memory database creation, complex SQL queries, table formatting, pipe streaming. |
| **[Classic DOOM Engine](examples/doom/)** | Pure C (83 files) | **605 KB** | Full id Software DOOM engine, 3D software rasterization, keyboard controls, audio mixer, streaming IWAD data. |
| **[Dear ImGui v1.91+](examples/imgui/)** | C++17 Industrial UI | **813 KB** | Full immediate-mode GUI, multi-window docking, plots, widgets, hardware-accelerated **DirectX 11 / DXGI** pipeline. |
| **[Box2D 3.0 Engine](examples/box2d/)** | Pure C (45 files) | **545 KB** | Continuous rigid body physics, dynamic BVH trees, collision impulse solver, real-time Win32 GDI vector rendering. |
| **[CHIP-8 Virtual Machine](examples/chip8/)** | Modern C++20 | **30 KB** | 35-opcode CPU emulation, frame timers, audio buzzer, 64x32 monochrome display matrix running *Pong* and *Tetris*. |
| **[Mimikatz 2.2.0](examples/mimikatz/)** | Complex C (114 Translation Units) | **1.20 MB** | Massive real-world code base testing tentative definitions merging, dynamic subsystem resolution, multi-tier struct alignment, and dual-CRT stdio pipe redirection. |
| **[Mock CLI Diagnostic Stub](examples/mock_cli/)** | Minimal C Runtime | **4.6 KB** | Dedicated diagnostic tool testing full-duplex stdio pipes, command line argument forwarding, streaming output throughput, and graceful exit handling. |

> 💡 **Batch Compile All Examples Locally**: Run `python examples/build_all.py` from the repository root to compile and extract all 7 showcase binaries in ~15 seconds!

---

## 🚀 Quick Start Guide

### Path A: CLI Fast Track (Recommended)

No complex IDE required. Convert any C/C++ project into a standalone flat binary in 4 clean steps:

```bat
:: 1. Compile source into COFF object with the MemLLVM IR Pass plugin
clang-cl /c /O2 /GS- /GR- /clang:-fpass-plugin="%MEMLLVM_DIR%\bin\MemLLVM.dll" ^
  /clang:-fno-jump-tables /clang:-fno-builtin ^
  /I"%MEMLLVM_DIR%\include" /I"%MEMLLVM_DIR%\ltl\include" ^
  main.cpp /Fo:main.obj

:: 2. Link with the freestanding LTL runtime (no default CRT libraries)
lld-link /NODEFAULTLIB /ENTRY:main /OPT:REF /OPT:ICF /ALIGN:512 /MERGE:.rdata=.text ^
  /LIBPATH:"%MEMLLVM_DIR%\lib" /LIBPATH:"%MEMLLVM_DIR%\ltl" ^
  main.obj ltl.lib kernel32.lib user32.lib /OUT:main.exe

:: 3. Extract the position-independent flat binary (.bin)
python "%MEMLLVM_DIR%\Tools\extract_flat_bin.py" main.exe main.bin

:: 4. Execute and inspect immediately with the bundled loader
"%MEMLLVM_DIR%\Tools\scloader_cli.exe" main.bin
```

---

### Path B: Visual Studio Project (.vcxproj) Configuration

#### 1. Download & One-Click Environment Setup
1. Download `MemLLVM-SDK-windows-x64.zip` from the latest [GitHub Releases](https://github.com/Laster-dev/MemLLVM/releases).
2. Extract to any directory (e.g. `C:\MemLLVM`).
3. **Double-click `setup_env.bat`**:
   - Automatically sets `MEMLLVM_DIR` in your User Environment.
   - **Auto-Binds Bundled Hermetic Toolchain**: Sets `LLVMInstallDir` to the SDK's embedded `toolchain/` directory so Visual Studio ClangCL automatically invokes the exact binary-aligned `clang-cl.exe`, eliminating host Clang ABI mismatches and plugin loading errors!
   - Mounts `toolchain\bin`, `bin`, and `Tools` into your User `PATH`.
   - **Smart Path Migration**: Moving the SDK in the future? Just run `setup_env.bat` again in the new folder—it automatically purges old stale paths.
   - **One-Click In-Place Auto-Update**: The SDK comes with `update.bat`. Run it at any time to automatically fetch the latest release from GitHub and overwrite your local SDK in-place, then re-synchronize configurations.

#### 2. Enable Clang in Visual Studio
* **Option A (Recommended Built-in Component)**: Open Visual Studio Installer -> Modify -> Individual components -> Search `Clang` -> Check **C++ Clang Compiler for Windows** and **MSBuild support for LLVM toolset**.
* **Option B (VSIX Extension)**: Visual Studio Extensions -> Manage Extensions -> Search & Install **LLVM Compiler Toolchain**.

#### 3. Visual Studio Project (.vcxproj) Properties
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
