// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// idtx_chunker — see idtx_chunker.h for the public C ABI contract.
//
// This file implements the addressing primitives (SHA-512/256 +
// chunk-URL builder) which are small, portable, and unblock the
// parity smoke test for the addressing layer.
//
// CDC, .caibx parse/emit, and .cacnk compress/verify are stubbed out
// returning code 99 (not-yet-implemented). They will land in a
// follow-up commit porting from multiplayer-fabric-godot/modules/
// multiplayer_fabric_asset/fabric_mmog_asset.cpp.

#include "idtx_core/idtx_chunker.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------
// SHA-512/256 (FIPS 180-4, Section 5.3.6.2).
//
// SHA-512/256 is NOT a truncation of SHA-512 — it uses the SHA-512
// round function but with different initial hash values (IVs derived
// per FIPS 180-4 §5.3.6 from "SHA-512/t IV Generation Function"). The
// final digest is the first 256 bits of the SHA-512 state.
//
// Matches folbricht/desync's chunk-id algorithm exactly, which is
// what casync/desync and the existing FabricMMOGAsset class use.
// ---------------------------------------------------------------------

namespace {

constexpr uint64_t SHA512_256_IV[8] = {
    0x22312194FC2BF72CULL, 0x9F555FA3C84C64C2ULL,
    0x2393B86B6F53B151ULL, 0x963877195940EABDULL,
    0x96283EE2A88EFFE3ULL, 0xBE5E1E2553863992ULL,
    0x2B0199FC2C85B8AAULL, 0x0EB72DDC81C52CA2ULL,
};

constexpr uint64_t SHA512_K[80] = {
    0x428A2F98D728AE22ULL, 0x7137449123EF65CDULL, 0xB5C0FBCFEC4D3B2FULL, 0xE9B5DBA58189DBBCULL,
    0x3956C25BF348B538ULL, 0x59F111F1B605D019ULL, 0x923F82A4AF194F9BULL, 0xAB1C5ED5DA6D8118ULL,
    0xD807AA98A3030242ULL, 0x12835B0145706FBEULL, 0x243185BE4EE4B28CULL, 0x550C7DC3D5FFB4E2ULL,
    0x72BE5D74F27B896FULL, 0x80DEB1FE3B1696B1ULL, 0x9BDC06A725C71235ULL, 0xC19BF174CF692694ULL,
    0xE49B69C19EF14AD2ULL, 0xEFBE4786384F25E3ULL, 0x0FC19DC68B8CD5B5ULL, 0x240CA1CC77AC9C65ULL,
    0x2DE92C6F592B0275ULL, 0x4A7484AA6EA6E483ULL, 0x5CB0A9DCBD41FBD4ULL, 0x76F988DA831153B5ULL,
    0x983E5152EE66DFABULL, 0xA831C66D2DB43210ULL, 0xB00327C898FB213FULL, 0xBF597FC7BEEF0EE4ULL,
    0xC6E00BF33DA88FC2ULL, 0xD5A79147930AA725ULL, 0x06CA6351E003826FULL, 0x142929670A0E6E70ULL,
    0x27B70A8546D22FFCULL, 0x2E1B21385C26C926ULL, 0x4D2C6DFC5AC42AEDULL, 0x53380D139D95B3DFULL,
    0x650A73548BAF63DEULL, 0x766A0ABB3C77B2A8ULL, 0x81C2C92E47EDAEE6ULL, 0x92722C851482353BULL,
    0xA2BFE8A14CF10364ULL, 0xA81A664BBC423001ULL, 0xC24B8B70D0F89791ULL, 0xC76C51A30654BE30ULL,
    0xD192E819D6EF5218ULL, 0xD69906245565A910ULL, 0xF40E35855771202AULL, 0x106AA07032BBD1B8ULL,
    0x19A4C116B8D2D0C8ULL, 0x1E376C085141AB53ULL, 0x2748774CDF8EEB99ULL, 0x34B0BCB5E19B48A8ULL,
    0x391C0CB3C5C95A63ULL, 0x4ED8AA4AE3418ACBULL, 0x5B9CCA4F7763E373ULL, 0x682E6FF3D6B2B8A3ULL,
    0x748F82EE5DEFB2FCULL, 0x78A5636F43172F60ULL, 0x84C87814A1F0AB72ULL, 0x8CC702081A6439ECULL,
    0x90BEFFFA23631E28ULL, 0xA4506CEBDE82BDE9ULL, 0xBEF9A3F7B2C67915ULL, 0xC67178F2E372532BULL,
    0xCA273ECEEA26619CULL, 0xD186B8C721C0C207ULL, 0xEADA7DD6CDE0EB1EULL, 0xF57D4F7FEE6ED178ULL,
    0x06F067AA72176FBAULL, 0x0A637DC5A2C898A6ULL, 0x113F9804BEF90DAEULL, 0x1B710B35131C471BULL,
    0x28DB77F523047D84ULL, 0x32CAAB7B40C72493ULL, 0x3C9EBE0A15C9BEBCULL, 0x431D67C49C100D4CULL,
    0x4CC5D4BECB3E42B6ULL, 0x597F299CFC657E2AULL, 0x5FCB6FAB3AD6FAECULL, 0x6C44198C4A475817ULL,
};

inline uint64_t rotr64(uint64_t x, unsigned n) { return (x >> n) | (x << (64 - n)); }

void sha512_compress(uint64_t h[8], const uint8_t block[128])
{
    uint64_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint64_t(block[i*8 + 0]) << 56) | (uint64_t(block[i*8 + 1]) << 48) |
               (uint64_t(block[i*8 + 2]) << 40) | (uint64_t(block[i*8 + 3]) << 32) |
               (uint64_t(block[i*8 + 4]) << 24) | (uint64_t(block[i*8 + 5]) << 16) |
               (uint64_t(block[i*8 + 6]) <<  8) | (uint64_t(block[i*8 + 7]));
    }
    for (int i = 16; i < 80; ++i) {
        uint64_t s0 = rotr64(w[i-15], 1)  ^ rotr64(w[i-15], 8)  ^ (w[i-15] >> 7);
        uint64_t s1 = rotr64(w[i-2], 19)  ^ rotr64(w[i-2], 61)  ^ (w[i-2]  >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint64_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint64_t e = h[4], f = h[5], g = h[6], hh = h[7];

    for (int i = 0; i < 80; ++i) {
        uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t t1 = hh + S1 + ch + SHA512_K[i] + w[i];
        uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        uint64_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = S0 + mj;

        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

} // namespace

extern "C" IDTX_CORE_API void idtx_chunker_sha512_256(
    const uint8_t* data,
    size_t         len,
    uint8_t        out_id[IDTX_CHUNKER_CHUNK_ID_BYTES])
{
    if (out_id == nullptr) return;

    uint64_t h[8];
    std::memcpy(h, SHA512_256_IV, sizeof(h));

    uint8_t block[128];
    size_t  full_blocks = len / 128;
    size_t  remainder   = len % 128;

    if (data != nullptr) {
        for (size_t i = 0; i < full_blocks; ++i) {
            sha512_compress(h, data + i * 128);
        }
    }

    // Final block(s) with padding + 128-bit length.
    uint8_t tail[256];
    std::memset(tail, 0, sizeof(tail));
    if (remainder > 0 && data != nullptr) {
        std::memcpy(tail, data + full_blocks * 128, remainder);
    }
    tail[remainder] = 0x80;

    size_t pad_len;
    if (remainder < 112) {
        pad_len = 128;
    } else {
        pad_len = 256;
    }

    // Length in bits as 128-bit big-endian (we only fill the low 64).
    uint64_t bit_len = uint64_t(len) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[pad_len - 1 - i] = uint8_t(bit_len >> (i * 8));
    }

    sha512_compress(h, tail);
    if (pad_len == 256) {
        sha512_compress(h, tail + 128);
    }

    // First 256 bits of the state, big-endian.
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 8; ++j) {
            out_id[i * 8 + j] = uint8_t(h[i] >> ((7 - j) * 8));
        }
    }
}

