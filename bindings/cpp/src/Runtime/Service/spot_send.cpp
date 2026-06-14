/* SPDX-License-Identifier: MPL-2.0 */

#include <Runtime/Service/spot_impl.hpp>
#include <Runtime/Core/duration_conversion.hpp>
#include <Runtime/Service/detail.hpp>
#include <Runtime/Service/spot_operation_submit.hpp>
#include <Runtime/Service/request_submitter.hpp>

namespace zlink
{
namespace service
{

namespace
{

template <typename SubmitPart>
bool submit_single_send_message (message_t &message_, send_flags_t flags_, SubmitPart submit_part_)
{
    if (!message_.valid ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    zlink_msg_t native;
    zlink::detail::move_to_native (message_, &native);
    if (message_.valid ()) {
        (void) zlink_msg_close (&native);
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }

    const submit_result_t rc =
      static_cast<submit_result_t> (submit_part_ (&native, ZLINK_PART_FINAL));
    if (rc == submit_result_t::ok)
        return true;

    const int err = zlink_errno ();
    (void) zlink_msg_close (&native);
    if (flags_ == send_flags_t::dontwait && rc == submit_result_t::backpressured)
        return false;
    throw submit_error_t (rc, err);
}

} // namespace

bool spot_t::publish (const std::string &topic_,
                      std::vector<message_t> &parts_,
                      send_flags_t flags_)
{
    const int rc = publish_impl (topic_.c_str (), parts_, flags_);
    if (rc != 0) {
        if (flags_ == send_flags_t::dontwait
            && static_cast<submit_result_t> (rc) == submit_result_t::backpressured)
            return false;
        throw submit_error_t (static_cast<submit_result_t> (rc), zlink_errno ());
    }
    return true;
}

bool spot_t::publish (const std::string &topic_, message_t &part_, send_flags_t flags_)
{
    if (flags_ == send_flags_t::dontwait) {
        send_result_t result = send_result_t::sent;
        if (try_publish_result_impl (result, topic_.c_str (), part_) != 0) {
            const int err = zlink_errno ();
            throw submit_error_t (zlink::detail::submit_result_from_errno (err), err);
        }
        if (result == send_result_t::not_ready)
            throw submit_error_t (submit_result_t::not_connected, zlink_errno ());
        return result == send_result_t::sent;
    }

    const int rc = publish_impl (topic_.c_str (), part_, flags_);
    if (rc != 0) {
        if (flags_ == send_flags_t::dontwait
            && static_cast<submit_result_t> (rc) == submit_result_t::backpressured)
            return false;
        throw submit_error_t (static_cast<submit_result_t> (rc), zlink_errno ());
    }
    return true;
}

bool spot_t::publish_discard_on_backpressure (const std::string &topic_, message_t &part_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
        throw submit_error_t (submit_result_t::invalid_argument, errno);
    }
    if (!part_.valid ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    zlink_msg_t native;
    zlink::detail::move_to_native (part_, &native);
    if (part_.valid ()) {
        (void) zlink_msg_close (&native);
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }

    const int rc = zlink_spot_publish_part (_impl->handle, topic_.c_str (), &native, ZLINK_DONTWAIT,
                                            ZLINK_PART_FINAL);
    if (rc == 0)
        return true;

    const int err = zlink_errno ();
    (void) zlink_msg_close (&native);
    if (rc == ZLINK_SUBMIT_BACKPRESSURED)
        return false;
    send_result_t result = send_result_t::sent;
    if (detail::classify_nonblocking_send_errno (err, result) && result != send_result_t::sent) {
        errno = err;
        if (result == send_result_t::backpressured)
            return false;
        throw submit_error_t (submit_result_t::not_connected, err);
    }
    throw submit_error_t (rc == ZLINK_SUBMIT_NOT_CONNECTED ? submit_result_t::not_connected
                                                           : static_cast<submit_result_t> (rc),
                          err);
}

bool spot_t::send_channel (const std::string &channel_name_, message_t &part_, send_flags_t flags_)
{
    validate_channel_name (channel_name_);
    if (flags_ == send_flags_t::dontwait) {
        send_result_t result = send_result_t::sent;
        if (send_channel_no_wait_result_impl (result, channel_name_.c_str (), part_) != 0) {
            const int err = zlink_errno ();
            throw submit_error_t (zlink::detail::submit_result_from_errno (err), err);
        }
        if (result == send_result_t::not_ready)
            throw submit_error_t (submit_result_t::not_connected, zlink_errno ());
        return result == send_result_t::sent;
    }

    return submit_single_send_message (
      part_, flags_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
          return zlink_spot_send_channel_part (
            _impl->handle, channel_name_.c_str (), part_out_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_);
      });
}

bool spot_t::send_channel (const std::string &channel_name_,
                           std::vector<message_t> &parts_,
                           send_flags_t flags_)
{
    validate_channel_name (channel_name_);
    const int rc = send_channel_impl (channel_name_.c_str (), parts_, flags_);
    if (rc != 0) {
        if (flags_ == send_flags_t::dontwait
            && static_cast<submit_result_t> (rc) == submit_result_t::backpressured)
            return false;
        throw submit_error_t (static_cast<submit_result_t> (rc), zlink_errno ());
    }
    return true;
}

bool spot_t::send_to_spot (const routing_id_t &dest_node_rid_,
                           const routing_id_t &dest_spot_rid_,
                           message_t message_,
                           send_flags_t flags_)
{
    return submit_single_send_message (
      message_, flags_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
          return zlink_spot_send_spot_part (
            _impl->handle, zlink::detail::routing_id_native (dest_node_rid_),
            zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_);
      });
}

bool spot_t::send_to_spot (const routing_id_t &dest_node_rid_,
                           const routing_id_t &dest_spot_rid_,
                           std::vector<message_t> &parts_,
                           send_flags_t flags_)
{
    const int raw_rc = detail::submit_message_parts_close_on_failure (
      parts_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return zlink_spot_send_spot_part (
            _impl->handle, zlink::detail::routing_id_native (dest_node_rid_),
            zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_);
      });
    if (raw_rc == -1)
        throw last_error ();
    const submit_result_t rc = static_cast<submit_result_t> (raw_rc);
    if (rc != submit_result_t::ok) {
        if (flags_ == send_flags_t::dontwait && rc == submit_result_t::backpressured)
            return false;
        throw submit_error_t (rc, zlink_errno ());
    }
    return true;
}

