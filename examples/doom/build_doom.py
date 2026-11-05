import os
import sys
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
import time

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
OBJ_DIR = os.path.join(WORK_DIR, "obj")
os.makedirs(OBJ_DIR, exist_ok=True)
os.environ["MEMLLVM_DISABLE_STACK_STRINGS"] = "1"

# 83 source files from doomgeneric
DOOM_SOURCES = [
    "am_map.c", "d_event.c", "d_items.c", "d_iwad.c", "d_loop.c", "d_main.c",
    "d_mode.c", "d_net.c", "doomdef.c", "doomgeneric.c", "doomgeneric_win.c",
    "doomstat.c", "dstrings.c", "dummy.c", "f_finale.c", "f_wipe.c", "g_game.c",
    "gusconf.c", "hu_lib.c", "hu_stuff.c", "i_cdmus.c", "i_endoom.c", "i_input.c",
    "i_joystick.c", "i_scale.c", "i_sound.c", "i_system.c", "i_timer.c", "i_video.c",
    "icon.c", "info.c", "m_argv.c", "m_bbox.c", "m_cheat.c", "m_config.c",
    "m_controls.c", "m_fixed.c", "m_menu.c", "m_misc.c", "m_random.c", "memio.c",
    "p_ceilng.c", "p_doors.c", "p_enemy.c", "p_floor.c", "p_inter.c", "p_lights.c",
    "p_map.c", "p_maputl.c", "p_mobj.c", "p_plats.c", "p_pspr.c", "p_saveg.c",
    "p_setup.c", "p_sight.c", "p_spec.c", "p_switch.c", "p_telept.c", "p_tick.c",
    "p_user.c", "r_bsp.c", "r_data.c", "r_draw.c", "r_main.c", "r_plane.c",
    "r_segs.c", "r_sky.c", "r_things.c", "s_sound.c", "sha1.c", "sounds.c",
    "st_lib.c", "st_stuff.c", "statdump.c", "tables.c", "v_video.c", "w_checksum.c",
    "w_file.c", "w_file_stdc.c", "w_main.c", "w_wad.c", "wi_stuff.c", "z_zone.c"
]

DEFINES = [
    "-DWIN32",
    "-D_WINDOWS",
    "-DNDEBUG",
    "-DNOMINMAX",
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
    "/W0", # Suppress C legacy warnings
    f"/clang:-fpass-plugin={PLUGIN_DLL}"
]

INCLUDES = [
    f"-I{LTL_INC}",
    f"-I{SRC_DIR}"
]

def compile_file(rel_src):
    src_path = os.path.join(SRC_DIR, rel_src)
    name = os.path.splitext(rel_src)[0] + ".obj"
    obj_path = os.path.join(OBJ_DIR, name)
    cmd = [CLANG_CL] + DEFINES + INCLUDES + ["-c", src_path, f"/Fo:{obj_path}"]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        return (False, rel_src, res.stderr + "\n" + res.stdout)
    return (True, rel_src, obj_path)

EXTRACT_BIN = os.path.join(REPO_ROOT, "Tools", "extract_flat_bin.py")

def main():
    print(f"[*] Compiling {len(DOOM_SOURCES)} DOOM source files with MemLLVM...")
    start_time = time.time()
    
    obj_files = []
    failed = []
    
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        futures = {pool.submit(compile_file, s): s for s in DOOM_SOURCES}
        done = 0
        for fut in as_completed(futures):
            done += 1
            success, src, res = fut.result()
            if success:
                obj_files.append(res)
                if done % 15 == 0 or done == len(DOOM_SOURCES):
                    print(f"  [{done}/{len(DOOM_SOURCES)}] Compiled {src}")
            else:
                print(f"[-] FAILED: {src}")
                print(res)
                failed.append((src, res))
                
    elapsed = time.time() - start_time
    print(f"[*] Compilation done in {elapsed:.2f}s. Success: {len(obj_files)}, Failed: {len(failed)}")
    
    if failed:
        print(f"[-] Aborting due to compile errors.")
        sys.exit(1)

    temp_exe = os.path.join(WORK_DIR, "doom_temp.exe")
    out_bin = os.path.join(WORK_DIR, "doom.bin")

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
