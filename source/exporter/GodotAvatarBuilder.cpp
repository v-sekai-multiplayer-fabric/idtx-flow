// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "GodotAvatarBuilder.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <vector>

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

// Translate one MeshInstance3D surface into an idtx_mesh_t. Multi-
// surface meshes call this once per surface; the caller manages
// adding each to the avatar.
static ::idtx_mesh_t* BuildMeshFromSurface(
    godot::Ref<godot::Mesh> const& mesh,
    int surface_index,
    godot::String const& base_name)
{
    if (mesh.is_null()) return nullptr;
    godot::Array arrays = mesh->surface_get_arrays(surface_index);
    if (arrays.size() <= godot::Mesh::ARRAY_VERTEX) return nullptr;

    godot::PackedVector3Array verts   = arrays[godot::Mesh::ARRAY_VERTEX];
    godot::PackedInt32Array   indices = arrays[godot::Mesh::ARRAY_INDEX];
    if (verts.size() == 0 || indices.size() == 0) return nullptr;

    ::idtx_mesh_t* out = ::idtx_mesh_create();
    godot::String name = base_name + godot::String("_") + godot::String::num_int64(surface_index);
    idtx_mesh_set_name(out, name.utf8().get_data());

    int vc = verts.size();
    std::vector<float> positions(static_cast<size_t>(vc) * 3);
    for (int i = 0; i < vc; ++i) {
        godot::Vector3 v = verts[i];
        positions[i * 3 + 0] = v.x;
        positions[i * 3 + 1] = v.y;
        positions[i * 3 + 2] = v.z;
    }

    std::vector<float> normals;
    if (arrays.size() > godot::Mesh::ARRAY_NORMAL) {
        godot::PackedVector3Array nrm = arrays[godot::Mesh::ARRAY_NORMAL];
        if (nrm.size() == vc) {
            normals.resize(static_cast<size_t>(vc) * 3);
            for (int i = 0; i < vc; ++i) {
                godot::Vector3 n = nrm[i];
                normals[i * 3 + 0] = n.x;
                normals[i * 3 + 1] = n.y;
                normals[i * 3 + 2] = n.z;
            }
        }
    }

    std::vector<float> uvs;
    if (arrays.size() > godot::Mesh::ARRAY_TEX_UV) {
        godot::PackedVector2Array uv = arrays[godot::Mesh::ARRAY_TEX_UV];
        if (uv.size() == vc) {
            uvs.resize(static_cast<size_t>(vc) * 2);
            for (int i = 0; i < vc; ++i) {
                godot::Vector2 u = uv[i];
                uvs[i * 2 + 0] = u.x;
                uvs[i * 2 + 1] = u.y;
            }
        }
    }

    std::vector<float> colors;
    if (arrays.size() > godot::Mesh::ARRAY_COLOR) {
        godot::PackedColorArray cs = arrays[godot::Mesh::ARRAY_COLOR];
        if (cs.size() == vc) {
            colors.resize(static_cast<size_t>(vc) * 4);
            for (int i = 0; i < vc; ++i) {
                godot::Color c = cs[i];
                colors[i * 4 + 0] = c.r;
                colors[i * 4 + 1] = c.g;
                colors[i * 4 + 2] = c.b;
                colors[i * 4 + 3] = c.a;
            }
        }
    }

    idtx_mesh_set_vertices(
        out, vc,
        positions.data(),
        normals.empty() ? nullptr : normals.data(),
        uvs.empty()     ? nullptr : uvs.data(),
        colors.empty()  ? nullptr : colors.data());

    std::vector<int32_t> idx_buf(indices.size());
    for (int i = 0; i < indices.size(); ++i) idx_buf[i] = indices[i];
    idtx_mesh_set_indices(out, indices.size(), idx_buf.data());

    // Skinning — Godot stores 4 bones/vertex by default. Both arrays
    // must be present and length-matched to set skinning data.
    if (arrays.size() > godot::Mesh::ARRAY_WEIGHTS) {
        godot::PackedInt32Array   bi = arrays[godot::Mesh::ARRAY_BONES];
        godot::PackedFloat32Array bw = arrays[godot::Mesh::ARRAY_WEIGHTS];
        if (bi.size() == bw.size() && bi.size() > 0 && (bi.size() % vc) == 0) {
            int bpv = bi.size() / vc;
            std::vector<int32_t> ibuf(bi.size());
            std::vector<float>   wbuf(bw.size());
            for (int i = 0; i < bi.size(); ++i) ibuf[i] = bi[i];
            for (int i = 0; i < bw.size(); ++i) wbuf[i] = bw[i];
            idtx_mesh_set_skinning(out, bpv, ibuf.data(), wbuf.data());
        }
    }

    return out;
}

