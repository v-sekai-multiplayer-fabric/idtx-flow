// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// VRM 1.0 importer — parses a .vrm (GLB) via cgltf, walks glTF
// nodes / meshes / skins, and rebuilds an idtx_avatar_t* with
// skeleton + meshes + materials + spring bones from the
// VRMC_vrm / VRMC_materials_mtoon / VRMC_springBone extensions.
//
// This translation unit is the single CGLTF_IMPLEMENTATION host —
// keep it that way, or the multiply-defined cgltf symbols will
// break linking.

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "idtx_core/idtx_core.h"
#include "idtx_core/internal/vrm_springbone_parse.h"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// Copy a cgltf_float[16] into idtx's row-major float[16]. cgltf stores
// matrices column-major (glTF spec); idtx_core uses row-major. Same
// transpose we do on the export side, just in reverse.
static void cgltf_matrix_to_idtx(cgltf_float const m[16], float out[16])
{
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            out[row * 4 + col] = static_cast<float>(m[col * 4 + row]);
        }
    }
}

// Build a row-major float[16] from a cgltf_node's TRS (or matrix).
static void cgltf_node_local_matrix(cgltf_node const& n, float out[16])
{
    cgltf_float m[16];
    cgltf_node_transform_local(&n, m);
    cgltf_matrix_to_idtx(m, out);
}

// Same, but world (after walking up the parent chain).
static void cgltf_node_world_matrix(cgltf_node const& n, float out[16])
{
    cgltf_float m[16];
    cgltf_node_transform_world(&n, m);
    cgltf_matrix_to_idtx(m, out);
}

// Read a contiguous float accessor into a host vector.
static bool read_floats(cgltf_accessor const* acc, std::vector<float>& out)
{
    if (acc == nullptr) return false;
    size_t comps = cgltf_num_components(acc->type);
    out.resize(acc->count * comps);
    return cgltf_accessor_unpack_floats(acc, out.data(), out.size()) == out.size();
}

// Read an index accessor (any int type) into uint32_t.
static bool read_indices(cgltf_accessor const* acc, std::vector<uint32_t>& out)
{
    if (acc == nullptr) return false;
    out.resize(acc->count);
    for (size_t i = 0; i < acc->count; ++i) {
        out[i] = static_cast<uint32_t>(cgltf_accessor_read_index(acc, i));
    }
    return true;
}

// Read a uint accessor (typically JOINTS_0 as uint8 or uint16) into int32_t.
static bool read_uint_array(cgltf_accessor const* acc, std::vector<int32_t>& out)
{
    if (acc == nullptr) return false;
    size_t comps = cgltf_num_components(acc->type);
    out.resize(acc->count * comps);
    for (size_t i = 0; i < acc->count; ++i) {
        cgltf_uint vals[16];
        cgltf_accessor_read_uint(acc, i, vals, comps);
        for (size_t k = 0; k < comps; ++k) {
            out[i * comps + k] = static_cast<int32_t>(vals[k]);
        }
    }
    return true;
}

// ---------------------------------------------------------------
// (jsmn-based VRMC_springBone parser lives in idtx_vrm_springbone_parse.cpp
// to avoid colliding with cgltf's bundled copy of jsmn. The helpers
// below are dead code stubs left in place to minimise the diff —
// they get removed in a follow-up cleanup once the new path soaks.)
// ---------------------------------------------------------------

#if 0  // moved out — see vrm_springbone_parse.{h,cpp}
static bool token_eq(const char* json, jsmntok_t const& tok, const char* s)
{
    return tok.type == JSMN_STRING
        && (int)std::strlen(s) == tok.end - tok.start
        && std::strncmp(json + tok.start, s, tok.end - tok.start) == 0;
}

static std::string token_string(const char* json, jsmntok_t const& tok)
{
    return std::string(json + tok.start, tok.end - tok.start);
}

static int token_int(const char* json, jsmntok_t const& tok)
{
    std::string s = token_string(json, tok);
    return std::atoi(s.c_str());
}

static float token_float(const char* json, jsmntok_t const& tok)
{
    std::string s = token_string(json, tok);
    return static_cast<float>(std::atof(s.c_str()));
}