send_operation_t spot_t::send_to_spot (const routing_id_t &dest_node_rid_,
                                       const routing_id_t &dest_spot_rid_)
{
    auto state_ptr = detail::acquire_state ();
    state_ptr->spot = this;
    state_ptr->kind = detail::spot_operation_kind_t::send_to_spot;
    state_ptr->first_rid = dest_node_rid_;
    state_ptr->second_rid = dest_spot_rid_;
    return send_operation_t (std::move (state_ptr));
}

request_operation_t spot_t::request_to_spot (const routing_id_t &dest_node_rid_,
                                             const routing_id_t &dest_spot_rid_)
{
    auto state_ptr = detail::acquire_state ();
    state_ptr->spot = this;
    state_ptr->kind = detail::spot_operation_kind_t::request_to_spot;
    state_ptr->first_rid = dest_node_rid_;
    state_ptr->second_rid = dest_spot_rid_;
    return request_operation_t (std::move (state_ptr));
}

async_result_t<std::vector<message_t>> spot_t::request_to_spot (const routing_id_t &dest_node_rid_,
                                                                const routing_id_t &dest_spot_rid_,
                                                                message_t message_,
                                                                std::chrono::milliseconds timeout_)
{
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, _impl->default_request_timeout);
    return detail::submit_request_part_awaitable (
      message_, detail::make_spot_request_progress (_impl->handle),
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, zlink_reply_handler_fn callback_,
           void *state_) {
          return zlink_spot_request_spot_part (
            _impl->handle, zlink::detail::routing_id_native (dest_node_rid_),
            zlink::detail::routing_id_native (dest_spot_rid_), part_out_, callback_, state_,
            ZLINK_SEND_FLAGS_NONE, part_flag_, callback_ ? timeout_ms : 0u);
      });
}

