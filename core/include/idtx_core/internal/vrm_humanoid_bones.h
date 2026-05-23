// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// VRM 1.0 humanoid bone name list — internal C++ helper. Mirrors
// openusd-fabric/maps/humanoid_bones_map.json (vrm1 column).

#ifndef IDTX_CORE_INTERNAL_VRM_HUMANOID_BONES_H
#define IDTX_CORE_INTERNAL_VRM_HUMANOID_BONES_H

#include <cstddef>

namespace idtx::core::vrm {

// Returns true if `name` is one of the VRM 1.0 humanoid bone slot
// names (case-sensitive match).
bool is_humanoid_bone(const char* name);

// Returns the canonical list of VRM 1.0 humanoid bone names. Stable
// pointer; out_count receives the array length.
const char* const* humanoid_bone_names(size_t* out_count);

}  // namespace idtx::core::vrm

#endif  // IDTX_CORE_INTERNAL_VRM_HUMANOID_BONES_H
