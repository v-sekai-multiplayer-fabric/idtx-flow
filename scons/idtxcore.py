"""
SCons tool: idtxcore
Builds libidtx_core.dll — the engine-agnostic C ABI for the idtx-flow
avatar pipeline. Consumers (libidtxflow GDExtension, libidtx_unity
P/Invoke assembly) link against this single artifact so the heavy USD
work lives in exactly one place.

Usage in SConstruct:
    env.BuildIdtxCore()
"""
import os
import platform


def generate(env):
    env.AddMethod(_build_idtx_core, 'BuildIdtxCore')


def exists(env):
    return True


def _build_idtx_core(env):
    print("Building libidtx_core (engine-agnostic C ABI)...")

    platform_name = env["platform_name"]
    build_target = env["target"]
    build_arch = env["arch"]

    core_env = env.Clone()

    # Include paths — core depends on libidtx_usd (USD schema layer) but
    # NOT on godot-cpp. Adding godot-cpp here would defeat the purpose.
    core_env.Append(CPPPATH=[
        "core/include",
    ])

    # /FS lets parallel cl.exe invocations share the PDB writer; /Z7
    # avoids the shared compile-time PDB entirely (consistent with the
    # other builders after the 9a0ef09 fix).
    if platform.system() == "Windows" and (env["CXX"] == "cl" or env["CC"] == "cl"):
        core_env.Append(CXXFLAGS=['/EHsc', '/GR', '/FS', '/std:c++20'])
        core_env.Append(CPPDEFINES=["NOMINMAX", "WIN32_LEAN_AND_MEAN", "IDTX_CORE_BUILDING_DLL"])
        if build_target in ["editor", "template_debug"]:
            core_env.Append(CCFLAGS=["/Z7", "/Od", "/MT"])
        else:
            core_env.Append(CCFLAGS=["/O2", "/MT"])
    else:
        core_env.Append(CXXFLAGS=['-fexceptions', '-frtti', '-std=c++20'])
        core_env.Append(CCFLAGS=["-fPIC"])
        core_env.Append(CCFLAGS=["-O3" if build_target == "template_release" else "-g"])
        core_env.Append(CPPDEFINES=["IDTX_CORE_BUILDING_DLL"])

    sources = [
        "core/src/idtx_core.cpp",
        "core/src/idtx_skeleton.cpp",
        "core/src/idtx_mesh.cpp",
        "core/src/idtx_material.cpp",
        "core/src/idtx_avatar.cpp",
    ]

    library_name = f"libidtx_core.{platform_name}.{build_arch}"
    library_extension = "dll" if platform_name == "windows" else ("dylib" if platform_name == "macos" else "so")

    build_dir = "build/idtx_core"
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)

    library = core_env.SharedLibrary(
        f"{build_dir}/{library_name}.{library_extension}",
        sources,
    )

    # Stash in env so downstream builders (gdextension, future
    # idtx_unity) can link/copy this artifact.
    env['idtx_core_lib_name'] = library_name
    env['idtx_core_lib_dir'] = os.path.abspath(build_dir)
    env['idtx_core_library_node'] = library

    core_env.Default(library)
    return library
