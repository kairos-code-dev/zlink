/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_SPOT_IMPL_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_SPOT_IMPL_HPP_INCLUDED

#include <zlink/Contracts/Service/spot.hpp>

#include <cerrno>
#include <condition_variable>
#include <mutex>

namespace zlink
{
namespace service
{

struct spot_dispatch_handler_state_t
{
    std::mutex mutex;
    std::condition_variable cv;
    spot_t *owner = nullptr;
    std::function<void (spot_t &, const spot_dispatch_info_t &)> handler;
    bool closed = false;
    std::size_t in_flight = 0;
};

struct spot_t::impl
{
    bool require_handle () const noexcept
    {
        if (handle)
            return true;
        errno = last_error != 0 ? last_error : EFAULT;
        return false;
    }

    void throw_invalid_handle () const
    {
        if (require_handle ())
            return;
        throw submit_error_t (submit_result_t::invalid_argument, errno);
    }

    void *handle = nullptr;
    int last_error = 0;
    std::chrono::milliseconds default_request_timeout;
    std::function<void ()> send_ready_handler;
    std::function<void (received_t)> routed_receive_handler;
    std::shared_ptr<spot_dispatch_handler_state_t> dispatch_state;
};

struct spot_t::native_handle_ctor_tag_t
{
};

} // namespace service
} // namespace zlink

#endif
