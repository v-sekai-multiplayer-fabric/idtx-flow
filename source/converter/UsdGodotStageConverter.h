#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/skin.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/core/math.hpp>

#include <array>
#include <map>
#include <vector>

#include <pxr/base/tf/token.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformCache.h>

#include <idtx/datasource.h>
#include <idtx/mockDatasource_RandomFloat.h>

#include <idtxflow/converter/TypeConverter.h>
#include <idtxflow/converter/StageConverter.h>
#include <idtxflow/converter/PrimConverterRegistry.h>

#include <idtxflow_godot/types/GodotTypes.h>

#include "nodes/UsdStaticBodyNode3D.h"
#include "nodes/UsdMeshInstanceNode3D.h"
#include "nodes/UsdMockDatasourceFloatNode3D.h"
#include "nodes/UsdXFormNode3D.h"
#include "nodes/UsdMultiMeshInstanceNode3D.h"

/**
 * Implement Godot engine specialization of the UsdStageConverter
 **/
namespace idtxflow
{
namespace helper
{    
    template<typename NodeType>
        requires requires(NodeType* n, godot::Ref<godot::Animation> anim) { n->set_animation(anim); }
    inline void AddAnimation(
        const converter::AnimationDescription<types::TargetEngineGodot>& animation,
        NodeType* node,
        double animation_length)
    {
        godot::Ref<godot::Animation> godot_animation;
        godot_animation.instantiate();
        godot_animation->set_length(static_cast<float>(animation_length));
        godot_animation->set_loop_mode(godot::Animation::LOOP_NONE);

        // in the XForm animation case we expect only one Transform track to be extracted from USD
        if (!animation.Tracks.empty())
        {
            for (const auto& track: animation.Tracks)
            {
                if (track.Keys.empty()) continue;
                
                switch (track.Type)
                {
                case converter::TRACK_TRANSFORM:
                    {
                        // a transform track need to be split into seperate animation tracks in godot for
                        // position, rotation and scale
                        int32_t pos_track = godot_animation->add_track(godot::Animation::TYPE_POSITION_3D);
                        godot_animation->track_set_path(pos_track, godot::NodePath(track.Name.c_str()));
                        int32_t rot_track = godot_animation->add_track(godot::Animation::TYPE_ROTATION_3D);
                        godot_animation->track_set_path(rot_track, godot::NodePath(track.Name.c_str()));
                        int32_t scale_track = godot_animation->add_track(godot::Animation::TYPE_SCALE_3D);
                        godot_animation->track_set_path(scale_track, godot::NodePath(track.Name.c_str()));
                        
                        for (const auto& key : track.Keys)
                        {
                            godot::Transform3D animTransform = std::get<types::TargetEngineTypes<types::TargetEngineGodot>::Transform>(key.Value);
                            godot_animation->position_track_insert_key(pos_track, key.Time, animTransform.origin);
                            godot_animation->rotation_track_insert_key(rot_track, key.Time, animTransform.basis.get_rotation_quaternion());
                            godot_animation->scale_track_insert_key(scale_track, key.Time, animTransform.basis.get_scale());
                        }
                        
                        break;
                    }
                    case converter::TRACK_POSITION:
                    {
                        int32_t pos_track = godot_animation->add_track(godot::Animation::TYPE_POSITION_3D);
                        godot_animation->track_set_path(pos_track, godot::NodePath(track.Name.c_str()));
                        for (const auto& key : track.Keys)
                        {
                            godot::Vector3 anim_pos = std::get<types::TargetEngineTypes<types::TargetEngineGodot>::Vector3>(key.Value);
                            godot_animation->position_track_insert_key(pos_track, key.Time, anim_pos);
                        }
                        break;
                    }
                    case converter::TRACK_ROTATION:
                    {
                        int32_t rot_track = godot_animation->add_track(godot::Animation::TYPE_ROTATION_3D);
                        godot_animation->track_set_path(rot_track, godot::NodePath(track.Name.c_str()));
                        for (const auto& key : track.Keys)
                        {
                            godot::Quaternion anim_rot = std::get<types::TargetEngineTypes<types::TargetEngineGodot>::Quaternion>(key.Value);
                            godot_animation->rotation_track_insert_key(rot_track, key.Time, anim_rot);
                        }
                        break;
                    }
                    case converter::TRACK_SCALE:
                    {
                        int32_t scl_track = godot_animation->add_track(godot::Animation::TYPE_SCALE_3D);
                        godot_animation->track_set_path(scl_track, godot::NodePath(track.Name.c_str()));
                        for (const auto& key : track.Keys)
                        {
                            godot::Vector3 anim_scl = std::get<types::TargetEngineTypes<types::TargetEngineGodot>::Vector3>(key.Value);
                            godot_animation->scale_track_insert_key(scl_track, key.Time, anim_scl);
                        }
                        break;
                    }
                }
            }
        } 
        
        node->set_animation(godot_animation);
    }
}

namespace converter
{
    constexpr float MIN_SPHERE_RADIUS = 1e-6f;
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertXform(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation)
    {
        UsdXformNode3D* converted_node = memnew(UsdXformNode3D);
        converted_node->set_transform(transform);

        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, converted_node, StageAnimationLength);
        }

