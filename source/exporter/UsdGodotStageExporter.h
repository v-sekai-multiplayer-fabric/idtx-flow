// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// UsdGodotStageExporter — inverse direction of UsdGodotStageConverter.
//
// Walks a Godot Node3D tree, emits a USD stage suitable for re-import
// via the existing UsdGodotStageConverter. Mirrors the converter's
// per-prim-type template specialisation so the import and export
// paths stay in lockstep visually.
//
// MVP scopes (filled out in subsequent cycles):
//   * Cycle A — Xform + Mesh (this file, current state).
//   * Cycle B — BaseMaterial3D -> UsdPreviewSurface (+ MaterialX twin).
//   * Cycle C — godot-vrm MToon -> apiSchemas=["VSekaiMToonAPI"] +
//               v_sekai:mtoon:* attributes (consumes the openusd-fabric
//               SCSS bridge map in reverse).
//   * Cycle D — Skeleton3D -> UsdSkel + SpringBoneSimulator3D ->
//               VSekaiSpringBoneAPI / VSekaiSpringBoneColliderAPI.
//
// Entry point: ExportSceneToFile(root, path). Returns true on success
// and leaves a .usda at `path`; on failure logs via IDTX_LOG and
// returns false without partial writes.

#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/variant/string.hpp>

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>

#include <idtxflow_godot/types/GodotTypes.h>

namespace idtxflow::exporter
{
    /// Convert a Godot Transform3D into a USD GfMatrix4d suitable for
    /// UsdGeomXformCommonAPI::SetTranslate / SetRotate / SetScale.
    pxr::GfMatrix4d GodotTransformToUsdMatrix(godot::Transform3D const& transform);

    /// Sanitise a Godot Node name into a USD-legal prim name. USD
    /// requires identifier-compatible names (`[A-Za-z_][A-Za-z0-9_]*`).
    std::string SanitisePrimName(godot::String const& godot_name);

    /// Build a unique, USD-legal prim path under `parent` given a desired
    /// leaf name. Duplicates get numeric suffixes (`_2`, `_3`, ...).
    pxr::SdfPath ChildPath(pxr::SdfPath const& parent, std::string const& desired);

    /// Per-cycle entry points. Cycle A handles Xform + Mesh; B/C/D
    /// extend this file with material / MToon / skeleton emitters as
    /// they land.

    /// Emit a UsdGeomXform at `path` from a plain Node3D's transform.
    pxr::UsdGeomXform ExportXform(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& path,
        godot::Transform3D const& transform);

    /// Emit a UsdGeomCube at `path` from a Godot BoxMesh.
    pxr::UsdGeomCube ExportCube(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& path,
        godot::Transform3D const& transform,
        godot::Ref<godot::BoxMesh> const& mesh);

    /// Emit a UsdGeomSphere at `path` from a Godot SphereMesh.
    pxr::UsdGeomSphere ExportSphere(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& path,
        godot::Transform3D const& transform,
        godot::Ref<godot::SphereMesh> const& mesh);

    /// Emit a UsdGeomCylinder at `path` from a Godot CylinderMesh.
    pxr::UsdGeomCylinder ExportCylinder(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& path,
        godot::Transform3D const& transform,
        godot::Ref<godot::CylinderMesh> const& mesh);

    /// Emit a UsdGeomMesh at `path` from a Godot ArrayMesh (or any
    /// Godot Mesh that yields surface arrays). Each surface in the
    /// Godot mesh becomes a faceSet on the single UsdGeomMesh.
    pxr::UsdGeomMesh ExportMesh(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& path,
        godot::Transform3D const& transform,
        godot::Ref<godot::Mesh> const& mesh);

    /// Recursive driver. Walks `node` and every Node3D descendant,
    /// dispatching to the per-type exporters above. Returns the prim
    /// path created for `node` (or the empty path if `node` was not a
    /// recognised Godot 3D node — that subtree is skipped).
    pxr::SdfPath ExportNodeRecursive(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& parent_path,
        godot::Node3D* node);

    /// Top-level entry: serialise the whole sub-tree rooted at `root`
    /// into a USD stage written to `path`. Returns true on success.
    /// The stage is authored in metres (metersPerUnit=1.0) and Y-up
    /// to match Godot's native convention; the importer adapts to
    /// authored values on the re-read.
    bool ExportSceneToFile(godot::Node3D* root, godot::String const& path);

