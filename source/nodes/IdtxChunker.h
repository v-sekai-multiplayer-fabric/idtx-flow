// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// IdtxChunker — GDScript-facing wrapper around libidtx_core's casync
// chunker + aria-storage transport. Provides parity with the standalone
// CLI (`idtxcli bake / fetch / verify`) inside Godot, so the same
// asset → CDN → asset roundtrip can be exercised either from the
// editor / a headless tool script OR from a shell.
//
// The class is intentionally thin — bodies marshal PackedByteArray ↔
// const uint8_t*/size_t and call the C ABI. The byte-level algorithm
// (CDC, SHA-512/256, caibx parse/emit, HTTP transport) lives once in
// libidtx_core; this class CANNOT diverge from the CLI behaviour.

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

class IdtxChunker : public RefCounted {
    GDCLASS(IdtxChunker, RefCounted);

public:
    IdtxChunker();
    ~IdtxChunker() override;

    // Configure the aria-storage endpoint. Required before any
    // bake/fetch/verify call. Returns true on success.
    bool open(const String& aria_base_url);

    // Optional auth bearer (Authorization: Bearer <token>).
    void set_auth(const String& bearer_token);

    // Insecure TLS toggle — localhost dev only. Returns the previous setting.
    bool set_insecure_tls(bool insecure);

    // CDN-side workflows. All return the caibx URL (bake) / assembled
    // bytes (fetch) / boolean (verify). Empty / null return signals
    // failure; check last_status() and last_error() for diagnostics.

    String          bake(const String& index_name, const PackedByteArray& blob);
    PackedByteArray fetch(const String& caibx_url);
    bool            verify(const String& caibx_url);

    // Pure addressing helpers — useful for tests / verifying parity
    // against the CLI without hitting the network.

    PackedByteArray sha512_256(const PackedByteArray& data);
    String          build_chunk_url(const String& store_url,
                                    const PackedByteArray& chunk_id);

    // Diagnostics.
    int    last_status() const;
    String last_error()  const;

protected:
    static void _bind_methods();

private:
    struct idtx_transport* transport_ = nullptr;
};

} // namespace godot
