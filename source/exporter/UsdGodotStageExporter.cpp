// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// UsdGodotStageExporter — see header for scope. Cycle A: Xform + Mesh.

#include "UsdGodotStageExporter.h"

#include <cctype>
#include <set>
#include <sstream>
#include <vector>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/tokens.h>

#include <idtxflow/logging.h>

namespace idtxflow::exporter
{
    pxr::GfMatrix4d GodotTransformToUsdMatrix(godot::Transform3D const& t)
    {
        // Godot Transform3D is row-major basis + origin; USD GfMatrix4d
        // is row-major homogeneous 4x4. The translation lands in the
        // last row's first three entries (USD convention: row-vector
        // math on the right of the matrix).
        return pxr::GfMatrix4d(
            t.basis[0][0], t.basis[0][1], t.basis[0][2], 0.0,
            t.basis[1][0], t.basis[1][1], t.basis[1][2], 0.0,
            t.basis[2][0], t.basis[2][1], t.basis[2][2], 0.0,
            t.origin.x,    t.origin.y,    t.origin.z,    1.0);
    }

    std::string SanitisePrimName(godot::String const& godot_name)
    {
        std::string in = std::string(godot_name.utf8().get_data());
        if (in.empty()) return "Unnamed";
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            char c = in[i];
            bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_';
            if (i == 0 && std::isdigit(static_cast<unsigned char>(c))) {
                out.push_back('_');
            }
            out.push_back(ok ? c : '_');
        }
        return out;
    }

    pxr::SdfPath ChildPath(pxr::SdfPath const& parent, std::string const& desired)
    {
        pxr::SdfPath candidate = parent.AppendChild(pxr::TfToken(desired));
        // Caller is responsible for ensuring uniqueness across a single
        // parent — recursive walker tracks siblings in a set below.
        return candidate;
    }

    pxr::UsdGeomXform ExportXform(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& path,
        godot::Transform3D const& transform)
    {
        pxr::UsdGeomXform xform = pxr::UsdGeomXform::Define(stage, path);
        pxr::UsdGeomXformCommonAPI(xform).SetXformVectors(
            pxr::GfVec3d(transform.origin.x, transform.origin.y, transform.origin.z),
            pxr::GfVec3f(0, 0, 0),  // rotate (filled below via matrix op fallback)
            pxr::GfVec3f(1, 1, 1),  // scale  (ditto)
            pxr::GfVec3f(0, 0, 0),  // pivot
            pxr::UsdGeomXformCommonAPI::RotationOrderXYZ,
            pxr::UsdTimeCode::Default());
        // For non-identity rotation / non-unit scale, clear the common
        // API ops and write a single transform op with the full matrix
        // — keeps the round-trip lossless without decomposing basis.
        bool is_identity_basis = transform.basis.is_orthogonal()
            && transform.basis.get_scale().is_equal_approx(godot::Vector3(1, 1, 1));
        if (!is_identity_basis) {
            // Drop the SetXformVectors ops and use a matrix op instead.
            xform.GetPrim().GetAttribute(pxr::TfToken("xformOp:translate")).Clear();
            auto op = xform.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble);
            op.Set(GodotTransformToUsdMatrix(transform));
        }
        return xform;
    }

    pxr::UsdGeomCube ExportCube(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& path,
        godot::Transform3D const& transform,
        godot::Ref<godot::BoxMesh> const& mesh)
    {
        pxr::UsdGeomCube cube = pxr::UsdGeomCube::Define(stage, path);
        if (mesh.is_valid()) {
            godot::Vector3 size = mesh->get_size();
            // UsdGeomCube uses a single `size` attribute; take the max
            // axis since USD's cube is uniform. Lossy for non-cubic
            // boxes — Cycle A documents this; Cycle B can switch to a
            // UsdGeomMesh for accurate boxes.
            double s = std::max({size.x, size.y, size.z});
            cube.CreateSizeAttr().Set(s);
        }
        auto op = cube.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble);
        op.Set(GodotTransformToUsdMatrix(transform));
        return cube;
    }

    pxr::UsdGeomSphere ExportSphere(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& path,
        godot::Transform3D const& transform,
        godot::Ref<godot::SphereMesh> const& mesh)
    {
        pxr::UsdGeomSphere sphere = pxr::UsdGeomSphere::Define(stage, path);
        if (mesh.is_valid()) {
            sphere.CreateRadiusAttr().Set(static_cast<double>(mesh->get_radius()));
        }
        auto op = sphere.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble);
        op.Set(GodotTransformToUsdMatrix(transform));
        return sphere;
    }

    pxr::UsdGeomCylinder ExportCylinder(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& path,
        godot::Transform3D const& transform,
        godot::Ref<godot::CylinderMesh> const& mesh)
    {
        pxr::UsdGeomCylinder cyl = pxr::UsdGeomCylinder::Define(stage, path);
        if (mesh.is_valid()) {
            cyl.CreateRadiusAttr().Set(static_cast<double>(mesh->get_top_radius()));
            cyl.CreateHeightAttr().Set(static_cast<double>(mesh->get_height()));
            cyl.CreateAxisAttr().Set(pxr::UsdGeomTokens->y);
        }
        auto op = cyl.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble);
        op.Set(GodotTransformToUsdMatrix(transform));
        return cyl;
    }

    pxr::UsdGeomMesh ExportMesh(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& path,
        godot::Transform3D const& transform,
        godot::Ref<godot::Mesh> const& mesh)
    {
        pxr::UsdGeomMesh usd_mesh = pxr::UsdGeomMesh::Define(stage, path);
        if (!mesh.is_valid()) {
            return usd_mesh;
        }

        // Concatenate all surfaces into a single UsdGeomMesh (each
        // godot surface contributes vertices + triangles in the same
        // index space). Cycle B will split per-material into separate
        // GeomSubsets so the material binding tracks the surface
        // boundary.
        pxr::VtArray<pxr::GfVec3f> points;
        pxr::VtArray<int> face_vertex_counts;
        pxr::VtArray<int> face_vertex_indices;
        pxr::VtArray<pxr::GfVec3f> normals;

        int index_base = 0;
        int surface_count = mesh->get_surface_count();
        for (int s = 0; s < surface_count; ++s) {
            godot::Array arrays = mesh->surface_get_arrays(s);
            if (arrays.size() <= godot::Mesh::ARRAY_VERTEX) continue;

            godot::PackedVector3Array verts = arrays[godot::Mesh::ARRAY_VERTEX];
            godot::PackedInt32Array indices = arrays[godot::Mesh::ARRAY_INDEX];
            godot::PackedVector3Array godot_normals;
            if (arrays.size() > godot::Mesh::ARRAY_NORMAL) {
                godot_normals = arrays[godot::Mesh::ARRAY_NORMAL];
            }

            for (int i = 0; i < verts.size(); ++i) {
                godot::Vector3 v = verts[i];
                points.push_back(pxr::GfVec3f(v.x, v.y, v.z));
                if (godot_normals.size() == verts.size()) {
                    godot::Vector3 n = godot_normals[i];
                    normals.push_back(pxr::GfVec3f(n.x, n.y, n.z));
                }
            }

            int tri_count = indices.size() / 3;
            for (int t = 0; t < tri_count; ++t) {
                face_vertex_counts.push_back(3);
                face_vertex_indices.push_back(index_base + indices[3 * t + 0]);
                face_vertex_indices.push_back(index_base + indices[3 * t + 1]);
                face_vertex_indices.push_back(index_base + indices[3 * t + 2]);
            }
            index_base += static_cast<int>(verts.size());
        }

        usd_mesh.CreatePointsAttr().Set(points);
        usd_mesh.CreateFaceVertexCountsAttr().Set(face_vertex_counts);
        usd_mesh.CreateFaceVertexIndicesAttr().Set(face_vertex_indices);
        if (!normals.empty()) {
            usd_mesh.CreateNormalsAttr().Set(normals);
            usd_mesh.SetNormalsInterpolation(pxr::UsdGeomTokens->vertex);
        }

        auto op = usd_mesh.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble);
        op.Set(GodotTransformToUsdMatrix(transform));

        return usd_mesh;
    }

    pxr::SdfPath ExportNodeRecursive(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& parent_path,
        godot::Node3D* node)
    {
        if (node == nullptr) return pxr::SdfPath();

        // Pick a USD-legal, unique prim name under parent_path.
        std::string desired = SanitisePrimName(node->get_name());
        std::set<std::string> siblings;
        if (auto parent_prim = stage->GetPrimAtPath(parent_path)) {
            for (auto const& child : parent_prim.GetChildren()) {
                siblings.insert(child.GetName().GetString());
            }
        }
        std::string final_name = desired;
        for (int n = 2; siblings.count(final_name); ++n) {
            std::ostringstream oss; oss << desired << "_" << n;
            final_name = oss.str();
        }
        pxr::SdfPath my_path = parent_path.AppendChild(pxr::TfToken(final_name));

        // Dispatch by Godot node type. MeshInstance3D + its primitive
        // mesh variants land at specific UsdGeom* prim types; plain
        // Node3D becomes an Xform.
        godot::MeshInstance3D* mi = godot::Object::cast_to<godot::MeshInstance3D>(node);
        if (mi != nullptr) {
            godot::Ref<godot::Mesh> mesh = mi->get_mesh();
            godot::Ref<godot::BoxMesh>      box = mesh;
            godot::Ref<godot::SphereMesh>   sph = mesh;
            godot::Ref<godot::CylinderMesh> cyl = mesh;
            if (box.is_valid())      ExportCube(stage, my_path, mi->get_transform(), box);
            else if (sph.is_valid()) ExportSphere(stage, my_path, mi->get_transform(), sph);
            else if (cyl.is_valid()) ExportCylinder(stage, my_path, mi->get_transform(), cyl);
            else                      ExportMesh(stage, my_path, mi->get_transform(), mesh);
        } else {
            ExportXform(stage, my_path, node->get_transform());
        }

        // Recurse into children. We only walk Node3D descendants;
        // non-3D children (Scripts, etc.) are skipped silently.
        int child_count = node->get_child_count();
        for (int i = 0; i < child_count; ++i) {
            godot::Node3D* child_3d = godot::Object::cast_to<godot::Node3D>(node->get_child(i));
            if (child_3d != nullptr) {
                ExportNodeRecursive(stage, my_path, child_3d);
            }
        }

        return my_path;
    }

    bool ExportSceneToFile(godot::Node3D* root, godot::String const& path)
    {
        if (root == nullptr) {
            IDTX_LOG(IDTX_ERROR, "UsdGodotStageExporter: null root node");
            return false;
        }
        std::string std_path = std::string(path.utf8().get_data());

        pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateNew(std_path);
        if (!stage) {
            IDTX_LOG(IDTX_ERROR, "UsdGodotStageExporter: failed to create stage at %s",
                     std_path.c_str());
            return false;
        }
        stage->SetMetadata(pxr::TfToken("upAxis"), pxr::VtValue(pxr::TfToken("Y")));
        stage->SetMetadata(pxr::TfToken("metersPerUnit"), pxr::VtValue(1.0));

        // Root prim — emit a top-level Xform that mirrors the Godot
        // root's transform so re-import lands the scene identically.
        std::string root_name = SanitisePrimName(root->get_name());
        pxr::SdfPath root_path = pxr::SdfPath(std::string("/") + root_name);
        ExportXform(stage, root_path, root->get_transform());
        stage->SetDefaultPrim(stage->GetPrimAtPath(root_path));

        int n = root->get_child_count();
        for (int i = 0; i < n; ++i) {
            godot::Node3D* child = godot::Object::cast_to<godot::Node3D>(root->get_child(i));
            if (child != nullptr) {
                ExportNodeRecursive(stage, root_path, child);
            }
        }
        stage->GetRootLayer()->Save();
        IDTX_LOG(IDTX_INFO, "UsdGodotStageExporter: wrote %s", std_path.c_str());
        return true;
    }
}
