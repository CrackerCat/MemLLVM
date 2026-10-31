这份技术需求设计文档根据你之前讨论的核心目标（构建自包含、无外部段依赖、纯内存可执行模块的 LLVM 编译器插件）整理而成。你可以直接使用此文档作为项目 README、技术白皮书或架构开发指南。

---

# 项目技术开发文档：MemLLVM (In-Memory PIC LLVM Plugin)

## 1. 项目概述 (Overview)

### 1.1 项目背景

在底层系统研究与特定内存执行场景中，编写的代码通常要求具备绝对位置无关（Position-Independent Code, PIC）**和**完全自包含（Self-Contained）特性。传统 C 编译器（如 MSVC、标准 GCC/Clang）编译出的二进制依赖操作系统的标准加载机制（PE Loader），会生成独立的只读数据段（`.rdata`）存放常量字符串，并通过导入表（IAT / `.idata`）绑定外部 Windows API。这使得生成的机器码无法脱离 PE 结构直接作为扁平字节流（Flat Binary / Blob）在任意内存地址自举运行。

### 1.2 核心目标

本项目旨在实现一个轻量级的 **LLVM New Pass Manager 插件（Pass Plugin）**，深度挂载于 Windows 环境下的 `clang-cl` 工具链。该插件在 LLVM 中间表示（IR）层面对编译行为进行干预与重构，无需修改 LLVM 源码，即可产出完全自包含、零只读数据段依赖、API 动态自解析的纯内存可执行代码。

---

## 2. 核心功能需求 (Functional Requirements)

### 2.1 字符串常量强制栈展开 (Stack String Transform)

* **需求描述**：消除模块对 `.rdata` 数据段的依赖，防止常量字符串被放入全局只读内存。
* **行为规范**：
1. 扫描当前 LLVM Module 中包含字符串常量的全局变量（`GlobalVariable`）。
2. 识别引用该常量的具体函数与基本块。
3. 在函数入口处（`entry` 块）通过 `alloca` 指令动态分配局部栈数组。
4. 利用逐字节（`i8`）或 64 位立即数（`i64`）赋值指令（`StoreInst`），将字符串内容直接写到函数栈帧上。
5. 将原指令中指向全局常量的指针替换为指向该局部栈变量的指针。
6. 从 Module 中安全剔除原始全局变量。



### 2.2 外部 Windows API 动态自解析 (Dynamic API Resolving)

* **需求描述**：消除生成代码对 PE 导入表（IAT / `.idata`）的硬编码依赖。
* **行为规范**：
1. 遍历并拦截未在当前模块内部定义的外部函数调用指令（`CallInst`，如 `VirtualAlloc`、`MessageBoxA` 等）。
2. 编译期计算目标 DLL 名称与函数符号的哈希值（如 ROR13 或 MurmurHash）。
3. 将静态直接调用转换为间接调用桩（Stub）：
* 运行时读取 TEB/PEB（x64 下通过 GS 寄存器寻址）。
* 遍历 `InMemoryOrderModuleList` 识别已加载模块。
* 解析目标模块导出表（EAT）并匹配函数名哈希。


4. 通过解析得到的真实函数指针发起 `call <reg>` 间接调用。



### 2.3 内存模块环境合规性保障 (Freestanding & ABI Compliance)

* **需求描述**：杜绝隐式外部符号引入，满足 Windows x64 ABI 约束。
* **行为规范**：
* 配合编译参数禁用 CRT 自动插桩（如禁止生成 `__chkstk` 栈探针，禁用内置的 `memcpy`/`memset`）。
* 严格保证 x64 调用约定的 16 字节栈对齐及 32 字节预留栈空间（Shadow Space）。



---

## 3. 技术架构与选型 (Architecture & Technology Stack)

| 模块 | 选型 | 说明 |
| --- | --- | --- |
| **开发语言** | C++17 / C++20 | 适配现代 LLVM API |
| **构建体系** | CMake + Visual Studio (MSVC / Clang) | 生成独立的 `.dll` 动态链接库 |
| **底层框架** | LLVM New Pass Manager | 使用官方插件体系，不重新编译 LLVM 源码 |
| **宿主编译器** | `clang-cl` (LLVM 官方发行包) | 负责前端解析与代码生成，集成至 Visual Studio |
| **调试与验证工具** | Visual Studio 附加调试器, `llvm-objdump`, `x64dbg` | 逆向检查提取出的 `.text` 机器码段 |

---

## 4. 模块详细设计 (Detailed Module Design)

