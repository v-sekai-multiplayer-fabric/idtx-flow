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

typedef struct idtx_skeleton         idtx_skeleton_t;
typedef struct idtx_mesh             idtx_mesh_t;
typedef struct idtx_material         idtx_material_t;
typedef struct idtx_spring_chain     idtx_spring_chain_t;
typedef struct idtx_spring_collider  idtx_spring_collider_t;
typedef struct idtx_avatar           idtx_avatar_t;

typedef enum idtx_collider_shape
{
    IDTX_COLLIDER_SPHERE  = 0,
    IDTX_COLLIDER_CAPSULE = 1,
} idtx_collider_shape_t;

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

// Override the default triangle-list face structure with custom
// per-face vertex counts (e.g. n-gons preserved through a USD
// round-trip). counts[i] is the number of vertices in face i;
// the values must sum to the mesh's index_count or the call is
// ignored. Passing count=0 / NULL clears the override and the
// USD exporter reverts to emitting all-3s.
IDTX_CORE_API void idtx_mesh_set_face_vertex_counts(
    idtx_mesh_t* mesh,
    int32_t count,
    const int32_t* counts);

IDTX_CORE_API int32_t idtx_mesh_get_face_vertex_count_count(const idtx_mesh_t* mesh);
IDTX_CORE_API void    idtx_mesh_get_face_vertex_counts(const idtx_mesh_t* mesh, int32_t* out_counts);

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

// ---------------------------------------------------------------------
// idtx_material — PBR + MToon material parameters.
//
// PBR fields are the UsdPreviewSurface baseline (used when round-
// tripping to MaterialX or UsdPreviewSurface). MToon fields are the
// VRM-extension overlay; if any mtoon_* setter is called, the material
// becomes flagged as MToon-typed and will round-trip via
// VRMC_materials_mtoon / VSekaiMToonAPI instead of UsdPreviewSurface.
// ---------------------------------------------------------------------

typedef enum idtx_alpha_mode
{
    IDTX_ALPHA_OPAQUE = 0,
    IDTX_ALPHA_MASK   = 1,
    IDTX_ALPHA_BLEND  = 2,
} idtx_alpha_mode_t;

IDTX_CORE_API idtx_material_t* idtx_material_create(void);
IDTX_CORE_API void             idtx_material_destroy(idtx_material_t* mat);

IDTX_CORE_API void        idtx_material_set_name(idtx_material_t* mat, const char* name);
IDTX_CORE_API const char* idtx_material_get_name(const idtx_material_t* mat);

// PBR baseline
IDTX_CORE_API void idtx_material_set_base_color(idtx_material_t* mat, float r, float g, float b, float a);
IDTX_CORE_API void idtx_material_get_base_color(const idtx_material_t* mat, float out_rgba[4]);
IDTX_CORE_API void idtx_material_set_metallic (idtx_material_t* mat, float metallic);
IDTX_CORE_API void idtx_material_set_roughness(idtx_material_t* mat, float roughness);
IDTX_CORE_API float idtx_material_get_metallic (const idtx_material_t* mat);
IDTX_CORE_API float idtx_material_get_roughness(const idtx_material_t* mat);
IDTX_CORE_API void idtx_material_set_alpha_mode(idtx_material_t* mat, idtx_alpha_mode_t mode);
IDTX_CORE_API idtx_alpha_mode_t idtx_material_get_alpha_mode(const idtx_material_t* mat);
IDTX_CORE_API void idtx_material_set_alpha_cutoff(idtx_material_t* mat, float cutoff);
IDTX_CORE_API float idtx_material_get_alpha_cutoff(const idtx_material_t* mat);

// Texture references — string paths. Empty / NULL means unset.
IDTX_CORE_API void        idtx_material_set_base_color_texture(idtx_material_t* mat, const char* path);
IDTX_CORE_API const char* idtx_material_get_base_color_texture(const idtx_material_t* mat);
IDTX_CORE_API void        idtx_material_set_normal_texture(idtx_material_t* mat, const char* path);
IDTX_CORE_API const char* idtx_material_get_normal_texture(const idtx_material_t* mat);

// MToon overlay — calling any of these marks the material as MToon.
IDTX_CORE_API void idtx_material_set_mtoon_shade_color(idtx_material_t* mat, float r, float g, float b);
IDTX_CORE_API void idtx_material_set_mtoon_rim_color  (idtx_material_t* mat, float r, float g, float b);
IDTX_CORE_API void idtx_material_set_mtoon_outline_width(idtx_material_t* mat, float width);
IDTX_CORE_API int32_t idtx_material_is_mtoon(const idtx_material_t* mat);
IDTX_CORE_API void idtx_material_get_mtoon_shade_color(const idtx_material_t* mat, float out_rgb[3]);
IDTX_CORE_API void idtx_material_get_mtoon_rim_color  (const idtx_material_t* mat, float out_rgb[3]);
IDTX_CORE_API float idtx_material_get_mtoon_outline_width(const idtx_material_t* mat);

