// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_core — engine-agnostic C ABI for the idtx-flow avatar pipeline.
//
// All public symbols here are extern "C" with primitive / opaque-handle
// types only. No Godot, no Unity, no C++ STL in the API surface. This
// header is what both libidtxflow (Godot GDExtension) and libidtx_unity
// (Unity P/Invoke) consume.

#ifndef IDTX_CORE_H
#define IDTX_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  ifdef IDTX_CORE_BUILDING_DLL
#    define IDTX_CORE_API __declspec(dllexport)
#  else
#    define IDTX_CORE_API __declspec(dllimport)
#  endif
#else
#  define IDTX_CORE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Returns a NUL-terminated semver string baked at build time. Stable
// pointer — callers must not free.
IDTX_CORE_API const char* idtx_core_version(void);

// ---------------------------------------------------------------------
// Opaque handle types.
// Every public function takes/returns these as pointers; the underlying
// layout is private to libidtx_core and may change without breaking ABI.
// ---------------------------------------------------------------------

typedef struct idtx_skeleton  idtx_skeleton_t;
typedef struct idtx_mesh      idtx_mesh_t;
typedef struct idtx_material  idtx_material_t;
typedef struct idtx_avatar    idtx_avatar_t;

// ---------------------------------------------------------------------
// idtx_skeleton — bone hierarchy with rest + bind transforms.
//   * rest transform: local pose relative to parent bone (Godot's
//                     skeleton->get_bone_rest equivalent).
//   * bind transform: global pose at the avatar's bind moment (Godot's
//                     skeleton->get_bone_global_rest equivalent).
// Transforms are row-major float[16] matrices in the avatar's coordinate
// system. The skeleton's own root transform sits on the avatar handle,
// not here.
// ---------------------------------------------------------------------

IDTX_CORE_API idtx_skeleton_t* idtx_skeleton_create(void);
IDTX_CORE_API void             idtx_skeleton_destroy(idtx_skeleton_t* skel);

IDTX_CORE_API void    idtx_skeleton_set_name(idtx_skeleton_t* skel, const char* name);
IDTX_CORE_API const char* idtx_skeleton_get_name(const idtx_skeleton_t* skel);

IDTX_CORE_API int32_t idtx_skeleton_add_bone(
    idtx_skeleton_t* skel,
    const char* name,
    int32_t parent_index,        // -1 for root bones
    const float rest_matrix[16],
    const float bind_matrix[16]);

IDTX_CORE_API int32_t idtx_skeleton_get_bone_count(const idtx_skeleton_t* skel);
IDTX_CORE_API const char* idtx_skeleton_get_bone_name(const idtx_skeleton_t* skel, int32_t index);
IDTX_CORE_API int32_t  idtx_skeleton_get_bone_parent(const idtx_skeleton_t* skel, int32_t index);
IDTX_CORE_API void     idtx_skeleton_get_bone_rest(const idtx_skeleton_t* skel, int32_t index, float out_matrix[16]);
IDTX_CORE_API void     idtx_skeleton_get_bone_bind(const idtx_skeleton_t* skel, int32_t index, float out_matrix[16]);

// ---------------------------------------------------------------------
// idtx_mesh — vertex / index / skinning data for a single mesh surface.
// Multi-surface meshes are represented as multiple idtx_mesh_t handles
// owned by the avatar.
//
// All arrays are caller-owned at set time; the mesh copies internally.
// Layouts:
//   positions: vertex_count * 3 floats (x,y,z)
//   normals:   vertex_count * 3 floats (x,y,z) — pass NULL to skip
//   uvs:       vertex_count * 2 floats (u,v)   — pass NULL to skip
//   colors:    vertex_count * 4 floats (r,g,b,a) — pass NULL to skip
//   indices:   index_count    int32s (triangle list)
//
// Skinning is set separately: bones_per_vertex is typically 4. Layout:
//   bone_indices: vertex_count * bones_per_vertex int32s
//   weights:      vertex_count * bones_per_vertex floats (summed to 1)
// ---------------------------------------------------------------------

IDTX_CORE_API idtx_mesh_t* idtx_mesh_create(void);
IDTX_CORE_API void         idtx_mesh_destroy(idtx_mesh_t* mesh);

IDTX_CORE_API void        idtx_mesh_set_name(idtx_mesh_t* mesh, const char* name);
IDTX_CORE_API const char* idtx_mesh_get_name(const idtx_mesh_t* mesh);

IDTX_CORE_API void idtx_mesh_set_vertices(
    idtx_mesh_t* mesh,
    int32_t vertex_count,
    const float* positions,   // required
    const float* normals,     // optional, NULL ok
    const float* uvs,         // optional, NULL ok
    const float* colors);     // optional, NULL ok

IDTX_CORE_API void idtx_mesh_set_indices(
    idtx_mesh_t* mesh,
    int32_t index_count,
    const int32_t* indices);

IDTX_CORE_API void idtx_mesh_set_skinning(
    idtx_mesh_t* mesh,
    int32_t bones_per_vertex,
    const int32_t* bone_indices,
    const float* weights);

IDTX_CORE_API int32_t idtx_mesh_get_vertex_count(const idtx_mesh_t* mesh);
IDTX_CORE_API int32_t idtx_mesh_get_index_count(const idtx_mesh_t* mesh);
IDTX_CORE_API int32_t idtx_mesh_get_bones_per_vertex(const idtx_mesh_t* mesh);

// Bulk getters — copy out into caller buffers. out_* may be NULL to
// signal "I just want the count." Pass a buffer of the size returned
// by the matching get_*_count function.
IDTX_CORE_API void idtx_mesh_get_positions(const idtx_mesh_t* mesh, float* out_positions);
IDTX_CORE_API void idtx_mesh_get_normals  (const idtx_mesh_t* mesh, float* out_normals);
IDTX_CORE_API void idtx_mesh_get_uvs      (const idtx_mesh_t* mesh, float* out_uvs);
IDTX_CORE_API void idtx_mesh_get_colors   (const idtx_mesh_t* mesh, float* out_colors);
IDTX_CORE_API void idtx_mesh_get_indices  (const idtx_mesh_t* mesh, int32_t* out_indices);
IDTX_CORE_API void idtx_mesh_get_bone_indices(const idtx_mesh_t* mesh, int32_t* out_bone_indices);
IDTX_CORE_API void idtx_mesh_get_weights     (const idtx_mesh_t* mesh, float* out_weights);

IDTX_CORE_API int32_t idtx_mesh_has_normals(const idtx_mesh_t* mesh);
IDTX_CORE_API int32_t idtx_mesh_has_uvs    (const idtx_mesh_t* mesh);
IDTX_CORE_API int32_t idtx_mesh_has_colors (const idtx_mesh_t* mesh);

#ifdef __cplusplus
}
#endif

#endif // IDTX_CORE_H