### 4.1 插件架构流程

```
源代码 (.c) 
    │
    ▼
[ clang-cl 前端解析 ]  (生成初始 LLVM IR)
    │
    ▼
┌────────────────────────────────────────────────────────┐
│  MemLLVM Plugin (.dll)                                 │
│                                                        │
│  ┌───────────────────────┐   ┌───────────────────────┐ │
│  │ 1. StackStringPass    │──▶│ 2. ApiResolvePass     │ │
│  │ (全局变量 -> 栈内联)   │   │ (外部调用 -> PEB寻址) │ │
│  └───────────────────────┘   └───────────────────────┘ │
└────────────────────────────────────────────────────────┘
    │
    ▼
[ LLVM 后端代码生成 (llc) ]
    │
    ▼
提取 .text 节区 ──▶ 纯内存自包含机器码 (Raw Flat Binary)

```

### 4.2 插件导出接口设计

插件需严格导出 C 符号供 Clang 加载器寻址：

```cpp
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION,
        "MemLLVM",
        LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            // 注册到 PipelineParsing 或早期优化扩展点
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM, ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "mem-llvm") {
                        MPM.addPass(StackStringPass());
                        MPM.addPass(ApiResolvePass());
                        return true;
                    }
                    return false;
                });
        }
    };
}

```

---

### 4.3 核心技术痛点与深度解决对策

#### 痛点一：外部加载断层（目标进程未加载特定 DLL）与自动化推导
* **深层危机**：
  宿主进程可能未预先加载特定子系统库（如 `user32.dll`、`gdi32.dll`、`d3d11.dll`、`bcrypt.dll`、`wininet.dll` 等），若直接遍历 PEB 搜索已加载模块列表，会返回 NULL 导致内存执行崩溃。如果依赖人工手动在源码开头写 `LoadLibraryA`，或者用固定硬编码数组盲测，极易遗漏冷门 API 且会注入冗余 DLL。
* **解决对策（AutoLoadLibraryPass：函数 -> .lib -> DLL 架构推导）**：
  MemLLVM 正式落地了权威的 `函数符号 -> SDK 导入库 (.lib) -> 目标 DLL` 自动化推导管线：
  1. **SDK Import Object Header 全量解析**：深入 Windows SDK COFF 导入库结构，直接提取 `IMPORT_OBJECT_HEADER` 中的 `SymbolName` 与 `DLLName` 对应关系，消除任何人工猜想或字典硬编码。
  2. **内置嵌入式二进制导入索引 (SdkImportIndex)**：编译期集成 207 KB 高效导入索引（收录 467 个 Windows SDK 核心库，覆盖 32,840 个 API 与 484 个 DLL）。通过 ROR13 哈希与 `std::lower_bound` 仅需约 15 次比较（50 纳秒级）即可瞬时精准定准 API 来源 DLL。
  3. **规范子系统优先判定 (Canonical Preference)**：遇到同时存在于 API-Set（如 `api-ms-win-...`）与原生子系统库的符号，自动优先提升绑定为标准子系统 DLL（如 `user32.dll`、`gdi32.dll`、`ole32.dll`），避免 ApiSet 转发兼容性断层。
  4. **动态工作区库检索回退 (Workspace Fallback)**：当代码引用用户自定义第三方库时，引擎自动动态扫描当前目录下的所有 `*.lib` 文件并即时解析其 COFF 导出头，动态录入缓存。
  5. **自动化入点插桩**：在入口 `main()` 最顶端精准注入最少必要集的 `LoadLibraryA("<dll>")`，并自动交由后续 Pass 进行栈字符串内联与 PEB 动态寻址。**源码 0 样板代码，0 冗余加载！**

#### 痛点二：LLVM 优化器的叛变（偷偷生成外部 memcpy 调用）
* **深层危机**：
  当源码中进行大结构体赋值或大数组初始化清空时，LLVM 后端优化器（如 `MemCpyOpt` / `ISel`）会“自作聪明”地把循环或连续展开的 store 操作优化掉，自动生成一条类似 `call memcpy` 或 `call memset` 的指令。在脱离标准 C 运行库（CRT）的环境中，这会导致生成代码中留下一处无法寻址的外部 CRT 引用，导致纯内存执行时崩溃。
* **解决对策**：
  1. 在控制编译与代码生成时，强制加入优化器参数 `-fno-builtin`。
  2. 在编译器代码生成器中，显式向 `TheModule` 注入一组完全自实现的、名称经过特征抹除的底层自包含数据搬运函数（例如 `void _internal_move(byte* dst, byte* src, size_t size)`），并强行把 LLVM 的内置拷贝内联展开（Inline Everywhere），彻底切断代码生成器向外部 CRT 求助的所有可能。