// Translate a Godot Material into an idtx_material_t (StandardMaterial3D
// fields only for the MVP; MToon detection is a future cycle).
static ::idtx_material_t* BuildMaterial(godot::Ref<godot::Material> const& mat)
{
    if (mat.is_null()) return nullptr;
    auto* std_mat = godot::Object::cast_to<godot::StandardMaterial3D>(mat.ptr());
    ::idtx_material_t* out = ::idtx_material_create();
    if (std_mat != nullptr) {
        godot::String name = std_mat->get_name();
        if (name.is_empty()) name = godot::String("Material");
        idtx_material_set_name(out, name.utf8().get_data());

        godot::Color albedo = std_mat->get_albedo();
        idtx_material_set_base_color(out, albedo.r, albedo.g, albedo.b, albedo.a);
        idtx_material_set_metallic(out, std_mat->get_metallic());
        idtx_material_set_roughness(out, std_mat->get_roughness());
    } else {
        idtx_material_set_name(out, "Material");
    }
    return out;
}

// Walk node + descendants, collecting every MeshInstance3D into the
// avatar. Each surface produces one idtx_mesh_t; surface materials get
// deduplicated into the avatar's material list by pointer identity.
struct MaterialCache
{
    std::vector<godot::Material*> seen;
    std::vector<int32_t>          indices;  // parallel to seen
};

static int32_t AddOrLookupMaterial(
    ::idtx_avatar_t* avatar,
    MaterialCache& cache,
    godot::Ref<godot::Material> const& mat)
{
    if (mat.is_null()) return -1;
    godot::Material* ptr = mat.ptr();
    for (size_t i = 0; i < cache.seen.size(); ++i) {
        if (cache.seen[i] == ptr) return cache.indices[i];
    }
    int32_t idx = idtx_avatar_add_material(avatar, BuildMaterial(mat));
    cache.seen.push_back(ptr);
    cache.indices.push_back(idx);
    return idx;
}

static void CollectMeshes(
    godot::Node* node,
    ::idtx_avatar_t* avatar,
    MaterialCache& cache)
{
    if (node == nullptr) return;
    if (auto* mi = godot::Object::cast_to<godot::MeshInstance3D>(node)) {
        godot::Ref<godot::Mesh> mesh = mi->get_mesh();
        if (mesh.is_valid()) {
            int surface_count = mesh->get_surface_count();
            godot::String base = mi->get_name();
            for (int s = 0; s < surface_count; ++s) {
                ::idtx_mesh_t* m = BuildMeshFromSurface(mesh, s, base);
                if (m == nullptr) continue;
                godot::Ref<godot::Material> surf_mat = mi->get_active_material(s);
                int32_t mat_idx = AddOrLookupMaterial(avatar, cache, surf_mat);
                idtx_avatar_add_mesh(avatar, m, mat_idx);
            }
        }
    }
    int n = node->get_child_count();
    for (int i = 0; i < n; ++i) {
        CollectMeshes(node->get_child(i), avatar, cache);
    }
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

    MaterialCache cache;
    CollectMeshes(root, avatar, cache);

    return avatar;
}

}  // namespace idtxflow::exporter
