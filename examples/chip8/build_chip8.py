import os
import sys
import subprocess
import shutil

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))

def resolve_llvm_tool(tool_name):
    env_direct = os.environ.get(tool_name.upper().replace("-", "_"))
    if env_direct:
        if os.path.exists(env_direct):
            return env_direct
        w = shutil.which(env_direct)
        if w:
            return w

    for var in ["LLVM_PATH", "LLVM_DIR", "LLVM_ROOT"]:
        base = os.environ.get(var)
        if base and os.path.exists(base):
            cand = os.path.join(base, "bin", f"{tool_name}.exe")
            if os.path.exists(cand):
                return cand
            cand = os.path.join(base, f"{tool_name}.exe")
            if os.path.exists(cand):
                return cand

    for cand in [
        rf"C:\LLVM-22-plugin\bin\{tool_name}.exe",
        rf"C:\LLVM-22-dev\bin\{tool_name}.exe",
        rf"C:\Program Files\LLVM\bin\{tool_name}.exe",
    ]:
        if os.path.exists(cand):
            return cand

    w = shutil.which(tool_name) or shutil.which(f"{tool_name}.exe")
    if w:
        return w

    print("=" * 72, file=sys.stderr)
    print(f"[-] ERROR: Required LLVM tool '{tool_name}' could not be located!", file=sys.stderr)
    print("[-] You MUST configure the LLVM environment variable before building:", file=sys.stderr)
    print("[-]   CMD:        set LLVM_PATH=C:\\path\\to\\llvm", file=sys.stderr)
    print("[-]   PowerShell: $env:LLVM_PATH = \"C:\\path\\to\\llvm\"", file=sys.stderr)
    print("[-]   Permanent:  setx LLVM_PATH \"C:\\path\\to\\llvm\"", file=sys.stderr)
    print("=" * 72, file=sys.stderr)
    sys.exit(1)

CLANG_CL = resolve_llvm_tool("clang-cl")
LLD_LINK = resolve_llvm_tool("lld-link")

PLUGIN_DLL = os.environ.get("PLUGIN_DLL")
if not PLUGIN_DLL or not os.path.exists(PLUGIN_DLL):
    for cand in [
        os.path.join(REPO_ROOT, "build", "MemLLVM", "Release", "MemLLVM.dll"),
        os.path.join(REPO_ROOT, "build", "Release", "MemLLVM.dll"),
        os.path.join(REPO_ROOT, "release_artifacts", "MemLLVM.dll")
    ]:
        if os.path.exists(cand):
            PLUGIN_DLL = cand
            break

if not PLUGIN_DLL or not os.path.exists(PLUGIN_DLL):
    print(f"[-] ERROR: MemLLVM plugin DLL not found! Please build MemLLVM first or set PLUGIN_DLL.", file=sys.stderr)
    sys.exit(1)

LTL_LIB = os.path.join(REPO_ROOT, "ltl", "ltl.lib")
LTL_INC = os.path.join(REPO_ROOT, "ltl", "include")
WORK_DIR = SCRIPT_DIR
OBJ_DIR = os.path.join(WORK_DIR, "obj")
os.makedirs(OBJ_DIR, exist_ok=True)

SOURCES = [
    os.path.join(WORK_DIR, "chip8_core.cpp"),
    os.path.join(WORK_DIR, "main.cpp")
]

DEFINES = [
    "/std:c++20",
    "-DNDEBUG",
    "-D_WINDOWS",
    "-D_WIN64",
    "-DNOMINMAX",
    "/GS-",
    "/GR-",
    "/Gy",
    "/clang:-ffunction-sections",
    "/clang:-fdata-sections",
    "/clang:-fno-exceptions",
    "/clang:-fno-builtin",
    "/O1",
    "/clang:-Oz",
    f"/clang:-fpass-plugin={PLUGIN_DLL}"
]

INCLUDES = [
    f"-I{LTL_INC}",
    f"-I{WORK_DIR}"
]

def compile_sources():
    objs = []
    for src in SOURCES:
        name = os.path.splitext(os.path.basename(src))[0] + ".obj"
        obj_path = os.path.join(OBJ_DIR, name)
        cmd = [CLANG_CL] + DEFINES + INCLUDES + ["-c", src, f"/Fo:{obj_path}"]
        print(f"[*] Compiling {os.path.basename(src)} with MemLLVM...")
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[-] Compilation failed for {src}:")
            print(res.stderr)
            print(res.stdout)
            sys.exit(1)
        objs.append(obj_path)
    return objs

EXTRACT_BIN = os.path.join(REPO_ROOT, "Tools", "extract_flat_bin.py")

def main():
    objs = compile_sources()
    temp_exe = os.path.join(WORK_DIR, "chip8_temp.exe")
    out_bin = os.path.join(WORK_DIR, "chip8.bin")

    link_cmd = [
        LLD_LINK,
        "/subsystem:windows",
        "/entry:main",
        "/nodefaultlib",
        "/safeseh:no",
        "/opt:ref",
        "/opt:icf",
        "/align:512",
        "/merge:.rdata=.text",
        f"/out:{temp_exe}",
        LTL_LIB
    ] + objs

    print("[*] Linking with lld-link...")
    res = subprocess.run(link_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("[-] Linking failed:")
        print(res.stderr)
        print(res.stdout)
        sys.exit(1)

    print("[*] Extracting flat binary payload...")
    res = subprocess.run([sys.executable, EXTRACT_BIN, temp_exe, out_bin])
    if res.returncode != 0:
        print("[-] Extracting flat binary failed!")
        sys.exit(1)

    # Clean up intermediate exe and pdb/ilk
    if os.path.exists(temp_exe):
        os.remove(temp_exe)
    for ext in (".pdb", ".ilk", ".map"):
        p = os.path.splitext(temp_exe)[0] + ext
        if os.path.exists(p):
            os.remove(p)
    shutil.rmtree(OBJ_DIR, ignore_errors=True)

    print("\n[+] Build complete! Flat binary ready at:")
    print(f"    {out_bin}")

if __name__ == "__main__":
    main()