---

### 4.4 Windows API 的“一键透明化动态解析”设计与实现

#### 1. C++ 编译期哈希计算（ROR13 算法实现）

```cpp
constexpr uint32_t calculateHash(const char* str) {
    uint32_t hash = 0;
    while (*str) {
        hash = (hash >> 13) | (hash << (32 - 13)); // 循环右移 13 位
        hash += *str;
        str++;
    }
    return hash;
}
```

#### 2. IR 级别透明化调用重写与桩生成

```cpp
llvm::Value* generateTransparentApiCall(llvm::IRBuilder<>& builder, llvm::Module* module, std::string apiName, std::vector<llvm::Value*> args) {
    uint32_t apiHash = calculateHash(apiName.c_str());

    // 1. 调用内置的万能 PEB 查找函数
    llvm::Function* lookupFn = module->getFunction("GetApiAddressByHash");
    llvm::Value* rawPtr = builder.CreateCall(lookupFn, { builder.getInt32(apiHash) });

    // 2. 将返回的裸指针 (i8*) 强转为当前 API 的真实函数签名指针类型
    llvm::FunctionType* apiSignature = getTargetApiSignature(apiName); 
    llvm::Value* typedFuncPtr = builder.CreateBitCast(rawPtr, apiSignature->getPointerTo());

    // 3. 发起间接 call
    return builder.CreateCall(apiSignature, typedFuncPtr, args);
}
```

---

### 4.5 编译开关与环境合规性保障 (Freestanding Compiler Flags)

为了彻底消除编译器隐式注入的外部符号及异常元数据，MemLLVM 要求配合一套严格的底层独立环境编译参数矩阵：

| 编译参数 (Clang/Clang-cl) | 解决的核心隐患与底层原理 |
| :--- | :--- |
| **`-ffreestanding` / `-fno-builtin`** | 杜绝隐式外部 CRT 依赖。严禁编译器在循环展开或大内存操作时代替开发者生成 `call memcpy` 或 `call memset`。 |
| **`-mno-stack-arg-probe`** (或 MSVC `/Gs9999999`) | 禁用栈探针。Windows x64 下局部栈变量超过 4KB（一页）时默认会调用 `__chkstk`，开启此项彻底消除该外部符号。 |
| **`-fno-stack-protector`** (或 MSVC `/GS-`) | 关闭栈金丝雀（Stack Canary）防护，彻底消除对 `__security_cookie` 和 `__security_check_cookie` 的外部符号引用。 |
| **`-fno-jump-tables`** | 禁止 `switch-case` 语句生成基于绝对地址重定位的跳转表，强制后端降级为纯条件分支树（`cmp`/`jmp`），防止产生 `.rdata` 残留。 |
| **`-fno-exceptions` / `-fno-rtti`** | 彻底移除 C++ 异常与运行时类型识别机制，防止代码生成器生成 `.pdata`（运行时函数表）和 `.xdata`（异常展开数据）节区。 |
| **`-fPIC`** | 强制所有内部函数调用与局部引用均使用 RIP 相对寻址（RIP-relative addressing），保障二进制的绝对位置无关性。 |

---

### 4.6 入口点与函数排布保障（确保起始偏移为 0x00）

#### 潜在陷阱：函数无序排布
当外部宿主程序（Host Loader）将提取出的裸机器码（Flat Binary）注入内存执行时，标准调用方式通常为：
```c
((void(*)())allocated_buffer)();
```
这意味着：**主入口函数（`main` 或指定 Entry）必须严格位于最终二进制的最前端（偏移量 `0x00` 处）**。
然而，C 编译器在多函数编译时，函数的实际布局顺序由内部遍历或优化器支配，并不保证源码中最先声明的函数一定排在 `.text` 节区首部。若辅助解析桩被排在 `0x00`，直接执行将导致流程严重混乱。

#### 解决对策：节区排序机制（Section Ordering）
利用 PE/COFF 链接器对带有 `$` 符号节区名称的字典序合并规则：
- 插件自动或显式为入口函数打上属性：`__attribute__((section(".text$00_entry")))`。
- 辅助解析桩自动归类至：`__attribute__((section(".text$zz_resolver")))`。
- 链接合并时，`.text$00_entry` 严格排布于 `0x0000` 偏移处，绝对保证入口点的第一指令命中率。

