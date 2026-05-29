/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "utils/allocator.hpp"
#include "utils/env.hpp"
#include "utils/macros.hpp"

#include <cstdlib>
#include <vector>

namespace
{
//  Pool bucket sizes in bytes. Allocations are rounded up to the next bucket.
//  Sizes above the last bucket are not pooled.
const std::size_t bucket_sizes[] = {64,   128,  256,   512,   1024,  2048,
                                    4096, 8192, 16384, 32768, 65536};
const std::size_t bucket_count =
  sizeof (bucket_sizes) / sizeof (bucket_sizes[0]);

std::size_t bucket_index_for_size (std::size_t size_)
{
    for (std::size_t i = 0; i < bucket_count; ++i)
        if (size_ <= bucket_sizes[i])
            return i;
    return bucket_count;
}

std::size_t bucket_capacity ()
{
    //  Per-bucket entry cap; bounds per-thread cached memory.
    static const std::size_t cap = static_cast<std::size_t> (
      zlink::env::positive_int ("ZLINK_ALLOC_TL_BUCKET_CAP", 64));
    return cap;
}

typedef std::vector<void *> bucket_pool_t;

std::vector<bucket_pool_t> &thread_buckets ()
{
    static thread_local std::vector<bucket_pool_t> buckets (bucket_count);
    return buckets;
}
}

namespace zlink
{
void *alloc (std::size_t size_)
{
    return std::malloc (size_);
}

void dealloc (void *ptr_)
{
    std::free (ptr_);
}

void *alloc_tl (std::size_t size_)
{
    const std::size_t idx = bucket_index_for_size (size_);
    if (idx >= bucket_count)
        return std::malloc (size_);

    std::vector<bucket_pool_t> &buckets = thread_buckets ();
    bucket_pool_t &pool = buckets[idx];
    if (!pool.empty ()) {
        void *ptr = pool.back ();
        pool.pop_back ();
        return ptr;
    }
    return std::malloc (bucket_sizes[idx]);
}

void dealloc_tl (void *ptr_, std::size_t size_)
{
    if (!ptr_)
        return;

    const std::size_t idx = bucket_index_for_size (size_);
    if (idx >= bucket_count) {
        std::free (ptr_);
        return;
    }

    std::vector<bucket_pool_t> &buckets = thread_buckets ();
    bucket_pool_t &pool = buckets[idx];
    if (pool.size () < bucket_capacity ())
        pool.push_back (ptr_);
    else
        std::free (ptr_);
}
}
