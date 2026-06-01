"""
SCons tool: idtxcore

Builds libidtx_core in TWO configurations from one source tree:

1. **Shared library** (`libidtx_core.<platform>.<arch>.{dll,so,dylib}`)
   — consumed by the GDExtension (addon/IDTXFlow/), the standalone CLI
   (tools/idtxcli/), and any future P/Invoke host. Symbols are
   `__declspec(dllexport)` on Windows, default ELF visibility elsewhere.

2. **Static archive** (`libidtx_core_static.<platform>.<arch>.{lib,a}`)
   — consumed by the multiplayer-fabric-godot engine module, which
   compiles libidtx_core's source directly into the Godot binary
   via a `git subtree --squash` at `thirdparty/idtx_core/`. Symbols
   carry NO import/export decoration (defined via `IDTX_CORE_STATIC`).

Single source list, single compile flags (modulo the export-decoration
define). The two artifacts cannot algorithmically diverge — they are
the same `.cpp` files.

Usage in SConstruct:
    env.BuildIdtxCore()              # both targets, default
    env.BuildIdtxCore(static=False)  # shared only (legacy callers)
    env.BuildIdtxCore(shared=False)  # static only (engine-module CI)
"""
import os
import platform


def generate(env):
    env.AddMethod(_build_idtx_core, 'BuildIdtxCore')


def exists(env):
    return True


def _common_env(env, *, building_dll, static):
    """Configure a build env for one of the two artifacts.

    Shared library build: `building_dll=True, static=False`
    Static archive build: `building_dll=False, static=True`
    """
    platform_name = env["platform_name"]
    build_target = env["target"]
    openusd_version = env.get('openusd_version', '')
    usd_root = f"thirdparty/openusd-{openusd_version}"
    usd_extension_path = "usd"

    cfg_env = env.Clone()

    cfg_env.Append(CPPPATH=[
        "core/include",
        f"{usd_root}/include",
        f"{usd_extension_path}/include",
        "libs/cgltf",
        "libs/jsmn",
    ])

    cfg_env.Append(LIBPATH=[
        f"{usd_root}/lib",
        f"{usd_extension_path}/libs/{platform_name}",
    ])

    cfg_env.Append(LIBS=[
        "usd_ms",
        "tbb12" if platform_name == "windows" else "tbb.12",
        "libidtx_usd",
    ])

    if platform.system() == "Windows" and (env["CXX"] == "cl" or env["CC"] == "cl"):
        cfg_env.Append(CXXFLAGS=['/EHsc', '/GR', '/FS', '/std:c++20'])
        cfg_env.Append(CPPDEFINES=["NOMINMAX", "WIN32_LEAN_AND_MEAN"])
        if build_target in ["editor", "template_debug"]:
            cfg_env.Append(CCFLAGS=["/Z7", "/Od", "/MT"])
        else:
            cfg_env.Append(CCFLAGS=["/O2", "/MT"])
    else:
        cfg_env.Append(CXXFLAGS=['-fexceptions', '-frtti', '-std=c++20'])
        cfg_env.Append(CCFLAGS=["-fPIC"])
        cfg_env.Append(CCFLAGS=["-O3" if build_target == "template_release" else "-g"])

    # The two configs diverge ONLY here. IDTX_CORE_BUILDING_DLL turns on
    # dllexport for the shared lib; IDTX_CORE_STATIC suppresses both
    # dllexport AND dllimport for the static archive. Defining both at
    # once is a bug — assert it.
    assert not (building_dll and static), "shared and static configs are mutually exclusive"
    if building_dll:
        cfg_env.Append(CPPDEFINES=["IDTX_CORE_BUILDING_DLL"])
    if static:
        cfg_env.Append(CPPDEFINES=["IDTX_CORE_STATIC"])

    return cfg_env


def _sources():
    """The single canonical source list. Both artifacts compile these."""
    return [
        "core/src/idtx_core.cpp",
        "core/src/idtx_skeleton.cpp",
        "core/src/idtx_mesh.cpp",
        "core/src/idtx_mesh_quads.cpp",
        "core/src/idtx_material.cpp",
        "core/src/idtx_avatar.cpp",
        "core/src/idtx_physics_collider.cpp",
        "core/src/idtx_springbone.cpp",
        "core/src/string_utils.cpp",
        "core/src/usd_helpers.cpp",
        "core/src/json_writer.cpp",
        "core/src/idtx_export_usd.cpp",
        "core/src/idtx_import_usd.cpp",
        "core/src/idtx_vrm.cpp",
        "core/src/idtx_vrm_import.cpp",
        "core/src/idtx_vrm_springbone_parse.cpp",
        "core/src/vrm_humanoid_bones.cpp",
        "core/src/idtx_chunker.cpp",
    ]


def _build_idtx_core(env, shared=True, static=True):
    print("Building libidtx_core (engine-agnostic C ABI)...")

    platform_name = env["platform_name"]
    build_arch = env["arch"]

    build_dir = "build/idtx_core"
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)

    library_name = f"libidtx_core.{platform_name}.{build_arch}"
    shared_extension = "dll" if platform_name == "windows" else ("dylib" if platform_name == "macos" else "so")
    static_extension = "lib" if platform_name == "windows" else "a"

    sources = _sources()
    targets = []

    shared_lib = None
    if shared:
        shared_env = _common_env(env, building_dll=True, static=False)
        # SCons SharedObject objects must be distinct from the static
        # ones (same .cpp → two different .obj/.o files) — use a
        # variant_dir or duplicated SharedObject calls. Here we call
        # SharedLibrary directly which handles object distinction.
        shared_lib = shared_env.SharedLibrary(
            f"{build_dir}/{library_name}.{shared_extension}",
            sources,
        )
        env['idtx_core_lib_name'] = library_name
        env['idtx_core_lib_dir'] = os.path.abspath(build_dir)
        env['idtx_core_library_node'] = shared_lib
        shared_env.Default(shared_lib)
        targets.append(shared_lib)

    static_lib = None
    if static:
        static_env = _common_env(env, building_dll=False, static=True)
        # StaticLibrary objects (.obj / .o) are kept separate from the
        # SharedLibrary ones by giving the archive a distinct base name
        # (`_static` suffix). SCons compiles each .cpp twice — once for
        # each config — because the CPPDEFINES differ.
        static_lib = static_env.StaticLibrary(
            f"{build_dir}/{library_name}_static.{static_extension}",
            sources,
        )
        env['idtx_core_static_lib_name'] = f"{library_name}_static"
        env['idtx_core_static_lib_dir'] = os.path.abspath(build_dir)
        env['idtx_core_static_library_node'] = static_lib
        static_env.Default(static_lib)
        targets.append(static_lib)

    # Backwards-compat: callers that only consumed `idtx_core_library_node`
    # still work because the shared lib is built by default.
    return targets[0] if len(targets) == 1 else targets