// ---------------------------------------------------------------------
// Chunk URL builder. Matches the existing FabricMMOGAsset format:
//   {store_url}/{hex[0:4]}/{full-hex}.cacnk
// Trailing slashes on store_url are stripped.
// ---------------------------------------------------------------------

extern "C" IDTX_CORE_API void idtx_chunker_build_chunk_url(
    const char*   store_url,
    const uint8_t id[IDTX_CHUNKER_CHUNK_ID_BYTES],
    char*         out,
    size_t        cap)
{
    if (store_url == nullptr || id == nullptr || out == nullptr || cap == 0) {
        if (out != nullptr && cap > 0) out[0] = '\0';
        return;
    }

    std::string base(store_url);
    while (!base.empty() && base.back() == '/') base.pop_back();

    char hex[65];
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < IDTX_CHUNKER_CHUNK_ID_BYTES; ++i) {
        hex[i * 2 + 0] = digits[(id[i] >> 4) & 0xf];
        hex[i * 2 + 1] = digits[ id[i]       & 0xf];
    }
    hex[64] = '\0';

    int written = std::snprintf(out, cap, "%s/%c%c%c%c/%s.cacnk",
                                base.c_str(),
                                hex[0], hex[1], hex[2], hex[3],
                                hex);
    if (written < 0 || size_t(written) >= cap) {
        out[cap - 1] = '\0';
    }
}

