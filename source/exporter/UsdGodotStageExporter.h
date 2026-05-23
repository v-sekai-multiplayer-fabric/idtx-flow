// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// UsdGodotStageExporter — thin Godot adapter. Walks a godot::Node3D
// tree, hands the avatar handle to libidtx_core for USD I/O, and
// returns success/failure to the caller. All actual USD-writing logic
// lives in core/src/idtx_export_usd.cpp; the walker that turns the
// Godot scene into an idtx_avatar_t* lives in GodotAvatarBuilder.
//
// Entry point: ExportSceneToFile(root, path). Returns true on success
// and leaves a .usda at `path`; on failure logs via IDTX_LOG and
// returns false without partial writes.

#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/string.hpp>

namespace idtxflow::exporter
{
    bool ExportSceneToFile(godot::Node3D* root, godot::String const& path);
}
