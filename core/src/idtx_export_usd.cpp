// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_core_export_avatar_to_usd — write an idtx_avatar_t* tree into a
// USD file at `path`. Lifts the body of UsdGodotStageExporter's
// ExportSceneToFile into the engine-agnostic core.

#include "idtx_core/idtx_core.h"
#include "idtx_core/internal/string_utils.h"
#include "idtx_core/internal/usd_helpers.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/valueTypeName.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformOp.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeleton.h>

#include <string>
#include <vector>

namespace idtx::core::detail {

// Emit the avatar's root Xform under `parent_path` with the avatar's
// root_transform. Returns the resulting prim path.
static pxr::SdfPath emit_root_xform(
    pxr::UsdStageRefPtr const& stage,
    pxr::SdfPath const& parent_path,
    idtx_avatar_t const* avatar)
{
    std::string root_name = sanitise_prim_name(idtx_avatar_get_name(avatar));
    pxr::SdfPath root_path = parent_path.AppendChild(pxr::TfToken(root_name));
    pxr::UsdGeomXform xf = pxr::UsdGeomXform::Define(stage, root_path);

    float m[16];
    idtx_avatar_get_root_transform(avatar, m);
    auto op = xf.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble);
    op.Set(float16_to_gf_matrix(m));

    return root_path;
}

// Emit UsdSkelRoot + UsdSkelSkeleton under `parent_path`. Returns the
// SkelRoot path (empty SdfPath if the skeleton handle is NULL / empty).
// Mirrors UsdGodotStageExporter::ExportSkeleton but reads from the
// engine-agnostic idtx_skeleton_t handle.
static pxr::SdfPath emit_skeleton(
    pxr::UsdStageRefPtr const& stage,
    pxr::SdfPath const& parent_path,
    idtx_skeleton_t const* skel)
{
    if (skel == nullptr) return pxr::SdfPath();
    int32_t n = idtx_skeleton_get_bone_count(skel);
    if (n <= 0) return pxr::SdfPath();

    std::string skel_root_name = sanitise_prim_name(idtx_skeleton_get_name(skel)) + "_SkelRoot";
    pxr::SdfPath root_path = parent_path.AppendChild(pxr::TfToken(skel_root_name));
    pxr::UsdSkelRoot::Define(stage, root_path);

    pxr::SdfPath skel_path = root_path.AppendChild(pxr::TfToken("Skeleton"));
    pxr::UsdSkelSkeleton usd_skel = pxr::UsdSkelSkeleton::Define(stage, skel_path);

    // Build skeleton-relative joint paths: each bone's joint identifier
    // is its parent's path + "/" + sanitised name; root bones live at
    // the top level. Matches the UsdSkel convention.
    std::vector<std::string> joint_paths(static_cast<size_t>(n));
    pxr::VtArray<pxr::TfToken>     joints;
    pxr::VtArray<pxr::GfMatrix4d>  rest_transforms;
    pxr::VtArray<pxr::GfMatrix4d>  bind_transforms;
    joints.reserve(n);
    rest_transforms.reserve(n);
    bind_transforms.reserve(n);

    for (int32_t i = 0; i < n; ++i) {
        std::string san = sanitise_prim_name(idtx_skeleton_get_bone_name(skel, i));
        int32_t parent = idtx_skeleton_get_bone_parent(skel, i);
        joint_paths[i] = (parent < 0)
            ? san
            : (joint_paths[static_cast<size_t>(parent)] + "/" + san);
        joints.push_back(pxr::TfToken(joint_paths[i]));

        float rest[16]; idtx_skeleton_get_bone_rest(skel, i, rest);
        float bind[16]; idtx_skeleton_get_bone_bind(skel, i, bind);
        rest_transforms.push_back(float16_to_gf_matrix(rest));
        bind_transforms.push_back(float16_to_gf_matrix(bind));
    }

    usd_skel.CreateJointsAttr().Set(joints);
    usd_skel.CreateRestTransformsAttr().Set(rest_transforms);
    usd_skel.CreateBindTransformsAttr().Set(bind_transforms);

    return root_path;
}