// ---------------------------------------------------------------------
// idtx_avatar — the top-level container.
//
// Ownership: the avatar OWNS its handles. Adding a handle transfers
// ownership; destroying the avatar frees all attached handles. Pass
// NULL to add_* to detach a slot.
//
// Mesh-to-material binding is by index — material_index[i] is the
// material attached to mesh[i] (or -1 for none).
// Mesh-to-skeleton binding is implicit (single skeleton per avatar
// in this MVP; multi-skeleton support can land if/when needed).
// ---------------------------------------------------------------------

IDTX_CORE_API idtx_avatar_t* idtx_avatar_create(void);
IDTX_CORE_API void           idtx_avatar_destroy(idtx_avatar_t* avatar);

IDTX_CORE_API void        idtx_avatar_set_name(idtx_avatar_t* avatar, const char* name);
IDTX_CORE_API const char* idtx_avatar_get_name(const idtx_avatar_t* avatar);

// Root transform: avatar's pose relative to its containing world.
IDTX_CORE_API void idtx_avatar_set_root_transform(idtx_avatar_t* avatar, const float matrix[16]);
IDTX_CORE_API void idtx_avatar_get_root_transform(const idtx_avatar_t* avatar, float out_matrix[16]);

// Skeleton — at most one. Replacing destroys the previous skeleton.
IDTX_CORE_API void idtx_avatar_set_skeleton(idtx_avatar_t* avatar, idtx_skeleton_t* skel);
IDTX_CORE_API idtx_skeleton_t* idtx_avatar_get_skeleton(const idtx_avatar_t* avatar);

// Mesh list. Returns the index assigned. material_index pairs the mesh
// with the avatar's material slot at that index (or -1).
IDTX_CORE_API int32_t idtx_avatar_add_mesh(idtx_avatar_t* avatar, idtx_mesh_t* mesh, int32_t material_index);
IDTX_CORE_API int32_t idtx_avatar_get_mesh_count(const idtx_avatar_t* avatar);
IDTX_CORE_API idtx_mesh_t* idtx_avatar_get_mesh(const idtx_avatar_t* avatar, int32_t index);
IDTX_CORE_API int32_t idtx_avatar_get_mesh_material(const idtx_avatar_t* avatar, int32_t mesh_index);

// Material list.
IDTX_CORE_API int32_t idtx_avatar_add_material(idtx_avatar_t* avatar, idtx_material_t* mat);
IDTX_CORE_API int32_t idtx_avatar_get_material_count(const idtx_avatar_t* avatar);
IDTX_CORE_API idtx_material_t* idtx_avatar_get_material(const idtx_avatar_t* avatar, int32_t index);

// ---------------------------------------------------------------------
// idtx_spring_chain — one VRMC_springBone joint chain. Joints reference
// bone indices in the avatar's skeleton. Dynamics fields match VRM 1.0:
//   stiffness     ([0..1+], how aggressively the chain returns to rest)
//   drag          ([0..1],  per-frame velocity damping)
//   gravity_power ([0..],   gravity scale; gravity_dir is the unit dir)
//   hit_radius    (meters,  collider intersection radius for the joint)
// ---------------------------------------------------------------------

IDTX_CORE_API idtx_spring_chain_t* idtx_spring_chain_create(void);
IDTX_CORE_API void                 idtx_spring_chain_destroy(idtx_spring_chain_t* chain);

IDTX_CORE_API void        idtx_spring_chain_set_name(idtx_spring_chain_t* chain, const char* name);
IDTX_CORE_API const char* idtx_spring_chain_get_name(const idtx_spring_chain_t* chain);

IDTX_CORE_API void idtx_spring_chain_set_joints(
    idtx_spring_chain_t* chain,
    int32_t count,
    const int32_t* bone_indices);   // indices into the avatar's idtx_skeleton

IDTX_CORE_API void idtx_spring_chain_set_dynamics(
    idtx_spring_chain_t* chain,
    float stiffness,
    float drag,
    float gravity_power,
    float hit_radius);

IDTX_CORE_API void idtx_spring_chain_set_gravity_dir(
    idtx_spring_chain_t* chain, float x, float y, float z);

// Append a collider-index reference (into the avatar's spring_collider
// list). Multiple colliders per chain are allowed.
IDTX_CORE_API void idtx_spring_chain_add_collider(idtx_spring_chain_t* chain, int32_t collider_index);

IDTX_CORE_API int32_t idtx_spring_chain_get_joint_count(const idtx_spring_chain_t* chain);
IDTX_CORE_API int32_t idtx_spring_chain_get_joint(const idtx_spring_chain_t* chain, int32_t index);
IDTX_CORE_API float   idtx_spring_chain_get_stiffness(const idtx_spring_chain_t* chain);
IDTX_CORE_API float   idtx_spring_chain_get_drag(const idtx_spring_chain_t* chain);
IDTX_CORE_API float   idtx_spring_chain_get_gravity_power(const idtx_spring_chain_t* chain);
IDTX_CORE_API float   idtx_spring_chain_get_hit_radius(const idtx_spring_chain_t* chain);
IDTX_CORE_API void    idtx_spring_chain_get_gravity_dir(const idtx_spring_chain_t* chain, float out_xyz[3]);
IDTX_CORE_API int32_t idtx_spring_chain_get_collider_count(const idtx_spring_chain_t* chain);
IDTX_CORE_API int32_t idtx_spring_chain_get_collider(const idtx_spring_chain_t* chain, int32_t index);

