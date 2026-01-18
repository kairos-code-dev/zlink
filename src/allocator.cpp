/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "allocator.hpp"
#include "macros.hpp"

#include <cstddef>
#include <memory_resource>
#include <new>

#include <mimalloc.h>

namespace zmq
{
namespace
{
struct alignas (max_align_t) alloc_header_t
{
    std::size_t size;
};

class mimalloc_resource_t : public std::pmr::memory_resource
{
  private:
    void *do_allocate (std::size_t bytes_, std::size_t alignment_) ZMQ_FINAL
    {
        return mi_malloc_aligned (bytes_, alignment_);
    }

    void do_deallocate (void *ptr_,
                        std::size_t bytes_,
                        std::size_t alignment_) ZMQ_FINAL
    {
        LIBZMQ_UNUSED (bytes_);
        LIBZMQ_UNUSED (alignment_);
        mi_free (ptr_);
    }

    bool do_is_equal (const std::pmr::memory_resource &other_) const
      ZMQ_NOEXCEPT ZMQ_FINAL
    {
        return this == &other_;
    }
};

std::pmr::memory_resource *select_resource ()
{
    static mimalloc_resource_t mi_resource;
    return &mi_resource;
}

std::pmr::memory_resource *get_resource ()
{
    static std::pmr::memory_resource *res = select_resource ();
    return res;
}

void *allocate_with_header (std::pmr::memory_resource *res_,
                            std::size_t size_)
{
    const std::size_t total = sizeof (alloc_header_t) + size_;
    void *block = res_->allocate (total, alignof (alloc_header_t));
    alloc_header_t *header = static_cast<alloc_header_t *> (block);
    header->size = size_;
    return header + 1;
}

std::pmr::memory_resource *get_thread_local_resource ()
{
    static thread_local std::pmr::memory_resource *res = [] () {
        static thread_local std::pmr::unsynchronized_pool_resource pool (
          get_resource ());
        return static_cast<std::pmr::memory_resource *> (&pool);
    }();
    return res;
}

void deallocate_with_header (std::pmr::memory_resource *res_, void *ptr_)
{
    if (!ptr_)
        return;

    alloc_header_t *header = static_cast<alloc_header_t *> (ptr_) - 1;
    const std::size_t total = sizeof (alloc_header_t) + header->size;
    res_->deallocate (header, total, alignof (alloc_header_t));
}
}

void *alloc (std::size_t size_)
{
    try {
        return allocate_with_header (get_resource (), size_);
    } catch (const std::bad_alloc &) {
        return NULL;
    }
}

void dealloc (void *ptr_)
{
    deallocate_with_header (get_resource (), ptr_);
}

void *alloc_tl (std::size_t size_)
{
    try {
        return allocate_with_header (get_thread_local_resource (), size_);
    } catch (const std::bad_alloc &) {
        return NULL;
    }
}

void dealloc_tl (void *ptr_)
{
    deallocate_with_header (get_thread_local_resource (), ptr_);
}
}
