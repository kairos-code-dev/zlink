/* SPDX-License-Identifier: MPL-2.0 */

#include "zlink/Contracts/Service/spot_node.hpp"
#include "zlink/Contracts/Sockets/message_socket_contracts.hpp"
#include "zlink/Contracts/Sockets/routed_socket_contracts.hpp"

#include <Runtime/Core/context_access.hpp>
#include <Runtime/Core/operation_detail.hpp>
#include <Runtime/Native/message_access.hpp>
#include <Runtime/Native/native_message_parts.hpp>
#include <Runtime/Service/detail.hpp>
#include <Runtime/Service/request_submitter.hpp>
#include <Runtime/Service/spot_access.hpp>
#include <Runtime/Sockets/socket_access.hpp>

#include <zlink.h>

#include <cerrno>
#include <cstring>
#include <unordered_map>

namespace zlink::service
{

namespace
{

void validate_channel_name (const std::string &channel_name_)
{
    zlink::detail::validate_bounded_c_string (channel_name_, 255u, "channel_name");
    if (channel_name_.empty ())
        throw config_error_t (config_result_t::invalid_argument, EINVAL);
}

void validate_topic (const std::string &topic_)
{
    zlink::detail::validate_bounded_c_string (topic_, 255u, "topic");
    if (topic_.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
}

zlink_spot_route_bridge_endpoint_options_t
endpoint_options (spot_route_bridge_t::endpoint_capabilities_t capabilities_) noexcept
{
    zlink_spot_route_bridge_endpoint_options_t options;
    std::memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.capabilities = static_cast<uint32_t> (capabilities_);
    return options;
}

void throw_config_rc (int rc_)
{
    zlink::detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (rc_));
}

} // namespace

struct spot_route_bridge_t::impl
{
    void *handle = nullptr;
    std::unordered_map<std::string, void *> endpoint_handles;

