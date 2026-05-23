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
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformOp.h>
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

    if (!stage->GetRootLayer()->Save()) return 3;
    return 0;
}
