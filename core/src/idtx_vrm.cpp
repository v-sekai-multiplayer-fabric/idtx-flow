// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// VRM 1.0 serializer — writes a glTF binary (GLB) with the VRMC_vrm
// extension. Imports follow the same structure in reverse.

#include "idtx_core/idtx_core.h"
#include "idtx_core/internal/json_writer.h"
#include "idtx_core/internal/vrm_humanoid_bones.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace idtx::core::detail {

// -----------------------------------------------------------------
// GLB container — 12-byte header + 1..N chunks.
//   header: magic(0x46546C67) version(2) length(total bytes)
//   chunk:  length(payload bytes) type(JSON=0x4E4F534A, BIN=0x004E4942)
//   payloads padded to 4-byte alignment with 0x20 (JSON) or 0x00 (BIN).
// -----------------------------------------------------------------

static constexpr uint32_t GLB_MAGIC      = 0x46546C67;  // "glTF"
static constexpr uint32_t GLB_VERSION    = 2;
static constexpr uint32_t CHUNK_JSON     = 0x4E4F534A;  // "JSON"
static constexpr uint32_t CHUNK_BIN      = 0x004E4942;  // "BIN\0"

static void pad_to_4(std::vector<uint8_t>& v, uint8_t pad)
{
    while ((v.size() % 4) != 0) v.push_back(pad);
}

// Append `count` bytes from `src` to `bin`, returning the byte offset
// at which they were written.
static uint32_t append_to_bin(std::vector<uint8_t>& bin, void const* src, size_t count)
{
    uint32_t offset = static_cast<uint32_t>(bin.size());
    auto const* p = static_cast<uint8_t const*>(src);
    bin.insert(bin.end(), p, p + count);
    // 4-byte align inside the BIN chunk — accessor offsets must be
    // multiples of their component size; padding here keeps subsequent
    // float/int accessors aligned for free.
    while ((bin.size() % 4) != 0) bin.push_back(0);
    return offset;
}

static bool write_glb(
    std::string const& json,
    std::vector<uint8_t> const& bin,
    char const* path)
{
    std::vector<uint8_t> j(json.begin(), json.end());
    pad_to_4(j, 0x20);  // pad JSON chunk with spaces
    std::vector<uint8_t> b = bin;
    pad_to_4(b, 0x00);

    uint32_t json_len = static_cast<uint32_t>(j.size());
    uint32_t bin_len  = static_cast<uint32_t>(b.size());

    uint32_t total = 12  // header
                   + 8 + json_len
                   + (bin_len > 0 ? 8 + bin_len : 0);

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    auto u32 = [&](uint32_t v) {
        f.write(reinterpret_cast<char const*>(&v), 4);
    };
    u32(GLB_MAGIC);
    u32(GLB_VERSION);
    u32(total);

    u32(json_len);
    u32(CHUNK_JSON);
    f.write(reinterpret_cast<char const*>(j.data()), j.size());

    if (bin_len > 0) {
        u32(bin_len);
        u32(CHUNK_BIN);
        f.write(reinterpret_cast<char const*>(b.data()), b.size());
    }
    return f.good();
}

}  // namespace idtx::core::detail

