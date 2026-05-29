/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_REGISTRY_ROUTE_HASH_HPP_INCLUDED
#define ZLINK_REGISTRY_ROUTE_HASH_HPP_INCLUDED

#include <stddef.h>
#include <stdint.h>

namespace zlink
{
namespace registry_route_hash
{

inline size_t combine (size_t seed_, size_t value_)
{
    return seed_ ^ (value_ + 0x9e3779b97f4a7c15ULL + (seed_ << 6)
                    + (seed_ >> 2));
}

inline uint64_t rotl64 (uint64_t value_, int shift_)
{
    return (value_ << shift_) | (value_ >> (64 - shift_));
}

inline uint64_t read_le64 (const unsigned char *data_)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<uint64_t> (data_[i]) << (i * 8);
    return value;
}

inline void sip_round (uint64_t *v0_,
                       uint64_t *v1_,
                       uint64_t *v2_,
                       uint64_t *v3_)
{
    *v0_ += *v1_;
    *v1_ = rotl64 (*v1_, 13);
    *v1_ ^= *v0_;
    *v0_ = rotl64 (*v0_, 32);
    *v2_ += *v3_;
    *v3_ = rotl64 (*v3_, 16);
    *v3_ ^= *v2_;
    *v0_ += *v3_;
    *v3_ = rotl64 (*v3_, 21);
    *v3_ ^= *v0_;
    *v2_ += *v1_;
    *v1_ = rotl64 (*v1_, 17);
    *v1_ ^= *v2_;
    *v2_ = rotl64 (*v2_, 32);
}

inline uint64_t sip24 (const unsigned char *data_, size_t len_)
{
    const uint64_t k0 = 0x0706050403020100ULL;
    const uint64_t k1 = 0x0f0e0d0c0b0a0908ULL;
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1;

    const unsigned char *p = data_;
    size_t remaining = len_;
    while (remaining >= 8) {
        const uint64_t m = read_le64 (p);
        v3 ^= m;
        sip_round (&v0, &v1, &v2, &v3);
        sip_round (&v0, &v1, &v2, &v3);
        v0 ^= m;
        p += 8;
        remaining -= 8;
    }

    uint64_t b = static_cast<uint64_t> (len_) << 56;
    for (size_t i = 0; i < remaining; ++i)
        b |= static_cast<uint64_t> (p[i]) << (i * 8);

    v3 ^= b;
    sip_round (&v0, &v1, &v2, &v3);
    sip_round (&v0, &v1, &v2, &v3);
    v0 ^= b;
    v2 ^= 0xff;
    sip_round (&v0, &v1, &v2, &v3);
    sip_round (&v0, &v1, &v2, &v3);
    sip_round (&v0, &v1, &v2, &v3);
    sip_round (&v0, &v1, &v2, &v3);
    return v0 ^ v1 ^ v2 ^ v3;
}

inline size_t bytes (const void *data_, size_t len_)
{
    const unsigned char *raw = static_cast<const unsigned char *> (data_);
    return static_cast<size_t> (sip24 (raw, len_));
}

inline size_t u64 (uint64_t value_)
{
    unsigned char bytes_[8];
    for (int i = 0; i < 8; ++i)
        bytes_[i] = static_cast<unsigned char> ((value_ >> (i * 8)) & 0xff);
    return bytes (bytes_, sizeof (bytes_));
}

}
}

#endif
