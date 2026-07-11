#pragma once
#include <string>
#include <vector>

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "idtxflow/types/TargetTypes.h"

namespace godot
{
    class Node3D;
    class Texture2D;
    class StandardMaterial3D;
}

namespace idtxflow
{
namespace types
{
    // define Godot as target engine
    struct TargetEngineGodot {
        static constexpr const char* name = "Godot";
    };

    // A single blend shape (morph target) belonging to a mesh section. The delta
    // arrays are index-aligned with the section's Vertices array (one entry per
    // engine vertex). Godot's RELATIVE blend-shape mode adds weight * delta to the
    // base geometry, so these hold the raw per-vertex OFFSETS (not base + delta):
    // storing absolute positions here would add an extra weight * base term and
    // blow the mesh apart. When the base surface has normals (it always does after
    // conversion), nrm_deltas is sized to match and zero-filled where the shape
    // authored no normal offsets -- Godot rejects a surface whose blend shapes do
    // not carry the same Vertex/Normal arrays as the base.
    struct BlendShapeData
    {
        std::string name;
        float weight = 0.0f;
        bool has_normals = false;
        godot::PackedVector3Array pos_deltas;   // size == Vertices.size()
        godot::PackedVector3Array nrm_deltas;   // size == Vertices.size()
    };

    struct MeshData
    {
        enum BoneWeightCount : uint32_t
        {
            BONEWEIGHT_COUNT_4 = 4,
            BONEWEIGHT_COUNT_8 = 8,
        };

        godot::PackedVector3Array Vertices;
        godot::PackedInt32Array Triangles;
        godot::PackedVector3Array Normals;
        godot::PackedVector2Array UVs;
        godot::PackedColorArray VertexColors;
        godot::PackedInt32Array Bones;
        godot::PackedFloat32Array Weights;
        // Blend shapes (morph targets) authored on the mesh's UsdSkelBlendShape
        // targets, densified onto this section's engine vertices. Empty for meshes
        // without blend shapes.
        std::vector<BlendShapeData> BlendShapes;
        // to be able to create the mesh array surface with the correct bone weight count we store this information here
        // default bone weight count per vertex is 4
        BoneWeightCount boneWeightCount = BONEWEIGHT_COUNT_4;
    };

    template<>
    struct TargetEngineTypes<TargetEngineGodot>
    {
        using Vector4 = godot::Vector4;
        using Vector3 = godot::Vector3;
        using Vector2 = godot::Vector2;
        using Quaternion = godot::Quaternion;
        using Color = godot::Color;
        using Transform = godot::Transform3D;
        using MeshData = MeshData;
        using Index = size_t;

        using Material = godot::Ref<godot::StandardMaterial3D>;
        using Texture = godot::Ref<godot::Texture2D>;

        using ConvertedEntity = godot::Node3D;
        using OwningEntity = godot::Node3D;
    };

    static_assert(TargetEngineTypesLike<TargetEngineTypes<TargetEngineGodot>>, 
                  "Godot's engine types don't satisfy concept requirements");
}
}
