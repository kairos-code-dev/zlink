/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_SPOT_IMPL_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_SPOT_IMPL_HPP_INCLUDED

#include <zlink/Contracts/Service/spot.hpp>

namespace zlink
{
namespace service
{

struct spot_t::impl
{
    void *handle = nullptr;
    int last_error = 0;
    std::chrono::milliseconds default_request_timeout;
    std::function<void ()> send_ready_handler;
    std::function<void (received_t)> routed_receive_handler;
    std::function<void (spot_t &, const spot_dispatch_info_t &)> dispatch_event_handler;
};

struct spot_t::native_handle_ctor_tag_t
{
};

} // namespace service
} // namespace zlink

#endif
