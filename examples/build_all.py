import os
import sys
import subprocess
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))

EXAMPLES = [
    ("Box2D 2D Physics Engine", ["python", os.path.join(SCRIPT_DIR, "box2d", "build_box2d.py")], os.path.join(SCRIPT_DIR, "box2d", "box2d.bin")),
    ("CHIP-8 Virtual Machine", ["python", os.path.join(SCRIPT_DIR, "chip8", "build_chip8.py")], os.path.join(SCRIPT_DIR, "chip8", "chip8.bin")),
    ("Classic DOOM Game Engine", ["python", os.path.join(SCRIPT_DIR, "doom", "build_doom.py")], os.path.join(SCRIPT_DIR, "doom", "doom.bin")),
    ("Dear ImGui DirectX 11 Demo", ["cmd", "/c", os.path.join(SCRIPT_DIR, "imgui", "build_imgui_demo.bat")], os.path.join(SCRIPT_DIR, "imgui", "imgui_demo.bin")),
    ("Official SQLite3 CLI Shell", ["python", os.path.join(SCRIPT_DIR, "sqlite", "build_sqlite.py")], os.path.join(SCRIPT_DIR, "sqlite", "sqlite3.bin"))
]

def main():
    print("=======================================================================")
    print("           MemLLVM Examples Suite - Batch Flat Binary Compiler        ")
    print("=======================================================================\n")
    
    total_start = time.time()
    results = []
    
    for name, cmd, bin_path in EXAMPLES:
        print(f"\n>>> [Building] {name}...")
        t0 = time.time()
        res = subprocess.run(cmd, cwd=os.path.dirname(cmd[-1]))
        elapsed = time.time() - t0
        
        if res.returncode == 0 and os.path.exists(bin_path):
            size = os.path.getsize(bin_path)
            size_kb = size / 1024.0
            print(f">>> [SUCCESS] {name} built in {elapsed:.2f}s ({size_kb:.2f} KB)")
            results.append((name, True, f"{size_kb:.2f} KB", f"{elapsed:.2f}s", bin_path))
        else:
            print(f">>> [FAILED] {name} failed with returncode {res.returncode}")
            results.append((name, False, "N/A", f"{elapsed:.2f}s", bin_path))
            
    total_elapsed = time.time() - total_start
    print("\n" + "=" * 75)
    print(f"{'Example Target':<32} | {'Status':<8} | {'Binary Size':<12} | {'Time':<8}")
    print("-" * 75)
    all_ok = True
    for name, ok, size_str, time_str, _ in results:
        status_str = "PASS" if ok else "FAIL"
        if not ok:
            all_ok = False
        print(f"{name:<32} | {status_str:<8} | {size_str:<12} | {time_str:<8}")
    print("=" * 75)
    print(f"Total build time: {total_elapsed:.2f}s\n")
    
    if not all_ok:
        sys.exit(1)

if __name__ == "__main__":
    main()
