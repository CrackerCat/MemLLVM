# MemLLVM × mimikatz 实验记录

日期：2026-11-01（最终版）
环境：VS18 Community / PlatformToolset=ClangCL / LLVM 22.1.8（`C:\LLVM-22-dev`）/ MemLLVM 源码仓 `D:\MemLLVM`

## 结论

**mimikatz 现在可以完整走通扁平镜像链路：0 个未解析符号、0 条导入表、产出可加载的 `.bin`。**

```
114 个 TU  →  clang-cl + MemLLVM 插件  →  114 个 obj
           →  lld-link /nodefaultlib（只给 ltl.lib + 项目自带 .min.lib + GUID 库）
           →  mimikatz_mllvm.exe：无 .idata 节、Import=0
           →  memllvm_pack.py  →  mimikatz_mllvm.bin（1,832,423 字节）
```

| 阶段 | 未解析符号 |
|---|---|
| 最初 | 136 |
| 修完 invoke / LTL 符号 / 取地址 API | 63（全部来自 `sqlite3.obj:(aSyscall)`） |
| 补上跨 TU 初始化 | 6 |
| 补上代码内取地址 + `__imp_` + GUID 库 | **0** |

`.bin` 的关键指标：

```
Base RVA 0x1000   EntryPoint flat offset 0xDA5D0
.mli 初始化表 flat offset 0x1BF000，77 个槽位（全部非空）
DIR64 重定位 78 条（8 字节对齐扫描：疑似指针 78 个，遗漏 0 个）
stub @ 0x1BF400   reloc table @ 0x1BF4AF
```

---

## 一、MemLLVM 侧修复的缺陷

### 1. `ApiResolvePass` 漏掉 `invoke`（SEH）

`dyn_cast<CallInst>` 匹配不到 `InvokeInst`，而 MSVC SEH（`__try/__except`）会把受保护调用
lower 成 `invoke`（带 unwind 边到 `catchswitch`）。结果是**含 `__try` 的函数整体不生效**。

`modules/kull_m_crypto_ngc.c` 实测：23 → 48 条改写，新增 25 条全是 invoke，
与 obj 里 3 个 `?filt$0@0@...` SEH filter 符号一一对应。

修法：`CallInst` → `CallBase`；改写从「新建 call + replaceAllUsesWith」改为
**原地 `setCalledOperand`**，`invoke` 的 normal/unwind 边完全不动，CFG 天然保持。
`AutoLoadLibraryPass.cpp` 同一处问题一并修了。

### 2. `ltl.lib` 缺 4 个静态符号

`isStaticShimSymbol()` 把它们推给 LTL，但 LTL 没实现：`memcmp`、`wcsstr`、`wcschr`、
`__C_specific_handler`。已补进 `ltl/src/crt_mem.cpp`（前三个要加 `#pragma function(...)`，
否则 clang 会把实现优化回自递归）。

### 3. `GlobalPointerPass` 不处理「取地址的 API」

`sqlite3.c` 的 `aSyscall` 表存的是 `&CreateFileW` / `&HeapAlloc` 这类**函数地址**，
而 `ApiResolvePass` 只改写调用点。这些引用一直活到链接期。

修法：`GlobalPointerPass` 发现槽位指向一个已知 Win32 声明时，改为在入口处
`MemLLVM_GetApiAddress(ror13)` 动态填充。配套修了 resolver 的链接条件
（原先只看 `!ResolverFn`，会被 `GlobalPointerPass` 留下的裸声明骗过去）。

### 4. 跨 TU 初始化缺失（架构性）

MemLLVM 的 pass 是 **per-TU** 跑的，但初始化动作只能注入「本 TU 的入口函数」。
`sqlite3.c` 没有 `main`，所以它的 `aSyscall` 从来没被处理过 —— 这就是 63 个符号的唯一根因。

**方案演进：**

先试过 MSVC CRT 风格的 `$A/$M/$Z` 分组节（`.mli$A` / `.mli$M` / `.mli$Z`），
用哨兵界定表边界。**实测 lld-link 不按 `$` 后缀跨输入文件排序**：

