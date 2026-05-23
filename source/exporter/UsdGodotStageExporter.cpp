// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// UsdGodotStageExporter — thin adapter. The actual USD I/O lives in
// libidtx_core; this file only handles the Godot-side scene walk +
// dispatch. See GodotAvatarBuilder.{h,cpp} for the walker.

#include "UsdGodotStageExporter.h"

#include "GodotAvatarBuilder.h"
#include "idtx_core/idtx_core.h"

#include <godot_cpp/variant/string.hpp>

#include <idtxflow/utils/Logger.h>

#include <string>

namespace idtxflow::exporter
{
    IDTX_LOG_CATEGORY("UsdGodotStageExporter")

    bool ExportSceneToFile(godot::Node3D* root, godot::String const& path)
    {
        if (root == nullptr) {
            IDTX_LOG(IDTX_ERROR, "UsdGodotStageExporter: null root node");
            return false;
        }
        std::string std_path = std::string(path.utf8().get_data());

        ::idtx_avatar_t* avatar = BuildIdtxAvatarFromGodotScene(root);
        if (avatar == nullptr) {
            IDTX_LOG(IDTX_ERROR, "UsdGodotStageExporter: avatar build failed");
            return false;
        }

        int32_t rc = ::idtx_core_export_avatar_to_usd(avatar, std_path.c_str());
        ::idtx_avatar_destroy(avatar);

        if (rc != 0) {
            IDTX_LOG(IDTX_ERROR, "UsdGodotStageExporter: core export failed (rc=%d) for %s",
                     rc, std_path.c_str());
            return false;
        }
        IDTX_LOG(IDTX_INFO, "UsdGodotStageExporter: wrote %s", std_path.c_str());
        return true;
    }
}
