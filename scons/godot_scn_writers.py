"""
SCons tool: godot_scn_writers

Wires the LeanSlang → Slang → slangc → C++ pipeline for the Godot .scn
binary writer functions used by `idtx_core_export_avatar_to_scn`.

Pipeline (executed at SCons configure time, before libidtx_core compile):

  1. `lake exe emit_artifacts`  in openusd-fabric/lean/
        → writes shaders/godot_scn.slang  (LeanSlang AST → Slang source)

  2. `slangc shaders/godot_scn.slang -target cpp -o build/godot_scn.cpp`
        → emits a C++ translation unit with the variant writer functions
          declared in core/include/idtx_core/godot_scn.h.

  3. SCons adds build/godot_scn.cpp to the libidtx_core source list so
     the same .cpp compiles into both the shared lib and the static
     archive — same byte-deterministic writer in all three deployment
     targets.

Prereqs (on PATH):
  - `lake`   (Lean 4 build tool — for emit_artifacts)
  - `slangc` (Slang compiler — for -target cpp)

If either is missing, the build prints a clear message and proceeds
without the .scn writer object. `idtx_core_export_avatar_to_scn` then
returns code 99 (not yet implemented) at runtime, instead of breaking
the whole build.

Usage in SConstruct (after BuildIdtxCore call):
    env.GenerateGodotScnWriters()
"""
import os
import shutil
import subprocess

from SCons.Script import Exit


def generate(env):
    env.AddMethod(_generate_godot_scn_writers, 'GenerateGodotScnWriters')


def exists(env):
    return True


def _have(cmd):
    return shutil.which(cmd) is not None


def _run(cmd, cwd=None, label=None):
    print(f"  [godot_scn] {label or ' '.join(cmd)}")
    res = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"  [godot_scn] FAILED rc={res.returncode}")
        if res.stdout:
            print(res.stdout)
        if res.stderr:
            print(res.stderr)
        return False
    return True


def _generate_godot_scn_writers(env):
    print("Generating godot_scn writers (LeanSlang → Slang → slangc → C++)...")

    lean_dir   = "openusd-fabric/lean"
    slang_path = "openusd-fabric/lean/shaders/godot_scn.slang"
    out_dir    = "build/godot_scn"
    out_cpp    = os.path.join(out_dir, "godot_scn.cpp")
    out_h      = os.path.join(out_dir, "godot_scn.h")

    if not os.path.isdir(lean_dir):
        print(f"  [godot_scn] openusd-fabric/lean not found at {lean_dir}; skipping")
        env['idtx_godot_scn_available'] = False
        return None

    have_lake   = _have("lake")
    have_slangc = _have("slangc")
    if not have_lake or not have_slangc:
        missing = []
        if not have_lake:   missing.append("lake")
        if not have_slangc: missing.append("slangc")
        print(f"  [godot_scn] missing tool(s) on PATH: {', '.join(missing)}")
        print(f"  [godot_scn] proceeding without .scn writer; export returns code 99")
        env['idtx_godot_scn_available'] = False
        return None

    # 1) Run lake to emit shaders/godot_scn.slang.
    if not _run(["lake", "exe", "emit_artifacts"], cwd=lean_dir, label="emit_artifacts"):
        env['idtx_godot_scn_available'] = False
        return None
    if not os.path.isfile(slang_path):
        print(f"  [godot_scn] expected {slang_path} after lake — not produced")
        env['idtx_godot_scn_available'] = False
        return None

    # 2) Run slangc to lower to C++.
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    ok = _run(
        ["slangc", slang_path, "-target", "cpp", "-o", out_cpp],
        label=f"slangc -target cpp → {out_cpp}",
    )
    if not ok or not os.path.isfile(out_cpp):
        env['idtx_godot_scn_available'] = False
        return None

    env['idtx_godot_scn_available'] = True
    env['idtx_godot_scn_cpp']       = out_cpp
    env['idtx_godot_scn_include']   = out_dir
    print(f"  [godot_scn] OK — {out_cpp}")
    return out_cpp