---

### 4.7 模块自动化纯度与重定位检查机制 (Purity & Relocation Check)

生成目标文件或扁平二进制后，在执行前必须建立严格的自动化纯度检查管线：

1. **节区纯度检查 (Section Purity)**：
   运行 `llvm-readobj --sections module.obj` 或 `llvm-objdump -h module.obj`：
   - 自包含内存模块在理想状态下**仅允许包含 `.text` 代码段**。
   - 严禁存在 `.rdata`（残留字符串/常量）、`.data`（全局变量）、`.bss`（未初始化全局量）或 `.pdata`/`.xdata`（异常表）。
2. **重定位项检查 (Relocations Check)**：
   运行 `llvm-readobj -r module.obj`：
   - 检查外部符号重定位列表。在自包含模块中，所有外部 API 均已转为 PEB 动态间接调用，不得包含任何对外部未定义符号的导入重定位（如 `__imp_*`）。

---

### 4.8 纯 C 开发规范与全局变量支持方案 (Pure C & Global Variable Merging)

#### 1. 严格禁止 C++ 代码，强制纯 C 规范
在纯内存扁平二进制（`.bin`）开发中，全面禁止使用 C++ 语法特性，强制采用纯 C 规范：
- **彻底杜绝虚表重定位陷阱**：C++ 虚函数表（`vftable`）在生成时内嵌死绝对虚拟地址（Absolute VA），无重定位表时调用虚函数必定崩溃。
- **杜绝 CRT 静态初始化陷阱**：C++ 全局静态对象依赖 `.CRT$XCU` 遍历初始化，裸二进制从 `0x00` 起始无 CRT 启动器，导致构造函数不执行。
- **杜绝异常与 RTTI 残留**：彻底根除 `.pdata`/`.xdata` 及 `type_info` 外部依赖。

#### 2. 全局变量解决方案：节区熔合技术 (Section Merging)
为了在纯 C 裸机器码中原生支持读写全局变量（如 `int g_counter = 100;`、`char g_buf[256];`）：
- **底层原理**：
  在 x64 架构下，Clang 为所有全局变量生成基于 `RIP` 的相对寻址指令（`mov reg, [rip + offset]`）。只要全局数据与代码在同一个内存页连续排布，相对偏移量就是绝对固定的！
- **链接器指令配置**：
  在 `lld-link` 时传入节区熔合参数：
  ```cmd
  /merge:.data=.text /merge:.bss=.text
  ```
- **构建结果与保障**：
  1. 链接器将 `.data`（已初始化全局数据）与 `.bss`（未初始化全局内存）直接紧密贴合在 `.text` 代码末尾，合并为单一的可读、可写、可执行 `.text` 节区。
  2. `llvm-objcopy --dump-section .text=module.bin` 抽取时，一次性将代码与完整的全局变量区域完整抽取入单一 `.bin`。
  3. 运行时无论 `VirtualAlloc` 将该 `.bin` 分配在何处，`[rip + offset]` 均 100% 精准指向对应全局变量！

#### 3. 全局静态写死指针的编译器透明化解决 (GlobalPointerPass)
针对开发者习惯书写的全局静态指针（如 `int *g_ptr = &g_val;`）：
- **底层难题**：在传统编译期，`&g_val` 会迫使链接器在全局数据区写入编译期固定的绝对虚拟地址（`IMAGE_REL_AMD64_ADDR64`）。脱离 PE 重定位器后，解引用 `*g_ptr` 将访问无效基址崩溃。
- **MemLLVM 解决方案 (GlobalPointerPass)**：
  1. 插件在 IR 阶段遍历检测到所有静态初始化指向全局变量或函数的指针；
  2. 自动将其编译期静态初始化值替换为 `null`，**彻底清零目标文件中的绝对地址重定位项（RelocationCount = 0）**；
  3. 在主入口函数 `main()` 的最前面自动注入动态重定位指令（`store ptr @g_val, ptr @g_ptr`）。该指令由后端编译为 `lea rax, [rip + offset]` 搭配 `mov [rip + offset], rax`；
  4. 二进制在任意内存基址被执行的第一条指令瞬间，通过 RIP 相对寻址计算出真实的运行时绝对内存地址并完成自我初始化；
  5. **开发者无需更改任何 C 代码习惯，直接编写 `int *g_ptr = &g_val;` 即可在纯内存 .bin 中无痛运行！**

---

## 5. 工程开发排期与路线图 (Roadmap)

