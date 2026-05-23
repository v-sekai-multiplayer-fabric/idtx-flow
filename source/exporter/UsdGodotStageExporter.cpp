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

#include <cmath>
#include <unordered_map>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/tokens.h>

#include <godot_cpp/classes/skeleton3d.hpp>

// LEMON's max-weight matching (libs/lemon/lemon/matching.h, cgg-bern
// fork). Included only in the .cpp so the header doesn't pull the
// LEMON dependency into every translation unit.
#include <lemon/smart_graph.h>
#include <lemon/matching.h>

#include <idtxflow/utils/Logger.h>

namespace idtxflow::exporter
{
    IDTX_LOG_CATEGORY("UsdGodotStageExporter")

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

    namespace {
        // Triangle helpers used by ReconstructQuads. The triangle
        // adjacency graph has one node per triangle and one edge
        // between two triangles iff they share an edge (i.e. two
        // vertex indices in common, in opposite winding).

        struct TriEdge {
            int a, b;  // canonical: a < b
            TriEdge(int x, int y) : a(std::min(x, y)), b(std::max(x, y)) {}
            bool operator==(TriEdge const& o) const { return a == o.a && b == o.b; }
        };
        struct TriEdgeHash {
            size_t operator()(TriEdge const& e) const noexcept {
                return std::hash<long long>()(
                    (static_cast<long long>(e.a) << 32) | static_cast<unsigned int>(e.b));
            }
        };

        // The "third vertex" of a triangle given the two endpoints of
        // its shared edge. Returns -1 if the edge isn't in the triangle.
        int OppositeVertex(int v0, int v1, int v2, int shared_a, int shared_b)
        {
            int s = (shared_a + shared_b);
            if (v0 + v1 == s && (v0 == shared_a || v0 == shared_b)) return v2;
            if (v0 + v2 == s && (v0 == shared_a || v0 == shared_b)) return v1;
            if (v1 + v2 == s && (v1 == shared_a || v1 == shared_b)) return v0;
            // Robust fallback: linear check.
            if (v0 != shared_a && v0 != shared_b) return v0;
            if (v1 != shared_a && v1 != shared_b) return v1;
            if (v2 != shared_a && v2 != shared_b) return v2;
            return -1;
        }

        pxr::GfVec3f TriNormal(pxr::GfVec3f const& p0,
                                pxr::GfVec3f const& p1,
                                pxr::GfVec3f const& p2)
        {
            pxr::GfVec3f n = pxr::GfCross(p1 - p0, p2 - p0);
            float l = n.GetLength();
            return (l > 1e-12f) ? (n / l) : pxr::GfVec3f(0, 0, 1);
        }

        // True iff the quad (a, b, c, d) is convex in its average
        // plane. Tested via the sign of consecutive 2D cross products
        // after projecting onto a basis orthogonal to the face normal.
        bool QuadIsConvex(pxr::GfVec3f const& a,
                          pxr::GfVec3f const& b,
                          pxr::GfVec3f const& c,
                          pxr::GfVec3f const& d)
        {
            pxr::GfVec3f n = TriNormal(a, b, c) + TriNormal(a, c, d);
            float nl = n.GetLength();
            if (nl < 1e-12f) return false;
            n /= nl;
            pxr::GfVec3f e1 = (b - a);
            float e1l = e1.GetLength();
            if (e1l < 1e-12f) return false;
            e1 /= e1l;
            pxr::GfVec3f e2 = pxr::GfCross(n, e1);

            auto proj = [&](pxr::GfVec3f const& p) {
                pxr::GfVec3f v = p - a;
                return pxr::GfVec2f(pxr::GfDot(v, e1), pxr::GfDot(v, e2));
            };
            pxr::GfVec2f p[4] = { proj(a), proj(b), proj(c), proj(d) };
            float prev_cross = 0.0f;
            for (int i = 0; i < 4; ++i) {
                pxr::GfVec2f u = p[(i + 1) % 4] - p[i];
                pxr::GfVec2f v = p[(i + 2) % 4] - p[(i + 1) % 4];
                float c2 = u[0] * v[1] - u[1] * v[0];
                if (i > 0 && (c2 * prev_cross) < 0.0f) return false;
                prev_cross = c2;
            }
            return true;
        }
    }