// Advance past the token at `idx` and any descendants. Returns the
// next sibling's index (or end of tokens). Walks object/array sizes.
static int skip_subtree(jsmntok_t const* tokens, int ntok, int idx)
{
    if (idx >= ntok) return idx;
    if (tokens[idx].type == JSMN_OBJECT) {
        int n = tokens[idx].size;
        int j = idx + 1;
        for (int k = 0; k < n; ++k) {
            j = skip_subtree(tokens, ntok, j);   // key (primitive)
            j = skip_subtree(tokens, ntok, j);   // value (any)
        }
        return j;
    }
    if (tokens[idx].type == JSMN_ARRAY) {
        int n = tokens[idx].size;
        int j = idx + 1;
        for (int k = 0; k < n; ++k) j = skip_subtree(tokens, ntok, j);
        return j;
    }
    return idx + 1;
}

// Find a key inside the object at `obj_idx`. Returns the value's
// token index, or -1 if not found.
static int object_find(
    const char* json,
    jsmntok_t const* tokens, int ntok,
    int obj_idx, const char* key)
{
    if (obj_idx >= ntok || tokens[obj_idx].type != JSMN_OBJECT) return -1;
    int n = tokens[obj_idx].size;
    int j = obj_idx + 1;
    for (int k = 0; k < n; ++k) {
        if (j >= ntok) return -1;
        if (token_eq(json, tokens[j], key)) return j + 1;
        j = skip_subtree(tokens, ntok, j);   // key
        j = skip_subtree(tokens, ntok, j);   // value
    }
    return -1;
}

// Extract a 3-float array from the token at `arr_idx`. Returns false
// if not an array of >= 3 primitives.
static bool array_to_vec3(
    const char* json,
    jsmntok_t const* tokens, int ntok,
    int arr_idx, float out[3])
{
    if (arr_idx < 0 || arr_idx >= ntok || tokens[arr_idx].type != JSMN_ARRAY) return false;
    int n = tokens[arr_idx].size;
    if (n < 3) return false;
    int j = arr_idx + 1;
    for (int k = 0; k < 3 && j < ntok; ++k) {
        out[k] = token_float(json, tokens[j]);
        j = skip_subtree(tokens, ntok, j);
    }
    return true;
}

