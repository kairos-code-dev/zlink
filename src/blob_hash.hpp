/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_BLOB_HASH_HPP_INCLUDED__
#define __ZMQ_BLOB_HASH_HPP_INCLUDED__

#include "blob.hpp"
#include <functional>
#include <cstring>

namespace zmq
{

//  Hash functor for blob_t with transparent hashing support.
//  Allows heterogeneous lookup in unordered_map without creating temporary blob_t.
//  Uses FNV-1a algorithm for good distribution and performance.
struct blob_hash {
    using is_transparent = void;  // Enable heterogeneous lookup (C++14+)

    //  Hash computation using FNV-1a algorithm.
    //  FNV-1a provides excellent distribution for byte strings and is
    //  particularly efficient for the small routing IDs used in ZeroMQ.
    size_t hash_bytes(const unsigned char* data, size_t size) const noexcept {
        // FNV-1a 64-bit constants
        constexpr size_t fnv_offset_basis = 14695981039346656037ULL;
        constexpr size_t fnv_prime = 1099511628211ULL;

        size_t hash = fnv_offset_basis;
        for (size_t i = 0; i < size; ++i) {
            hash ^= static_cast<size_t>(data[i]);
            hash *= fnv_prime;
        }
        return hash;
    }

    //  Hash for blob_t
    size_t operator()(const blob_t& b) const noexcept {
        return hash_bytes(b.data(), b.size());
    }

    //  Hash for C-style array view (pair of pointer and size)
    //  Enables heterogeneous lookup without creating temporary blob_t
    size_t operator()(const std::pair<const unsigned char*, size_t>& p) const noexcept {
        return hash_bytes(p.first, p.second);
    }
};

//  Equality functor for blob_t with transparent comparison support.
//  Enables heterogeneous lookup in unordered_map.
struct blob_equal {
    using is_transparent = void;  // Enable heterogeneous lookup (C++14+)

    //  Compare blob_t with blob_t
    bool operator()(const blob_t& a, const blob_t& b) const noexcept {
        if (a.size() != b.size()) return false;
        if (a.size() == 0) return true;
        return std::memcmp(a.data(), b.data(), a.size()) == 0;
    }

    //  Compare blob_t with C-style array view
    //  Enables heterogeneous lookup without creating temporary blob_t
    bool operator()(const blob_t& a, const std::pair<const unsigned char*, size_t>& b) const noexcept {
        if (a.size() != b.second) return false;
        if (a.size() == 0) return true;
        return std::memcmp(a.data(), b.first, a.size()) == 0;
    }

    bool operator()(const std::pair<const unsigned char*, size_t>& a, const blob_t& b) const noexcept {
        return operator()(b, a);
    }
};

} // namespace zmq

#endif