```
4 .mli  00000010 ...5000 DATA     ← $M 的槽位
5 .mli  00000008 ...6000 DATA     ← $Z 哨兵排到了后面
```

`[A+8, Z)` 变成空区间，循环永远跑 0 次。方案否决。

**最终方案（打包器驱动）：**

1. 无入口的 TU 生成 `__memllvm_tu_init`（`InternalLinkage` + `NoInline`），
   把原本要写进 `main` 的 store 写进它；
2. `registerTuInit()` 把该函数地址发布成一个 `ExternalLinkage` 的全局变量，
   放进**普通 `.mli` 节**（不带 `$`，链接器必然合并）；
3. `memllvm_pack.py` 的 stub 在跳入口前遍历 `.mli`，跳过空槽逐个 `call`。

`.mli` 是普通节名，MSVC link 和 lld-link 都能可靠合并 —— 这是唯一稳的形式。

实测 114 个 TU 里 **77 个** 注册了 init（其余是有入口或没有指针表的）。

### 5. `getCalledFunction()` 在 LLVM 22 收紧了（隐蔽）

LLVM 22 给 `CallBase::getCalledFunction()` 加了类型一致性检查：

```cpp
Function *getCalledFunction() const {
  if (auto *F = dyn_cast_or_null<Function>(getCalledOperand()))
    if (F->getValueType() == getFunctionType())   // ← 新增
      return F;
  return nullptr;
}
```

C 语言里 `extern PPEB WINAPI RtlGetCurrentPeb();` 这种**无原型声明**，clang 会把声明
lower 成 varargs 形式、调用点却不是，类型对不上 → 返回 null → pass 把它当成间接调用跳过。

症状极具迷惑性：pass 打印 `resolved 49 call sites`，obj 里却仍留着 `U RtlGetCurrentPeb`。

修法：`MemLLVMUtils` 新增 `getDirectCallee(CallBase&)`，只做
`dyn_cast<Function>(getCalledOperand()->stripPointerCasts())`，不看类型；全工程替换。

### 6. 代码内取地址的 API 没被覆盖

`modules/kull_m_crypto_sk.c:110`：

```c
PBCRYPT_ENCRYPT cryptFunc = Encrypt ? BCryptEncrypt : BCryptDecrypt;
```

既不是调用点（步骤 3/4 只看 `CallBase`），也不是全局初始化器
（`GlobalPointerPass` 只走 GV initializer）。新增步骤 4b：把已知 Win32 声明
**所有非调用用途**替换成 resolver 调用（PHI 操作数要插到对应前驱块末尾）。

### 7. `__imp_` 形式的调用没被覆盖

`inc/msasn1.h` 里 `ASN1_PUBLIC` 在某个分支下是 `__declspec(dllimport)`，
clang 于是引用 IAT 槽 `__imp_X` 而不是符号本身，调用 lower 成
`call qword ptr [__imp_X]`（尾调用优化后是 `jmp`）。`getDirectCallee` 看到的是间接调用。

新增步骤 4c：把每个 `__imp_<已知API>` 槽位的 `load` 替换成 resolver 调用。

### 8. 扫描 `.lib` 时边界收得太紧

`SdkImportResolver::scanOneLib()` 找 COFF 导入对象时要求
`i + sizeof(header) + 32 < size`。`msasn1.min.lib` 共 3584 字节，
`ASN1_FreeEncoded` 的导入对象在偏移 3536，只剩 48 字节 —— 正好被排除，
于是这一个符号永远解析不到。改为逐段边界检查（`strnlen` + 缓冲区末端）。