static void parse_springbone_json(
    idtx_avatar_t* avatar,
    cgltf_data const* data,
    std::unordered_map<cgltf_node const*, int32_t> const& node_to_bone,
    const char* json,
    size_t json_len)
{
    // Pass 1: count tokens.
    jsmn_parser p;
    jsmn_init(&p);
    int needed = jsmn_parse(&p, json, json_len, nullptr, 0);
    if (needed <= 0) return;

    std::vector<jsmntok_t> tokens(needed);
    jsmn_init(&p);
    int ntok = jsmn_parse(&p, json, json_len, tokens.data(), needed);
    if (ntok <= 0) return;

    // Helper: map a glTF node index (as written in JSON) to the
    // matching bone index in the avatar's skeleton. Returns -1 if
    // the node isn't a bone in the skin.
    auto gltf_node_to_bone = [&](int gltf_node_idx) -> int32_t {
        if (gltf_node_idx < 0
            || static_cast<size_t>(gltf_node_idx) >= data->nodes_count) return -1;
        auto it = node_to_bone.find(&data->nodes[gltf_node_idx]);
        return (it != node_to_bone.end()) ? it->second : -1;
    };

    // ---- colliders ----
    int colliders_arr = object_find(json, tokens.data(), ntok, 0, "colliders");
    std::vector<int32_t> collider_idx_map;   // VRM collider index -> idtx index
    if (colliders_arr >= 0 && tokens[colliders_arr].type == JSMN_ARRAY) {
        int n = tokens[colliders_arr].size;
        int j = colliders_arr + 1;
        for (int k = 0; k < n && j < ntok; ++k) {
            int obj = j;
            int v;
            int attached_bone = -1;
            if ((v = object_find(json, tokens.data(), ntok, obj, "node")) >= 0) {
                attached_bone = gltf_node_to_bone(token_int(json, tokens[v]));
            }
            idtx_spring_collider_t* col = idtx_spring_collider_create();
            idtx_spring_collider_set_attached_bone(col, attached_bone);

            int shape = object_find(json, tokens.data(), ntok, obj, "shape");
            int sphere = (shape >= 0) ? object_find(json, tokens.data(), ntok, shape, "sphere")  : -1;
            int capsule = (shape >= 0) ? object_find(json, tokens.data(), ntok, shape, "capsule") : -1;
            int prim = (capsule >= 0) ? capsule : sphere;
            if (prim >= 0) {
                idtx_spring_collider_set_shape(col,
                    capsule >= 0 ? IDTX_COLLIDER_CAPSULE : IDTX_COLLIDER_SPHERE);
                float off[3] = {0, 0, 0};
                int off_idx = object_find(json, tokens.data(), ntok, prim, "offset");
                if (off_idx >= 0) array_to_vec3(json, tokens.data(), ntok, off_idx, off);
                idtx_spring_collider_set_offset(col, off[0], off[1], off[2]);
                int r_idx = object_find(json, tokens.data(), ntok, prim, "radius");
                if (r_idx >= 0) idtx_spring_collider_set_radius(col, token_float(json, tokens[r_idx]));
                if (capsule >= 0) {
                    float tail[3] = {0, 0, 0};
                    int t_idx = object_find(json, tokens.data(), ntok, prim, "tail");
                    if (t_idx >= 0) array_to_vec3(json, tokens.data(), ntok, t_idx, tail);
                    idtx_spring_collider_set_tail(col, tail[0], tail[1], tail[2]);
                }
            }
            int32_t added = idtx_avatar_add_spring_collider(avatar, col);
            collider_idx_map.push_back(added);

            j = skip_subtree(tokens.data(), ntok, j);
        }
    }

    // ---- colliderGroups ----
    int groups_arr = object_find(json, tokens.data(), ntok, 0, "colliderGroups");
    std::vector<std::vector<int32_t>> group_collider_indices;
    if (groups_arr >= 0 && tokens[groups_arr].type == JSMN_ARRAY) {
        int n = tokens[groups_arr].size;
        int j = groups_arr + 1;
        for (int k = 0; k < n && j < ntok; ++k) {
            std::vector<int32_t> cols;
            int cols_idx = object_find(json, tokens.data(), ntok, j, "colliders");
            if (cols_idx >= 0 && tokens[cols_idx].type == JSMN_ARRAY) {
                int m = tokens[cols_idx].size;
                int c = cols_idx + 1;
                for (int u = 0; u < m && c < ntok; ++u) {
                    int vrm_collider_idx = token_int(json, tokens[c]);
                    if (vrm_collider_idx >= 0
                        && static_cast<size_t>(vrm_collider_idx) < collider_idx_map.size()) {
                        cols.push_back(collider_idx_map[vrm_collider_idx]);
                    }
                    c = skip_subtree(tokens.data(), ntok, c);
                }
            }
            group_collider_indices.push_back(std::move(cols));
            j = skip_subtree(tokens.data(), ntok, j);
        }
    }

    // ---- springs ----
    int springs_arr = object_find(json, tokens.data(), ntok, 0, "springs");
    if (springs_arr >= 0 && tokens[springs_arr].type == JSMN_ARRAY) {
        int n = tokens[springs_arr].size;
        int j = springs_arr + 1;
        for (int k = 0; k < n && j < ntok; ++k) {
            idtx_spring_chain_t* chain = idtx_spring_chain_create();
            int name_idx = object_find(json, tokens.data(), ntok, j, "name");
            if (name_idx >= 0) {
                std::string nm = token_string(json, tokens[name_idx]);
                idtx_spring_chain_set_name(chain, nm.c_str());
            }
            // joints: collect bone indices; pull dynamics from joints[0].
            int joints_idx = object_find(json, tokens.data(), ntok, j, "joints");
            std::vector<int32_t> bone_idxs;
            float stiff = 1.0f, drag = 0.4f, grav_p = 0.0f, hit_r = 0.02f;
            float gdir[3] = {0, -1, 0};
            if (joints_idx >= 0 && tokens[joints_idx].type == JSMN_ARRAY) {
                int m = tokens[joints_idx].size;
                int jj = joints_idx + 1;
                for (int u = 0; u < m && jj < ntok; ++u) {
                    int node_idx = object_find(json, tokens.data(), ntok, jj, "node");
                    if (node_idx >= 0) {
                        int32_t bi = gltf_node_to_bone(token_int(json, tokens[node_idx]));
                        if (bi >= 0) bone_idxs.push_back(bi);
                    }
                    if (u == 0) {
                        int v;
                        if ((v = object_find(json, tokens.data(), ntok, jj, "hitRadius"))    >= 0) hit_r  = token_float(json, tokens[v]);
                        if ((v = object_find(json, tokens.data(), ntok, jj, "stiffness"))    >= 0) stiff  = token_float(json, tokens[v]);
                        if ((v = object_find(json, tokens.data(), ntok, jj, "gravityPower")) >= 0) grav_p = token_float(json, tokens[v]);
                        if ((v = object_find(json, tokens.data(), ntok, jj, "dragForce"))    >= 0) drag   = token_float(json, tokens[v]);
                        if ((v = object_find(json, tokens.data(), ntok, jj, "gravityDir"))   >= 0)
                            array_to_vec3(json, tokens.data(), ntok, v, gdir);
                    }
                    jj = skip_subtree(tokens.data(), ntok, jj);
                }
            }
            idtx_spring_chain_set_dynamics(chain, stiff, drag, grav_p, hit_r);
            idtx_spring_chain_set_gravity_dir(chain, gdir[0], gdir[1], gdir[2]);
            if (!bone_idxs.empty()) {
                idtx_spring_chain_set_joints(chain, static_cast<int32_t>(bone_idxs.size()), bone_idxs.data());
            }
            // colliderGroups -> collider indices via the lookup table
            int cg_idx = object_find(json, tokens.data(), ntok, j, "colliderGroups");
            if (cg_idx >= 0 && tokens[cg_idx].type == JSMN_ARRAY) {
                int m = tokens[cg_idx].size;
                int c = cg_idx + 1;
                for (int u = 0; u < m && c < ntok; ++u) {
                    int group_index = token_int(json, tokens[c]);
                    if (group_index >= 0 && static_cast<size_t>(group_index) < group_collider_indices.size()) {
                        for (int32_t ci : group_collider_indices[group_index]) {
                            idtx_spring_chain_add_collider(chain, ci);
                        }
                    }
                    c = skip_subtree(tokens.data(), ntok, c);
                }
            }
            idtx_avatar_add_spring_chain(avatar, chain);
            j = skip_subtree(tokens.data(), ntok, j);
        }
    }
}
#endif  // moved out — see vrm_springbone_parse.{h,cpp}

}  // namespace

