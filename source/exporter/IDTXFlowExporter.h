// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// IDTXFlowExporter — GDScript-callable wrapper around
// idtxflow::exporter::ExportSceneToFile.
//
// Usage from GDScript:
//
//   var exporter := IDTXFlowExporter.new()
//   if exporter.export_scene(my_node3d, "res://exported.usda"):
//       print("ok")
//
// Returns true on success, false on failure (failure detail in the
// IDTX log output).

#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>

namespace godot { class String; }

class IDTXFlowExporter : public godot::RefCounted
{
    GDCLASS(IDTXFlowExporter, godot::RefCounted)

public:
    IDTXFlowExporter() = default;
    ~IDTXFlowExporter() override = default;

    /// Export a Godot Node3D sub-tree to a .usda file.
    /// Returns true on success. The file path can be a `res://` /
    /// `user://` URI; it gets globalized before being passed to USD.
    bool export_scene(godot::Node3D* root, godot::String const& path);

protected:
    static void _bind_methods();
};
