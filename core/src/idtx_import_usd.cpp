// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_core_import_avatar_from_usd — read a USD file and rebuild an
// idtx_avatar_t* from it. Reverse direction of idtx_export_usd.cpp;
// powers the future Unity import path.

#include "idtx_core/idtx_core.h"
#include "idtx_core/internal/usd_helpers.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xformable.h>

#include <string>

namespace idtx::core::detail {

// Pick the avatar root prim: the stage's defaultPrim if set, otherwise
// the first top-level prim. Returns an invalid UsdPrim if neither exists.
static pxr::UsdPrim pick_avatar_root(pxr::UsdStageRefPtr const& stage)
{
    pxr::UsdPrim def = stage->GetDefaultPrim();
    if (def.IsValid()) return def;
    for (auto const& p : stage->GetPseudoRoot().GetChildren()) {
        return p;
    }
    return pxr::UsdPrim();
}

}  // namespace idtx::core::detail

extern "C" IDTX_CORE_API idtx_avatar_t* idtx_core_import_avatar_from_usd(const char* path)
{
    if (path == nullptr) return nullptr;
    pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(std::string(path));
    if (!stage) return nullptr;

    pxr::UsdPrim root = idtx::core::detail::pick_avatar_root(stage);
    if (!root.IsValid()) return nullptr;

    idtx_avatar_t* avatar = idtx_avatar_create();
    idtx_avatar_set_name(avatar, root.GetName().GetString().c_str());

    // Read root transform if the root prim is Xformable.
    if (auto xf = pxr::UsdGeomXformable(root)) {
        pxr::GfMatrix4d local_xform(1.0);
        bool resets = false;
        xf.GetLocalTransformation(&local_xform, &resets);
        float m[16];
        idtx::core::gf_matrix_to_float16(local_xform, m);
        idtx_avatar_set_root_transform(avatar, m);
    }

    // Skeleton, mesh, material import land in subsequent cycles.
    return avatar;
}
