// Copyright 2026 The openusd-fabric authors.
// SPDX-License-Identifier: MIT
//
// Godot 4.7 GLSL port of shaders/outline_jfa/jfa_step.slang.

#[compute]
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
    int  stride;
    int  _pad0;
    int  _pad1;
    int  _pad2;
} params;

layout(set = 0, binding = 0, rgba32i) restrict readonly  uniform iimage2D seed_in;
layout(set = 0, binding = 1, rgba32i) restrict writeonly uniform iimage2D seed_out;

const ivec2 kOffsets[9] = ivec2[](
    ivec2(-1, -1), ivec2( 0, -1), ivec2( 1, -1),
    ivec2(-1,  0), ivec2( 0,  0), ivec2( 1,  0),
    ivec2(-1,  1), ivec2( 0,  1), ivec2( 1,  1)
);

int sqr_dist(ivec2 a, ivec2 b) {
    ivec2 d = a - b;
    return d.x * d.x + d.y * d.y;
}

void main() {
    ivec2 dim = imageSize(seed_in);
    ivec2 here = ivec2(gl_GlobalInvocationID.xy);
    if (here.x >= dim.x || here.y >= dim.y) {
        return;
    }

    ivec4 best = imageLoad(seed_in, here);
    int best_d2 = (best.x < 0) ? 0x7FFFFFFF : sqr_dist(here, best.xy);

    for (int i = 0; i < 9; ++i) {
        ivec2 sample_coord = here + kOffsets[i] * params.stride;
        if (sample_coord.x < 0 || sample_coord.y < 0 ||
            sample_coord.x >= dim.x || sample_coord.y >= dim.y) {
            continue;
        }
        ivec4 cand = imageLoad(seed_in, sample_coord);
        if (cand.x < 0) {
            continue;
        }
        int cand_d2 = sqr_dist(here, cand.xy);
        if (cand_d2 < best_d2) {
            best = cand;
            best_d2 = cand_d2;
        }
    }
    imageStore(seed_out, here, best);
}
