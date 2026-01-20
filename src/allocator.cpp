/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "allocator.hpp"
#include "macros.hpp"

#include <cstdlib>

namespace zmq
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
    return std::malloc (size_);
}

void dealloc_tl (void *ptr_)
{
    std::free (ptr_);
}
}