    ReconstructedTopology ReconstructQuads(
        std::vector<int> const& triangulated_indices,
        pxr::VtArray<pxr::GfVec3f> const& vertices,
        pxr::VtArray<pxr::GfVec2f> const& uvs,
        float planarity_max_degrees,
        bool  uv_seam_check)
    {
        ReconstructedTopology out;
        int tri_count = static_cast<int>(triangulated_indices.size()) / 3;
        if (tri_count <= 1) {
            out.face_vertex_counts.reserve(tri_count);
            out.face_vertex_indices = triangulated_indices;
            for (int t = 0; t < tri_count; ++t) out.face_vertex_counts.push_back(3);
            return out;
        }

        // 1. Build edge -> [triangle ids] map. Each interior edge
        //    appears in exactly 2 triangles in a manifold mesh.
        std::unordered_map<TriEdge, std::vector<int>, TriEdgeHash> edge_to_tris;
        edge_to_tris.reserve(tri_count * 3);
        for (int t = 0; t < tri_count; ++t) {
            int i0 = triangulated_indices[3*t + 0];
            int i1 = triangulated_indices[3*t + 1];
            int i2 = triangulated_indices[3*t + 2];
            edge_to_tris[TriEdge(i0, i1)].push_back(t);
            edge_to_tris[TriEdge(i1, i2)].push_back(t);
            edge_to_tris[TriEdge(i2, i0)].push_back(t);
        }

        // 2. Set up the LEMON graph: one node per triangle, one
        //    candidate matching edge per shared edge between
        //    triangles that passes the quality filters.
        lemon::SmartGraph g;
        std::vector<lemon::SmartGraph::Node> tri_node(tri_count);
        for (int t = 0; t < tri_count; ++t) tri_node[t] = g.addNode();
        lemon::SmartGraph::EdgeMap<double> weight(g);

        // For each candidate, remember which shared edge produced it
        // so we can rebuild the quad winding when emitting.
        struct CandidateEdge {
            int tri_a, tri_b;
            int shared_v0, shared_v1;   // canonical endpoints of the dissolved edge
        };
        std::vector<CandidateEdge> candidates;
        candidates.reserve(edge_to_tris.size());
        std::vector<lemon::SmartGraph::Edge> cand_edges;
        cand_edges.reserve(edge_to_tris.size());

        float planarity_cos_threshold = std::cos(
            planarity_max_degrees * 3.14159265358979f / 180.0f);

        for (auto const& kv : edge_to_tris) {
            if (kv.second.size() != 2) continue;  // boundary or non-manifold
            int ta = kv.second[0];
            int tb = kv.second[1];
            int sa = kv.first.a;
            int sb = kv.first.b;

            int ta_i0 = triangulated_indices[3*ta + 0];
            int ta_i1 = triangulated_indices[3*ta + 1];
            int ta_i2 = triangulated_indices[3*ta + 2];
            int tb_i0 = triangulated_indices[3*tb + 0];
            int tb_i1 = triangulated_indices[3*tb + 1];
            int tb_i2 = triangulated_indices[3*tb + 2];

            int opp_a = OppositeVertex(ta_i0, ta_i1, ta_i2, sa, sb);
            int opp_b = OppositeVertex(tb_i0, tb_i1, tb_i2, sa, sb);
            if (opp_a < 0 || opp_b < 0) continue;

            // Planarity gate via normal-dot.
            if (sa >= (int)vertices.size() || sb >= (int)vertices.size()
                || opp_a >= (int)vertices.size() || opp_b >= (int)vertices.size()) continue;
            pxr::GfVec3f na = TriNormal(vertices[ta_i0], vertices[ta_i1], vertices[ta_i2]);
            pxr::GfVec3f nb = TriNormal(vertices[tb_i0], vertices[tb_i1], vertices[tb_i2]);
            float cos_nn = pxr::GfDot(na, nb);
            if (cos_nn < planarity_cos_threshold) continue;

            // Convexity gate.
            pxr::GfVec3f const& va = vertices[opp_a];
            pxr::GfVec3f const& vsa = vertices[sa];
            pxr::GfVec3f const& vb = vertices[opp_b];
            pxr::GfVec3f const& vsb = vertices[sb];
            if (!QuadIsConvex(va, vsa, vb, vsb)) continue;

            // UV-seam gate (skip when UVs aren't available).
            if (uv_seam_check && !uvs.empty()) {
                // The two triangles share endpoints sa, sb. In a UV
                // seam the same vertex index would map to different
                // UV coords in each surface — but at the mesh level
                // we only see one UV per vertex index, so a UV
                // mismatch shows up as the same index referenced by
                // both triangles with the same UV (i.e. no seam, OK).
                // For our purposes the check reduces to "same vertex
                // index used by both triangles for the shared edge",
                // which is implicit. The genuine seam case lives in
                // duplicated vertices upstream — when those exist
                // edge_to_tris matches by index so the two halves of
                // the seam never end up in the same key.
                // (No filter to apply here in the index-deduplicated
                // case; left as a documented seam.)
            }

            // Score = planarity quality. Range [-1, 1]; scale into a
            // positive integer-friendly weight for LEMON.
            double score = static_cast<double>(cos_nn) + 1.0;  // [0, 2]
            lemon::SmartGraph::Edge e = g.addEdge(tri_node[ta], tri_node[tb]);
            weight[e] = score;
            candidates.push_back({ta, tb, sa, sb});
            cand_edges.push_back(e);
        }

        // 3. Run max-weight matching.
        lemon::MaxWeightedMatching<lemon::SmartGraph,
            lemon::SmartGraph::EdgeMap<double>> matcher(g, weight);
        matcher.run();

        // 4. Emit: for each matched pair, write the quad; for each
        //    unmatched triangle, write the triangle.
        std::vector<bool> consumed(tri_count, false);
        out.face_vertex_counts.reserve(tri_count);
        out.face_vertex_indices.reserve(triangulated_indices.size());

        for (size_t i = 0; i < candidates.size(); ++i) {
            if (!matcher.matching(cand_edges[i])) continue;
            CandidateEdge const& c = candidates[i];
            if (consumed[c.tri_a] || consumed[c.tri_b]) continue;

            int ta = c.tri_a, tb = c.tri_b;
            int ta_i0 = triangulated_indices[3*ta + 0];
            int ta_i1 = triangulated_indices[3*ta + 1];
            int ta_i2 = triangulated_indices[3*ta + 2];
            int tb_i0 = triangulated_indices[3*tb + 0];
            int tb_i1 = triangulated_indices[3*tb + 1];
            int tb_i2 = triangulated_indices[3*tb + 2];

            int opp_a = OppositeVertex(ta_i0, ta_i1, ta_i2, c.shared_v0, c.shared_v1);
            int opp_b = OppositeVertex(tb_i0, tb_i1, tb_i2, c.shared_v0, c.shared_v1);
            // Quad winding: opp_a, shared_v0, opp_b, shared_v1.
            // (Standard "fan dissolved across shared edge" order.)
            out.face_vertex_counts.push_back(4);
            out.face_vertex_indices.push_back(opp_a);
            out.face_vertex_indices.push_back(c.shared_v0);
            out.face_vertex_indices.push_back(opp_b);
            out.face_vertex_indices.push_back(c.shared_v1);
            consumed[ta] = true;
            consumed[tb] = true;
        }
        for (int t = 0; t < tri_count; ++t) {
            if (consumed[t]) continue;
            out.face_vertex_counts.push_back(3);
            out.face_vertex_indices.push_back(triangulated_indices[3*t + 0]);
            out.face_vertex_indices.push_back(triangulated_indices[3*t + 1]);
            out.face_vertex_indices.push_back(triangulated_indices[3*t + 2]);
        }
        return out;
    }