### 阶段一：插件基础骨架与环境搭建 (Week 1) [已完成]

* [x] 下载配置 LLVM Release 开发包（头文件与 `.lib` 库）—— 已适配系统 `C:\LLVM-22-dev` 完整开发套件。
* [x] 编写 CMakeLists.txt，成功编译出一个能被 `clang-cl` 识别的基础插件 DLL (`MemLLVM.dll`)。
* [x] 成功跑通 `clang-cl /clang:-fpass-plugin=...`，实现编译期控制台打印插件加载日志与 Pass 触发。

### 阶段二：StackString 栈字符串展开实现 (Week 2) [已完成]

* [x] 实现 `ModulePass` 对全局常量的遍历与筛选逻辑（覆盖 `ConstantDataSequential` 与 `ConstantArray`）。
* [x] 编写 `IRBuilder` 逻辑，在函数入口插入 `alloca` 局部数组并构造分块 `volatile` 写栈指令（`i64`/`i32`/`i16`/`i8`）。
* [x] 自动解包函数内 `ConstantExpr` 引用并安全清除死常量与全局变量。
* [x] 验证生成的二进制文件，确认 `.rdata` 节区不再包含明文字符串常量（实现 0 `.rdata` 依赖），并通过运行时功能测试。

### 阶段三：外部 API 动态解析桩注入 (Week 3) [已完成]

* [x] 实现外部 Windows API 函数调用的自动匹配与 ROR13 哈希计算替换。
* [x] 内置嵌入式纯自包含 PEB/EAT 遍历解析器 (`MemLLVM_GetApiAddress`)，无任何 CRT 依赖，动态链接至目标模块。
* [x] 将静态直接调用全部改写为安全间接调用桩（`call <reg>`），并彻底剥离未定义外部符号。
* [x] 验证生成的目标文件，导入表（IAT）外部 Windows API 引用彻底清零，并通过运行时功能回归测试。

### 阶段四：纯内存提取与自举测试 (Week 4) [已完成]

* [x] 使用 `llvm-objcopy --dump-section .text=<output.bin>` 抽取纯 `.text` 节区机器码（消除 PE 头与未对齐空洞）。
* [x] 编写测试 Host 程序 (`tests/host_runner.c`)，通过 `VirtualAlloc(PAGE_EXECUTE_READWRITE)` 分配内存，直接将二进制复制并从偏移 `0x00` 跳转执行，验证无 loader 下的自举成功率。
* [x] 实现自动化纯度验证脚本 (`scripts/verify_purity.py`)，检测目标文件/可执行文件是否具备 0 `.rdata`/`.data`/`.bss` 且 0 `__imp_*` 导入表项。
* [x] 构建端到端自动化测试流水线 (`tests/run_all_tests.ps1`)，全自动化覆盖编译、纯度检测、节区剥离与内存直接加载运行验证。

---

## 6. 一体化单 DLL 架构与测试指南 (All-in-One DLL Architecture & Test Guide)

### 6.1 核心设计理念：一个 DLL 搞定所有 (One DLL Does Everything)
MemLLVM 遵循极简且高度集成的工程理念：**构建产出的唯一交付物为一个独立的 `MemLLVM.dll`，插件本身即是完整的测试引擎与自举执行器**。彻底抛弃外部零散的 Python 脚本、PowerShell 脚本或独立的 Host Runner 可执行文件：
- **作为编译器插件**：导出 `llvmGetPassPluginInfo`，挂载至 `clang-cl /clang:-fpass-plugin=...` 参与完整工程编译与混淆。
- **作为全内置自测引擎**：导出 `RunAllTests`、`RunTests`、`RunAllTestsW`，内部集成 LLVM IR 内存级单元测试、原生 COFF 纯度解析器与 RWX 纯内存 0x00 偏移自举执行器。

### 6.2 导出的统一测试接口
| 导出符号 | 调用协议 | 适用场景 |
| :--- | :--- | :--- |
| **`RunAllTests`** | `int __cdecl (void)` | 供外部 C/C++、Python (ctypes)、PowerShell (P/Invoke) 直接调用，返回 0 表示全量通过。 |
| **`RunTests`** | `void CALLBACK (HWND, HINSTANCE, LPSTR, int)` | 兼容 Windows 原生 `rundll32.exe` 调用协议，零依赖开箱即测。 |
| **`RunAllTestsW`** | `void CALLBACK (HWND, HINSTANCE, LPWSTR, int)` | 兼容 Windows 原生 `rundll32.exe` Unicode 宽字符调用。 |

