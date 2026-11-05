import os
import sys
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
import time
import glob

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
SRC_DIR = os.path.join(WORK_DIR, "src")
INC_DIR = os.path.join(WORK_DIR, "include")
OBJ_DIR = os.path.join(WORK_DIR, "obj")
os.makedirs(OBJ_DIR, exist_ok=True)
os.environ["MEMLLVM_DISABLE_STACK_STRINGS"] = "1"

# All Box2D sources + main.c
SOURCES = sorted(glob.glob(os.path.join(SRC_DIR, "*.c"))) + [os.path.join(WORK_DIR, "main.c")]

DEFINES = [
    "-DWIN32",
    "-D_WINDOWS",
    "-DNDEBUG",
    "-DNOMINMAX",
    "-DB2_SIMD_SSE2",
    "-D_CRT_SECURE_NO_WARNINGS",
    "-D_CRT_NONSTDC_NO_DEPRECATE",
    "/GS-",
    "/GR-",
    "/Gy",
    "/clang:-ffunction-sections",
    "/clang:-fdata-sections",
    "/clang:-fno-exceptions",
    "/clang:-fno-builtin",
    "/O1",
    "/clang:-Oz",
    "/W0",
    f"/clang:-fpass-plugin={PLUGIN_DLL}"
]

INCLUDES = [
    f"-I{LTL_INC}",
    f"-I{INC_DIR}",
    f"-I{SRC_DIR}"
]

def compile_file(src_path):
    rel_name = os.path.basename(src_path)
    name = os.path.splitext(rel_name)[0] + ".obj"
    obj_path = os.path.join(OBJ_DIR, name)
    cmd = [CLANG_CL] + DEFINES + INCLUDES + ["-c", src_path, f"/Fo:{obj_path}"]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        return (False, rel_name, res.stderr + "\n" + res.stdout)
    return (True, rel_name, obj_path)

EXTRACT_BIN = os.path.join(REPO_ROOT, "Tools", "extract_flat_bin.py")

def main():
    print(f"[*] Compiling {len(SOURCES)} Box2D source files with MemLLVM...")
    start_time = time.time()
    
    obj_files = []
    failed = []
    
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        futures = {pool.submit(compile_file, s): s for s in SOURCES}
        done = 0
        for fut in as_completed(futures):
            done += 1
            success, rel_name, res = fut.result()
            if success:
                obj_files.append(res)
                if done % 10 == 0 or done == len(SOURCES):
                    print(f"  [{done}/{len(SOURCES)}] Compiled {rel_name}")
            else:
                print(f"[-] FAILED: {rel_name}")
                print(res)
                failed.append((rel_name, res))
                
    elapsed = time.time() - start_time
    print(f"[*] Compilation done in {elapsed:.2f}s. Success: {len(obj_files)}, Failed: {len(failed)}")
    
    if failed:
        print(f"[-] Aborting due to compile errors.")
        sys.exit(1)

    temp_exe = os.path.join(WORK_DIR, "box2d_temp.exe")
    out_bin = os.path.join(WORK_DIR, "box2d.bin")

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
    ] + obj_files

    print("[*] Linking with lld-link...")
    res = subprocess.run(link_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("[-] Linking failed:")
        print(res.stderr)
        print(res.stdout)
        sys.exit(1)

    print(f"[*] Extracting flat binary payload to {out_bin}...")
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
