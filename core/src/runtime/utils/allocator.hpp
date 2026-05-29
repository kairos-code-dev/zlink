/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_ALLOCATOR_HPP_INCLUDED__
#define __ZLINK_ALLOCATOR_HPP_INCLUDED__

#include <cstddef>

namespace zlink
{
void *alloc (std::size_t size_);
void dealloc (void *ptr_);

//  Thread-local bucketed pool. `alloc_tl` may return a chunk strictly
//  larger than `size_` rounded up to the next pool bucket; callers must
//  pass the same logical size to `dealloc_tl` so the matching bucket
//  can be recovered. Sizes above the largest bucket fall through to
//  `std::malloc`/`std::free`.
void *alloc_tl (std::size_t size_);
void dealloc_tl (void *ptr_, std::size_t size_);
}

#endif