### 6.3 编译 MemLLVM 插件 DLL
```powershell
cmake -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
# 生成唯一目标物: build/MemLLVM/Release/MemLLVM.dll
```

### 6.4 一键调用 DLL 内置全量测试套件

#### 方式 1：Windows 原生 rundll32 一键自测（零依赖推荐）
```cmd
rundll32.exe D:\MemLLVM\build\MemLLVM\Release\MemLLVM.dll,RunTests
```

#### 方式 2：PowerShell 原生直接调用
```powershell
$sig = '[DllImport(@"D:\MemLLVM\build\MemLLVM\Release\MemLLVM.dll", EntryPoint = "RunAllTests")] public static extern int RunAllTests();'
$runner = Add-Type -MemberDefinition $sig -Name "MemLLVMRunner" -Namespace "MemLLVM" -PassThru
$runner::RunAllTests()
```

#### 方式 3：Python 一行直接调用
```python
import ctypes
assert ctypes.CDLL(r"D:\MemLLVM\build\MemLLVM\Release\MemLLVM.dll").RunAllTests() == 0
```

### 6.5 DLL 内置 4 项核心测试全景
1. **[Test 1/4] StackStringPass 纯内存 IR 单元测试**：
   - 在内存中即时创建 LLVM Module 与上下文，注入全局字符串常量；
   - 触发 `StackStringPass`，验证全局变量在内存中 100% 被抹除，并验证入口块生成了对应的 `AllocaInst` 和分块 Store。
2. **[Test 2/4] ApiResolvePass 纯内存 IR 单元测试**：
   - 内存动态注入外部 API 声明（`VirtualAlloc`/`VirtualFree`）与调用点；
   - 触发 `ApiResolvePass`，验证外部未定义符号彻底消除，嵌入式纯汇编 PEB/EAT 解析器自动合并，静态调用重写为间接动态调用。
3. **[Test 3/4] 原生内置 COFF 节区纯度核验**：
   - DLL 内部自带纯 C++ 原生 COFF 解析实现，脱离 `llvm-readobj` 与 Python；
   - 遍历目标 Section Headers，硬性核验 `.rdata`/`.data`/`.bss` 大小为 0，并断言不存在 `__imp_*` 外部重定位。
4. **[Test 4/4] 纯内存扁平机器码 0x00 偏移自举执行测试**：
   - DLL 内部嵌有纯净无重定位的 381 字节机器码；
   - 通过 `VirtualAlloc(PAGE_EXECUTE_READWRITE)` 分配内存后直接跳转至首字节 **偏移 0x00**：`((int(*)())buf)()`；
   - 验证机器码在没有 PE Loader 下动态通过 GS 寄存器遍历 PEB/EAT，完成内存分配、数据读写校验与安全释放，成功返回状态码 `77`。

### 6.6 唯一端到端示例测试 (`tests/test.c`)
项目中仅保留唯一一个最具说服力的全特性端到端示例：[tests/test.c](file:///D:/MemLLVM/tests/test.c)。
该示例在一份代码中同时融合了 MemLLVM 的两大核心能力：
1. **常量字符串栈展开**：使用 `volatile const char *msg = "MemLLVM_Freestanding_Success";`，强制触发 `StackStringPass` 将常量消除为局部栈赋值，产出 0 `.rdata`。
2. **外部 API 动态解析**：调用 `VirtualAlloc` 与 `VirtualFree`，强制触发 `ApiResolvePass` 将其转换为 ROR13 动态 PEB/EAT 间接调用，产出 0 IAT。
3. **内存读写校验与安全释放**：执行字符串内存复制、关键字节断言校验与动态释放，成功返回状态码 `77`。

**编译与提取纯机器码命令**：
```powershell
# 1. 配合独立环境参数编译为 0 依赖目标文件
clang-cl /clang:-fpass-plugin=build/MemLLVM/Release/MemLLVM.dll /GS- /clang:-ffreestanding /clang:-fno-builtin /clang:-mno-stack-arg-probe /clang:-fno-jump-tables /clang:-fno-exceptions /clang:-fno-rtti -O2 -c tests/test.c -Fotests/test.obj

# 2. 链接并提取纯净扁平 .text 机器码
lld-link /entry:main /subsystem:console /nodefaultlib /safeseh:no tests/test.obj -out:tests/test.exe
llvm-objcopy --dump-section .text=tests/test.bin tests/test.exe
```

---

如果文档中的模块划分或逻辑细节需要调整，可以随时指出补充。