    namespace {
        // Per CHI-251: importer stores the original n-gon face counts
        // on the Godot mesh as a sidecar so the round-trip back to
        // USD is lossless. Look for that metadata first.
        bool TryReadSidecarFaceCounts(
            godot::Ref<godot::Mesh> const& mesh,
            int surface_index,
            std::vector<int>& out_counts)
        {
            if (!mesh.is_valid()) return false;
            godot::String key = godot::String("original_face_vertex_counts_")
                + godot::String::num_int64(surface_index);
            if (!mesh->has_meta(key)) return false;
            godot::Variant v = mesh->get_meta(key);
            godot::PackedInt32Array arr = v;
            out_counts.clear();
            out_counts.reserve(arr.size());
            for (int i = 0; i < arr.size(); ++i) out_counts.push_back(arr[i]);
            return !out_counts.empty();
        }

        // CHI-253 per-node opt-out: source authors mark a Godot node
        // with `v_sekai:export:reconstructQuads = false` when the mesh
        // is intentionally tri-soup (hair cards, decals, prebaked LODs).
        bool ShouldReconstructQuads(godot::Node3D* source_node)
        {
            if (source_node == nullptr) return true;
            godot::String key = "v_sekai:export:reconstructQuads";
            if (!source_node->has_meta(key)) return true;
            return source_node->get_meta(key).booleanize();
        }
    }

