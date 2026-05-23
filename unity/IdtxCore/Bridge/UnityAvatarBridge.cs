// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// UnityAvatarBridge — walks a UnityEngine.GameObject hierarchy and
// populates an idtx_avatar_t via the C ABI. Symmetric to the Godot
// adapter's GodotAvatarBuilder. The reverse direction (idtx_avatar
// -> GameObject) lives in this same class as AvatarToGameObject.

using System.Collections.Generic;
using IdtxCore.Native;
using UnityEngine;

namespace IdtxCore.Bridge
{
    public static class UnityAvatarBridge
    {
        /// <summary>
        /// Walk `root`'s descendants and build an IdtxAvatar populated with
        /// skeleton (from any SkinnedMeshRenderer's bone array) and meshes
        /// (from every MeshFilter+MeshRenderer and SkinnedMeshRenderer).
        ///
        /// Caller owns the returned IdtxAvatar and must Dispose it.
        /// </summary>
        public static IdtxAvatar AvatarFromGameObject(GameObject root)
        {
            if (root == null) return null;

            var avatar = new IdtxAvatar { Name = root.name };
            avatar.SetRootTransform(MatrixToFloat16(root.transform.localToWorldMatrix));

            // Skeleton: use the first SkinnedMeshRenderer's bones as the
            // canonical bone list. Multi-skeleton avatars are reduced to
            // one for the MVP — same constraint as the Godot adapter.
            var firstSmr = root.GetComponentInChildren<SkinnedMeshRenderer>();
            Transform[] skeletonBones = null;
            int[] boneIndexInSkeleton = null;
            if (firstSmr != null && firstSmr.bones != null && firstSmr.bones.Length > 0)
            {
                skeletonBones = firstSmr.bones;
                boneIndexInSkeleton = new int[skeletonBones.Length];
                var skel = BuildSkeleton(skeletonBones, firstSmr.rootBone, boneIndexInSkeleton);
                NativeMethods.idtx_avatar_set_skeleton(avatar.Handle, skel);
            }

            // Materials are deduplicated by reference identity so two
            // renderers sharing a Material end up referencing the same
            // idtx_material slot on the avatar.
            var materialCache = new Dictionary<Material, int>();

            // Static meshes (MeshFilter + MeshRenderer)
            foreach (var mf in root.GetComponentsInChildren<MeshFilter>())
            {
                var mesh = mf.sharedMesh;
                if (mesh == null) continue;
                var mr = mf.GetComponent<MeshRenderer>();
                AddMeshSurfaces(avatar.Handle, mesh, mf.gameObject.name, mr?.sharedMaterials, materialCache, null, null);
            }

            // Skinned meshes (SkinnedMeshRenderer)
            foreach (var smr in root.GetComponentsInChildren<SkinnedMeshRenderer>())
            {
                var mesh = smr.sharedMesh;
                if (mesh == null) continue;
                AddMeshSurfaces(avatar.Handle, mesh, smr.gameObject.name, smr.sharedMaterials, materialCache,
                                smr.bones, boneIndexInSkeleton);
            }

            return avatar;
        }

        // ---------------------------------------------------------------
        // Helpers
        // ---------------------------------------------------------------

        // Pack a UnityEngine.Matrix4x4 (column-major) into a row-major
        // float[16] — the layout idtx_core uses. Unity stores m[col,row]
        // so transposing here lines up with idtx's row-major convention.
        private static float[] MatrixToFloat16(Matrix4x4 m)
        {
            var o = new float[16];
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 4; ++col)
                    o[row * 4 + col] = m[row, col];
            return o;
        }

