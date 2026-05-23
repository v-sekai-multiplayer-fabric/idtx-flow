// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "IDTXFlowExporter.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>

#include "UsdGodotStageExporter.h"

bool IDTXFlowExporter::export_scene(godot::Node3D* root, godot::String const& path)
{
    if (root == nullptr) return false;

    // Globalize res:// / user:// before passing to USD.
    godot::String real_path = path;
    if (godot::ProjectSettings* ps = godot::ProjectSettings::get_singleton()) {
        real_path = ps->globalize_path(path);
    }
    return idtxflow::exporter::ExportSceneToFile(root, real_path);
}

void IDTXFlowExporter::_bind_methods()
{
    godot::ClassDB::bind_method(
        godot::D_METHOD("export_scene", "root", "path"),
        &IDTXFlowExporter::export_scene);
}
