/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_SECURE_ALLOCATOR_HPP_INCLUDED__
#define __ZMQ_SECURE_ALLOCATOR_HPP_INCLUDED__

#include "utils/macros.hpp"
#include <memory>

namespace zmq
{
template <typename T> struct secure_allocator_t : std::allocator<T>
{
    secure_allocator_t () ZMQ_DEFAULT;

    template <class U>
    secure_allocator_t (const secure_allocator_t<U> &) ZMQ_NOEXCEPT
    {
    }

    template <class U> struct rebind
    {
        typedef secure_allocator_t<U> other;
    };
};
}

#endif