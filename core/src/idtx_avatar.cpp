// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

// IDTX_CORE_BUILDING_DLL is set by scons/idtxcore.py CPPDEFINES.
#include "idtx_core/idtx_core.h"

#include <cstring>
#include <string>
#include <vector>

struct idtx_avatar
{
    std::string name;
    float root_transform[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    idtx_skeleton_t* skeleton = nullptr;
    std::vector<idtx_mesh_t*>     meshes;
    std::vector<int32_t>          mesh_material;   // parallel to meshes
    std::vector<idtx_material_t*> materials;
};

extern "C" IDTX_CORE_API idtx_avatar_t* idtx_avatar_create(void)
{
    return new idtx_avatar();
}

extern "C" IDTX_CORE_API void idtx_avatar_destroy(idtx_avatar_t* avatar)
{
    if (avatar == nullptr) return;
    if (avatar->skeleton != nullptr) idtx_skeleton_destroy(avatar->skeleton);
    for (auto* m : avatar->meshes)    if (m != nullptr) idtx_mesh_destroy(m);
    for (auto* mat : avatar->materials) if (mat != nullptr) idtx_material_destroy(mat);
    delete avatar;
}

extern "C" IDTX_CORE_API void idtx_avatar_set_name(idtx_avatar_t* avatar, const char* name)
{
    if (avatar == nullptr) return;
    avatar->name = (name != nullptr) ? name : "";
}

extern "C" IDTX_CORE_API const char* idtx_avatar_get_name(const idtx_avatar_t* avatar)
{
    return (avatar != nullptr) ? avatar->name.c_str() : "";
}

extern "C" IDTX_CORE_API void idtx_avatar_set_root_transform(idtx_avatar_t* avatar, const float matrix[16])
{
    if (avatar == nullptr || matrix == nullptr) return;
    std::memcpy(avatar->root_transform, matrix, sizeof(float) * 16);
}

extern "C" IDTX_CORE_API void idtx_avatar_get_root_transform(const idtx_avatar_t* avatar, float out_matrix[16])
{
    if (out_matrix == nullptr) return;
    if (avatar == nullptr) {
        std::memset(out_matrix, 0, sizeof(float) * 16);
        out_matrix[0] = out_matrix[5] = out_matrix[10] = out_matrix[15] = 1.0f;
        return;
    }
    std::memcpy(out_matrix, avatar->root_transform, sizeof(float) * 16);
}

extern "C" IDTX_CORE_API void idtx_avatar_set_skeleton(idtx_avatar_t* avatar, idtx_skeleton_t* skel)
{
    if (avatar == nullptr) return;
    if (avatar->skeleton != nullptr && avatar->skeleton != skel) {
        idtx_skeleton_destroy(avatar->skeleton);
    }
    avatar->skeleton = skel;
}

extern "C" IDTX_CORE_API idtx_skeleton_t* idtx_avatar_get_skeleton(const idtx_avatar_t* avatar)
{
    return (avatar != nullptr) ? avatar->skeleton : nullptr;
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_add_mesh(idtx_avatar_t* avatar, idtx_mesh_t* mesh, int32_t material_index)
{
    if (avatar == nullptr) return -1;
    int32_t idx = static_cast<int32_t>(avatar->meshes.size());
    avatar->meshes.push_back(mesh);
    avatar->mesh_material.push_back(material_index);
    return idx;
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_get_mesh_count(const idtx_avatar_t* avatar)
{
    return (avatar != nullptr) ? static_cast<int32_t>(avatar->meshes.size()) : 0;
}

extern "C" IDTX_CORE_API idtx_mesh_t* idtx_avatar_get_mesh(const idtx_avatar_t* avatar, int32_t index)
{
    if (avatar == nullptr || index < 0 || index >= static_cast<int32_t>(avatar->meshes.size())) return nullptr;
    return avatar->meshes[index];
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_get_mesh_material(const idtx_avatar_t* avatar, int32_t mesh_index)
{
    if (avatar == nullptr || mesh_index < 0 || mesh_index >= static_cast<int32_t>(avatar->mesh_material.size())) return -1;
    return avatar->mesh_material[mesh_index];
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_add_material(idtx_avatar_t* avatar, idtx_material_t* mat)
{
    if (avatar == nullptr) return -1;
    int32_t idx = static_cast<int32_t>(avatar->materials.size());
    avatar->materials.push_back(mat);
    return idx;
}

extern "C" IDTX_CORE_API int32_t idtx_avatar_get_material_count(const idtx_avatar_t* avatar)
{
    return (avatar != nullptr) ? static_cast<int32_t>(avatar->materials.size()) : 0;
}

extern "C" IDTX_CORE_API idtx_material_t* idtx_avatar_get_material(const idtx_avatar_t* avatar, int32_t index)
{
    if (avatar == nullptr || index < 0 || index >= static_cast<int32_t>(avatar->materials.size())) return nullptr;
    return avatar->materials[index];
}
