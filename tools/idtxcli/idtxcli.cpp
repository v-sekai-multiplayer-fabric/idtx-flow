// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtxcli — minimal command-line driver for libidtx_core. Useful for
// smoke-testing the avatar pipeline without spinning up Godot / Unity.
//
// Subcommands:
//   idtxcli usd-to-vrm <in.usda>  <out.vrm>
//   idtxcli vrm-to-usd <in.vrm>   <out.usda>
//   idtxcli usd-to-usd <in.usda>  <out.usda>     (round-trip)
//   idtxcli vrm-to-vrm <in.vrm>   <out.vrm>      (round-trip)
//   idtxcli reconstruct-quads <in.usda> <out.usda> [planarity_deg]
//   idtxcli version
//
// Returns 0 on success, non-zero on failure.

#include "idtx_core/idtx_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int usage(const char* arg0)
{
    std::fprintf(stderr,
        "idtxcli — libidtx_core driver\n"
        "Usage:\n"
        "  %s usd-to-vrm <in.usda> <out.vrm>\n"
        "  %s vrm-to-usd <in.vrm>  <out.usda>\n"
        "  %s usd-to-usd <in.usda> <out.usda>   (round-trip)\n"
        "  %s vrm-to-vrm <in.vrm>  <out.vrm>    (round-trip)\n"
        "  %s reconstruct-quads <in.usda> <out.usda> [planarity_deg]\n"
        "  %s version\n",
        arg0, arg0, arg0, arg0, arg0, arg0);
    return 2;
}

static int run_convert(
    idtx_avatar_t* (*import_fn)(const char*),
    int32_t (*export_fn)(const idtx_avatar_t*, const char*),
    const char* in_path,
    const char* out_path,
    const char* op_name)
{
    idtx_avatar_t* a = import_fn(in_path);
    if (a == nullptr) {
        std::fprintf(stderr, "%s: failed to import %s\n", op_name, in_path);
        return 3;
    }
    int32_t rc = export_fn(a, out_path);
    idtx_avatar_destroy(a);
    if (rc != 0) {
        std::fprintf(stderr, "%s: export to %s failed (rc=%d)\n", op_name, out_path, rc);
        return 4;
    }
    std::fprintf(stdout, "%s: %s -> %s ok\n", op_name, in_path, out_path);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) return usage(argv[0]);

    std::string cmd = argv[1];
    if (cmd == "version") {
        std::fprintf(stdout, "idtxcli libidtx_core %s\n", idtx_core_version());
        return 0;
    }
    if (argc < 4) return usage(argv[0]);

    if (cmd == "usd-to-vrm") {
        return run_convert(
            &idtx_core_import_avatar_from_usd,
            &idtx_core_export_avatar_to_vrm,
            argv[2], argv[3], "usd-to-vrm");
    }
    if (cmd == "vrm-to-usd") {
        return run_convert(
            &idtx_core_import_avatar_from_vrm,
            &idtx_core_export_avatar_to_usd,
            argv[2], argv[3], "vrm-to-usd");
    }
    if (cmd == "usd-to-usd") {
        return run_convert(
            &idtx_core_import_avatar_from_usd,
            &idtx_core_export_avatar_to_usd,
            argv[2], argv[3], "usd-to-usd");
    }
    if (cmd == "vrm-to-vrm") {
        return run_convert(
            &idtx_core_import_avatar_from_vrm,
            &idtx_core_export_avatar_to_vrm,
            argv[2], argv[3], "vrm-to-vrm");
    }
    if (cmd == "reconstruct-quads") {
        // USD -> idtx_avatar -> run tris-to-quads per mesh -> USD.
        float planarity = (argc >= 5) ? std::atof(argv[4]) : 5.0f;
        idtx_avatar_t* a = idtx_core_import_avatar_from_usd(argv[2]);
        if (a == nullptr) {
            std::fprintf(stderr, "reconstruct-quads: failed to import %s\n", argv[2]);
            return 3;
        }
        int32_t total_quads = 0;
        int32_t mesh_count  = idtx_avatar_get_mesh_count(a);
        for (int32_t i = 0; i < mesh_count; ++i) {
            idtx_mesh_t* m = idtx_avatar_get_mesh(a, i);
            int32_t n = idtx_mesh_reconstruct_quads(m, planarity);
            if (n > 0) total_quads += n;
        }
        int32_t rc = idtx_core_export_avatar_to_usd(a, argv[3]);
        idtx_avatar_destroy(a);
        if (rc != 0) {
            std::fprintf(stderr,
                "reconstruct-quads: export to %s failed (rc=%d)\n",
                argv[3], rc);
            return 4;
        }
        std::fprintf(stdout,
            "reconstruct-quads: %s -> %s ok (%d quad(s) formed across %d mesh(es))\n",
            argv[2], argv[3], total_quads, mesh_count);
        return 0;
    }
    return usage(argv[0]);
}
