// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_core_export_avatar_to_scn — Godot 4 .scn (binary PackedScene)
// writer. See idtx_core.h for the public C ABI contract.
//
// Format spec: E:\lean-predictive-bvh\PredictiveBVH\Codegen\
//              GodotBinary.lean (Lean 4 model of FORMAT_VERSION=6,
//              33 variant tags, RSRC/RSCC headers, string table,
//              ext/int resource tables; strings have NO 4-byte
//              padding, validated against Godot 4.7 output).
//
// Slang emitter: E:\idtx-flow-v-sekai\openusd-fabric\lean\Fabric\
//                Serialization\GodotScn.lean — emits godot_scn.slang
//                with the variant writers + headers.
//
// Test fixtures: openusd-fabric/tests/scn_validate/ — real Godot-
//                baked .scn files (simple, mesh+basisu, all-Node3D-
//                types) plus parse_scn.py and validate_all_node3d.py.
//
// Status: STUBBED. Returns code 99 (not yet implemented). The full
// writer requires (a) compiling godot_scn.slang via slangc -target
// cpp into a linkable .o, OR (b) hand-porting the LeanSlang-emitted
// writers as plain C++. Decision deferred to the writer-impl commit.

#include "idtx_core/idtx_core.h"

#include <cstring>

extern "C" IDTX_CORE_API void idtx_scn_opts_init(idtx_scn_opts_t* opts)
{
    if (opts == nullptr) return;
    opts->compression       = IDTX_SCN_ZSTD;
    opts->block_size        = 4096;     // Godot 4 RSCC default
    opts->generate_lods     = 1;        // meshoptimizer on by default
    opts->lod_target_error  = 1.0f;     // ImporterMesh::generate_lods stop
    opts->basis_universal   = 1;        // PortableCompressedTexture2D
}

extern "C" IDTX_CORE_API int32_t idtx_core_export_avatar_to_scn(
    const idtx_avatar_t* avatar,
    const char*          path,
    const idtx_scn_opts_t* /*opts*/)
{
    if (avatar == nullptr || path == nullptr) {
        return 1;  // invalid argument
    }
    // TODO(ART-44 task #2): link the LeanSlang-emitted godot_scn writers
    // and serialize the avatar's scene tree into a Godot 4 .scn file.
    return 99;  // not yet implemented
}

extern "C" IDTX_CORE_API int64_t idtx_core_export_avatar_to_scn_buffer(
    const idtx_avatar_t* avatar,
    uint8_t*             out_buf,
    size_t               out_cap,
    const idtx_scn_opts_t* /*opts*/)
{
    if (avatar == nullptr) return -1;
    // Two-call size-query pattern: caller passes out_buf=NULL/out_cap=0
    // to learn the required capacity. We don't know it yet; return a
    // negative not-implemented signal that callers can distinguish from
    // a legitimate size by being -99 specifically.
    if (out_buf == nullptr || out_cap == 0) {
        return -99;  // not yet implemented
    }
    return -99;
}

// ---------------------------------------------------------------------
// Progress callback registry — shared state for the whole library.
// Hosts call idtx_core_set_progress_cb() once; internal call sites do:
//   if (g_progress_cb) g_progress_cb(fraction, message, g_progress_user);
// ---------------------------------------------------------------------

namespace {
idtx_progress_fn g_progress_cb   = nullptr;
void*            g_progress_user = nullptr;
}

extern "C" IDTX_CORE_API void idtx_core_set_progress_cb(
    idtx_progress_fn cb, void* user)
{
    g_progress_cb   = cb;
    g_progress_user = user;
}

// ---------------------------------------------------------------------
// idtx_core_init — one-time setup. Prepends the shipped schema plugin
// directory to PXR_PLUGINPATH_NAME so codeless v_sekai:* attributes
// resolve in every host before any UsdStage::Open() call.
//
// Stubbed for now: real impl will resolve the shared lib's neighbour
// `share/idtx_core/` (or honour the caller's override) and prepend it
// via setenv("PXR_PLUGINPATH_NAME", ...). Returns 0 on success.
// ---------------------------------------------------------------------

extern "C" IDTX_CORE_API int32_t idtx_core_init(const char* /*plugin_dir*/)
{
    // TODO(ART-44 task #4): resolve schema dir and prepend to
    // PXR_PLUGINPATH_NAME. For now, no-op success — the library still
    // works, but v_sekai:* attrs are opaque.
    return 0;
}