// ---------------------------------------------------------------------
// Buffer accessors — stubbed; will be wired up alongside CDC port.
// ---------------------------------------------------------------------

struct idtx_buffer {
    uint8_t* data;
    size_t   size;
};

extern "C" IDTX_CORE_API const uint8_t* idtx_buffer_data(const idtx_buffer_t* buf)
{
    return buf != nullptr ? buf->data : nullptr;
}

extern "C" IDTX_CORE_API size_t idtx_buffer_size(const idtx_buffer_t* buf)
{
    return buf != nullptr ? buf->size : 0;
}

extern "C" IDTX_CORE_API void idtx_buffer_destroy(idtx_buffer_t* buf)
{
    if (buf == nullptr) return;
    delete[] buf->data;
    delete buf;
}

// ---------------------------------------------------------------------
// CDC + caibx + cacnk — NOT YET IMPLEMENTED. Stubs return 99.
// Follow-up commit will port from fabric_mmog_asset.cpp.
// ---------------------------------------------------------------------

extern "C" IDTX_CORE_API int32_t idtx_chunker_cdc(
    const uint8_t*, size_t, size_t, size_t, size_t,
    idtx_chunklist_t** out_chunks)
{
    if (out_chunks != nullptr) *out_chunks = nullptr;
    return 99;
}

extern "C" IDTX_CORE_API int32_t idtx_chunklist_count(const idtx_chunklist_t*) { return 0; }
extern "C" IDTX_CORE_API int32_t idtx_chunklist_get(const idtx_chunklist_t*, int32_t,
    uint64_t*, uint64_t*, uint8_t[IDTX_CHUNKER_CHUNK_ID_BYTES]) { return 99; }
extern "C" IDTX_CORE_API void    idtx_chunklist_destroy(idtx_chunklist_t*) {}

extern "C" IDTX_CORE_API int32_t idtx_chunker_parse_caibx(
    const uint8_t*, size_t, idtx_caibx_t** out)
{
    if (out != nullptr) *out = nullptr;
    return 99;
}

extern "C" IDTX_CORE_API int32_t idtx_caibx_chunk_count(const idtx_caibx_t*) { return 0; }
extern "C" IDTX_CORE_API int32_t idtx_caibx_get_chunk(const idtx_caibx_t*, int32_t,
    uint64_t*, uint64_t*, uint8_t[IDTX_CHUNKER_CHUNK_ID_BYTES]) { return 99; }
extern "C" IDTX_CORE_API void    idtx_caibx_destroy(idtx_caibx_t*) {}

extern "C" IDTX_CORE_API int32_t idtx_chunker_emit_caibx(
    const idtx_chunklist_t*, size_t, size_t, size_t, idtx_buffer_t** out)
{
    if (out != nullptr) *out = nullptr;
    return 99;
}

extern "C" IDTX_CORE_API int32_t idtx_chunker_decompress_and_verify(
    const uint8_t*, size_t,
    const uint8_t[IDTX_CHUNKER_CHUNK_ID_BYTES],
    idtx_buffer_t** out)
{
    if (out != nullptr) *out = nullptr;
    return 99;
}

extern "C" IDTX_CORE_API int32_t idtx_chunker_compress(
    const uint8_t*, size_t, idtx_buffer_t** out)
{
    if (out != nullptr) *out = nullptr;
    return 99;
}
