/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SECURE_ALLOCATOR_HPP_INCLUDED__
#define __ZLINK_SECURE_ALLOCATOR_HPP_INCLUDED__

#include "utils/macros.hpp"
#include <memory>

namespace zlink
{
template <typename T> struct secure_allocator_t : std::allocator<T>
{
    secure_allocator_t () ZLINK_DEFAULT;

    template <class U>
    secure_allocator_t (const secure_allocator_t<U> &) ZLINK_NOEXCEPT
    {
    }

    template <class U> struct rebind
    {
        typedef secure_allocator_t<U> other;
    };
};
}

#endif