    pxr::UsdGeomMesh ExportMesh(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& path,
        godot::Transform3D const& transform,
        godot::Ref<godot::Mesh> const& mesh,
        godot::Node3D* source_node)
    {
        pxr::UsdGeomMesh usd_mesh = pxr::UsdGeomMesh::Define(stage, path);
        if (!mesh.is_valid()) {
            return usd_mesh;
        }

        pxr::VtArray<pxr::GfVec3f> points;
        pxr::VtArray<int> face_vertex_counts;
        pxr::VtArray<int> face_vertex_indices;
        pxr::VtArray<pxr::GfVec3f> normals;

        bool reconstruct = ShouldReconstructQuads(source_node);

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

            // n-gon recovery decision (CHI-253):
            // 1. sidecar from the importer wins.
            // 2. else if reconstruction is enabled, dispatch the
            //    tris-to-quads hook.
            // 3. else write triangulated counts.
            std::vector<int> sidecar_counts;
            std::vector<int> tri_indices;
            tri_indices.reserve(indices.size());
            for (int i = 0; i < indices.size(); ++i) {
                tri_indices.push_back(index_base + indices[i]);
            }

            // Collect per-surface vertex slice + UVs for the
            // reconstruction call (it needs positions to gate
            // candidates by planarity/convexity).
            pxr::VtArray<pxr::GfVec3f> surf_verts;
            surf_verts.reserve(verts.size());
            for (int i = 0; i < verts.size(); ++i) {
                godot::Vector3 v = verts[i];
                surf_verts.push_back(pxr::GfVec3f(v.x, v.y, v.z));
            }
            pxr::VtArray<pxr::GfVec2f> surf_uvs;
            if (arrays.size() > godot::Mesh::ARRAY_TEX_UV) {
                godot::PackedVector2Array godot_uvs = arrays[godot::Mesh::ARRAY_TEX_UV];
                if (godot_uvs.size() == verts.size()) {
                    surf_uvs.reserve(godot_uvs.size());
                    for (int i = 0; i < godot_uvs.size(); ++i) {
                        godot::Vector2 uv = godot_uvs[i];
                        surf_uvs.push_back(pxr::GfVec2f(uv.x, uv.y));
                    }
                }
            }
            // tri_indices were built with index_base offset; for the
            // reconstruction call we work in the surface-local index
            // space, then re-add the base when emitting.
            std::vector<int> local_tri_indices(indices.size());
            for (int i = 0; i < indices.size(); ++i) local_tri_indices[i] = indices[i];

            if (TryReadSidecarFaceCounts(mesh, s, sidecar_counts)) {
                // Sidecar path — write counts verbatim, indices are
                // already in n-gon order on the original USD.
                for (int c : sidecar_counts) face_vertex_counts.push_back(c);
                for (int v : tri_indices)    face_vertex_indices.push_back(v);
            } else if (reconstruct) {
                ReconstructedTopology recovered = ReconstructQuads(
                    local_tri_indices, surf_verts, surf_uvs);
                for (int c : recovered.face_vertex_counts)  face_vertex_counts.push_back(c);
                for (int v : recovered.face_vertex_indices) face_vertex_indices.push_back(index_base + v);
            } else {
                int tri_count = static_cast<int>(indices.size()) / 3;
                for (int t = 0; t < tri_count; ++t) {
                    face_vertex_counts.push_back(3);
                    face_vertex_indices.push_back(tri_indices[3 * t + 0]);
                    face_vertex_indices.push_back(tri_indices[3 * t + 1]);
                    face_vertex_indices.push_back(tri_indices[3 * t + 2]);
                }
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
            else                      ExportMesh(stage, my_path, mi->get_transform(), mesh, mi);
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

    pxr::SdfPath ExportMaterial(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& mats_scope_path,
        std::string const& desired_name,
        godot::Ref<godot::BaseMaterial3D> const& mat)
    {
        if (!mat.is_valid()) return pxr::SdfPath();

        pxr::SdfPath mat_path = mats_scope_path.AppendChild(pxr::TfToken(desired_name));
        pxr::UsdShadeMaterial usd_mat = pxr::UsdShadeMaterial::Define(stage, mat_path);

        // Pull the colour. BaseMaterial3D::get_albedo() works on every
        // subclass that has an albedo, including StandardMaterial3D.
        godot::Color albedo = mat->get_albedo();
        float roughness = 0.5f;
        float metallic  = 0.0f;
        godot::Ref<godot::StandardMaterial3D> std_mat = mat;
        if (std_mat.is_valid()) {
            roughness = std_mat->get_roughness();
            metallic  = std_mat->get_metallic();
        }

        // UsdPreviewSurface output (hdStorm + Blender's USD importer).
        pxr::SdfPath preview_shader_path = mat_path.AppendChild(pxr::TfToken("PreviewSurface"));
        pxr::UsdShadeShader preview = pxr::UsdShadeShader::Define(stage, preview_shader_path);
        preview.CreateIdAttr(pxr::VtValue(pxr::TfToken("UsdPreviewSurface")));
        preview.CreateInput(pxr::TfToken("diffuseColor"), pxr::SdfValueTypeNames->Color3f)
            .Set(pxr::GfVec3f(albedo.r, albedo.g, albedo.b));
        preview.CreateInput(pxr::TfToken("roughness"), pxr::SdfValueTypeNames->Float)
            .Set(roughness);
        preview.CreateInput(pxr::TfToken("metallic"), pxr::SdfValueTypeNames->Float)
            .Set(metallic);
        pxr::UsdShadeOutput preview_out = preview.CreateOutput(
            pxr::TfToken("surface"), pxr::SdfValueTypeNames->Token);
        usd_mat.CreateSurfaceOutput().ConnectToSource(preview_out);

        // MaterialX twin (`outputs:mtlx:surface`). Standard surface
        // shader for cross-renderer portability.
        pxr::SdfPath mtlx_shader_path = mat_path.AppendChild(pxr::TfToken("MtlxSurface"));
        pxr::UsdShadeShader mtlx = pxr::UsdShadeShader::Define(stage, mtlx_shader_path);
        mtlx.CreateIdAttr(pxr::VtValue(pxr::TfToken("ND_standard_surface_surfaceshader")));
        mtlx.CreateInput(pxr::TfToken("base_color"), pxr::SdfValueTypeNames->Color3f)
            .Set(pxr::GfVec3f(albedo.r, albedo.g, albedo.b));
        mtlx.CreateInput(pxr::TfToken("base"), pxr::SdfValueTypeNames->Float).Set(1.0f);
        mtlx.CreateInput(pxr::TfToken("specular_roughness"), pxr::SdfValueTypeNames->Float)
            .Set(roughness);
        mtlx.CreateInput(pxr::TfToken("metalness"), pxr::SdfValueTypeNames->Float)
            .Set(metallic);
        pxr::UsdShadeOutput mtlx_out = mtlx.CreateOutput(
            pxr::TfToken("out"), pxr::SdfValueTypeNames->Token);
        usd_mat.CreateSurfaceOutput(pxr::TfToken("mtlx")).ConnectToSource(mtlx_out);

        return mat_path;
    }

    void BindMaterial(pxr::UsdPrim const& geom_prim, pxr::SdfPath const& material_path)
    {
        if (!geom_prim || material_path.IsEmpty()) return;
        pxr::UsdShadeMaterialBindingAPI binding =
            pxr::UsdShadeMaterialBindingAPI::Apply(geom_prim);
        pxr::UsdShadeMaterial mat = pxr::UsdShadeMaterial::Get(
            geom_prim.GetStage(), material_path);
        if (mat) binding.Bind(mat);
    }

    // ----------------------------------------------------------------
    // Cycle C — godot-vrm MToon detection + VSekaiMToonAPI emission.
    // ----------------------------------------------------------------

    bool IsGodotVrmMToon(godot::Ref<godot::Material> const& material)
    {
        if (!material.is_valid()) return false;
        godot::Ref<godot::ShaderMaterial> shader_mat = material;
        if (!shader_mat.is_valid()) return false;
        godot::Ref<godot::Shader> shader = shader_mat->get_shader();
        if (!shader.is_valid()) return false;
        // godot-vrm's shaders are named "mtoon", "mtoon_outline",
        // "mtoon_cutout", "mtoon_trans*", "mtoon_cull_off". Match any.
        godot::String name = shader->get_path().get_file().get_basename();
        std::string lc = std::string(name.to_lower().utf8().get_data());
        return lc.find("mtoon") != std::string::npos;
    }

    // Per-input mappings from godot-vrm MToon 0.x uniform names to
    // VRM 1.0 MToon attribute names. The forward direction is what
    // we apply here; the openusd-fabric SCSS bridge table lives in
    // maps/scss_mtoon_map.json and ships the inverse for the SCSS
    // path. Adding a real Lean-emitted table here is the next step
    // — for the cycle-C scaffold the inline list keeps the change
    // surface bounded.
    struct MToonAttr {
        char const* godot_uniform;   // godot-vrm uniform name (e.g. "_ShadeColor")
        char const* vsekai_attr;     // schema attribute (e.g. "v_sekai:mtoon:shadeColorFactor")
        char const* sdf_type;        // "Color3f", "Float", "Token", "Bool", "Int"
    };

    static constexpr MToonAttr kMToonMap[] = {
        {"_ShadeColor",         "v_sekai:mtoon:shadeColorFactor",            "Color3f"},
        {"_ShadeShift",         "v_sekai:mtoon:shadingShiftFactor",          "Float"},
        {"_ShadeToony",         "v_sekai:mtoon:shadingToonyFactor",          "Float"},
        {"_IndirectLightIntensity", "v_sekai:mtoon:giEqualizationFactor",    "Float"},
        {"_RimColor",           "v_sekai:mtoon:parametricRimColorFactor",    "Color3f"},
        {"_RimFresnelPower",    "v_sekai:mtoon:parametricRimFresnelPowerFactor", "Float"},
        {"_RimLift",            "v_sekai:mtoon:parametricRimLiftFactor",     "Float"},
        {"_RimLightingMix",     "v_sekai:mtoon:rimLightingMixFactor",        "Float"},
        {"_MatcapColor",        "v_sekai:mtoon:matcapFactor",                "Color3f"},
        {"_OutlineWidthMode",   "v_sekai:mtoon:outlineWidthMode",            "Token"},
        {"_OutlineWidth",       "v_sekai:mtoon:outlineWidthFactor",          "Float"},
        {"_OutlineColor",       "v_sekai:mtoon:outlineColorFactor",          "Color3f"},
        {"_OutlineLightingMix", "v_sekai:mtoon:outlineLightingMixFactor",    "Float"},
    };

    static char const* OutlineWidthModeToken(int idx)
    {
        // godot-vrm's _OutlineWidthMode enum: 0=None, 1=World, 2=Screen.
        // VRM 1.0 tokens: "none" / "worldCoordinates" / "screenCoordinates".
        switch (idx) {
            case 0:  return "none";
            case 1:  return "worldCoordinates";
            case 2:  return "screenCoordinates";
            default: return "none";
        }
    }

    void ApplyVSekaiMToonAPI(
        pxr::UsdShadeMaterial const& usd_mat,
        godot::Ref<godot::ShaderMaterial> const& source)
    {
        if (!usd_mat || !source.is_valid()) return;
        pxr::UsdPrim prim = usd_mat.GetPrim();
        prim.ApplyAPI(pxr::TfToken("VSekaiMToonAPI"));

        for (auto const& m : kMToonMap) {
            godot::Variant v = source->get_shader_parameter(m.godot_uniform);
            if (v.get_type() == godot::Variant::NIL) continue;
            pxr::SdfValueTypeName ty;
            std::string ty_s = m.sdf_type;
            if (ty_s == "Color3f") ty = pxr::SdfValueTypeNames->Color3f;
            else if (ty_s == "Float") ty = pxr::SdfValueTypeNames->Float;
            else if (ty_s == "Token") ty = pxr::SdfValueTypeNames->Token;
            else if (ty_s == "Bool")  ty = pxr::SdfValueTypeNames->Bool;
            else if (ty_s == "Int")   ty = pxr::SdfValueTypeNames->Int;
            else continue;

            pxr::UsdAttribute attr = prim.CreateAttribute(pxr::TfToken(m.vsekai_attr), ty);
            if (ty_s == "Color3f") {
                godot::Color c = v;
                attr.Set(pxr::GfVec3f(c.r, c.g, c.b));
            } else if (ty_s == "Float") {
                attr.Set(static_cast<float>(double(v)));
            } else if (ty_s == "Token") {
                // outlineWidthMode is the only Token in the current
                // map; godot-vrm stores it as a float enum.
                int idx = static_cast<int>(double(v));
                attr.Set(pxr::TfToken(OutlineWidthModeToken(idx)));
            } else if (ty_s == "Bool") {
                attr.Set(static_cast<bool>(v));
            } else if (ty_s == "Int") {
                attr.Set(static_cast<int>(int64_t(v)));
            }
        }
    }

    // ----------------------------------------------------------------
    // Cycle D — Skeleton3D + SpringBoneSimulator3D.
    // ----------------------------------------------------------------

    pxr::SdfPath ExportSkeleton(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& parent_path,
        godot::Skeleton3D* skeleton)
    {
        if (skeleton == nullptr) return pxr::SdfPath();
        std::string skel_root_name = SanitisePrimName(skeleton->get_name()) + "_SkelRoot";
        pxr::SdfPath root_path = parent_path.AppendChild(pxr::TfToken(skel_root_name));
        pxr::UsdSkelRoot::Define(stage, root_path);

        pxr::SdfPath skel_path = root_path.AppendChild(pxr::TfToken("Skeleton"));
        pxr::UsdSkelSkeleton usd_skel = pxr::UsdSkelSkeleton::Define(stage, skel_path);

        // Collect joints, parents, rest + bind transforms. UsdSkel
        // joint identifiers are skeleton-relative path strings: each
        // bone's path is its parent's path + "/" + sanitised name,
        // with the root bones at the top level.
        int n = skeleton->get_bone_count();
        std::vector<std::string> joint_paths(n);
        pxr::VtArray<pxr::TfToken> joints;
        pxr::VtArray<pxr::GfMatrix4d> bind_transforms;
        pxr::VtArray<pxr::GfMatrix4d> rest_transforms;

        for (int i = 0; i < n; ++i) {
            godot::String bone_name = skeleton->get_bone_name(i);
            std::string san = SanitisePrimName(bone_name);
            int parent = skeleton->get_bone_parent(i);
            joint_paths[i] = (parent < 0) ? san : (joint_paths[parent] + "/" + san);
            joints.push_back(pxr::TfToken(joint_paths[i]));

            godot::Transform3D rest = skeleton->get_bone_rest(i);
            rest_transforms.push_back(GodotTransformToUsdMatrix(rest));
            godot::Transform3D bind = skeleton->get_bone_global_rest(i);
            bind_transforms.push_back(GodotTransformToUsdMatrix(bind));
        }

        usd_skel.CreateJointsAttr().Set(joints);
        usd_skel.CreateRestTransformsAttr().Set(rest_transforms);
        usd_skel.CreateBindTransformsAttr().Set(bind_transforms);

        auto op = usd_skel.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble);
        op.Set(GodotTransformToUsdMatrix(skeleton->get_transform()));

        return root_path;
    }

    pxr::SdfPath ExportSpringBones(
        pxr::UsdStageRefPtr const& stage,
        pxr::SdfPath const& parent_path,
        godot::Node3D* spring_bone_simulator)
    {
        if (spring_bone_simulator == nullptr) return pxr::SdfPath();
        // Group container so chains + colliders stay tidy under one prim.
        pxr::SdfPath group = parent_path.AppendChild(pxr::TfToken("SpringBones"));
        pxr::UsdGeomScope::Define(stage, group);

        // SpringBoneSimulator3D's chain + collider APIs are on the
        // Godot side. We introspect the node's children via the
        // Variant-callable surface so the exporter compiles without
        // depending on godot-vrm's headers at build time. For the MVP
        // each child Node3D that names itself "*_Chain" becomes a
        // VSekaiSpringBoneAPI prim; each "*_Collider" becomes a
        // VSekaiSpringBoneColliderAPI prim. Reading the actual
        // SpringBoneSimulator3D state via its scripted API lands when
        // godot-vrm exposes the chain/collider arrays as Godot
        // properties (issue tracked under CHI-252).
        int n = spring_bone_simulator->get_child_count();
        for (int i = 0; i < n; ++i) {
            godot::Node3D* child = godot::Object::cast_to<godot::Node3D>(
                spring_bone_simulator->get_child(i));
            if (child == nullptr) continue;
            std::string name = SanitisePrimName(child->get_name());
            pxr::SdfPath child_path = group.AppendChild(pxr::TfToken(name));
            pxr::UsdGeomXform xf = pxr::UsdGeomXform::Define(stage, child_path);
            auto op = xf.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble);
            op.Set(GodotTransformToUsdMatrix(child->get_transform()));

            bool is_chain = name.find("Chain") != std::string::npos;
            bool is_collider = name.find("Collider") != std::string::npos;
            if (is_chain) {
                xf.GetPrim().ApplyAPI(pxr::TfToken("VSekaiSpringBoneAPI"));
                // Default values matching the schema; runtime values
                // get filled in when godot-vrm's SpringBoneSimulator3D
                // exposes its per-chain parameters.
                xf.GetPrim().CreateAttribute(
                    pxr::TfToken("v_sekai:springBone:stiffness"),
                    pxr::SdfValueTypeNames->Float).Set(1.0f);
                xf.GetPrim().CreateAttribute(
                    pxr::TfToken("v_sekai:springBone:drag"),
                    pxr::SdfValueTypeNames->Float).Set(0.4f);
                xf.GetPrim().CreateAttribute(
                    pxr::TfToken("v_sekai:springBone:hitRadius"),
                    pxr::SdfValueTypeNames->Float).Set(0.02f);
            } else if (is_collider) {
                xf.GetPrim().ApplyAPI(pxr::TfToken("VSekaiSpringBoneColliderAPI"));
                xf.GetPrim().CreateAttribute(
                    pxr::TfToken("v_sekai:springBone:collider:shape"),
                    pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("sphere"));
                xf.GetPrim().CreateAttribute(
                    pxr::TfToken("v_sekai:springBone:collider:radius"),
                    pxr::SdfValueTypeNames->Float).Set(0.05f);
            }
        }
        return group;
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