// Emit one UsdGeomMesh prim for `mesh` under `parent_path`. Returns
// the mesh prim path. `siblings` tracks already-used names so multi-
// mesh avatars don't collide on identically-named meshes.
static pxr::SdfPath emit_mesh(
    pxr::UsdStageRefPtr const& stage,
    pxr::SdfPath const& parent_path,
    idtx_mesh_t const* mesh,
    std::set<std::string>& siblings)
{
    if (mesh == nullptr) return pxr::SdfPath();
    int32_t vc = idtx_mesh_get_vertex_count(mesh);
    int32_t ic = idtx_mesh_get_index_count(mesh);
    if (vc <= 0 || ic <= 0) return pxr::SdfPath();

    std::string desired = sanitise_prim_name(idtx_mesh_get_name(mesh));
    if (desired.empty()) desired = "Mesh";
    pxr::SdfPath mesh_path = unique_child_path(parent_path, desired, siblings);
    pxr::UsdGeomMesh usd_mesh = pxr::UsdGeomMesh::Define(stage, mesh_path);

    // Positions
    std::vector<float> positions(static_cast<size_t>(vc) * 3);
    idtx_mesh_get_positions(mesh, positions.data());
    pxr::VtArray<pxr::GfVec3f> points;
    points.reserve(vc);
    for (int32_t i = 0; i < vc; ++i) {
        points.push_back(pxr::GfVec3f(
            positions[i * 3 + 0],
            positions[i * 3 + 1],
            positions[i * 3 + 2]));
    }
    usd_mesh.CreatePointsAttr().Set(points);

    // Indices — assume triangle list; faceVertexCounts is all 3's.
    int32_t face_count = ic / 3;
    pxr::VtArray<int> face_vertex_counts(face_count, 3);
    pxr::VtArray<int> face_vertex_indices;
    face_vertex_indices.reserve(ic);
    {
        std::vector<int32_t> indices(static_cast<size_t>(ic));
        idtx_mesh_get_indices(mesh, indices.data());
        for (int32_t i = 0; i < ic; ++i) {
            face_vertex_indices.push_back(static_cast<int>(indices[i]));
        }
    }
    usd_mesh.CreateFaceVertexCountsAttr().Set(face_vertex_counts);
    usd_mesh.CreateFaceVertexIndicesAttr().Set(face_vertex_indices);

    // Normals (vertex-interpolated)
    if (idtx_mesh_has_normals(mesh)) {
        std::vector<float> n_buf(static_cast<size_t>(vc) * 3);
        idtx_mesh_get_normals(mesh, n_buf.data());
        pxr::VtArray<pxr::GfVec3f> normals;
        normals.reserve(vc);
        for (int32_t i = 0; i < vc; ++i) {
            normals.push_back(pxr::GfVec3f(
                n_buf[i * 3 + 0],
                n_buf[i * 3 + 1],
                n_buf[i * 3 + 2]));
        }
        auto attr = usd_mesh.CreateNormalsAttr();
        attr.Set(normals);
        usd_mesh.SetNormalsInterpolation(pxr::UsdGeomTokens->vertex);
    }

    // UVs as primvar:st (vertex-interpolated)
    if (idtx_mesh_has_uvs(mesh)) {
        std::vector<float> uv_buf(static_cast<size_t>(vc) * 2);
        idtx_mesh_get_uvs(mesh, uv_buf.data());
        pxr::VtArray<pxr::GfVec2f> uvs;
        uvs.reserve(vc);
        for (int32_t i = 0; i < vc; ++i) {
            uvs.push_back(pxr::GfVec2f(uv_buf[i * 2 + 0], uv_buf[i * 2 + 1]));
        }
        pxr::UsdGeomPrimvarsAPI pvapi(usd_mesh.GetPrim());
        auto st = pvapi.CreatePrimvar(
            pxr::TfToken("st"),
            pxr::SdfValueTypeNames->TexCoord2fArray,
            pxr::UsdGeomTokens->vertex);
        st.Set(uvs);
    }

    // Display color (per-vertex)
    if (idtx_mesh_has_colors(mesh)) {
        std::vector<float> c_buf(static_cast<size_t>(vc) * 4);
        idtx_mesh_get_colors(mesh, c_buf.data());
        pxr::VtArray<pxr::GfVec3f> rgb;
        rgb.reserve(vc);
        for (int32_t i = 0; i < vc; ++i) {
            // Drop alpha — USD displayColor is RGB; alpha would go in
            // displayOpacity if needed (omitted for the MVP).
            rgb.push_back(pxr::GfVec3f(
                c_buf[i * 4 + 0],
                c_buf[i * 4 + 1],
                c_buf[i * 4 + 2]));
        }
        auto attr = usd_mesh.CreateDisplayColorAttr();
        attr.Set(rgb);
        usd_mesh.SetNormalsInterpolation(pxr::UsdGeomTokens->vertex);
    }

    return mesh_path;
}