extern "C" IDTX_CORE_API idtx_avatar_t* idtx_core_import_avatar_from_vrm(const char* path)
{
    if (path == nullptr) return nullptr;

    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        return nullptr;
    }
    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
        cgltf_free(data);
        return nullptr;
    }

    idtx_avatar_t* avatar = idtx_avatar_create();

    // Avatar name + root transform: use the first scene's first root
    // node. Sufficient for the avatars idtx-flow writes (single root).
    cgltf_node* root_node = nullptr;
    if (data->scenes_count > 0 && data->scenes[0].nodes_count > 0) {
        root_node = data->scenes[0].nodes[0];
    } else if (data->nodes_count > 0) {
        root_node = &data->nodes[0];
    }
    if (root_node != nullptr) {
        idtx_avatar_set_name(avatar, root_node->name ? root_node->name : "");
        float rm[16];
        cgltf_node_local_matrix(*root_node, rm);
        idtx_avatar_set_root_transform(avatar, rm);
    }

    // Skeleton from skins[0]. VRM 1.0 avatars consistently have a
    // single skin covering every bone; we don't try to merge multi-skin
    // glTFs in this MVP.
    std::unordered_map<cgltf_node const*, int32_t> node_to_bone;
    if (data->skins_count > 0) {
        cgltf_skin const& skin = data->skins[0];
        idtx_skeleton_t* skel = idtx_skeleton_create();
        idtx_skeleton_set_name(skel, skin.name ? skin.name : "Skeleton");

        // First pass: assign bone indices in the order skin.joints
        // appears. Parent resolution can then be done in a second
        // pass since every parent is already in the map.
        for (size_t i = 0; i < skin.joints_count; ++i) {
            node_to_bone[skin.joints[i]] = static_cast<int32_t>(i);
        }

        // inverseBindMatrices accessor — used to recover the bone's
        // bind transform (= inverse of the IBM).
        std::vector<float> ibms;
        bool have_ibms = (skin.inverse_bind_matrices != nullptr)
                       && read_floats(skin.inverse_bind_matrices, ibms);

        for (size_t i = 0; i < skin.joints_count; ++i) {
            cgltf_node const& bn = *skin.joints[i];
            int32_t parent = -1;
            auto it = (bn.parent != nullptr) ? node_to_bone.find(bn.parent)
                                              : node_to_bone.end();
            if (it != node_to_bone.end()) parent = it->second;

            float rest[16];
            cgltf_node_local_matrix(bn, rest);

            float bind[16];
            if (have_ibms && (i + 1) * 16 <= ibms.size()) {
                // bind = inverse(IBM). cgltf gives us IBM column-major;
                // invert via the world-pose accessor (cgltf computes it).
                cgltf_node_world_matrix(bn, bind);
            } else {
                cgltf_node_world_matrix(bn, bind);
            }

            idtx_skeleton_add_bone(skel, bn.name ? bn.name : "", parent, rest, bind);
        }
        idtx_avatar_set_skeleton(avatar, skel);
    }

    // Materials — index in glTF == index in our avatar's materials
    // list. cgltf already resolves the extension data into typed
    // structs, including VRMC_materials_mtoon's extras as JSON text
    // on the material's `extensions` field.
    std::unordered_map<cgltf_material const*, int32_t> mat_to_idx;
    for (size_t i = 0; i < data->materials_count; ++i) {
        cgltf_material const& gm = data->materials[i];
        idtx_material_t* m = idtx_material_create();
        idtx_material_set_name(m, gm.name ? gm.name : "");
        if (gm.has_pbr_metallic_roughness) {
            auto const& p = gm.pbr_metallic_roughness;
            idtx_material_set_base_color(m, p.base_color_factor[0],
                                            p.base_color_factor[1],
                                            p.base_color_factor[2],
                                            p.base_color_factor[3]);
            idtx_material_set_metallic(m,  p.metallic_factor);
            idtx_material_set_roughness(m, p.roughness_factor);
        }
        if (gm.alpha_mode == cgltf_alpha_mode_mask) {
            idtx_material_set_alpha_mode(m, IDTX_ALPHA_MASK);
            idtx_material_set_alpha_cutoff(m, gm.alpha_cutoff);
        } else if (gm.alpha_mode == cgltf_alpha_mode_blend) {
            idtx_material_set_alpha_mode(m, IDTX_ALPHA_BLEND);
        }
        // MToon detection — cgltf surfaces extensions as a JSON blob;
        // a name-substring check is enough to flag the material so
        // downstream renderers know to apply a toon shader.
        for (size_t e = 0; e < gm.extensions_count; ++e) {
            if (gm.extensions[e].name != nullptr
                && std::strcmp(gm.extensions[e].name, "VRMC_materials_mtoon") == 0) {
                // Trigger MToon flag via a setter (any of them flips
                // it on). Detailed parameter recovery via JSON parse
                // is a follow-up; the flag alone is enough for the
                // export round trip to re-add VRMC_materials_mtoon.
                idtx_material_set_mtoon_outline_width(m, 0.0f);
                break;
            }
        }
        mat_to_idx[&gm] = idtx_avatar_add_material(avatar, m);
    }

    // Meshes — each glTF mesh primitive becomes one idtx_mesh.
    for (size_t mi = 0; mi < data->meshes_count; ++mi) {
        cgltf_mesh const& gm = data->meshes[mi];
        for (size_t pi = 0; pi < gm.primitives_count; ++pi) {
            cgltf_primitive const& p = gm.primitives[pi];
            cgltf_accessor const* pos_acc = nullptr;
            cgltf_accessor const* nrm_acc = nullptr;
            cgltf_accessor const* uv_acc  = nullptr;
            cgltf_accessor const* col_acc = nullptr;
            cgltf_accessor const* joi_acc = nullptr;
            cgltf_accessor const* wgt_acc = nullptr;
            for (size_t a = 0; a < p.attributes_count; ++a) {
                cgltf_attribute const& at = p.attributes[a];
                switch (at.type) {
                    case cgltf_attribute_type_position: pos_acc = at.data; break;
                    case cgltf_attribute_type_normal:   nrm_acc = at.data; break;
                    case cgltf_attribute_type_texcoord:
                        if (at.index == 0) uv_acc = at.data; break;
                    case cgltf_attribute_type_color:    col_acc = at.data; break;
                    case cgltf_attribute_type_joints:
                        if (at.index == 0) joi_acc = at.data; break;
                    case cgltf_attribute_type_weights:
                        if (at.index == 0) wgt_acc = at.data; break;
                    default: break;
                }
            }
            if (pos_acc == nullptr || p.indices == nullptr) continue;

            std::vector<float> positions; read_floats(pos_acc, positions);
            std::vector<float> normals;   if (nrm_acc) read_floats(nrm_acc, normals);
            std::vector<float> uvs;       if (uv_acc)  read_floats(uv_acc,  uvs);
            std::vector<float> colors;    if (col_acc) read_floats(col_acc, colors);
            int32_t vc = static_cast<int32_t>(pos_acc->count);

            idtx_mesh_t* m = idtx_mesh_create();
            std::string mesh_name = (gm.name ? gm.name : "Mesh");
            if (gm.primitives_count > 1) mesh_name += "_" + std::to_string(pi);
            idtx_mesh_set_name(m, mesh_name.c_str());
            idtx_mesh_set_vertices(m, vc,
                positions.data(),
                normals.empty() ? nullptr : normals.data(),
                uvs.empty()     ? nullptr : uvs.data(),
                colors.empty()  ? nullptr : colors.data());

            std::vector<uint32_t> idx; read_indices(p.indices, idx);
            std::vector<int32_t> idx_s(idx.begin(), idx.end());
            idtx_mesh_set_indices(m, static_cast<int32_t>(idx_s.size()), idx_s.data());

            if (joi_acc != nullptr && wgt_acc != nullptr) {
                std::vector<int32_t> joints;
                std::vector<float>   weights;
                read_uint_array(joi_acc, joints);
                read_floats(wgt_acc, weights);
                if (!joints.empty() && joints.size() == weights.size()) {
                    int32_t bpv = static_cast<int32_t>(joints.size()) / vc;
                    idtx_mesh_set_skinning(m, bpv, joints.data(), weights.data());
                }
            }

            int32_t mat_idx = -1;
            if (p.material != nullptr) {
                auto it = mat_to_idx.find(p.material);
                if (it != mat_to_idx.end()) mat_idx = it->second;
            }
            idtx_avatar_add_mesh(avatar, m, mat_idx);
        }
    }

    // Spring bones — VRMC_springBone is a top-level extension on
    // data->extensions. Walk to find it, then delegate parsing to
    // idtx_vrm_springbone_parse.cpp (which owns jsmn so cgltf's
    // bundled copy doesn't collide).
    struct SpringCtx {
        cgltf_data const* data;
        std::unordered_map<cgltf_node const*, int32_t> const* node_to_bone;
    } sb_ctx{data, &node_to_bone};
    auto mapper = +[](int gltf_idx, void* user) -> int32_t {
        auto* ctx = static_cast<SpringCtx*>(user);
        if (gltf_idx < 0
            || static_cast<size_t>(gltf_idx) >= ctx->data->nodes_count) return -1;
        auto it = ctx->node_to_bone->find(&ctx->data->nodes[gltf_idx]);
        return (it != ctx->node_to_bone->end()) ? it->second : -1;
    };
    for (size_t e = 0; e < data->data_extensions_count; ++e) {
        cgltf_extension const& ext = data->data_extensions[e];
        if (ext.name != nullptr
            && std::strcmp(ext.name, "VRMC_springBone") == 0
            && ext.data != nullptr) {
            idtx::core::vrm::parse_springbone_json(
                avatar, ext.data, std::strlen(ext.data), mapper, &sb_ctx);
            break;
        }
    }

    cgltf_free(data);
    return avatar;
}
