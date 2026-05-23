// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "GodotAvatarBuilder.h"

#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>

namespace idtxflow::exporter
{

// Pack a Godot Transform3D into a row-major float[16] — the layout
// idtx_skeleton / idtx_avatar expect.
static void GodotTransformToFloat16(godot::Transform3D const& t, float out[16])
{
    // Godot Transform3D: 3x3 basis (rows xyz) + Vector3 origin.
    // idtx convention (matches USD GfMatrix4d): row-major homogeneous
    // 4x4 with translation in row 3 cols 0..2 and w=1 at [3][3].
    out[0]  = static_cast<float>(t.basis[0].x);
    out[1]  = static_cast<float>(t.basis[0].y);
    out[2]  = static_cast<float>(t.basis[0].z);
    out[3]  = 0.0f;

    out[4]  = static_cast<float>(t.basis[1].x);
    out[5]  = static_cast<float>(t.basis[1].y);
    out[6]  = static_cast<float>(t.basis[1].z);
    out[7]  = 0.0f;

    out[8]  = static_cast<float>(t.basis[2].x);
    out[9]  = static_cast<float>(t.basis[2].y);
    out[10] = static_cast<float>(t.basis[2].z);
    out[11] = 0.0f;

    out[12] = static_cast<float>(t.origin.x);
    out[13] = static_cast<float>(t.origin.y);
    out[14] = static_cast<float>(t.origin.z);
    out[15] = 1.0f;
}

// Depth-first search for the first Skeleton3D descendant. nullptr if
// none. The MVP assumes one skeleton per avatar; multi-skeleton
// avatars would need a list-returning variant.
static godot::Skeleton3D* FindFirstSkeleton(godot::Node* node)
{
    if (node == nullptr) return nullptr;
    auto* as_skel = godot::Object::cast_to<godot::Skeleton3D>(node);
    if (as_skel != nullptr) return as_skel;
    int n = node->get_child_count();
    for (int i = 0; i < n; ++i) {
        if (auto* found = FindFirstSkeleton(node->get_child(i))) return found;
    }
    return nullptr;
}

static ::idtx_skeleton_t* BuildSkeleton(godot::Skeleton3D* skel)
{
    if (skel == nullptr) return nullptr;
    ::idtx_skeleton_t* out = ::idtx_skeleton_create();
    godot::String name = skel->get_name();
    idtx_skeleton_set_name(out, name.utf8().get_data());

    int n = skel->get_bone_count();
    for (int i = 0; i < n; ++i) {
        godot::String bone_name = skel->get_bone_name(i);
        int parent = skel->get_bone_parent(i);

        float rest[16];
        GodotTransformToFloat16(skel->get_bone_rest(i), rest);
        float bind[16];
        GodotTransformToFloat16(skel->get_bone_global_rest(i), bind);

        idtx_skeleton_add_bone(
            out,
            bone_name.utf8().get_data(),
            parent,
            rest,
            bind);
    }
    return out;
}

::idtx_avatar_t* BuildIdtxAvatarFromGodotScene(godot::Node3D* root)
{
    if (root == nullptr) return nullptr;

    ::idtx_avatar_t* avatar = ::idtx_avatar_create();
    godot::String name = root->get_name();
    idtx_avatar_set_name(avatar, name.utf8().get_data());

    float root_xform[16];
    GodotTransformToFloat16(root->get_transform(), root_xform);
    idtx_avatar_set_root_transform(avatar, root_xform);

    if (auto* skel = FindFirstSkeleton(root)) {
        idtx_avatar_set_skeleton(avatar, BuildSkeleton(skel));
    }

    // Mesh + material extraction land in subsequent cycles.
    return avatar;
}

}  // namespace idtxflow::exporter