    // ----------------------------------------------------------------
    // Cycle B — Materials.
    //
    // Each unique Godot BaseMaterial3D becomes a single Material prim
    // under `/<root>/Materials/<sanitised_name>` carrying both a
    // UsdPreviewSurface (`outputs:surface`) AND a MaterialX
    // `ND_standard_surface_surfaceshader` (`outputs:mtlx:surface`),
    // matching the Macbeth fixture convention from openusd-fabric.
    //
    // Geometry prims (Mesh / Cube / Sphere / Cylinder) apply
    // MaterialBindingAPI + `material:binding` to point at the
    // emitted material. The exporter caches material -> SdfPath so
    // the same Godot material instance shared across N meshes
    // produces exactly one Material prim.
    // ----------------------------------------------------------------

    /// Emit a Material prim under `mats_scope_path` mirroring the
    /// values on a Godot BaseMaterial3D. Returns the SdfPath of the
    /// Material so the caller can wire the geometry's material:binding.
    ///
    /// Reference for the glTF-PBR <-> MaterialX standard_surface
    /// parameter mapping (textures, normals, emission, etc. — Cycle
    /// B currently only covers the scalar/colour subset):
    /// https://github.com/KhronosGroup/glTF-MaterialX-Converter
    pxr::SdfPath ExportMaterial(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& mats_scope_path,
        std::string const& desired_name,
        godot::Ref<godot::BaseMaterial3D> const& mat);

    /// Apply MaterialBindingAPI on the geometry prim and bind to
    /// `material_path`. No-op when `material_path` is empty.
    void BindMaterial(
        pxr::UsdPrim const& geom_prim,
        pxr::SdfPath const& material_path);

    // ----------------------------------------------------------------
    // Cycle C — godot-vrm MToon -> VSekaiMToonAPI.
    //
    // Detects whether a Godot ShaderMaterial uses one of godot-vrm's
    // MToon shaders (Godot-MToon-Shader/mtoon.gdshader and family);
    // when it does, stamps `apiSchemas = ["VSekaiMToonAPI"]` on the
    // emitted Material prim and copies the per-uniform values into
    // `v_sekai:mtoon:*` attributes per the schema in openusd-fabric/
    // schema/v_sekai_schema.usda.
    //
    // For VRM 0.x MToon uniforms (`_ShadeColor`, `_ShadeShift`, ...),
    // applies the openusd-fabric upgrade table in reverse to land
    // VRM 1.0 MToon factor names. The same canonical
    // maps/scss_mtoon_map.json is the source of truth; the C++ table
    // ships embedded so the exporter has no runtime file dependency.
    // ----------------------------------------------------------------

    /// True if the godot-vrm MToon shader is bound on this material.
    bool IsGodotVrmMToon(godot::Ref<godot::Material> const& material);

    /// Stamp VSekaiMToonAPI on the already-emitted UsdShadeMaterial
    /// and copy the godot-vrm MToon shader-parameter values into
    /// `v_sekai:mtoon:*` attributes (VRM 1.0 naming).
    void ApplyVSekaiMToonAPI(
        pxr::UsdShadeMaterial const& usd_mat,
        godot::Ref<godot::ShaderMaterial> const& source);

    // ----------------------------------------------------------------
    // Cycle D — Skeleton3D + SpringBoneSimulator3D.
    //
    // Skeleton3D becomes a UsdSkelRoot wrapping a UsdSkelSkeleton:
    //   * `joints` token[] in skeleton-relative paths
    //   * `bindTransforms` and `restTransforms` matrix4d[]
    //
    // SpringBoneSimulator3D's chains map to Xform prims tagged with
    // `VSekaiSpringBoneAPI` (stiffness / drag / gravity*); colliders
    // map to Xform prims with `VSekaiSpringBoneColliderAPI` (shape /
    // radius / offset / tail / normal / inside). Chain -> collider
    // wiring goes through `v_sekai:springBone:colliders` rel arrays,
    // matching the schema in openusd-fabric/schema/v_sekai_schema.usda.
    // ----------------------------------------------------------------

    /// Emit a UsdSkelRoot + UsdSkelSkeleton hierarchy from a Godot
    /// Skeleton3D. Returns the SdfPath of the SkelRoot prim.
    pxr::SdfPath ExportSkeleton(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& parent_path,
        godot::Skeleton3D* skeleton);

    /// Walk a SpringBoneSimulator3D, emit one Xform per chain root
    /// (apiSchemas = ["VSekaiSpringBoneAPI"]) and one per collider
    /// (apiSchemas = ["VSekaiSpringBoneColliderAPI"]). Returns the
    /// SdfPath of the container Scope used to group them.
    pxr::SdfPath ExportSpringBones(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& parent_path,
        godot::Node3D* spring_bone_simulator);
}
