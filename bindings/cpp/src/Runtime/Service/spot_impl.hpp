/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_SPOT_IMPL_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_SPOT_IMPL_HPP_INCLUDED

#include <zlink/Contracts/Service/spot.hpp>

#include <cerrno>

namespace zlink
{
namespace service
{

struct spot_t::impl
{
    void *handle = nullptr;
    int last_error = 0;
};

struct spot_t::native_handle_ctor_tag_t
{
};

} // namespace service
} // namespace zlink

#endif