    void *endpoint_handle (const std::string &channel_name_) const noexcept
    {
        const auto it = endpoint_handles.find (channel_name_);
        return it == endpoint_handles.end () ? nullptr : it->second;
    }
};

spot_route_bridge_t::spot_route_bridge_t (context_t &ctx_, spot_node_t &node_) :
    spot_route_bridge_t (zlink::detail::native_handle (ctx_), node_)
{
}

spot_route_bridge_t::spot_route_bridge_t (void *ctx_handle_, spot_node_t &node_) :
    _impl (std::make_unique<impl> ()), _last_error (0)
{
    zlink_spot_route_bridge_options_t options;
    std::memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    _impl->handle = zlink_spot_route_bridge_new (
      ctx_handle_, zlink::detail::native_handle (node_), &options);
    if (!_impl->handle)
        _last_error = errno != 0 ? errno : EFAULT;
}

spot_route_bridge_t::~spot_route_bridge_t ()
{
    try {
        close ();
    }
    catch (...) {
    }
}

spot_route_bridge_t::spot_route_bridge_t (spot_route_bridge_t &&other_) noexcept :
    _impl (std::move (other_._impl)), _last_error (other_._last_error)
{
    if (!other_._impl)
        other_._impl = std::make_unique<impl> ();
    other_._last_error = 0;
}

spot_route_bridge_t &spot_route_bridge_t::operator= (spot_route_bridge_t &&other_) noexcept
{
    if (this == &other_)
        return *this;
    try {
        close ();
    }
    catch (...) {
    }
    _impl = std::move (other_._impl);
    _last_error = other_._last_error;
    if (!other_._impl)
        other_._impl = std::make_unique<impl> ();
    other_._last_error = 0;
    return *this;
}

bool spot_route_bridge_t::valid () const noexcept
{
    return _impl && _impl->handle != nullptr;
}

void spot_route_bridge_t::attach_router_channel (const std::string &channel_name_,
                                                 zlink::router_socket_t &router_)
{
    attach_router_channel (channel_name_, router_, endpoint_capabilities_t::route_only);
}

void spot_route_bridge_t::attach_router_channel (const std::string &channel_name_,
                                                 zlink::router_socket_t &router_,
                                                 endpoint_capabilities_t capabilities_)
{
    validate_channel_name (channel_name_);
    zlink_spot_route_bridge_endpoint_options_t options = endpoint_options (capabilities_);
    throw_config_rc (zlink_spot_route_bridge_attach_router_channel (
      _impl->handle, channel_name_.c_str (), zlink::detail::native_handle (router_), &options));
    _impl->endpoint_handles[channel_name_] = zlink::detail::native_handle (router_);
}

bool spot_route_bridge_t::send (const std::string &channel_name_,
                                const routing_id_t &target_node_rid_,
                                const routing_id_t &target_spot_rid_,
                                std::vector<message_t> &parts_,
                                send_flags_t flags_)
{
    validate_channel_name (channel_name_);
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_parts_, size_t part_count_) {
          return zlink_spot_route_bridge_send (
            _impl->handle, channel_name_.c_str (),
            zlink::detail::routing_id_native (target_node_rid_),
            zlink::detail::routing_id_native (target_spot_rid_), native_parts_, part_count_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    if (rc == 0)
        return true;
    if (rc == -1)
        throw zlink::detail::last_error ();
    const submit_result_t result = static_cast<submit_result_t> (rc);
    if (flags_ == send_flags_t::dontwait && result == submit_result_t::backpressured)
        return false;
    throw submit_error_t (result, zlink_errno ());
}

async_result_t<std::vector<message_t>>
spot_route_bridge_t::request (const std::string &channel_name_,
                              const routing_id_t &target_node_rid_,
                              const routing_id_t &target_spot_rid_,
                              std::vector<message_t> &parts_,
                              std::chrono::milliseconds timeout_)
{
    validate_channel_name (channel_name_);
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, std::chrono::milliseconds (0));
    std::unique_ptr<detail::request_state_t> state (detail::make_future_request_state ());
    std::future<std::vector<message_t>> future = state->promise->get_future ();

    const int raw_rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_parts_, size_t part_count_) {
          return zlink_spot_route_bridge_request (
            _impl->handle, channel_name_.c_str (),
            zlink::detail::routing_id_native (target_node_rid_),
            zlink::detail::routing_id_native (target_spot_rid_), native_parts_, part_count_,
            &detail::request_callback_trampoline, state.get (), ZLINK_SEND_FLAGS_NONE,
            timeout_ms);
      });
    if (raw_rc == -1)
        throw zlink::detail::last_error ();
    const submit_result_t rc = static_cast<submit_result_t> (raw_rc);
    if (rc != submit_result_t::ok)
        throw submit_error_t (rc, zlink_errno ());

    state.release ();
    return async_result_t<std::vector<message_t>> (
      std::move (future),
      zlink::detail::make_request_progress_callback (_impl->endpoint_handle (channel_name_)));
}

bool spot_route_bridge_t::request (const std::string &channel_name_,
                                   const routing_id_t &target_node_rid_,
                                   const routing_id_t &target_spot_rid_,
                                   std::vector<message_t> &parts_,
                                   request_callback_t callback_,
                                   send_flags_t flags_,
                                   std::chrono::milliseconds timeout_)
{
    validate_channel_name (channel_name_);
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, std::chrono::milliseconds (0));
    std::unique_ptr<detail::request_state_t> state (
      detail::make_callback_request_state (std::move (callback_)));

    const int raw_rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_parts_, size_t part_count_) {
          return zlink_spot_route_bridge_request (
            _impl->handle, channel_name_.c_str (),
            zlink::detail::routing_id_native (target_node_rid_),
            zlink::detail::routing_id_native (target_spot_rid_), native_parts_, part_count_,
            &detail::request_callback_trampoline, state.get (),
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), timeout_ms);
      });
    if (raw_rc == -1)
        throw zlink::detail::last_error ();
    const submit_result_t rc = static_cast<submit_result_t> (raw_rc);
    if (rc != submit_result_t::ok) {
        if (flags_ == send_flags_t::dontwait && rc == submit_result_t::backpressured)
            return false;
        throw submit_error_t (rc, zlink_errno ());
    }

    state.release ();
    return true;
}