async_result_t<std::vector<message_t>> spot_t::request_to_spot (const routing_id_t &dest_node_rid_,
                                                                const routing_id_t &dest_spot_rid_,
                                                                std::vector<message_t> &parts_,
                                                                std::chrono::milliseconds timeout_)
{
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, _impl->default_request_timeout);
    return detail::submit_request_parts_awaitable (
      parts_, detail::make_spot_request_progress (_impl->handle),
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, zlink_reply_handler_fn callback_,
           void *state_) {
          return zlink_spot_request_spot_part (
            _impl->handle, zlink::detail::routing_id_native (dest_node_rid_),
            zlink::detail::routing_id_native (dest_spot_rid_), part_out_, callback_, state_,
            ZLINK_SEND_FLAGS_NONE, part_flag_, callback_ ? timeout_ms : 0u);
      });
}

bool spot_t::request_to_spot (
  const routing_id_t &dest_node_rid_,
  const routing_id_t &dest_spot_rid_,
  message_t message_,
  std::function<void (request_result_t, std::vector<message_t>)> callback_,
  send_flags_t flags_,
  std::chrono::milliseconds timeout_)
{
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, _impl->default_request_timeout);
    return detail::submit_request_part_callback (
      message_, std::move (callback_), flags_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, zlink_reply_handler_fn callback,
           void *state) {
          return zlink_spot_request_spot_part (
            _impl->handle, zlink::detail::routing_id_native (dest_node_rid_),
            zlink::detail::routing_id_native (dest_spot_rid_), part_out_, callback, state,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_,
            callback ? timeout_ms : 0u);
      });
}

bool spot_t::request_to_spot (
  const routing_id_t &dest_node_rid_,
  const routing_id_t &dest_spot_rid_,
  std::vector<message_t> &parts_,
  std::function<void (request_result_t, std::vector<message_t>)> callback_,
  send_flags_t flags_,
  std::chrono::milliseconds timeout_)
{
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, _impl->default_request_timeout);
    return detail::submit_request_parts_callback (
      parts_, std::move (callback_), flags_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, zlink_reply_handler_fn callback,
           void *state) {
          return zlink_spot_request_spot_part (
            _impl->handle, zlink::detail::routing_id_native (dest_node_rid_),
            zlink::detail::routing_id_native (dest_spot_rid_), part_out_, callback, state,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_,
            callback ? timeout_ms : 0u);
      });
}

request_operation_t spot_t::request_to_router (const routing_id_t &peer_rid_)
{
    auto state_ptr = detail::acquire_state ();
    state_ptr->spot = this;
    state_ptr->kind = detail::spot_operation_kind_t::request_to_router;
    state_ptr->first_rid = peer_rid_;
    return request_operation_t (std::move (state_ptr));
}

async_result_t<std::vector<message_t>> spot_t::request_to_router (
  const routing_id_t &peer_rid_, message_t message_, std::chrono::milliseconds timeout_)
{
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, _impl->default_request_timeout);
    return detail::submit_request_part_awaitable (
      message_, detail::make_spot_request_progress (_impl->handle),
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, zlink_reply_handler_fn callback,
           void *state) {
          return zlink_spot_request_router_part (
            _impl->handle, zlink::detail::routing_id_native (peer_rid_), part_out_, callback, state,
            ZLINK_SEND_FLAGS_NONE, part_flag_, callback ? timeout_ms : 0u);
      });
}

async_result_t<std::vector<message_t>> spot_t::request_to_router (
  const routing_id_t &peer_rid_, std::vector<message_t> &parts_, std::chrono::milliseconds timeout_)
{
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, _impl->default_request_timeout);
    return detail::submit_request_parts_awaitable (
      parts_, detail::make_spot_request_progress (_impl->handle),
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, zlink_reply_handler_fn callback,
           void *state) {
          return zlink_spot_request_router_part (
            _impl->handle, zlink::detail::routing_id_native (peer_rid_), part_out_, callback, state,
            ZLINK_SEND_FLAGS_NONE, part_flag_, callback ? timeout_ms : 0u);
      });
}