// ---------------------------------------------------------------------
// idtx_spring_collider — one VRMC_springBone collider primitive.
// Sphere uses offset + radius. Capsule adds a tail point (offset to
// tail forms the line segment, radius is the swept radius).
// ---------------------------------------------------------------------

IDTX_CORE_API idtx_spring_collider_t* idtx_spring_collider_create(void);
IDTX_CORE_API void                    idtx_spring_collider_destroy(idtx_spring_collider_t* col);

IDTX_CORE_API void        idtx_spring_collider_set_name(idtx_spring_collider_t* col, const char* name);
IDTX_CORE_API const char* idtx_spring_collider_get_name(const idtx_spring_collider_t* col);

IDTX_CORE_API void idtx_spring_collider_set_attached_bone(idtx_spring_collider_t* col, int32_t bone_index);
IDTX_CORE_API int32_t idtx_spring_collider_get_attached_bone(const idtx_spring_collider_t* col);

IDTX_CORE_API void idtx_spring_collider_set_shape(idtx_spring_collider_t* col, idtx_collider_shape_t shape);
IDTX_CORE_API idtx_collider_shape_t idtx_spring_collider_get_shape(const idtx_spring_collider_t* col);

IDTX_CORE_API void idtx_spring_collider_set_offset(idtx_spring_collider_t* col, float x, float y, float z);
IDTX_CORE_API void idtx_spring_collider_get_offset(const idtx_spring_collider_t* col, float out_xyz[3]);
IDTX_CORE_API void idtx_spring_collider_set_radius(idtx_spring_collider_t* col, float radius);
IDTX_CORE_API float idtx_spring_collider_get_radius(const idtx_spring_collider_t* col);
IDTX_CORE_API void idtx_spring_collider_set_tail(idtx_spring_collider_t* col, float x, float y, float z);
IDTX_CORE_API void idtx_spring_collider_get_tail(const idtx_spring_collider_t* col, float out_xyz[3]);

// Avatar-level spring chain / collider lists.
IDTX_CORE_API int32_t idtx_avatar_add_spring_chain(idtx_avatar_t* avatar, idtx_spring_chain_t* chain);
IDTX_CORE_API int32_t idtx_avatar_get_spring_chain_count(const idtx_avatar_t* avatar);
IDTX_CORE_API idtx_spring_chain_t* idtx_avatar_get_spring_chain(const idtx_avatar_t* avatar, int32_t index);

IDTX_CORE_API int32_t idtx_avatar_add_spring_collider(idtx_avatar_t* avatar, idtx_spring_collider_t* col);
IDTX_CORE_API int32_t idtx_avatar_get_spring_collider_count(const idtx_avatar_t* avatar);
IDTX_CORE_API idtx_spring_collider_t* idtx_avatar_get_spring_collider(const idtx_avatar_t* avatar, int32_t index);

// ---------------------------------------------------------------------
// Top-level I/O entry points.
//
// Returns 0 on success, non-zero on failure. Error categories:
//   1 = invalid argument (NULL avatar / NULL path)
//   2 = USD stage creation failed (e.g. unwritable path)
//   3 = USD write failed (Save() returned false)
// ---------------------------------------------------------------------

IDTX_CORE_API int32_t idtx_core_export_avatar_to_usd(
    const idtx_avatar_t* avatar,
    const char* path);

// Open a USD stage at `path` and rebuild an idtx_avatar_t* from its
// default prim (or the first prim if no default is set). Returns NULL
// on failure (NULL path, open failed, no usable root prim). Caller
// owns the returned handle and frees with idtx_avatar_destroy.
IDTX_CORE_API idtx_avatar_t* idtx_core_import_avatar_from_usd(
    const char* path);

// VRM 1.0 export — writes a .vrm (glTF binary container) with the
// VRMC_vrm extension. MToon materials get VRMC_materials_mtoon; if a
// skeleton is present, its bone names are looked up against the
// humanoid-bones bridge map for the VRMC_vrm.humanoid.humanBones table.
//
// Error codes match the USD path:
//   0 = success
//   1 = invalid argument
//   2 = file open failed
//   3 = write failed
//   99 = not yet implemented for the requested feature combination
IDTX_CORE_API int32_t idtx_core_export_avatar_to_vrm(
    const idtx_avatar_t* avatar,
    const char* path);

// VRM 1.0 import — parses a .vrm at `path` and rebuilds an
// idtx_avatar_t* from it (inverse of the export).
IDTX_CORE_API idtx_avatar_t* idtx_core_import_avatar_from_vrm(
    const char* path);

#ifdef __cplusplus
}
#endif

#endif // IDTX_CORE_H
