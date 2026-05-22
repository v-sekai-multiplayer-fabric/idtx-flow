// Copyright 2026 The openusd-fabric authors.
// SPDX-License-Identifier: MIT
//
// Godot 4.7 GLSL port of shaders/outline_jfa/final.slang.

#[compute]
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

struct PaletteEntry {
    vec4  colour;      // RGB + outlineLightingMix
    float width_px;
    float _pad0;
    float _pad1;
    float _pad2;
};

layout(push_constant, std430) uniform Params {
    uint  palette_count;
    uint  depth_occluded;
    float depth_bias_metres;
    float _pad0;
} params;

layout(set = 0, binding = 0, std430) restrict readonly buffer Palette {
    PaletteEntry entries[];
} palette;

layout(set = 0, binding = 1, rgba32i) restrict readonly  uniform iimage2D seed_in;
layout(set = 0, binding = 2, r32f)    restrict readonly  uniform image2D  scene_depth;
layout(set = 0, binding = 3, rgba16f) restrict           uniform image2D  scene_colour_rw;

void main() {
    ivec2 dim = imageSize(seed_in);
    ivec2 here = ivec2(gl_GlobalInvocationID.xy);
    if (here.x >= dim.x || here.y >= dim.y) {
        return;
    }
    ivec4 seed = imageLoad(seed_in, here);
    if (seed.x < 0) {
        return;
    }
    vec2 d = vec2(here - seed.xy);
    float dist = length(d);
    if (dist <= 0.0) {
        return;
    }
    uint mat_id = uint(seed.z);
    if (mat_id == 0u || mat_id >= params.palette_count) {
        return;
    }
    PaletteEntry entry = palette.entries[mat_id];

    float width_px = entry.width_px > 0.0 ? entry.width_px : float(seed.w);
    if (width_px <= 0.0 || dist > width_px) {
        return;
    }

    if (params.depth_occluded != 0u) {
        float cur_d = imageLoad(scene_depth, here).r;
        float silh_d = imageLoad(scene_depth, seed.xy).r;
        if (silh_d > cur_d + params.depth_bias_metres) {
            return;
        }
    }

    float aa = clamp(width_px - dist, 0.0, 1.0);
    vec4 scene = imageLoad(scene_colour_rw, here);
    vec4 outl  = entry.colour;
    imageStore(scene_colour_rw, here, mix(scene, outl, aa));
}