        private static SkeletonHandle BuildSkeleton(
            Transform[] bones,
            Transform rootBone,
            int[] outBoneIndexInSkeleton)
        {
            var skel = NativeMethods.idtx_skeleton_create();
            NativeMethods.idtx_skeleton_set_name(skel, rootBone != null ? rootBone.name : "Skeleton");

            // Build a Transform -> array-index map for parent lookup.
            var transformToIdx = new Dictionary<Transform, int>(bones.Length);
            for (int i = 0; i < bones.Length; ++i)
            {
                if (bones[i] != null && !transformToIdx.ContainsKey(bones[i]))
                    transformToIdx[bones[i]] = i;
            }

            for (int i = 0; i < bones.Length; ++i)
            {
                var t = bones[i];
                if (t == null)
                {
                    NativeMethods.idtx_skeleton_add_bone(skel, $"_missing_{i}", -1,
                        IdentityMatrix16(), IdentityMatrix16());
                    outBoneIndexInSkeleton[i] = i;
                    continue;
                }

                // Parent index = index of t.parent in the bones array,
                // or -1 if the parent is the skeleton root / outside.
                int parent = -1;
                if (t.parent != null && transformToIdx.TryGetValue(t.parent, out int p))
                    parent = p;

                // rest = local transform; bind = world transform (the
                // bone's pose when it was bound, which Unity's
                // SkinnedMeshRenderer captures via bindposes — but
                // bindposes[i] is inverse(boneToWorld), so the bind
                // matrix proper is boneToWorld = t.localToWorldMatrix.
                int added = NativeMethods.idtx_skeleton_add_bone(
                    skel,
                    t.name,
                    parent,
                    MatrixToFloat16(Matrix4x4.TRS(t.localPosition, t.localRotation, t.localScale)),
                    MatrixToFloat16(t.localToWorldMatrix));
                outBoneIndexInSkeleton[i] = added;
            }
            return skel;
        }

        private static int AddOrLookupMaterial(
            AvatarHandle avatar,
            Dictionary<Material, int> cache,
            Material mat)
        {
            if (mat == null) return -1;
            if (cache.TryGetValue(mat, out int existing)) return existing;
            var mh = NativeMethods.idtx_material_create();
            NativeMethods.idtx_material_set_name(mh, mat.name);
            // Best-effort: read color/glossiness from Standard/URP-Lit
            // shader uniforms. Unrecognised shaders fall through to the
            // default PBR baseline set by idtx_material_create.
            if (mat.HasProperty("_Color"))
            {
                var c = mat.GetColor("_Color");
                NativeMethods.idtx_material_set_base_color(mh, c.r, c.g, c.b, c.a);
            }
            else if (mat.HasProperty("_BaseColor"))
            {
                var c = mat.GetColor("_BaseColor");
                NativeMethods.idtx_material_set_base_color(mh, c.r, c.g, c.b, c.a);
            }
            if (mat.HasProperty("_Metallic"))
                NativeMethods.idtx_material_set_metallic(mh, mat.GetFloat("_Metallic"));
            if (mat.HasProperty("_Glossiness"))
                NativeMethods.idtx_material_set_roughness(mh, 1.0f - mat.GetFloat("_Glossiness"));
            else if (mat.HasProperty("_Smoothness"))
                NativeMethods.idtx_material_set_roughness(mh, 1.0f - mat.GetFloat("_Smoothness"));

            int idx = NativeMethods.idtx_avatar_add_material(avatar, mh);
            cache[mat] = idx;
            return idx;
        }

