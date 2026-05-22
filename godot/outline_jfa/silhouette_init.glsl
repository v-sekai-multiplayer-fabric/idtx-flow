// Copyright 2026 The openusd-fabric authors.
// SPDX-License-Identifier: MIT
//
// Godot 4.7 GLSL port of shaders/outline_jfa/silhouette_init.slang.
// The Slang file is the spec; this file is the runtime artifact the
// CompositorEffect dispatches. When the slangc -> SPIR-V build step
// lands (CHI-255 follow-up), this file is regenerated, not edited.

#[compute]
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rg16ui) restrict readonly  uniform uimage2D id_aov;
layout(set = 0, binding = 1, rgba32i) restrict writeonly uniform iimage2D seed_rw;

void main() {
    ivec2 dim = imageSize(id_aov);
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= dim.x || coord.y >= dim.y) {
        return;
    }
    uvec2 sample = imageLoad(id_aov, coord).rg;
    ivec4 seed;
    if (sample.x != 0u) {
        seed = ivec4(coord.x, coord.y, int(sample.x), int(sample.y));
    } else {
        seed = ivec4(-1, -1, 0, 0);
    }
    imageStore(seed_rw, coord, seed);
}