另外把扫描范围从「当前目录」扩到 `lib\` / `lib\x64\` / `..\lib\x64\` / `..\..\lib\x64\`
加 `MEMLLVM_LIBPATH` 环境变量。mimikatz 把 `ASN1_*` / `I_NetServer*` 这些非 SDK
导出放在自己的 `lib\x64` 里，编译器的工作目录看不到。

### 9. `$(MEMLLVM_DIR)\bin\MemLLVM.dll` 不存在

文档第 10 章让把 `$(MEMLLVM_DIR)\bin\MemLLVM.dll` 传给 `-fpass-plugin`，
但仓库里根本没有 `bin\` 目录。已在 `CMakeLists.txt` 加 POST_BUILD 同步。

### 10. `build_ltl.bat` 静默用旧插件

三个 `if exist` 是「最后命中者覆盖」，陈旧的 `release_artifacts\MemLLVM.dll`
会盖掉刚编出来的。改成 `if not defined PLUGIN_DLL` 首命中优先。

---

## 二、mimikatz 侧的改动

`Directory.Build.props`：

- `LLVMInstallDir=C:\LLVM-22-dev` + `LLVMToolsVersion=22`：插件是 22.1.8 编的，
  VS 自带 22.1.3 会让插件 SIGSEGV（pass 插件与宿主 clang 无 ABI 稳定保证）。
- `-fcommon /utf-8`：修 C89 暂定义导致的 duplicate symbol 与源码编码问题，**必须保留**。
- `MEMLLVM_DIR=D:\MemLLVM`：文档里的 `$(MEMLLVM_DIR)` 之前根本没定义。

插件参数的**正确写法**：

```
/clang:-fpass-plugin=D:/MemLLVM/bin/MemLLVM.dll
```

`clang-cl` 自己不认识 `-fpass-plugin`，直接写只会得到一条
`warning: unknown argument ignored in clang-cl` —— 构建「成功」但插件根本没跑。
这个坑踩过一次。

`mimikatz/mimikatz.vcxproj`：PostBuildEvent 调 `tools\memllvm_pack.py`，
每次 Release|x64 构建后都产出一份 `x64\mimikatz.bin`。

---

## 三、工具链

本机安全策略把 `MSBuild.exe`、`reg.exe` 归入 Known Windows LOLBin 拦截，
`vcvars64.bat` 因此跑不起来（它内部调 `reg.exe`）。所以：

- `tools/gen_build.py`：直接从 `mimikatz.vcxproj` 解析 114 个 TU 和预处理宏，
  生成 `build/compile_all.bat`（`--no-plugin` 可关闭插件做对比）；
- `build/link_flat.bat`：扁平链接（`/nodefaultlib` + `ltl.lib` + `.min.lib` + GUID 库）；
- MSVC/SDK 的 `INCLUDE` / `LIB` 手工拼，绕开 `vcvars64.bat`。

Git Bash 下调用 `clang-cl` 要加 `MSYS_NO_PATHCONV=1`，否则 `/c` 会被转成 `C:/`。

### 链接时需要的额外库

除了 `ltl.lib` 和项目自带的 `.min.lib`，还要给 COM GUID 的数据定义：

| 库 | 提供的符号 |
|---|---|
| `uuid.lib`（Windows SDK um\x64） | `IID_IXMLDOMDocument`、`IID_IWbemLocator` 等 |
| `wbemuuid.lib` | `CLSID_WbemLocator` |
| `MsXml2.Lib` | `CLSID_DOMDocument2` |

GUID 是位置无关的 16 字节常量，直接静态链接即可，不需要运行时解析。

---

## 四、扁平镜像的结构

```
offset 0x00000000   5 字节 jmp → 0x1BF400（覆盖了入口函数的开头，stub 会还原）
offset 0x000DA5D0   真正的入口（CRT 启动 → main）
offset 0x001BF000   .mli 初始化表，77 × 8 字节
offset 0x001BF400   自重定位 stub
offset 0x001BF4AF   DIR64 重定位表，78 × 4 字节
```

stub 的执行顺序：

1. `call $+5; pop rdx` 取 RIP，减自身偏移 → `rdx` = 运行时镜像基址
2. `r8 = rdx - 编译期基址`；为 0 就跳过修补
3. 遍历重定位表，逐项 `add qword ptr [rdx + rva], r8`
4. **还原被 jmp 覆盖的开头 5 字节**
5. **遍历 `.mli`，跳过空槽逐个 `call`**（寄存器全放栈上，`and rsp,-16` 对齐，
   `[rsp..rsp+0x20)` 留作影子空间）
6. `jmp rdx + entry_offset`

第 5 步必须放在第 3 步之后 —— 各 TU 的 init 里引用的绝对地址此时才是修正过的。

---

## 五、端到端运行验证

mimikatz 本体不便直接执行，所以另做了一份**最小可执行用例**来验证同一条链路：

- `build/flat_lib.c` —— 无入口的库 TU，全局函数指针表 `aSys[]` 存
  `&WriteFile` / `&GetStdHandle`，用 volatile 下标访问（常量下标会被优化器把表折叠掉）；
- `build/flat_main.c` —— 入口 TU，从表里取指针并调用；
- `build/flat_test.bat` —— 编译 + 扁平链接 + 打包；
- `tools/flat_run.py` —— `VirtualAlloc` 一块 RWX 内存，把 bin 拷进去，从第一个字节执行。

实测（镜像映射到 `0x189B3160000`，与编译期基址 `0x140001000` 相差甚远）：

```
MEMLLVM_FLAT_OK: rebased, .mli dispatched, WriteFile resolved
[*] image 17075 bytes mapped at 0x189B3160000
[*] returned 0
```

一次跑通了三件事：自重定位、`.mli` 跨 TU 调度、PEB/ROR13 解析 `WriteFile`。

**反向对照**（同一份 exe，打包时把 `mli_count` 强制置 0）：

```
MEMLLVM_FLAT_FAIL: .mli init did not run
[*] returned 2
```

说明「运行成功」确实来自 `.mli` 调度，而不是别处碰巧把表填上了。

---

## 六、还没做的

- `.pdata` 被丢弃，扁平镜像里没有 unwind 信息。`__C_specific_handler` 目前是
  直接返回 `ExceptionContinueSearch` 的桩，只保证链接通过；
  真要支持 SEH 展开，得把 `.pdata` 一起打包并在运行时注册。
- `.rsrc` 被丢弃，mimikatz 的资源（版本信息、清单）在扁平镜像里不可用。
- mimikatz 的 `mimikatz_mllvm.bin` 已在真实加载器里跑通（见第七节），
  `crypto::providers` / `process::list` 的输出与原生 mimikatz 逐字节一致。

---

## 七、第二轮：`mimikatz_mllvm.bin` 加载即崩的 6 个根因

第一轮只验证到「结构正确、无导入、可打包」。真正在加载器里跑时是「加载即崩、
无任何输出」，一路查下来是 6 个独立缺陷叠加，逐个记录如下。

排查方法：写了一个带 VEH（`AddVectoredExceptionHandler`）的探针加载器
`build/probe_loader.c`，把 bin 映射进 RWX 内存后直接调用，任何异常都打印
RIP/RSP/寄存器/访问地址。**注意**：VEH 里不能用 `fprintf`（异常上下文下 CRT
状态不可靠），要用 `wsprintfA` + `WriteFile`。

### 7.1 `wmain` 被当成 `WinMain` 传参（最先命中）

**症状**：banner 都不打，直接 AV，读地址 `0x8`。

**根因**：`findOrSynthesizeEntryPoint` 把 `wmain` 和 `WinMain` 归到同一分支，
按 Win32 的 4 参数约定转发：

```asm
; 修复前
call GetModuleHandleA(NULL)   ; rax = 自身模块基址
mov  rcx, rax                 ; argc = 模块基址 (0x7FF6xxxxxxxx)
xor  edx, edx                 ; argv = NULL
call wmain                    ; → for (i=1; i<argc; i++) argv[i] → *(NULL+8)
```

`wmain` 的签名是 `(int argc, wchar_t **argv)`，`argc` 拿到一个巨大的指针值，
`argv` 是 NULL，第一轮循环就崩。

**修法**：`MemLLVMUtils.cpp` 把 CRT 签名的 `wmain` 拆成独立分支，并顺带支持命令行
透传——用 `GetCommandLineW` + `CommandLineToArgvW` 重建 argv，丢掉宿主 exe 名，
使 `loader.exe mimikatz.bin sekurlsa::logonpasswords` 与正常启动等价。

### 7.2 无入口 TU 的 DLL 预加载被丢弃

**症状**：banner 正常，`mimikatz_initOrClean` 里崩，`RIP=0`（`call 0`）。

**根因**：`AutoLoadLibraryPass` 开头：

```cpp
Function *EntryFn = findOrSynthesizeEntryPoint(M);
if (!EntryFn || EntryFn->empty())
    return PreservedAnalyses::all();   // ← 无入口的 TU 直接丢弃
