/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ALLOCATOR_HPP_INCLUDED__
#define __ZMQ_ALLOCATOR_HPP_INCLUDED__

#include <cstddef>

namespace zmq
{
void *alloc (std::size_t size_);
void dealloc (void *ptr_);
void *alloc_tl (std::size_t size_);
void dealloc_tl (void *ptr_);
}

#endif
