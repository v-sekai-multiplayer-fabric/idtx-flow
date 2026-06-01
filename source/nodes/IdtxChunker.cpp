// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// IdtxChunker — see header. Thin marshaling layer; algorithm lives in
// libidtx_core.

#include "IdtxChunker.h"

#include <godot_cpp/core/class_db.hpp>

#include "idtx_core/idtx_chunker.h"
#include "idtx_core/idtx_transport.h"

#include <cstring>

using namespace godot;

IdtxChunker::IdtxChunker() = default;

IdtxChunker::~IdtxChunker()
{
    if (transport_ != nullptr) {
        idtx_transport_destroy(transport_);
        transport_ = nullptr;
    }
}

bool IdtxChunker::open(const String& aria_base_url)
{
    if (transport_ != nullptr) {
        idtx_transport_destroy(transport_);
        transport_ = nullptr;
    }
    const CharString u = aria_base_url.utf8();
    transport_ = idtx_transport_new(u.get_data());
    return transport_ != nullptr;
}

void IdtxChunker::set_auth(const String& bearer_token)
{
    if (transport_ == nullptr) return;
    const CharString t = bearer_token.utf8();
    idtx_transport_set_auth(transport_, t.get_data());
}

bool IdtxChunker::set_insecure_tls(bool insecure)
{
    if (transport_ == nullptr) return false;
    return idtx_transport_set_insecure_tls(transport_, insecure ? 1 : 0) != 0;
}

String IdtxChunker::bake(const String& index_name, const PackedByteArray& blob)
{
    if (transport_ == nullptr) return String();
    const CharString name = index_name.utf8();
    char url[1024] = {0};
    int32_t rc = idtx_chunker_upload_asset(
        transport_,
        name.get_data(),
        blob.ptr(), size_t(blob.size()),
        url, sizeof(url));
    if (rc != 0) return String();
    return String(url);
}

PackedByteArray IdtxChunker::fetch(const String& caibx_url)
{
    if (transport_ == nullptr) return PackedByteArray();

    const CharString u = caibx_url.utf8();
    idtx_buffer_t* caibx_buf = nullptr;
    int32_t rc = idtx_transport_get_caibx(transport_, u.get_data(), &caibx_buf);
    if (rc != 0) return PackedByteArray();

    idtx_caibx_t* idx = nullptr;
    rc = idtx_chunker_parse_caibx(idtx_buffer_data(caibx_buf),
                                  idtx_buffer_size(caibx_buf), &idx);
    idtx_buffer_destroy(caibx_buf);
    if (rc != 0) return PackedByteArray();

    idtx_buffer_t* blob = nullptr;
    rc = idtx_chunker_assemble(transport_, idx, &blob);
    idtx_caibx_destroy(idx);
    if (rc != 0) return PackedByteArray();

    PackedByteArray out;
    out.resize(int64_t(idtx_buffer_size(blob)));
    if (!out.is_empty()) {
        std::memcpy(out.ptrw(), idtx_buffer_data(blob), idtx_buffer_size(blob));
    }
    idtx_buffer_destroy(blob);
    return out;
}

bool IdtxChunker::verify(const String& caibx_url)
{
    if (transport_ == nullptr) return false;

    const CharString u = caibx_url.utf8();
    idtx_buffer_t* caibx_buf = nullptr;
    int32_t rc = idtx_transport_get_caibx(transport_, u.get_data(), &caibx_buf);
    if (rc != 0) return false;

    idtx_caibx_t* idx = nullptr;
    rc = idtx_chunker_parse_caibx(idtx_buffer_data(caibx_buf),
                                  idtx_buffer_size(caibx_buf), &idx);
    idtx_buffer_destroy(caibx_buf);
    if (rc != 0) return false;

    bool ok = true;
    const int32_t n = idtx_caibx_chunk_count(idx);
    for (int32_t i = 0; i < n; ++i) {
        uint8_t id[IDTX_CHUNKER_CHUNK_ID_BYTES];
        idtx_caibx_get_chunk(idx, i, nullptr, nullptr, id);
        if (idtx_transport_head_chunk(transport_, id) != 0) { ok = false; break; }
    }
    idtx_caibx_destroy(idx);
    return ok;
}

PackedByteArray IdtxChunker::sha512_256(const PackedByteArray& data)
{
    PackedByteArray out;
    out.resize(IDTX_CHUNKER_CHUNK_ID_BYTES);
    idtx_chunker_sha512_256(data.ptr(), size_t(data.size()), out.ptrw());
    return out;
}

String IdtxChunker::build_chunk_url(const String& store_url,
                                    const PackedByteArray& chunk_id)
{
    if (chunk_id.size() != IDTX_CHUNKER_CHUNK_ID_BYTES) return String();
    const CharString s = store_url.utf8();
    char url[512] = {0};
    idtx_chunker_build_chunk_url(s.get_data(), chunk_id.ptr(), url, sizeof(url));
    return String(url);
}

int IdtxChunker::last_status() const
{
    return transport_ != nullptr ? idtx_transport_last_status(transport_) : 0;
}

String IdtxChunker::last_error() const
{
    return transport_ != nullptr ? String(idtx_transport_last_error(transport_)) : String();
}

void IdtxChunker::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("open",   "aria_base_url"),  &IdtxChunker::open);
    ClassDB::bind_method(D_METHOD("set_auth", "bearer_token"), &IdtxChunker::set_auth);
    ClassDB::bind_method(D_METHOD("set_insecure_tls", "insecure"), &IdtxChunker::set_insecure_tls);
    ClassDB::bind_method(D_METHOD("bake",   "index_name", "blob"), &IdtxChunker::bake);
    ClassDB::bind_method(D_METHOD("fetch",  "caibx_url"),     &IdtxChunker::fetch);
    ClassDB::bind_method(D_METHOD("verify", "caibx_url"),     &IdtxChunker::verify);
    ClassDB::bind_method(D_METHOD("sha512_256", "data"),      &IdtxChunker::sha512_256);
    ClassDB::bind_method(D_METHOD("build_chunk_url", "store_url", "chunk_id"),
                                  &IdtxChunker::build_chunk_url);
    ClassDB::bind_method(D_METHOD("last_status"),             &IdtxChunker::last_status);
    ClassDB::bind_method(D_METHOD("last_error"),              &IdtxChunker::last_error);
}