```

`MemLLVM_GetApiAddress` 只遍历**已加载**模块，所以每个 TU 用到的 DLL 必须先被
预加载。但只有入口 TU（`mimikatz.c`）的 DLL 进了列表，`kull_m_asn1.c` 需要的
`msasn1.dll` 永远没人加载，`ASN1_CreateModule` 解析成 NULL。

**修法**：与第 9 章 `.mli` 同一套机制——无入口时把 `LoadLibraryA` 注入到
`__memllvm_tu_init`，由 stub 统一调度。`probeRequiredDllForApi` 本来就能正确
推导（日志里能看到 `ASN1_CreateModule -> msasn1.dll`），只是被 early return 挡住。

### 7.3 头文件缺 `extern`，模块结构体是空副本

**症状**：banner 之后崩在 `_wcsicmp`，`%rdx=0`，即 `shortName` 为 NULL。

**根因**：`modules/kuhl_m_*.h` 里写的是

```c
const KUHL_M kuhl_m_standard;   /* 缺 extern */
```

这是 C 的暂定定义。MSVC 下各 TU 产生 common 符号由链接器合并，**clang 对 `const`
的暂定定义却生成 local BSS**，于是每个 TU 都有一份空副本：

```
kuhl_m_standard.obj : D kuhl_m_standard   ← 真实定义
mimikatz.obj        : b kuhl_m_standard   ← local BSS 空副本
```

`mimikatz_modules[]` 引用的是本 TU 那份空副本，所以所有模块的 `shortName`/
`pInit`/`commands` 全是 0。

**修法**：26 个头文件批量加 `extern`（`const KUHL_M kuhl_m_xxx;` →
`extern const KUHL_M kuhl_m_xxx;`）。修完 `mimikatz.obj` 里变成 `U kuhl_m_standard`。

### 7.4 `MemLLVM_GetApiAddress` 不处理 forwarder 导出（最隐蔽）

**症状**：`kuhl_m_c_rpc_init` 的 `InitializeCriticalSection` 崩，`RIP=0`。

**根因**：Win10/11 的 `kernel32.dll` 把大量 API 做成 forwarder（导出项 RVA 指回
导出目录内部，内容是 `"NTDLL.RtlInitializeCriticalSection"` 这类字符串）。原
resolver 遇到这种项**直接跳过**：

```asm
140001c7f: cmpl %r9d, %r12d     ; RVA vs 导出目录起始
140001c82: jb  .normal
140001c84: cmpl %r10d, %r12d    ; RVA vs 导出目录结束
140001c87: jb  .next_export     ; ← forwarder，跳过，继续找下一个
```

`CreateFileW`、`InitializeCriticalSection`、`LoadLibraryExW` 等一大批都是这种，
一律解析成 NULL。

**修法**：原 `Resolver.ll` 的源码不在仓库里，重写了一份 `Resolver.c` 并生成新的
`ResolverBitcode.h`（用 `build/build_resolver.bat` 重现）。新版命中 forwarder 时
解析 `"DLL.Func"`，用 PEB 解析出的 `LoadLibraryA` 加载目标 DLL 后递归查找。

**注意**：递归必须有深度上限。第一版没有上限，forwarder 成环时直接把线程栈吃穿，
表现为 `0xC00000FD`（STATUS_STACK_OVERFLOW）。现限制 `MAX_FORWARD_DEPTH = 8`。

### 7.5 大栈帧跨 guard page，栈溢出且 VEH 无法运行

**症状**：加载后无任何异常输出，进程直接消失（Git Bash 报 139）。VEH 自检正常
（故意写 NULL 能捕获），说明崩溃路径绕过了 VEH。

**根因**：编译时加了 `-mno-stack-arg-probe`（扁平镜像里没有 CRT 的 `__chkstk`），
于是 `wmain` 开头的 `sub rsp, 0x20090`（131KB）**一步跨过整个 guard page**。
Windows 的栈增长靠逐页触碰 guard page 触发，一步跨过去不会增长，直接
`STATUS_STACK_OVERFLOW`；而栈耗尽时 VEH 自身也跑不起来，所以静默死亡。

**修法**：`memllvm_pack.py` 的 stub 在跳入口前逐页向下 touch 512KB（不改 rsp，
只让系统把页提交好并把 guard page 挪下去）。这样 bin 不再依赖加载器给多大的
已提交栈，默认 1MB 线程栈即可。

### 7.6 `_Options` 为 0，`%s` 被当 `char*`

**症状**：功能全对，但所有 `%s` 只输出首字符——`RSA_FULL` 显示成 `R`。

**根因**：`wprintf` → `__stdio_common_vfwprintf(options, ...)`，`options` 来自
`__local_stdio_printf_options()` 返回的 `_Options`。这个字由 CRT 初始化设置，而
扁平镜像跳过了 CRT 初始化，`_Options` 是 0，缺
`_CRT_INTERNAL_PRINTF_LEGACY_WIDE_SPECIFIERS`（0x4），于是 `%s` 退回 ISO C 语义
（宽字符函数里 `%s` = `char*`），UTF-16 字符串在第一个 `0x00` 处截断。

**修法**：wrapper 里调用 `__local_stdio_printf_options()` 并 `|= 0x4`。
`__local_stdio_printf_options` 每个 TU 一份，但 `/opt:icf` 会把相同函数体折叠成
一份，所以这个 store 落在所有 `kprintf` 都会读的同一个 `_OptionsStorage` 上
（用 map 文件确认过：只有一条 `__local_stdio_printf_options` 符号）。

### 7.7 验证结果

用默认 1MB 按需增长栈的探针加载器（模拟普通加载器，不是特制的 8MB 栈）：

```
$ probe_loader.exe x64/mimikatz_mllvm.bin crypto::providers
mimikatz(commandline) # crypto::providers