extern "C" IDTX_CORE_API int32_t idtx_core_export_avatar_to_vrm(
    const idtx_avatar_t* avatar,
    const char* path)
{
    if (avatar == nullptr || path == nullptr) return 1;

    // ----- BIN chunk accumulator + accessor/bufferView descriptors -----
    std::vector<uint8_t> bin;

    struct BufferView { uint32_t byte_offset; uint32_t byte_length; int target; };
    struct Accessor   { int buffer_view; int component_type; int count;
                        std::string type; bool has_min_max;
                        float min3[3]; float max3[3]; };

    std::vector<BufferView> buffer_views;
    std::vector<Accessor>   accessors;

    // glTF constants
    constexpr int CT_UINT32  = 5125;
    constexpr int CT_FLOAT   = 5126;
    constexpr int TARGET_ARRAY_BUFFER         = 34962;
    constexpr int TARGET_ELEMENT_ARRAY_BUFFER = 34963;

    // Per-mesh accessor planning. Index into mesh_primitives is the
    // glTF mesh index; each entry stores accessor indices for its
    // attributes + indices accessor.
    struct PrimitivePlan {
        int positions_accessor = -1;
        int normals_accessor   = -1;
        int uvs_accessor       = -1;
        int indices_accessor   = -1;
    };
    int32_t mesh_count = idtx_avatar_get_mesh_count(avatar);
    std::vector<PrimitivePlan> mesh_primitives(mesh_count);

    for (int32_t mi = 0; mi < mesh_count; ++mi) {
        auto* mh = idtx_avatar_get_mesh(avatar, mi);
        if (mh == nullptr) continue;
        int32_t vc = idtx_mesh_get_vertex_count(mh);
        int32_t ic = idtx_mesh_get_index_count(mh);
        if (vc <= 0 || ic <= 0) continue;

        // positions
        std::vector<float> pos(static_cast<size_t>(vc) * 3);
        idtx_mesh_get_positions(mh, pos.data());
        uint32_t off = idtx::core::detail::append_to_bin(bin, pos.data(), pos.size() * sizeof(float));
        Accessor a;
        a.buffer_view = static_cast<int>(buffer_views.size());
        a.component_type = CT_FLOAT;
        a.count = vc;
        a.type = "VEC3";
        a.has_min_max = true;
        a.min3[0] = a.min3[1] = a.min3[2] =  1e30f;
        a.max3[0] = a.max3[1] = a.max3[2] = -1e30f;
        for (int32_t v = 0; v < vc; ++v) {
            for (int c = 0; c < 3; ++c) {
                float x = pos[v * 3 + c];
                if (x < a.min3[c]) a.min3[c] = x;
                if (x > a.max3[c]) a.max3[c] = x;
            }
        }
        buffer_views.push_back({off, static_cast<uint32_t>(pos.size() * sizeof(float)), TARGET_ARRAY_BUFFER});
        mesh_primitives[mi].positions_accessor = static_cast<int>(accessors.size());
        accessors.push_back(a);

        // normals (optional)
        if (idtx_mesh_has_normals(mh)) {
            std::vector<float> nrm(static_cast<size_t>(vc) * 3);
            idtx_mesh_get_normals(mh, nrm.data());
            uint32_t no = idtx::core::detail::append_to_bin(bin, nrm.data(), nrm.size() * sizeof(float));
            Accessor an; an.buffer_view = static_cast<int>(buffer_views.size());
            an.component_type = CT_FLOAT; an.count = vc; an.type = "VEC3"; an.has_min_max = false;
            buffer_views.push_back({no, static_cast<uint32_t>(nrm.size() * sizeof(float)), TARGET_ARRAY_BUFFER});
            mesh_primitives[mi].normals_accessor = static_cast<int>(accessors.size());
            accessors.push_back(an);
        }

        // uvs (optional)
        if (idtx_mesh_has_uvs(mh)) {
            std::vector<float> uvs(static_cast<size_t>(vc) * 2);
            idtx_mesh_get_uvs(mh, uvs.data());
            uint32_t uo = idtx::core::detail::append_to_bin(bin, uvs.data(), uvs.size() * sizeof(float));
            Accessor au; au.buffer_view = static_cast<int>(buffer_views.size());
            au.component_type = CT_FLOAT; au.count = vc; au.type = "VEC2"; au.has_min_max = false;
            buffer_views.push_back({uo, static_cast<uint32_t>(uvs.size() * sizeof(float)), TARGET_ARRAY_BUFFER});
            mesh_primitives[mi].uvs_accessor = static_cast<int>(accessors.size());
            accessors.push_back(au);
        }

        // indices (uint32 — glTF doesn't have uint16/uint32 ambiguity
        // when we always pick uint32 for the export).
        std::vector<int32_t> idx(static_cast<size_t>(ic));
        idtx_mesh_get_indices(mh, idx.data());
        std::vector<uint32_t> idxu(idx.begin(), idx.end());
        uint32_t io = idtx::core::detail::append_to_bin(bin, idxu.data(), idxu.size() * sizeof(uint32_t));
        Accessor ai; ai.buffer_view = static_cast<int>(buffer_views.size());
        ai.component_type = CT_UINT32; ai.count = ic; ai.type = "SCALAR"; ai.has_min_max = false;
        buffer_views.push_back({io, static_cast<uint32_t>(idxu.size() * sizeof(uint32_t)), TARGET_ELEMENT_ARRAY_BUFFER});
        mesh_primitives[mi].indices_accessor = static_cast<int>(accessors.size());
        accessors.push_back(ai);
    }

    // ---------------------------------------------------------------
    auto* skel = idtx_avatar_get_skeleton(avatar);
    int32_t bone_count = (skel != nullptr) ? idtx_skeleton_get_bone_count(skel) : 0;
    int32_t root_node_index = 0;
    auto bone_node_index = [&](int32_t bone) -> int32_t {
        return (bone < 0) ? root_node_index : (1 + bone);
    };

    // For humanoid mapping, walk the bones once to find which bone
    // (if any) carries each VRM 1.0 humanoid name.
    std::unordered_map<std::string, int32_t> humanoid_to_bone;
    if (skel != nullptr) {
        for (int32_t i = 0; i < bone_count; ++i) {
            const char* bn = idtx_skeleton_get_bone_name(skel, i);
            if (idtx::core::vrm::is_humanoid_bone(bn)) {
                humanoid_to_bone[bn] = i;
            }
        }
    }

    idtx::core::JsonWriter j;
    j.begin_object();
        j.key("asset"); j.begin_object();
            j.key("version");   j.string("2.0");
            j.key("generator"); j.string("idtx-flow / libidtx_core");
        j.end_object();

        // extensionsUsed — VRMC_vrm is unconditional; VRMC_materials_mtoon
        // is added only if any avatar material is MToon-flagged.
        int32_t mat_count_total = idtx_avatar_get_material_count(avatar);
        bool any_mtoon = false;
        for (int32_t mi = 0; mi < mat_count_total; ++mi) {
            auto* mat = idtx_avatar_get_material(avatar, mi);
            if (mat != nullptr && idtx_material_is_mtoon(mat)) { any_mtoon = true; break; }
        }
        j.key("extensionsUsed"); j.begin_array();
            j.string("VRMC_vrm");
            if (any_mtoon) j.string("VRMC_materials_mtoon");
        j.end_array();

        // Scene + scenes
        j.key("scene"); j.integer(0);
        j.key("scenes"); j.begin_array();
            j.begin_object();
                j.key("nodes"); j.begin_array(); j.integer(root_node_index); j.end_array();
            j.end_object();
        j.end_array();

        // Nodes — root first, then bones. Each bone references its
        // children via parent-of-i scan. Bones with no parent
        // (parent_index == -1) become children of the avatar root.
        j.key("nodes"); j.begin_array();
            // Root node — its children are the orphan bones.
            j.begin_object();
                j.key("name"); j.string(idtx_avatar_get_name(avatar));
                if (bone_count > 0) {
                    std::vector<int32_t> root_children;
                    for (int32_t i = 0; i < bone_count; ++i) {
                        if (idtx_skeleton_get_bone_parent(skel, i) < 0) {
                            root_children.push_back(bone_node_index(i));
                        }
                    }
                    if (!root_children.empty()) {
                        j.key("children");
                        j.int_array(root_children.data(), root_children.size());
                    }
                }
            j.end_object();

            // One node per bone, with children = bones whose parent
            // is this bone.
            for (int32_t i = 0; i < bone_count; ++i) {
                j.begin_object();
                    j.key("name"); j.string(idtx_skeleton_get_bone_name(skel, i));
                    std::vector<int32_t> children;
                    for (int32_t k = 0; k < bone_count; ++k) {
                        if (idtx_skeleton_get_bone_parent(skel, k) == i) {
                            children.push_back(bone_node_index(k));
                        }
                    }
                    if (!children.empty()) {
                        j.key("children");
                        j.int_array(children.data(), children.size());
                    }
                j.end_object();
            }
        j.end_array();

        // meshes
        if (!mesh_primitives.empty()) {
            j.key("meshes"); j.begin_array();
            for (int32_t mi = 0; mi < mesh_count; ++mi) {
                auto const& mp = mesh_primitives[mi];
                if (mp.positions_accessor < 0) continue;
                j.begin_object();
                    j.key("name");
                    auto* mh = idtx_avatar_get_mesh(avatar, mi);
                    j.string(mh != nullptr ? idtx_mesh_get_name(mh) : "");
                    j.key("primitives"); j.begin_array();
                        j.begin_object();
                            j.key("attributes"); j.begin_object();
                                j.key("POSITION"); j.integer(mp.positions_accessor);
                                if (mp.normals_accessor >= 0) {
                                    j.key("NORMAL"); j.integer(mp.normals_accessor);
                                }
                                if (mp.uvs_accessor >= 0) {
                                    j.key("TEXCOORD_0"); j.integer(mp.uvs_accessor);
                                }
                            j.end_object();
                            if (mp.indices_accessor >= 0) {
                                j.key("indices"); j.integer(mp.indices_accessor);
                            }
                            int32_t mat_index = idtx_avatar_get_mesh_material(avatar, mi);
                            if (mat_index >= 0 && mat_index < mat_count_total) {
                                j.key("material"); j.integer(mat_index);
                            }
                            j.key("mode"); j.integer(4);  // TRIANGLES
                        j.end_object();
                    j.end_array();
                j.end_object();
            }
            j.end_array();
        }

        // materials
        if (mat_count_total > 0) {
            j.key("materials"); j.begin_array();
            for (int32_t mi = 0; mi < mat_count_total; ++mi) {
                auto* mat = idtx_avatar_get_material(avatar, mi);
                j.begin_object();
                    if (mat != nullptr) {
                        j.key("name"); j.string(idtx_material_get_name(mat));
                        float rgba[4]; idtx_material_get_base_color(mat, rgba);
                        j.key("pbrMetallicRoughness"); j.begin_object();
                            j.key("baseColorFactor"); j.float_array(rgba, 4);
                            j.key("metallicFactor");  j.number(idtx_material_get_metallic(mat));
                            j.key("roughnessFactor"); j.number(idtx_material_get_roughness(mat));
                        j.end_object();

                        idtx_alpha_mode_t am = idtx_material_get_alpha_mode(mat);
                        if (am == IDTX_ALPHA_MASK) {
                            j.key("alphaMode"); j.string("MASK");
                            j.key("alphaCutoff"); j.number(idtx_material_get_alpha_cutoff(mat));
                        } else if (am == IDTX_ALPHA_BLEND) {
                            j.key("alphaMode"); j.string("BLEND");
                        }

                        if (idtx_material_is_mtoon(mat)) {
                            j.key("extensions"); j.begin_object();
                                j.key("VRMC_materials_mtoon"); j.begin_object();
                                    j.key("specVersion"); j.string("1.0");
                                    float shade[3]; idtx_material_get_mtoon_shade_color(mat, shade);
                                    float rim[3];   idtx_material_get_mtoon_rim_color(mat, rim);
                                    j.key("shadeColorFactor");        j.float_array(shade, 3);
                                    j.key("parametricRimColorFactor"); j.float_array(rim, 3);
                                    j.key("outlineWidthMode"); j.string("worldCoordinates");
                                    j.key("outlineWidthFactor");
                                    j.number(idtx_material_get_mtoon_outline_width(mat));
                                j.end_object();
                            j.end_object();
                        }
                    }
                j.end_object();
            }
            j.end_array();
        }

        // accessors
        if (!accessors.empty()) {
            j.key("accessors"); j.begin_array();
            for (auto const& a : accessors) {
                j.begin_object();
                    j.key("bufferView");    j.integer(a.buffer_view);
                    j.key("componentType"); j.integer(a.component_type);
                    j.key("count");         j.integer(a.count);
                    j.key("type");          j.string(a.type);
                    if (a.has_min_max) {
                        j.key("min"); j.float_array(a.min3, 3);
                        j.key("max"); j.float_array(a.max3, 3);
                    }
                j.end_object();
            }
            j.end_array();
        }

        // bufferViews
        if (!buffer_views.empty()) {
            j.key("bufferViews"); j.begin_array();
            for (auto const& bv : buffer_views) {
                j.begin_object();
                    j.key("buffer");     j.integer(0);
                    j.key("byteOffset"); j.integer(bv.byte_offset);
                    j.key("byteLength"); j.integer(bv.byte_length);
                    j.key("target");     j.integer(bv.target);
                j.end_object();
            }
            j.end_array();
        }

        // buffers — single embedded BIN chunk if any geometry data was written
        if (!bin.empty()) {
            j.key("buffers"); j.begin_array();
                j.begin_object();
                    j.key("byteLength"); j.integer(static_cast<int64_t>(bin.size()));
                j.end_object();
            j.end_array();
        }

        j.key("extensions"); j.begin_object();
            j.key("VRMC_vrm"); j.begin_object();
                j.key("specVersion"); j.string("1.0");
                j.key("meta"); j.begin_object();
                    j.key("name"); j.string(idtx_avatar_get_name(avatar));
                    j.key("version"); j.string("1.0");
                    j.key("authors"); j.begin_array(); j.string("idtx-flow"); j.end_array();
                    j.key("licenseUrl"); j.string("https://vrm.dev/licenses/1.0/");
                j.end_object();
                j.key("humanoid"); j.begin_object();
                    j.key("humanBones"); j.begin_object();
                        for (auto const& kv : humanoid_to_bone) {
                            j.key(kv.first.c_str());
                            j.begin_object();
                                j.key("node"); j.integer(bone_node_index(kv.second));
                            j.end_object();
                        }
                    j.end_object();
                j.end_object();
            j.end_object();
        j.end_object();
    j.end_object();

    if (!idtx::core::detail::write_glb(j.str(), bin, path)) return 3;
    return 0;
}

extern "C" IDTX_CORE_API idtx_avatar_t* idtx_core_import_avatar_from_vrm(
    const char* path)
{
    (void)path;
    // TODO(Phase 7.5+): parse GLB, walk nodes/meshes/skins, read
    // VRMC_vrm extension for humanoid + meta, return populated avatar.
    return nullptr;
}
