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

SRC_C = os.path.join(SCRIPT_DIR, "mock_cli.c")
OBJ_FILE = os.path.join(SCRIPT_DIR, "mock_cli.obj")
EXE_FILE = os.path.join(SCRIPT_DIR, "mock_cli.exe")
BIN_FILE = os.path.join(SCRIPT_DIR, "mock_cli.bin")

def main():
    print("=======================================================================")
    print("      MemLLVM Example - Interactive CLI Diagnostic Stub (Mock CLI)     ")
    print("=======================================================================\n")

    print(f"[*] Compiler   : {CLANG_CL}")
    print(f"[*] Plugin     : {PLUGIN_DLL}")
    print(f"[*] Source     : {SRC_C}")

    # 1. Compile mock_cli.c with MemLLVM plugin
    compile_cmd = [
        CLANG_CL, "/c", "/nologo", "/W3",
        f"/clang:-fpass-plugin={PLUGIN_DLL}",
        "/GS-", "/clang:-ffreestanding", "/clang:-fno-builtin",
        "/clang:-fno-jump-tables", "/clang:-fno-exceptions", "/clang:-fno-rtti",
        "/D_HAS_EXCEPTIONS=0",
        f"-I{LTL_INC}",
        "-O2",
        SRC_C,
        f"/Fo{OBJ_FILE}"
    ]

    print("[*] Compiling mock_cli.c with MemLLVM plugin...")
    res = subprocess.run(compile_cmd)
    if res.returncode != 0:
        print("[-] Compilation failed!")
        return 1

    # 2. Link with MemLLVM-LTL
    link_cmd = [
        LLD_LINK, "/nologo", "/nodefaultlib",
        "/entry:main", "/subsystem:console",
        f"/out:{EXE_FILE}",
        OBJ_FILE,
        LTL_LIB
    ]

    print("[*] Linking mock_cli.exe with ltl.lib...")
    res = subprocess.run(link_cmd)
    if res.returncode != 0:
        print("[-] Linking failed!")
        return 1

    # 3. Extract flat binary
    print("[*] Extracting flat binary payload...")
    res = subprocess.run([sys.executable, EXTRACT_BIN, EXE_FILE, BIN_FILE])
    if res.returncode != 0:
        print("[-] extract_flat_bin failed!")
        return 1

    bin_size = os.path.getsize(BIN_FILE)
    print(f"[+] Successfully generated: {BIN_FILE} ({bin_size} bytes, {bin_size/1024.0:.2f} KB)")

    # 4. Copy to SCLoader
    scloader_dst = os.path.join(r"E:\C++project\CUI\SCLoader", "mock_cli.bin")
    try:
        shutil.copy2(BIN_FILE, scloader_dst)
        print(f"[+] Synchronized to: {scloader_dst}")
    except Exception as e:
        print(f"[!] Note: Could not copy to {scloader_dst}: {e}")

    return 0

if __name__ == "__main__":
    sys.exit(main())