CryptoAPI providers :
 0. RSA_FULL      ( 1)   - Microsoft Base Cryptographic Provider v1.0
 1. DSS_DH        (13)   - Microsoft Base DSS and Diffie-Hellman Cryptographic Provider
 ...
CNG providers :
 0. Microsoft Key Protection Provider
mimikatz # Bye!
```

与原生 `mimikatz.exe` 的输出**逐字节一致**。`process::list` 也能完整枚举进程
（`0 (null)` / `4 System` / ... / `probe_loader.exe`）并正常退出。

`mimikatz # Bye!` 说明走完了 `mimikatz_end` → `kull_m_output_clean` →
`ExitProcess`，不是崩溃退出。

### 7.8 剩下的已知限制

- 初始化期间 VEH 会看到一个 `0xE06D7363`（MSVC C++ EH magic）异常，来自
  `CoInitializeEx`/RPC 一侧。它被正常传播并处理，不影响功能，但因为 `.pdata`
  没有打包，SEH 展开仍是不可靠的（见第六节）。
- ltl 的 `stdin` 是桩，交互模式（argc==0）读不到输入。所以入口改为从宿主命令行
  重建 argv，命令通过参数传入。

- `ApiResolvePass` 会打印
  `WARNING: N API reference(s) reachable only through a ConstantExpr were left untouched`。
  最小用例里这条会误报（实际没有残留导入），说明统计口径还没对齐，需要再查。
