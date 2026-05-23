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
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformOp.h>

#include <string>

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

    if (!stage->GetRootLayer()->Save()) return 3;
    return 0;
}