bool spot_t::request_to_router (
  const routing_id_t &peer_rid_,
  message_t message_,
  std::function<void (request_result_t, std::vector<message_t>)> callback_,
  send_flags_t flags_,
  std::chrono::milliseconds timeout_)
{
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, _impl->default_request_timeout);
    return detail::submit_request_part_callback (
      message_, std::move (callback_), flags_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, zlink_reply_handler_fn callback,
           void *state) {
          return zlink_spot_request_router_part (
            _impl->handle, zlink::detail::routing_id_native (peer_rid_), part_out_, callback, state,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_,
            callback ? timeout_ms : 0u);
      });
}

bool spot_t::request_to_router (
  const routing_id_t &peer_rid_,
  std::vector<message_t> &parts_,
  std::function<void (request_result_t, std::vector<message_t>)> callback_,
  send_flags_t flags_,
  std::chrono::milliseconds timeout_)
{
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, _impl->default_request_timeout);
    return detail::submit_request_parts_callback (
      parts_, std::move (callback_), flags_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, zlink_reply_handler_fn callback,
           void *state) {
          return zlink_spot_request_router_part (
            _impl->handle, zlink::detail::routing_id_native (peer_rid_), part_out_, callback, state,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_,
            callback ? timeout_ms : 0u);
      });
}

async_result_t<std::vector<message_t>> spot_t::request_channel (const std::string &channel_name_,
                                                                message_t &part_,
                                                                std::chrono::milliseconds timeout_)
{
    validate_channel_name (channel_name_);
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, _impl->default_request_timeout);
    return detail::submit_request_part_awaitable (
      part_, detail::make_spot_request_progress (_impl->handle),
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, zlink_reply_handler_fn callback,
           void *state) {
          return zlink_spot_request_channel_part (_impl->handle, channel_name_.c_str (), part_out_,
                                                  callback, state, ZLINK_SEND_FLAGS_NONE,
                                                  part_flag_, callback ? timeout_ms : 0u);
      });
}

async_result_t<std::vector<message_t>> spot_t::request_channel (const std::string &channel_name_,
                                                                std::vector<message_t> &parts_,
                                                                std::chrono::milliseconds timeout_)
{
    validate_channel_name (channel_name_);
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, _impl->default_request_timeout);
    return detail::submit_request_parts_awaitable (
      parts_, detail::make_spot_request_progress (_impl->handle),
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, zlink_reply_handler_fn callback,
           void *state) {
          return zlink_spot_request_channel_part (_impl->handle, channel_name_.c_str (), part_out_,
                                                  callback, state, ZLINK_SEND_FLAGS_NONE,
                                                  part_flag_, callback ? timeout_ms : 0u);
      });
}

bool spot_t::request_channel (
  const std::string &channel_name_,
  message_t &part_,
  std::function<void (request_result_t, std::vector<message_t>)> callback_,
  send_flags_t flags_,
  std::chrono::milliseconds timeout_)
{
    validate_channel_name (channel_name_);
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, _impl->default_request_timeout);
    return detail::submit_request_part_callback (
      part_, std::move (callback_), flags_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, zlink_reply_handler_fn callback,
           void *state) {
          return zlink_spot_request_channel_part (
            _impl->handle, channel_name_.c_str (), part_out_, callback, state,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_,
            callback ? timeout_ms : 0u);
      });
}

bool spot_t::request_channel (
  const std::string &channel_name_,
  std::vector<message_t> &parts_,
  std::function<void (request_result_t, std::vector<message_t>)> callback_,
  send_flags_t flags_,
  std::chrono::milliseconds timeout_)
{
    validate_channel_name (channel_name_);
    const uint32_t timeout_ms =
      zlink::detail::native_request_timeout_ms (timeout_, _impl->default_request_timeout);
    return detail::submit_request_parts_callback (
      parts_, std::move (callback_), flags_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, zlink_reply_handler_fn callback,
           void *state) {
          return zlink_spot_request_channel_part (
            _impl->handle, channel_name_.c_str (), part_out_, callback, state,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_,
            callback ? timeout_ms : 0u);
      });
}

} // namespace service
} // namespace zlink
