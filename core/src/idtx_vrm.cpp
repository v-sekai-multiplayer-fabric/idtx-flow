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

    // Plan the node table: index 0 is the avatar root; bones (if any
    // skeleton attached) get sequential indices after it. We need the
    // bone -> node-index map twice: once to write nodes[].children and
    // once to write humanBones[boneName].node.
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

        j.key("extensionsUsed"); j.begin_array();
            j.string("VRMC_vrm");
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

    std::vector<uint8_t> empty_bin;
    if (!idtx::core::detail::write_glb(j.str(), empty_bin, path)) return 3;
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
