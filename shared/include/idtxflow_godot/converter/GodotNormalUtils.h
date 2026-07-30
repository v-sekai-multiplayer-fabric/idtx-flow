#pragma once

/**
 * @file GodotNormalUtils.h
 * @brief Reusable normal-computation utilities for Godot mesh conversion.
 *
 * Provides smoothing-group-aware (a.k.a. "split") normal generation for meshes
 * that lack authored normals.  The algorithm mirrors Blender's
 * `normals_split_get()` / `vertex_normals_get()`: vertices that share a
 * position AND a face-corner normal belong to one smoothing group, and the
 * contribution of every incident triangle's face normal is accumulated into
 * each vertex of the group.  The result is smooth shading where the artist
 * intended it (continuous normal field) and faceted shading at hard edges
 * where the normal was authored sharp or derived from a split vertex.
 *
 * Both the base-mesh builder and the blend-shape normal-derivation code use
 * these routines so that the delta between a morphed shape and its base is
 * exactly zero on vertices the shape does not move.
 */

#include <map>

#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/core/math.hpp>

namespace idtxflow
{
namespace converter
{

// Quantization factor for weld keys: positions/normals are rounded to this
// many steps per unit so tiny float differences do not split a smoothing
// vertex.
constexpr float WELD_QUANT = 4096.0f;

/**
 * Integer key that identifies a SMOOTHING vertex by its quantized base
 * position and base normal -- i.e. the mesh's smoothing groups.  Corners that
 * share a position and a normal are one smooth vertex; a hard edge (same
 * position, different authored normal) stays split.  This mirrors how
 * Blender's glTF importer welds verts (merge_duplicate_verts, on position +
 * rounded normal), so recomputed normals respect exactly the smoothing the
 * source authored -- smooth where it is smooth, faceted where it is flat.
 * std::map needs a strict-weak ordering, hence operator<.
 */
struct WeldKey
{
    int64_t v[6];

    bool operator<(const WeldKey& other) const
    {
        for (int i = 0; i < 6; ++i)
        {
            if (v[i] != other.v[i])
            {
                return v[i] < other.v[i];
            }
        }
        return false;
    }
};

inline WeldKey MakeWeldKey(const godot::Vector3& position, const godot::Vector3& normal)
{
    WeldKey key;
    key.v[0] = static_cast<int64_t>(godot::Math::round(position.x * WELD_QUANT));
    key.v[1] = static_cast<int64_t>(godot::Math::round(position.y * WELD_QUANT));
    key.v[2] = static_cast<int64_t>(godot::Math::round(position.z * WELD_QUANT));
    key.v[3] = static_cast<int64_t>(godot::Math::round(normal.x * WELD_QUANT));
    key.v[4] = static_cast<int64_t>(godot::Math::round(normal.y * WELD_QUANT));
    key.v[5] = static_cast<int64_t>(godot::Math::round(normal.z * WELD_QUANT));
    return key;
}

/**
 * Compute per-vertex normals for a mesh, respecting the source's smoothing
 * groups (Blender's normals_split_get).  The smoothing partition is keyed on
 * the base (position, normal) via keyVerts/keyNormals, while the face normals
 * are measured from facePositions -- pass the base positions to get the rest
 * normals, or the morphed positions to get a shape's normals.  Because both
 * use the SAME partition, a face the shape does not move yields an identical
 * normal in both -> the delta is zero there, keeping shapes independent.
 *
 * @param facePositions  The vertex positions to compute face normals from
 *                       (these may be the base positions or the morphed
 *                       positions of a blend shape).
 * @param keyVerts       The base positions used for the smoothing-group weld
 *                       key (always the base mesh, never the morphed shape).
 * @param keyNormals     The base normals used for the smoothing-group weld
 *                       key (always the base mesh, never the morphed shape).
 * @param tris           Triangle index array emitted by the mesh builder.
 * @return Per-vertex normals, size == facePositions.size().
 */
inline godot::PackedVector3Array ComputeGroupedNormals(
    const godot::PackedVector3Array& facePositions,
    const godot::PackedVector3Array& keyVerts,
    const godot::PackedVector3Array& keyNormals,
    const godot::PackedInt32Array& tris)
{
    const int64_t vertexCount = facePositions.size();
    godot::PackedVector3Array normals;
    normals.resize(vertexCount);

    std::map<WeldKey, godot::Vector3> accumulated;
    for (int64_t t = 0; t + 2 < tris.size(); t += 3)
    {
        const int indexA = tris[t];
        const int indexB = tris[t + 1];
        const int indexC = tris[t + 2];
        // The builder emits Godot's REVERSED winding uniformly, so cross the
        // edges in reversed order (C-A, B-A) to recover the true outward-facing
        // normal.
        const godot::Vector3 faceNormal =
            (facePositions[indexC] - facePositions[indexA])
                .cross(facePositions[indexB] - facePositions[indexA]);
        accumulated[MakeWeldKey(keyVerts[indexA], keyNormals[indexA])] += faceNormal;
        accumulated[MakeWeldKey(keyVerts[indexB], keyNormals[indexB])] += faceNormal;
        accumulated[MakeWeldKey(keyVerts[indexC], keyNormals[indexC])] += faceNormal;
    }
    for (int64_t i = 0; i < vertexCount; ++i)
    {
        const godot::Vector3 accumulatedNormal = accumulated[MakeWeldKey(keyVerts[i], keyNormals[i])];
        if (accumulatedNormal.length_squared() < 1e-20f)
        {
            normals[i] = godot::Vector3(0.0f, 1.0f, 0.0f); // degenerate: fall back to UP
        }
        else
        {
            normals[i] = accumulatedNormal.normalized();
        }
    }
    return normals;
}

} // namespace converter
} // namespace idtxflow