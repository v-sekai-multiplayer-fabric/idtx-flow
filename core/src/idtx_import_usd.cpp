// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_core_import_avatar_from_usd — read a USD file and rebuild an
// idtx_avatar_t* from it. Reverse direction of idtx_export_usd.cpp;
// powers the future Unity import path.

#include "idtx_core/idtx_core.h"
#include "idtx_core/internal/usd_helpers.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdSkel/skeleton.h>

#include <string>
#include <vector>

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

// Find the first UsdSkelSkeleton anywhere under `root`. Returns
// invalid UsdPrim if none.
static pxr::UsdPrim find_first_skeleton_prim(pxr::UsdPrim const& root)
{
    if (!root.IsValid()) return pxr::UsdPrim();
    for (auto const& p : pxr::UsdPrimRange(root)) {
        if (p.IsA<pxr::UsdSkelSkeleton>()) return p;
    }
    return pxr::UsdPrim();
}

// Build idtx_skeleton_t from a UsdSkelSkeleton prim. Returns NULL if
// the skeleton has no joints or required attrs are missing.
static idtx_skeleton_t* read_skeleton(pxr::UsdPrim const& skel_prim)
{
    if (!skel_prim.IsValid()) return nullptr;
    pxr::UsdSkelSkeleton usd_skel(skel_prim);

    pxr::VtArray<pxr::TfToken>    joints;
    pxr::VtArray<pxr::GfMatrix4d> rest_transforms;
    pxr::VtArray<pxr::GfMatrix4d> bind_transforms;

    auto joints_attr = usd_skel.GetJointsAttr();
    if (!joints_attr || !joints_attr.Get(&joints) || joints.empty()) return nullptr;
    usd_skel.GetRestTransformsAttr().Get(&rest_transforms);
    usd_skel.GetBindTransformsAttr().Get(&bind_transforms);

    idtx_skeleton_t* skel = idtx_skeleton_create();
    idtx_skeleton_set_name(skel, skel_prim.GetName().GetString().c_str());

    // joint paths are "a/b/c"; parent of joint i is the joint whose
    // path is i's path minus the trailing /name. Build a map of
    // path -> index as we go.
    std::vector<std::string> joint_paths;
    joint_paths.reserve(joints.size());
    for (auto const& jt : joints) joint_paths.push_back(jt.GetString());

    auto find_parent = [&](std::string const& jp) -> int32_t {
        size_t slash = jp.find_last_of('/');
        if (slash == std::string::npos) return -1;
        std::string parent_path = jp.substr(0, slash);
        for (size_t k = 0; k < joint_paths.size(); ++k) {
            if (joint_paths[k] == parent_path) return static_cast<int32_t>(k);
        }
        return -1;
    };

    for (size_t i = 0; i < joints.size(); ++i) {
        std::string const& jp = joint_paths[i];
        size_t slash = jp.find_last_of('/');
        std::string bone_name = (slash == std::string::npos) ? jp : jp.substr(slash + 1);

        float rest[16];
        float bind[16];
        if (i < rest_transforms.size()) {
            idtx::core::gf_matrix_to_float16(rest_transforms[i], rest);
        } else {
            for (int k = 0; k < 16; ++k) rest[k] = (k % 5 == 0) ? 1.0f : 0.0f;
        }
        if (i < bind_transforms.size()) {
            idtx::core::gf_matrix_to_float16(bind_transforms[i], bind);
        } else {
            for (int k = 0; k < 16; ++k) bind[k] = (k % 5 == 0) ? 1.0f : 0.0f;
        }

        idtx_skeleton_add_bone(skel, bone_name.c_str(), find_parent(jp), rest, bind);
    }

    return skel;
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

    if (auto skel_prim = idtx::core::detail::find_first_skeleton_prim(root)) {
        if (auto* skel = idtx::core::detail::read_skeleton(skel_prim)) {
            idtx_avatar_set_skeleton(avatar, skel);
        }
    }

    // Mesh and material import land in subsequent cycles.
    return avatar;
}