        return converted_node;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertCube(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation,
        const std::optional<godot::Ref<godot::StandardMaterial3D>>& material,
        float cube_size,
        const pxr::VtArray<class pxr::GfVec4f>& display_colors,
        const class pxr::TfToken& color_interpolation)
    {
        godot::Ref<godot::BoxMesh> box;
        box.instantiate();
        box->set_size(godot::Vector3(cube_size, cube_size, cube_size));

        godot::Ref<godot::StandardMaterial3D> standard_material;
        if (material.has_value())
        {
            standard_material = material.value();
        } else
        {
            // use a default material if none could be created
            standard_material.instantiate();
            // if the current Mesh prim authored a display color as a constant value, pass this into the
            // default material as base albedo color.
            if (!display_colors.empty() && color_interpolation == pxr::UsdGeomTokens->constant)
            {
                godot::Color albedo = TypeConverter::toColor(display_colors[0]);
                standard_material->set_albedo(albedo);
                if (albedo.a < 1.0f) standard_material->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_ALPHA);
            } else
                standard_material->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        }
        
        box->set_material(standard_material);
        
        UsdMeshInstanceNode3D* converted_node = memnew(UsdMeshInstanceNode3D);
        converted_node->set_mesh(box);
        converted_node->set_transform(transform);
        
        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, converted_node, StageAnimationLength);
        }
        
        return converted_node;
    }

    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertCylinder(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation,
        const std::optional<godot::Ref<godot::StandardMaterial3D>>& material,
        float cylinder_radius,
        float cylinder_height,
        const pxr::VtArray<class pxr::GfVec4f>& display_colors,
        const class pxr::TfToken& color_interpolation)
    {
        godot::Ref<godot::CylinderMesh> cylinder;
        cylinder.instantiate();
        
        cylinder->set_top_radius(cylinder_radius);
        cylinder->set_bottom_radius(cylinder_radius);
        cylinder->set_height(cylinder_height);

        godot::Ref<godot::StandardMaterial3D> standard_material;
        if (material.has_value())
        {
            standard_material = material.value();
        } else
        {
            // use a default material if none could be created
            standard_material.instantiate();
            // if the current Mesh prim authored a display color as a constant value, pass this into the
            // default material as base albedo color.
            if (!display_colors.empty() && color_interpolation == pxr::UsdGeomTokens->constant)
            {
                godot::Color albedo = TypeConverter::toColor(display_colors[0]);
                standard_material->set_albedo(albedo);
                if (albedo.a < 1.0f) standard_material->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_ALPHA);
            } else
                standard_material->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        }
        
        cylinder->set_material(standard_material);

        UsdMeshInstanceNode3D* converted_node = memnew(UsdMeshInstanceNode3D);
        converted_node->set_mesh(cylinder);
        converted_node->set_transform(transform);
        
        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, converted_node, StageAnimationLength);
        }
        
        return converted_node;
    }

    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertCone(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation,
        const std::optional<godot::Ref<godot::StandardMaterial3D>>& material,
        float cone_radius,
        float cone_height,
        const pxr::VtArray<class pxr::GfVec4f>& display_colors,
        const class pxr::TfToken& color_interpolation)
    {
        godot::Ref<godot::CylinderMesh> cylinder;
        cylinder.instantiate();
        
        cylinder->set_top_radius(0.0);
        cylinder->set_bottom_radius(cone_radius);
        cylinder->set_height(cone_height);

        godot::Ref<godot::StandardMaterial3D> standard_material;
        if (material.has_value())
        {
            standard_material = material.value();
        } else
        {
            // use a default material if none could be created
            standard_material.instantiate();
            // if the current Mesh prim authored a display color as a constant value, pass this into the
            // default material as base albedo color.
            if (!display_colors.empty() && color_interpolation == pxr::UsdGeomTokens->constant)
            {
                godot::Color albedo = TypeConverter::toColor(display_colors[0]);
                standard_material->set_albedo(albedo);
                if (albedo.a < 1.0f) standard_material->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_ALPHA);
            } else
                standard_material->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        }
        
        cylinder->set_material(standard_material);

        UsdMeshInstanceNode3D* converted_node = memnew(UsdMeshInstanceNode3D);
        converted_node->set_mesh(cylinder);
        converted_node->set_transform(transform);
        
        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, converted_node, StageAnimationLength);
        }
        
        return converted_node;
    }

    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertSphere(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation,
        const std::optional<typename Types::Material>& material,
        float sphere_radius,
        const pxr::VtArray<class pxr::GfVec4f>& display_colors,
        const class pxr::TfToken& color_interpolation)
    {
        godot::Ref<godot::SphereMesh> sphere;
        sphere.instantiate();
        // ensure that the sphere radius never get 0.0 as this would cause the following error downstream
        // servers/rendering/renderer_scene_cull.cpp:991 - Condition "!v.is_finite()" is true.
        sphere->set_radius(std::max(sphere_radius, MIN_SPHERE_RADIUS));
        sphere->set_height(sphere_radius * 2.0f);
        
        godot::Ref<godot::StandardMaterial3D> standard_material;
        if (material.has_value())
        {
            standard_material = material.value();
        } else
        {
            // use a default material if none could be created
            standard_material.instantiate();
            // if the current Mesh prim authored a display color as a constant value, pass this into the
            // default material as base albedo color.
            if (!display_colors.empty() && color_interpolation == pxr::UsdGeomTokens->constant)
            {
                godot::Color albedo = TypeConverter::toColor(display_colors[0]);
                standard_material->set_albedo(albedo);
                if (albedo.a < 1.0f) standard_material->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_ALPHA);
            } else
                standard_material->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        }
        
        sphere->set_material(standard_material);
        
        UsdMeshInstanceNode3D* converted_node = memnew(UsdMeshInstanceNode3D);
        converted_node->set_mesh(sphere);
        converted_node->set_transform(transform);
        
        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, converted_node, StageAnimationLength);
        }
        
        return converted_node;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertCollisionRoot(
        const godot::Transform3D& transform,
        const pxr::GfVec3f highlightColor,
        const std::string identifier,
        const bool enabled,
        const bool highlightable)
    {
        UsdXformNode3D* converted_node = memnew(UsdXformNode3D);
        converted_node->set_transform(transform);

        godot::Color color = TypeConverter::toColor(highlightColor);
        
        converted_node->set_meta("collision_enabled", enabled);
        converted_node->set_meta("highlightable", highlightable);
        converted_node->set_meta("highlight_color", color);  
        converted_node->set_meta("identifier", identifier.c_str());
        
        return converted_node;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertCollision(
        const godot::Transform3D& transform,
        const pxr::TfToken shape,
        const pxr::VtArray<pxr::TfToken> types,
        const pxr::GfVec3f axis,
        const double height,
        const double radius)
    {
        UsdStaticBodyNode3D* collisionNode = memnew(UsdStaticBodyNode3D);
        
        collisionNode->set_transformData(transform);
        collisionNode->set_collision_shape(shape.GetString().c_str());  // Set shape
        
        // Set interaction type
        // Convert incoming token array to Godot array
        godot::PackedStringArray result;
        result.resize(types.size());
        for (int i = 0; i < types.size(); ++i) {
            result.set(i, godot::String(types[i].GetText()));
        }
        collisionNode->set_collision_type( result );    
        
        godot::Vector3 main_axis = TypeConverter::toVector3(axis);
        collisionNode->set_axis(main_axis);
        
        if (height) {collisionNode->set_height(height); }
        if (radius) {collisionNode->set_radius(radius); }
        
        return collisionNode;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertMesh(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation,
        const std::vector<MeshDescription<UsdMeshConverter<types::TargetEngineGodot>::MeshDataType>>& mesh_descriptions,
        const pxr::VtArray<class pxr::GfVec4f>& display_colors,
        const class pxr::TfToken& color_interpolation)
    {
        godot::Ref<godot::ArrayMesh> mesh;
        mesh.instantiate();
        
        for (size_t MeshSection = 0; MeshSection < mesh_descriptions.size(); ++MeshSection)
        {
            godot::Array mesh_arrays;
            mesh_arrays.resize(godot::Mesh::ARRAY_MAX);
            mesh_arrays[godot::Mesh::ARRAY_VERTEX] = mesh_descriptions[MeshSection].meshData.Vertices;
            mesh_arrays[godot::Mesh::ARRAY_INDEX] = mesh_descriptions[MeshSection].meshData.Triangles;
            mesh_arrays[godot::Mesh::ARRAY_NORMAL] = mesh_descriptions[MeshSection].meshData.Normals;
            if (!mesh_descriptions[MeshSection].meshData.UVs.is_empty())
                mesh_arrays[godot::Mesh::ARRAY_TEX_UV] = mesh_descriptions[MeshSection].meshData.UVs;
            if (!mesh_descriptions[MeshSection].meshData.VertexColors.is_empty())
                mesh_arrays[godot::Mesh::ARRAY_COLOR] = mesh_descriptions[MeshSection].meshData.VertexColors;

            mesh->add_surface_from_arrays(godot::Mesh::PRIMITIVE_TRIANGLES, mesh_arrays);

            godot::Ref<godot::StandardMaterial3D> standard_material;
            std::optional<godot::Ref<godot::StandardMaterial3D>> material = ConvertMaterial(mesh_descriptions[MeshSection].usdMaterial);
            if (material.has_value())
            {
                standard_material = material.value();
            } else
            {
                // use a default material if none could be created
                standard_material.instantiate();
                // if the current Mesh prim authored a display color as a constant value, pass this into the
                // default material as base albedo color.
                if (!display_colors.empty() && color_interpolation == pxr::UsdGeomTokens->constant)
                {
                    godot::Color albedo = TypeConverter::toColor(display_colors[0]);
                    standard_material->set_albedo(albedo);
                    if (albedo.a < 1.0f) standard_material->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_ALPHA);
                } else
                    standard_material->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
            }
            
            mesh->surface_set_material(mesh->get_surface_count() - 1, standard_material);
        }
        
        UsdMeshInstanceNode3D* converted_node = memnew(UsdMeshInstanceNode3D);
        converted_node->set_mesh(mesh);
        converted_node->set_transform(transform);
        
        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, converted_node, StageAnimationLength);
        }
        
        return converted_node;
    }

    // Quantization factor for weld keys: positions/normals are rounded to this many
    // steps per unit so tiny float differences do not split a smoothing vertex.
    constexpr float WELD_QUANT = 4096.0f;

    // Integer key that identifies a SMOOTHING vertex by its quantized base position
    // and base normal -- i.e. the mesh's smoothing groups. Corners that share a
    // position and a normal are one smooth vertex; a hard edge (same position,
    // different authored normal) stays split. This mirrors how Blender's glTF
    // importer welds verts (merge_duplicate_verts, on position + rounded normal),
    // so recomputed normals respect exactly the smoothing the source authored --
    // smooth where it is smooth, faceted where it is flat. std::map needs a
    // strict-weak ordering, hence operator<.
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

    // Compute per-vertex normals for a mesh, respecting the source's smoothing groups
    // (Blender's normals_split_get). The smoothing partition is keyed on the BASE
    // (position, normal) via keyVerts/keyNormals, while the face normals are measured
    // from facePositions -- pass the base positions to get the rest normals, or the
    // morphed positions to get a shape's normals. Because both use the SAME partition,
    // a face the shape does not move yields an identical normal in both -> the delta
    // is zero there, keeping shapes independent.
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
            // The builder emits Godot's REVERSED winding uniformly, so cross the edges
            // in reversed order (C-A, B-A) to recover the true outward-facing normal.
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

    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertSkeleton(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation,
        const SkeletonDescription<types::TargetEngineGodot>& skeleton_description)
    {
        using GodotMaterialConverter = UsdMaterialConverter<types::TargetEngineGodot>;
        UsdSkeletonNode3D* skeleton = memnew(UsdSkeletonNode3D);
        
        // construct the skeletons bone hierarchy and store the map between bone name and it's index
        godot::Dictionary bone_name_map;
        for (auto& bone: skeleton_description.Bones)
        {
            std::string boneName = bone.Name;
            // we need to replace special characters in bone names that godot refuses to be allowed
            std::replace(boneName.begin(), boneName.end(), '/', '_');
            int32_t boneIndex = skeleton->add_bone(godot::String(boneName.c_str()));
            // keep track of a map between original bone name and none index (required for efficient bone animation)
            bone_name_map.set(godot::NodePath(bone.Name.c_str()), boneIndex);
            skeleton->set_bone_parent(boneIndex, bone.parentIndex);
            skeleton->set_bone_rest(boneIndex, bone.restTransform);
        }
        skeleton->set_joint_to_bone_map(bone_name_map);
        
        // the skeleton might be skinned by different meshes/skin targets. Create the corresponding MeshInstances
        // used to skin the skeleton
        for (auto& skinTarget: skeleton_description.SkinTargets)
        {
            godot::Ref<godot::Skin> skin;
            skin.instantiate();
            
            for (auto& bone: skeleton_description.Bones)
            {
                std::string boneName = bone.Name;
                // we need to replace special characters in bone names that godot refuses to be allowed
                std::replace(boneName.begin(), boneName.end(), '/', '_');
                godot::Transform3D bindTransform = bone.bindPose.affine_inverse() * skinTarget.GeomBindingTransform;
                skin->add_named_bind(godot::String(boneName.c_str()), bindTransform);
            }
            
            godot::Ref<godot::ArrayMesh> mesh;
            mesh.instantiate();
            
            auto& MeshDescriptions = skinTarget.MeshDescriptions;

            // Register blend shapes (morph targets) at the mesh level before any
            // surface is added: Godot requires every surface of a mesh to share the
            // same ordered blend-shape set. Every section of one USD mesh is bound to
            // the same UsdSkelBlendShape targets, so the first section that carries
            // them defines the names (and current weights) for the whole mesh.
            std::vector<float> blendShapeWeights;
            size_t blendShapeCount = 0;
            for (const MeshDescription<types::MeshData>& meshDescription: MeshDescriptions)
            {
                if (!meshDescription.meshData.BlendShapes.empty())
                {
                    // NORMALIZED, not RELATIVE: Godot stores blend-shape normals
                    // octahedral-encoded (direction only, unit length), so a non-unit
                    // "morph - base" normal delta loses its magnitude and a flipped
                    // face decodes to garbage. Godot's own glTF importer therefore uses
                    // NORMALIZED mode with ABSOLUTE morphed positions/normals -- the
                    // shader computes (1 - sum w)*base + sum w*absolute, which for
                    // positions is algebraically the additive delta result, and for
                    // normals keeps every stored value unit (oct-safe).
                    mesh->set_blend_shape_mode(godot::Mesh::BLEND_SHAPE_MODE_NORMALIZED);
                    for (const types::BlendShapeData& bs: meshDescription.meshData.BlendShapes)
                    {
                        mesh->add_blend_shape(godot::String(bs.name.c_str()));
                        blendShapeWeights.push_back(bs.weight);
                    }
                    blendShapeCount = meshDescription.meshData.BlendShapes.size();
                    break;
                }
            }

            // Set a custom AABB that covers the base geometry grown by the largest morph
            // offset, so editor framing and culling stay correct across the full morph
            // range instead of relying on Godot's per-mode auto AABB.
            bool haveAabb = false;
            godot::AABB aabb;
            float maxBlendDelta = 0.0f;

            //for (size_t MeshSection = 0; MeshSection < MeshDescriptions.size(); ++MeshSection)
            for (const auto& meshDescription: MeshDescriptions)
            {
                godot::Array mesh_arrays;
                mesh_arrays.resize(godot::Mesh::ARRAY_MAX);
                mesh_arrays[godot::Mesh::ARRAY_VERTEX] = meshDescription.meshData.Vertices;
                mesh_arrays[godot::Mesh::ARRAY_INDEX] = meshDescription.meshData.Triangles;
                mesh_arrays[godot::Mesh::ARRAY_NORMAL] = meshDescription.meshData.Normals;
                if (!meshDescription.meshData.UVs.is_empty())
                    mesh_arrays[godot::Mesh::ARRAY_TEX_UV] = meshDescription.meshData.UVs;
                if (!meshDescription.meshData.VertexColors.is_empty())
                    mesh_arrays[godot::Mesh::ARRAY_COLOR] = meshDescription.meshData.VertexColors;
                if (!meshDescription.meshData.Bones.is_empty())
                    mesh_arrays[godot::Mesh::ARRAY_BONES] = meshDescription.meshData.Bones;
                if (!meshDescription.meshData.Weights.is_empty())
                    mesh_arrays[godot::Mesh::ARRAY_WEIGHTS] = meshDescription.meshData.Weights;

                // If any blend shape lacks authored normal offsets we derive normals
                // from the deformed geometry. To keep shapes INDEPENDENT -- activating
                // one shape must not re-shade vertices it does not move -- the base
                // surface normals and every shape's delta are computed with the SAME
                // smooth method, so an unmoved vertex yields exactly a zero delta. This
                // mirrors Blender's glTF exporter (delta = morph_split - base_split).
                bool recomputeNormals = false;
                for (const types::BlendShapeData& bs: meshDescription.meshData.BlendShapes)
                {
                    if (!bs.has_normals)
                    {
                        recomputeNormals = true;
                        break;
                    }
                }
                godot::PackedVector3Array baseSplitNormals;
                if (recomputeNormals)
                {
                    // Rest normals, recomputed within the authored smoothing groups so
                    // the delta below shares the exact same partition (Blender's
                    // base = relative_key.normals_split_get()).
                    baseSplitNormals = ComputeGroupedNormals(
                        meshDescription.meshData.Vertices,
                        meshDescription.meshData.Vertices,
                        meshDescription.meshData.Normals,
                        meshDescription.meshData.Triangles);
                    mesh_arrays[godot::Mesh::ARRAY_NORMAL] = baseSplitNormals;
                }

                // depending on the stored bone weight count per vertex we need to pass a flag to ensure the
                // bone and bone-weight arrays are treated the right way
                uint64_t flags = 0;
                if (meshDescription.meshData.boneWeightCount == types::MeshData::BONEWEIGHT_COUNT_8)
                    flags = godot::Mesh::ARRAY_FLAG_USE_8_BONE_WEIGHTS;

                // Assemble this section's blend-shape arrays. NORMALIZED mode stores
                // ABSOLUTE morphed positions and normals (not deltas); it must match the
                // mesh-level blend-shape count/order registered above, so only emit them
                // when this section agrees.
                const godot::PackedVector3Array& baseVerts = meshDescription.meshData.Vertices;
                godot::Array blend_arrays;
                if (blendShapeCount > 0
                    && meshDescription.meshData.BlendShapes.size() == blendShapeCount)
                {
                    for (const types::BlendShapeData& bs: meshDescription.meshData.BlendShapes)
                    {
                        godot::Array bs_arr;
                        bs_arr.resize(godot::Mesh::ARRAY_MAX);

                        // ABSOLUTE morphed positions: base + delta.
                        godot::PackedVector3Array morphedVerts;
                        morphedVerts.resize(baseVerts.size());
                        for (int64_t i = 0; i < baseVerts.size(); ++i)
                        {
                            morphedVerts[i] = baseVerts[i] + bs.pos_deltas[i];
                        }
                        bs_arr[godot::Mesh::ARRAY_VERTEX] = morphedVerts;

                        // ABSOLUTE morphed normals (unit, so octahedral encoding is
                        // lossless). Every shape must supply a normal array or
                        // add_surface_from_arrays fails.
                        if (!recomputeNormals)
                        {
                            // Authored offsets: absolute = normalize(base_normal + delta).
                            const godot::PackedVector3Array& baseNormals = meshDescription.meshData.Normals;
                            godot::PackedVector3Array absNormals;
                            absNormals.resize(baseNormals.size());
                            for (int64_t i = 0; i < baseNormals.size(); ++i)
                            {
                                const godot::Vector3 n = baseNormals[i] + bs.nrm_deltas[i];
                                absNormals[i] = (n.length_squared() < 1e-20f) ? baseNormals[i] : n.normalized();
                            }
                            bs_arr[godot::Mesh::ARRAY_NORMAL] = absNormals;
                        }
                        else
                        {
                            // Recompute the morphed normals within the authored smoothing
                            // groups (Blender's key_block.normals_split_get). Already unit
                            // and absolute; a face this shape does not move reproduces the
                            // base normal, so activating the shape leaves it unchanged.
                            bs_arr[godot::Mesh::ARRAY_NORMAL] = ComputeGroupedNormals(
                                morphedVerts,
                                baseVerts,
                                meshDescription.meshData.Normals,
                                meshDescription.meshData.Triangles);
                        }
                        blend_arrays.push_back(bs_arr);

                        const godot::PackedVector3Array& d = bs.pos_deltas;
                        for (int64_t i = 0; i < d.size(); ++i)
                        {
                            maxBlendDelta = godot::Math::max(maxBlendDelta, godot::Math::abs(d[i].x));
                            maxBlendDelta = godot::Math::max(maxBlendDelta, godot::Math::abs(d[i].y));
                            maxBlendDelta = godot::Math::max(maxBlendDelta, godot::Math::abs(d[i].z));
                        }
                    }
                }

                // grow the base-geometry AABB with this section's vertices
                const godot::PackedVector3Array& secVerts = meshDescription.meshData.Vertices;
                for (int64_t i = 0; i < secVerts.size(); ++i)
                {
                    if (!haveAabb)
                    {
                        aabb = godot::AABB(secVerts[i], godot::Vector3());
                        haveAabb = true;
                    }
                    else
                    {
                        aabb = aabb.expand(secVerts[i]);
                    }
                }

                mesh->add_surface_from_arrays(
                    godot::Mesh::PRIMITIVE_TRIANGLES,
                    mesh_arrays,
                    blend_arrays,
                    godot::Dictionary(),
                    flags);

                godot::Ref<godot::StandardMaterial3D> standard_material;
                std::optional<godot::Ref<godot::StandardMaterial3D>> material = ConvertMaterial(meshDescription.usdMaterial);
                if (material.has_value())
                {
                    standard_material =  material.value();
                } else
                {
                    // use a default material if none could be created
                    standard_material.instantiate();
                }
                
                mesh->surface_set_material(mesh->get_surface_count() - 1, standard_material);
            }

            // With blend shapes present, override Godot's (wrong) auto AABB with the
            // base-geometry bounds grown by the largest morph offset, so editor
            // framing/culling stay correct instead of stretching toward the origin.
            if (blendShapeCount > 0 && haveAabb)
            {
                mesh->set_custom_aabb(aabb.grow(maxBlendDelta));
            }

            UsdMeshInstanceNode3D* node = memnew(UsdMeshInstanceNode3D);
            node->set_mesh(mesh);
            node->set_skin(skin);
            node->set_skeleton(skeleton);
            node->set_name(skinTarget.Name.c_str());
            node->set_stage_path(Stage->GetRootLayer()->GetRealPath().c_str());
            // TODO: check if required, as all nodes converted from an usdPrim implement IUsdNode3D
            node->set_meta("USD_NODE", true);

            node->set_prim_name(skinTarget.Name.c_str());
            node->set_prim_type("Mesh");

            // Apply each blend shape's authored/animated weight (the mesh must already
            // be set so the shapes exist on the instance). A weight of 0 is the rest
            // pose; these mirror whatever the bound skel animation resolved to.
            for (size_t b = 0; b < blendShapeWeights.size(); ++b)
            {
                node->set_blend_shape_value(static_cast<int>(b), blendShapeWeights[b]);
            }

            skeleton->add_child(node);
        }
        
        skeleton->reset_bone_poses();
        skeleton->set_transform(transform);
        
        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, skeleton, StageAnimationLength);
        }
        
        return skeleton;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertGprimPseudoInstance(
        const godot::Transform3D& transform,
        const pxr::UsdGeomGprim& usd_gprim,
        const pxr::SdfPath& usd_prototype_path)
    {
        // when converting pseudo instances, each prim is provided as fully composed version. Thus we need to create
        // the "prototype" from the first appearance of the pseudo instance and only add instances for any following prim
        // assuming they only differ in their transform
        if (StagePrototypeMap.contains(usd_prototype_path))
        {
            // if the prototype has been converted into a multimesh already, use this and add a new instance to it
            // once done, this will not return a converted node, or should it?
            pxr::UsdGeomXformCache xform_cache;
            godot::Transform3D global_instance_transform = TypeConverter::toTransform(xform_cache.GetLocalToWorldTransform(usd_gprim.GetPrim()));
            UsdMultiMeshInstanceNode3D* multimesh_node = static_cast<UsdMultiMeshInstanceNode3D*>(StagePrototypeMap.at(usd_prototype_path));
            multimesh_node->add_instance(global_instance_transform);
            // when re-using the converted prototype we will return nullptr instead of the already converted node
            return nullptr;
        }
        
        // create the instantiable mesh and add this first occurence as the first instance
        UsdMeshInstanceNode3D* mesh_instance = dynamic_cast<UsdMeshInstanceNode3D*>(ConvertGprim(usd_gprim));
        if (!mesh_instance) return nullptr;
        
        UsdMultiMeshInstanceNode3D* converted_node = memnew(UsdMultiMeshInstanceNode3D);
        godot::Ref<godot::MultiMesh> multi_mesh;
        multi_mesh.instantiate();
        multi_mesh->set_transform_format(godot::MultiMesh::TRANSFORM_3D);
        multi_mesh->set_mesh(mesh_instance->get_mesh());
        multi_mesh->set_instance_count(1);
        multi_mesh->set_instance_transform(0, transform);
        
        converted_node->set_multimesh(multi_mesh);
        
        // get the global transform of this first instance and store it in the MultimeshInstance node
        // but calculate it's parent global transform as instances will be actually parented to the MultiMesh instance
        // not to the first item in it
        pxr::UsdGeomXformCache xform_cache;
        godot::Transform3D global_transform = TypeConverter::toTransform(xform_cache.GetLocalToWorldTransform(usd_gprim.GetPrim()));
        converted_node->set_global_base_transform(global_transform * transform.affine_inverse());
        // release the converted prim used to build the MultiMeshInstance entity
        memdelete(mesh_instance);
        
        // store the converted node as prototype for all subsequent calls of this method
        StagePrototypeMap.emplace(usd_prototype_path, static_cast<void*>(converted_node));
        
        return converted_node;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertPrimWithPayload(
        const pxr::UsdPrim& usd_prim,
        const std::string& payload_uri,
        const godot::Transform3D& transform,
        const pxr::SdfLayerRefPtr& override_layer)
    {
        if (payload_uri.empty()) return nullptr;
        
        // a Prim with an authored payload that is not yet loaded will be treated as "standalone" stage node that
        // takes the payloadURI and runs the conversion of the referred stage on its own.
        UsdStageNode3D* stage_node = memnew(UsdStageNode3D);
        stage_node->set_meta("USD_PARENT_MPU", static_cast<float>(StageMetersPerUnit));
        stage_node->set_meta("USD_PARENT_UP", StageUpAxis.GetString().c_str());
        
        // To be able to compose the authored opinions from the current layer into the playload after it has been loaded
        // we will use the override layer containing this data. However, its contents need to be serialized and persisted
        // with the node to survive packaging and re-instantiation. Thus store content and identifier of the layer in the
        // metadata of the node
        std::string override_layer_content;
        override_layer->ExportToString(&override_layer_content);
        stage_node->set_meta("USD_OVERRIDE_LAYER", override_layer_content.c_str());
        stage_node->set_meta("USD_OVERRIDE_LAYERID", override_layer->GetIdentifier().c_str());
        // setting the stage uri will trigger the loading of the stage and conversion either immediately or
        // on `_ready()` of the node
        stage_node->set_stage_uri(payload_uri.c_str());
        
        // we do not the transform on the node itself as it will be set from the payload contents composed with the
        // override layer!
        
        return stage_node;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertDatasource(const pxr::IDTXDatasource& usdDatasource)
    {
        if (usdDatasource.GetPrim().IsA<pxr::IDTXMockDatasource_RandomFloat>())
        {
            pxr::IDTXMockDatasource_RandomFloat usd_mock_source(usdDatasource.GetPrim());
            
            // from the data source prim create a mock data source node that will be added to the scene tree and
            // use it's _process() method to request fresh data and author it into the prim's "outputs:data" property
            UsdMockDatasourceFloatNode3D* data_source = memnew(UsdMockDatasourceFloatNode3D);
            usd_mock_source.GetIntervalAttr().Get<float>(&data_source->refresh_interval_);
            
            return data_source;
        }
        
        return nullptr;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertPrimPostProcess(
        const pxr::UsdPrim& usd_prim,
        godot::Node3D* converted_prim,
        godot::Node3D* converted_parent_prim
    )
    {
        if (converted_prim == nullptr) return nullptr;
        
        // maintain parent-child-relationship
        if (converted_parent_prim != nullptr) converted_parent_prim->add_child(converted_prim);
        
        // set name and add META-Tags:
        converted_prim->set_name(usd_prim.GetName().GetText());
        // TODO: check if required, as all nodes converted from an usdPrim implement IUsdNode3D
        converted_prim->set_meta("USD_NODE", true);
        
        // store data in the shared IUsdNode3D class
        IUsdNode3D* usd_node = IUsdNode3D::from_node(converted_prim);
        // it is an implementation error if the node we convert into does not implement IUsdNode3D
        if (!usd_node) IDTX_LOG(IDTX_ERROR, "Unable to get the IUsdNode3D interface for this node: %s", usd_prim.GetName().GetText());
        assert(usd_node != nullptr);
        usd_node->set_prim_name(usd_prim.GetName().GetText());
        usd_node->set_prim_path(usd_prim.GetPath().GetText());
        usd_node->set_prim_type(usd_prim.GetTypeName().GetText());
        usd_node->set_stage_path(Stage->GetRootLayer()->GetRealPath().c_str());
        
        return converted_prim;
    }
    
    template<>
    inline std::vector<godot::Node3D*> UsdStageConverter<types::TargetEngineGodot>::ConvertStagePostProcess(
        const std::vector<godot::Node3D*>& converted_entities)
    {
        // once the whole stage has been converted into respective godot nodes we will apply  any required rotation and
        // scaling based on the authored up-axis and meters-per-units in the stage
        // This seems to be a much more performant way of aligning the whole stage to Godot's coordinate system and
        // orientation compared to re-calculating each individual mesh and it's transforms        
        for (godot::Node3D* node : converted_entities)
        {
            // rotate and scale the converted nodes based on up-axis and MPU settings
            
            if (StageUpAxis == pxr::UsdGeomTokens->z)
            {
                node->rotate_x(static_cast<float>(godot::Math::deg_to_rad(-90.0)));
            } else if (StageUpAxis == pxr::UsdGeomTokens->x)
            {
                node->rotate_z(static_cast<float>(godot::Math::deg_to_rad(90.0)));
            }
            // if this node represents a nested stage it contains metadata about the parents UP axis. In this case
            // we will need to apply the "reverse" rotation of the parent to keep the correct orientation
            if (OwningEntity && OwningEntity->has_meta("USD_PARENT_UP"))
            {
                godot::String parent_up = OwningEntity->get_meta("USD_PARENT_UP");
                std::string parent_up_str = std::string(parent_up.utf8().get_data());
                if (parent_up_str == pxr::UsdGeomTokens->z.GetString())
                {
                    node->rotate_x(static_cast<float>(godot::Math::deg_to_rad(+90.0)));
                } else if (parent_up_str == pxr::UsdGeomTokens->x.GetString())
                {
                    node->rotate_z(static_cast<float>(godot::Math::deg_to_rad(-90.0)));
                }
            }
            
            float mpu = static_cast<float>(StageMetersPerUnit);
            // We will only scale the converted nodes based on MPU if they are not part of a nested stage (via payload).
            // This matches the expectations, that different MPU's in referenced stages/layers need to be adjusted with
            // scaling of the prim, that refers a layer with different MPU setting.
            if (!OwningEntity || !OwningEntity->has_meta("USD_PARENT_MPU"))
            {
                godot::Vector3 scale = node->get_scale() * mpu;
                node->set_scale(scale);
            }
        }
        
        return converted_entities;
    }
}
    

}