        private static void AddMeshSurfaces(
            AvatarHandle avatar,
            Mesh mesh,
            string baseName,
            Material[] perSubmeshMaterials,
            Dictionary<Material, int> materialCache,
            Transform[] smrBones,
            int[] boneIndexInSkeleton)
        {
            int subCount = mesh.subMeshCount;
            var positions = MeshToFloat3Array(mesh.vertices);
            var normals = mesh.normals != null && mesh.normals.Length == mesh.vertexCount
                          ? MeshToFloat3Array(mesh.normals)
                          : null;
            var uvs = mesh.uv != null && mesh.uv.Length == mesh.vertexCount
                      ? MeshToFloat2Array(mesh.uv)
                      : null;
            var colors = mesh.colors != null && mesh.colors.Length == mesh.vertexCount
                         ? MeshColorsToFloat4Array(mesh.colors)
                         : null;

            // Skinning, if SkinnedMeshRenderer-sourced.
            int bpv = 0;
            int[] boneIndices = null;
            float[] weights = null;
            if (smrBones != null && mesh.boneWeights != null && mesh.boneWeights.Length == mesh.vertexCount)
            {
                bpv = 4;  // Unity BoneWeight has 4 fixed slots
                boneIndices = new int[mesh.vertexCount * 4];
                weights = new float[mesh.vertexCount * 4];
                var bws = mesh.boneWeights;
                for (int i = 0; i < bws.Length; ++i)
                {
                    boneIndices[i * 4 + 0] = MapBone(bws[i].boneIndex0, boneIndexInSkeleton);
                    boneIndices[i * 4 + 1] = MapBone(bws[i].boneIndex1, boneIndexInSkeleton);
                    boneIndices[i * 4 + 2] = MapBone(bws[i].boneIndex2, boneIndexInSkeleton);
                    boneIndices[i * 4 + 3] = MapBone(bws[i].boneIndex3, boneIndexInSkeleton);
                    weights[i * 4 + 0] = bws[i].weight0;
                    weights[i * 4 + 1] = bws[i].weight1;
                    weights[i * 4 + 2] = bws[i].weight2;
                    weights[i * 4 + 3] = bws[i].weight3;
                }
            }

            for (int s = 0; s < subCount; ++s)
            {
                var indices = mesh.GetIndices(s);
                if (indices == null || indices.Length == 0) continue;

                var mh = NativeMethods.idtx_mesh_create();
                NativeMethods.idtx_mesh_set_name(mh, $"{baseName}_{s}");
                NativeMethods.idtx_mesh_set_vertices(
                    mh, mesh.vertexCount, positions, normals, uvs, colors);
                NativeMethods.idtx_mesh_set_indices(mh, indices.Length, indices);
                if (bpv > 0)
                    NativeMethods.idtx_mesh_set_skinning(mh, bpv, boneIndices, weights);

                int matIdx = -1;
                if (perSubmeshMaterials != null && s < perSubmeshMaterials.Length)
                    matIdx = AddOrLookupMaterial(avatar, materialCache, perSubmeshMaterials[s]);

                NativeMethods.idtx_avatar_add_mesh(avatar, mh, matIdx);
            }
        }

        private static int MapBone(int boneIndex, int[] indexInSkeleton)
        {
            if (indexInSkeleton == null) return boneIndex;
            if (boneIndex < 0 || boneIndex >= indexInSkeleton.Length) return -1;
            return indexInSkeleton[boneIndex];
        }

        private static float[] MeshToFloat3Array(Vector3[] vs)
        {
            var o = new float[vs.Length * 3];
            for (int i = 0; i < vs.Length; ++i)
            {
                o[i * 3 + 0] = vs[i].x;
                o[i * 3 + 1] = vs[i].y;
                o[i * 3 + 2] = vs[i].z;
            }
            return o;
        }

        private static float[] MeshToFloat2Array(Vector2[] vs)
        {
            var o = new float[vs.Length * 2];
            for (int i = 0; i < vs.Length; ++i)
            {
                o[i * 2 + 0] = vs[i].x;
                o[i * 2 + 1] = vs[i].y;
            }
            return o;
        }

        private static float[] MeshColorsToFloat4Array(Color[] cs)
        {
            var o = new float[cs.Length * 4];
            for (int i = 0; i < cs.Length; ++i)
            {
                o[i * 4 + 0] = cs[i].r;
                o[i * 4 + 1] = cs[i].g;
                o[i * 4 + 2] = cs[i].b;
                o[i * 4 + 3] = cs[i].a;
            }
            return o;
        }

        private static float[] IdentityMatrix16()
        {
            return new float[16]
            {
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1,
            };
        }
    }
}