bool spot_route_bridge_t::handle_router_received (const std::string &channel_name_,
                                                  const routing_id_t &source_node_rid_,
                                                  std::vector<message_t> &parts_,
                                                  std::optional<uint64_t> request_seq_)
{
    validate_channel_name (channel_name_);
    std::vector<zlink_msg_t> native_parts;
    if (zlink::detail::move_parts_to_native (parts_, native_parts) != 0)
        throw zlink::detail::last_error ();

    bool handled = false;
    const auto restore = [&] {
        zlink::detail::restore_parts_from_native (parts_, native_parts.data (), native_parts.size ());
    };

    const int rc = zlink_spot_route_bridge_handle_router_received (
      _impl->handle, channel_name_.c_str (), zlink::detail::routing_id_native (source_node_rid_),
      request_seq_.value_or (0), native_parts.data (), native_parts.size (), &handled);
    if (rc != 0) {
        restore ();
        throw zlink::detail::last_error ();
    }
    if (!handled)
        restore ();
    return handled;
}

int spot_route_bridge_t::drain ()
{
    return zlink_spot_route_bridge_drain (_impl->handle);
}

void spot_route_bridge_t::close ()
{
    if (!_impl || !_impl->handle)
        return;
    void *tmp = _impl->handle;
    zlink::detail::throw_if_failed<close_error_t> (
      static_cast<close_result_t> (zlink_spot_route_bridge_close (tmp)));
    _impl->handle = nullptr;
    _impl->endpoint_handles.clear ();
}

struct spot_node_publisher_t::impl
{
    void *handle = nullptr;
};

spot_node_publisher_t::spot_node_publisher_t (spot_node_t &node_) :
    _impl (std::make_unique<impl> ()), _last_error (0)
{
    _impl->handle = zlink_spot_node_publisher_new (zlink::detail::native_handle (node_));
    if (!_impl->handle)
        _last_error = errno != 0 ? errno : EFAULT;
}

spot_node_publisher_t::~spot_node_publisher_t ()
{
    try {
        close ();
    }
    catch (...) {
    }
}

spot_node_publisher_t::spot_node_publisher_t (spot_node_publisher_t &&other_) noexcept :
    _impl (std::move (other_._impl)), _last_error (other_._last_error)
{
    if (!other_._impl)
        other_._impl = std::make_unique<impl> ();
    other_._last_error = 0;
}

spot_node_publisher_t &spot_node_publisher_t::operator= (spot_node_publisher_t &&other_) noexcept
{
    if (this == &other_)
        return *this;
    try {
        close ();
    }
    catch (...) {
    }
    _impl = std::move (other_._impl);
    _last_error = other_._last_error;
    if (!other_._impl)
        other_._impl = std::make_unique<impl> ();
    other_._last_error = 0;
    return *this;
}

bool spot_node_publisher_t::valid () const noexcept
{
    return _impl && _impl->handle != nullptr;
}

bool spot_node_publisher_t::publish (const std::string &topic_,
                                     std::vector<message_t> &parts_,
                                     send_flags_t flags_)
{
    validate_topic (topic_);
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_parts_, size_t part_count_) {
          return zlink_spot_node_publisher_publish (
            _impl->handle, topic_.c_str (), native_parts_, part_count_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    if (rc == 0)
        return true;
    if (rc == -1)
        throw zlink::detail::last_error ();
    const submit_result_t result = static_cast<submit_result_t> (rc);
    if (flags_ == send_flags_t::dontwait && result == submit_result_t::backpressured)
        return false;
    throw submit_error_t (result, zlink_errno ());
}

void spot_node_publisher_t::close ()
{
    if (!_impl || !_impl->handle)
        return;
    void *tmp = _impl->handle;
    zlink::detail::throw_if_failed<close_error_t> (
      static_cast<close_result_t> (zlink_spot_node_publisher_close (tmp)));
    _impl->handle = nullptr;
}

} // namespace zlink::service