// Emit a UsdShadeMaterial under `parent_path` from idtx_material_t.
// PBR materials get a UsdPreviewSurface shader subtree; MToon-flagged
// materials get the same surface plus the VSekaiMToonAPI applied
// schema so downstream code (or a future VRM serializer) can find the
// MToon parameters.
static pxr::SdfPath emit_material(
    pxr::UsdStageRefPtr const& stage,
    pxr::SdfPath const& parent_path,
    idtx_material_t const* mat,
    std::set<std::string>& siblings)
{
    if (mat == nullptr) return pxr::SdfPath();
    std::string desired = sanitise_prim_name(idtx_material_get_name(mat));
    if (desired.empty()) desired = "Material";
    pxr::SdfPath mat_path = unique_child_path(parent_path, desired, siblings);
    pxr::UsdShadeMaterial usd_mat = pxr::UsdShadeMaterial::Define(stage, mat_path);

    // UsdPreviewSurface shader subprim
    pxr::SdfPath shader_path = mat_path.AppendChild(pxr::TfToken("PreviewSurface"));
    pxr::UsdShadeShader shader = pxr::UsdShadeShader::Define(stage, shader_path);
    shader.CreateIdAttr().Set(pxr::TfToken("UsdPreviewSurface"));

    float rgba[4]; idtx_material_get_base_color(mat, rgba);
    shader.CreateInput(pxr::TfToken("diffuseColor"), pxr::SdfValueTypeNames->Color3f)
          .Set(pxr::GfVec3f(rgba[0], rgba[1], rgba[2]));
    shader.CreateInput(pxr::TfToken("opacity"), pxr::SdfValueTypeNames->Float)
          .Set(rgba[3]);
    shader.CreateInput(pxr::TfToken("metallic"), pxr::SdfValueTypeNames->Float)
          .Set(idtx_material_get_metallic(mat));
    shader.CreateInput(pxr::TfToken("roughness"), pxr::SdfValueTypeNames->Float)
          .Set(idtx_material_get_roughness(mat));

    // Connect shader surface output to material
    pxr::UsdShadeOutput surf_out = shader.CreateOutput(
        pxr::TfToken("surface"), pxr::SdfValueTypeNames->Token);
    usd_mat.CreateSurfaceOutput().ConnectToSource(surf_out);

    // MToon overlay — apply VSekaiMToonAPI applied schema + write the
    // v_sekai:mtoon:* attributes for round-tripping.
    if (idtx_material_is_mtoon(mat)) {
        pxr::UsdPrim prim = usd_mat.GetPrim();
        prim.ApplyAPI(pxr::TfToken("VSekaiMToonAPI"));

        float shade[3]; idtx_material_get_mtoon_shade_color(mat, shade);
        float rim[3];   idtx_material_get_mtoon_rim_color(mat, rim);
        prim.CreateAttribute(
            pxr::TfToken("v_sekai:mtoon:shadeColor"),
            pxr::SdfValueTypeNames->Color3f).Set(pxr::GfVec3f(shade[0], shade[1], shade[2]));
        prim.CreateAttribute(
            pxr::TfToken("v_sekai:mtoon:rimColor"),
            pxr::SdfValueTypeNames->Color3f).Set(pxr::GfVec3f(rim[0], rim[1], rim[2]));
        prim.CreateAttribute(
            pxr::TfToken("v_sekai:mtoon:outlineWidth"),
            pxr::SdfValueTypeNames->Float).Set(idtx_material_get_mtoon_outline_width(mat));
    }

    return mat_path;
}

}  // namespace idtx::core::detail

extern "C" IDTX_CORE_API int32_t idtx_core_export_avatar_to_usd(
    const idtx_avatar_t* avatar,
    const char* path)
{
    if (avatar == nullptr || path == nullptr) return 1;

    pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateNew(std::string(path));
    if (!stage) return 2;

    stage->SetMetadata(pxr::TfToken("upAxis"),       pxr::VtValue(pxr::TfToken("Y")));
    stage->SetMetadata(pxr::TfToken("metersPerUnit"), pxr::VtValue(1.0));

    pxr::SdfPath root_path = idtx::core::detail::emit_root_xform(
        stage, pxr::SdfPath::AbsoluteRootPath(), avatar);
    stage->SetDefaultPrim(stage->GetPrimAtPath(root_path));

    idtx::core::detail::emit_skeleton(
        stage, root_path, idtx_avatar_get_skeleton(avatar));

    // Materials live in a scope so they can be referenced by mesh
    // binding by path. Emit them first, then meshes pick them up.
    std::vector<pxr::SdfPath> material_paths;
    {
        std::set<std::string> mat_siblings;
        int32_t mat_count = idtx_avatar_get_material_count(avatar);
        material_paths.reserve(static_cast<size_t>(mat_count));
        for (int32_t i = 0; i < mat_count; ++i) {
            material_paths.push_back(
                idtx::core::detail::emit_material(
                    stage, root_path, idtx_avatar_get_material(avatar, i), mat_siblings));
        }
    }

    {
        std::set<std::string> mesh_siblings;
        int32_t mesh_count = idtx_avatar_get_mesh_count(avatar);
        for (int32_t i = 0; i < mesh_count; ++i) {
            pxr::SdfPath mp = idtx::core::detail::emit_mesh(
                stage, root_path, idtx_avatar_get_mesh(avatar, i), mesh_siblings);
            if (mp.IsEmpty()) continue;
            int32_t mat_index = idtx_avatar_get_mesh_material(avatar, i);
            if (mat_index >= 0 && mat_index < static_cast<int32_t>(material_paths.size())
                && !material_paths[mat_index].IsEmpty()) {
                pxr::UsdPrim mesh_prim = stage->GetPrimAtPath(mp);
                pxr::UsdShadeMaterial mat = pxr::UsdShadeMaterial::Get(
                    stage, material_paths[mat_index]);
                pxr::UsdShadeMaterialBindingAPI(mesh_prim).Bind(mat);
            }
        }
    }

    if (!stage->GetRootLayer()->Save()) return 3;
    return 0;
}
