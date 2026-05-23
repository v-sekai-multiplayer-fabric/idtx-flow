// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// VRM 1.0 serializer / parser — incremental implementation. The MVP
// stub establishes the C ABI surface so Unity P/Invoke can bind
// against it; the actual glTF + VRMC_vrm machinery lands across
// subsequent commits.

#include "idtx_core/idtx_core.h"

extern "C" IDTX_CORE_API int32_t idtx_core_export_avatar_to_vrm(
    const idtx_avatar_t* avatar,
    const char* path)
{
    if (avatar == nullptr || path == nullptr) return 1;
    // TODO(Phase 7.2+): assemble glTF buffers (nodes, meshes, skins,
    // accessors, buffer views), append VRMC_vrm extension with
    // humanoid map, write GLB container. Reads the lean-emitted
    // openusd-fabric/maps/humanoid_bones_map.json + scss_mtoon_map.json
    // for the mapping tables — see memory:project-chi254-lean-method-status
    // for the byte-pin caveat (maps may drift from their Lean spec).
    return 99;  // not yet implemented
}

extern "C" IDTX_CORE_API idtx_avatar_t* idtx_core_import_avatar_from_vrm(
    const char* path)
{
    (void)path;
    // TODO(Phase 7.3+): parse GLB, walk nodes/meshes/skins, read
    // VRMC_vrm extension for humanoid + meta, return populated avatar.
    return nullptr;
}
