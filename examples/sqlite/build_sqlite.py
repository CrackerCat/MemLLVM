import os
import sys
import subprocess
import shutil

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))

def resolve_llvm_tool(tool_name):
    for var in ["LLVM_PATH", "LLVM_DIR", "LLVM_ROOT"]:
        base = os.environ.get(var)
        if base and os.path.exists(base):
            cand = os.path.join(base, "bin", f"{tool_name}.exe")
            if os.path.exists(cand):
                return cand
            cand = os.path.join(base, f"{tool_name}.exe")
            if os.path.exists(cand):
                return cand
    if os.path.exists(rf"C:\LLVM-22-dev\bin\{tool_name}.exe"):
        return rf"C:\LLVM-22-dev\bin\{tool_name}.exe"
    w = shutil.which(tool_name) or shutil.which(f"{tool_name}.exe")
    if w:
        return w
    print(f"[-] ERROR: Required tool '{tool_name}' not found!", file=sys.stderr)
    sys.exit(1)

CLANG_CL = resolve_llvm_tool("clang-cl")
LLD_LINK = resolve_llvm_tool("lld-link")

PLUGIN_DLL = os.environ.get("PLUGIN_DLL")
if not PLUGIN_DLL or not os.path.exists(PLUGIN_DLL):
    for cand in [
        os.path.join(REPO_ROOT, "bin", "MemLLVM.dll"),
        os.path.join(REPO_ROOT, "build", "MemLLVM", "Release", "MemLLVM.dll"),
        os.path.join(REPO_ROOT, "build", "Release", "MemLLVM.dll"),
        os.path.join(REPO_ROOT, "release_artifacts", "MemLLVM.dll")
    ]:
        if os.path.exists(cand):
            PLUGIN_DLL = cand
            break

if not PLUGIN_DLL or not os.path.exists(PLUGIN_DLL):
    print("[-] ERROR: MemLLVM.dll plugin not found!", file=sys.stderr)
    sys.exit(1)

LTL_LIB = os.path.join(REPO_ROOT, "ltl", "ltl.lib")
LTL_INC = os.path.join(REPO_ROOT, "ltl", "include")
EXTRACT_BIN = os.path.join(REPO_ROOT, "Tools", "extract_flat_bin.py")

SHELL_C = os.path.join(SCRIPT_DIR, "shell.c")
SHELL_OBJ = os.path.join(SCRIPT_DIR, "shell.obj")
SQLITE_EXE = os.path.join(SCRIPT_DIR, "sqlite3.exe")
SQLITE_BIN = os.path.join(SCRIPT_DIR, "sqlite3.bin")

def main():
    print("=======================================================================")
    print("      MemLLVM Example - Official SQLite3 Command Line Shell (CLI)      ")
    print("=======================================================================\n")

    print(f"[*] Compiler   : {CLANG_CL}")
    print(f"[*] Plugin     : {PLUGIN_DLL}")
    print(f"[*] Source     : {SHELL_C}")

    # 1. Compile shell.c with MemLLVM plugin
    compile_cmd = [
        CLANG_CL, "/c", "/nologo", "/W3",
        f"/clang:-fpass-plugin={PLUGIN_DLL}",
        "/GS-", "/clang:-ffreestanding", "/clang:-fno-builtin",
        "/clang:-fno-jump-tables", "/clang:-fno-exceptions", "/clang:-fno-rtti",
        "/D_HAS_EXCEPTIONS=0",
        f"-I{LTL_INC}",
        f"-I{SCRIPT_DIR}",
        "-O2",
        "/DUSE_SYSTEM_SQLITE=1",
        SHELL_C,
        f"/Fo{SHELL_OBJ}"
    ]

    print("[*] Compiling official shell.c...")
    res = subprocess.run(compile_cmd)
    if res.returncode != 0:
        print("[-] Compilation failed!")
        return 1

    # 2. Link with MemLLVM-LTL
    link_cmd = [
        LLD_LINK, "/nologo", "/nodefaultlib",
        "/entry:main", "/subsystem:console",
        f"/out:{SQLITE_EXE}",
        SHELL_OBJ,
        LTL_LIB
    ]

    print("[*] Linking sqlite3.exe with ltl.lib...")
    res = subprocess.run(link_cmd)
    if res.returncode != 0:
        print("[-] Linking failed!")
        return 1

    # 3. Extract flat binary
    print("[*] Extracting flat binary sqlite3.bin...")
    extract_cmd = [sys.executable, EXTRACT_BIN, SQLITE_EXE, SQLITE_BIN]
    res = subprocess.run(extract_cmd)
    if res.returncode != 0:
        print("[-] Extracting flat binary failed!")
        return 1

    # Copy to SCLoader directory for easy access
    scloader_dst = r"E:\C++project\CUI\SCLoader\sqlite3.bin"
    try:
        shutil.copy2(SQLITE_BIN, scloader_dst)
        print(f"[+] Also copied to {scloader_dst}")
    except Exception as e:
        pass

    size_kb = os.path.getsize(SQLITE_BIN) / 1024.0
    print(f"\n[+] SUCCESS: Official SQLite3 CLI built: {SQLITE_BIN} ({size_kb:.2f} KB)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
