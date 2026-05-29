/* SPDX-License-Identifier: MPL-2.0 */

#include <Runtime/Service/spot_impl.hpp>
#include <Runtime/Core/duration_conversion.hpp>
#include <Runtime/Service/detail.hpp>
#include <Runtime/Service/spot_operation_submit.hpp>

namespace zlink
{
namespace service
{

namespace
{

template<typename SubmitPart>
async_result_t<std::vector<message_t>>
submit_spot_request_async (void *spot_handle_,
                           std::vector<message_t> &parts_,
                           SubmitPart submit_part_)
{
    std::unique_ptr<detail::request_state_t> state (
      detail::make_future_request_state ());
    std::future<std::vector<message_t> > future = state->promise->get_future ();
    std::vector<zlink_msg_t> native;
    if (detail::move_parts_to_native (parts_, native) != 0)
        throw last_error ();
    size_t failed_index = 0;
    const submit_result_t rc =
      static_cast<submit_result_t> (detail::submit_native_parts (
        native, failed_index,
        [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
             bool is_final_) {
            return submit_part_ (
              part_out_, part_flag_,
              is_final_ ? &detail::request_callback_trampoline : nullptr,
              is_final_ ? state.get () : nullptr);
        }));
    if (rc != submit_result_t::ok) {
        detail::close_native_parts (native, failed_index);
        throw submit_error_t (rc, zlink_errno ());
    }
    state.release ();
    return async_result_t<std::vector<message_t> > (
      std::move (future), detail::make_spot_request_progress (spot_handle_));
}

template<typename SubmitPart>
bool submit_spot_request_callback (std::vector<message_t> &parts_,
                                   request_callback_t callback_,
                                   send_flags_t flags_,
                                   SubmitPart submit_part_)
{
    std::unique_ptr<detail::request_state_t> state (
      detail::make_callback_request_state (std::move (callback_)));
    std::vector<zlink_msg_t> native;
    if (detail::move_parts_to_native (parts_, native) != 0)
        throw last_error ();
    size_t failed_index = 0;
    const submit_result_t rc =
      static_cast<submit_result_t> (detail::submit_native_parts (
        native, failed_index,
        [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
             bool is_final_) {
            return submit_part_ (
              part_out_, part_flag_,
              is_final_ ? &detail::request_callback_trampoline : nullptr,
              is_final_ ? state.get () : nullptr);
        }));
    if (rc != submit_result_t::ok) {
        detail::close_native_parts (native, failed_index);
        if (flags_ == send_flags_t::dontwait
            && rc == submit_result_t::backpressured)
            return false;
        throw submit_error_t (rc, zlink_errno ());
    }
    state.release ();
    return true;
}

} // namespace

bool spot_t::publish (const std::string &topic_,
                      std::vector<message_t> &parts_,
                      send_flags_t flags_)
{
    const int rc = publish_impl (topic_.c_str (), parts_, flags_);
    if (rc != 0) {
        if (flags_ == send_flags_t::dontwait
            && static_cast<submit_result_t> (rc)
                 == submit_result_t::backpressured)
            return false;
        throw submit_error_t (static_cast<submit_result_t> (rc),
                              zlink_errno ());
    }
    return true;
}

bool spot_t::publish (const std::string &topic_,
                      message_t &part_,
                      send_flags_t flags_)
{
    if (flags_ == send_flags_t::dontwait) {
        send_result_t result = send_result_t::sent;
        if (publish_no_wait_result_impl (result, topic_.c_str (), part_) != 0) {
            const int err = zlink_errno ();
            throw submit_error_t (zlink::detail::submit_result_from_errno (err),
                                  err);
        }
        if (result == send_result_t::not_ready)
            throw submit_error_t (submit_result_t::not_connected,
                                  zlink_errno ());
        return result == send_result_t::sent;
    }

    const int rc = publish_impl (topic_.c_str (), part_, flags_);
    if (rc != 0) {
        if (flags_ == send_flags_t::dontwait
            && static_cast<submit_result_t> (rc)
                 == submit_result_t::backpressured)
            return false;
        throw submit_error_t (static_cast<submit_result_t> (rc),
                              zlink_errno ());
    }
    return true;
}

bool spot_t::publish_discard_on_backpressure (const std::string &topic_,
                                              message_t &part_)
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

    const int rc =
      zlink_spot_publish_part (_impl->handle, topic_.c_str (), &native,
                               ZLINK_DONTWAIT, ZLINK_PART_FINAL);
    if (rc == 0)
        return true;

    const int err = zlink_errno ();
    (void) zlink_msg_close (&native);
    if (rc == ZLINK_SUBMIT_BACKPRESSURED)
        return false;
    send_result_t result = send_result_t::sent;
    if (detail::classify_nonblocking_send_errno (err, result)
        && result != send_result_t::sent) {
        errno = err;
        if (result == send_result_t::backpressured)
            return false;
        throw submit_error_t (submit_result_t::not_connected, err);
    }
    throw submit_error_t (rc == ZLINK_SUBMIT_NOT_CONNECTED
                            ? submit_result_t::not_connected
                            : static_cast<submit_result_t> (rc),
                          err);
}

bool spot_t::send_channel (const std::string &channel_name_,
                           message_t &part_,
                           send_flags_t flags_)
{
    validate_channel_name (channel_name_);
    if (flags_ == send_flags_t::dontwait) {
        send_result_t result = send_result_t::sent;
        if (send_channel_no_wait_result_impl (result, channel_name_.c_str (),
                                              part_)
            != 0) {
            const int err = zlink_errno ();
            throw submit_error_t (zlink::detail::submit_result_from_errno (err),
                                  err);
        }
        if (result == send_result_t::not_ready)
            throw submit_error_t (submit_result_t::not_connected,
                                  zlink_errno ());
        return result == send_result_t::sent;
    }

    std::vector<message_t> parts;
    parts.push_back (std::move (part_));
    const bool submitted = send_channel (channel_name_, parts, flags_);
    if (!submitted && !parts.empty ())
        part_ = std::move (parts.front ());
    return submitted;
}

bool spot_t::send_channel (const std::string &channel_name_,
                           std::vector<message_t> &parts_,
                           send_flags_t flags_)
{
    validate_channel_name (channel_name_);
    const int rc = send_channel_impl (channel_name_.c_str (), parts_, flags_);
    if (rc != 0) {
        if (flags_ == send_flags_t::dontwait
            && static_cast<submit_result_t> (rc)
                 == submit_result_t::backpressured)
            return false;
        throw submit_error_t (static_cast<submit_result_t> (rc),
                              zlink_errno ());
    }
    return true;
}

bool spot_t::send_to_spot (const routing_id_t &dest_node_rid_,
                           const routing_id_t &dest_spot_rid_,
                           message_t message_,
                           send_flags_t flags_)
{
    std::vector<message_t> parts;
    parts.push_back (std::move (message_));
    return send_to_spot (dest_node_rid_, dest_spot_rid_, parts, flags_);
}

bool spot_t::send_to_spot (const routing_id_t &dest_node_rid_,
                           const routing_id_t &dest_spot_rid_,
                           std::vector<message_t> &parts_,
                           send_flags_t flags_)
{
    std::vector<zlink_msg_t> native;
    if (detail::move_parts_to_native (parts_, native) != 0)
        throw last_error ();
    size_t failed_index = 0;
    const submit_result_t rc =
      static_cast<submit_result_t> (detail::submit_native_parts (
        native, failed_index,
        [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
            return zlink_spot_send_spot_part (
              _impl->handle, zlink::detail::routing_id_native (dest_node_rid_),
              zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
              static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
              part_flag_);
        }));
    if (rc != submit_result_t::ok) {
        detail::close_native_parts (native, failed_index);
        if (flags_ == send_flags_t::dontwait
            && rc == submit_result_t::backpressured)
            return false;
        throw submit_error_t (rc, zlink_errno ());
    }
    return true;
}

send_operation_t spot_t::send_to_spot (const routing_id_t &dest_node_rid_,
                                       const routing_id_t &dest_spot_rid_)
{
    detail::spot_operation_state_t state;
    state.spot = this;
    state.kind = detail::spot_operation_kind_t::send_to_spot;
    state.first_rid = dest_node_rid_;
    state.second_rid = dest_spot_rid_;
    return send_operation_t (std::move (state));
}

request_operation_t spot_t::request_to_spot (const routing_id_t &dest_node_rid_,
                                             const routing_id_t &dest_spot_rid_)
{
    detail::spot_operation_state_t state;
    state.spot = this;
    state.kind = detail::spot_operation_kind_t::request_to_spot;
    state.first_rid = dest_node_rid_;
    state.second_rid = dest_spot_rid_;
    return request_operation_t (std::move (state));
}

async_result_t<std::vector<message_t> >
spot_t::request_to_spot (const routing_id_t &dest_node_rid_,
                         const routing_id_t &dest_spot_rid_,
                         message_t message_,
                         std::chrono::milliseconds timeout_)
{
    std::vector<message_t> parts;
    parts.push_back (std::move (message_));
    const uint32_t timeout_ms = zlink::detail::native_timeout_ms (
      zlink::detail::resolve_timeout (timeout_, _impl->default_request_timeout));
    return submit_spot_request_async (
      _impl->handle, parts,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
           zlink_reply_handler_fn callback_, void *state_) {
          return zlink_spot_request_spot_part (
            _impl->handle, zlink::detail::routing_id_native (dest_node_rid_),
            zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
            callback_, state_, ZLINK_SEND_FLAGS_NONE, part_flag_,
            callback_ ? timeout_ms : 0u);
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
    std::vector<message_t> parts;
    parts.push_back (std::move (message_));
    const uint32_t timeout_ms = zlink::detail::native_timeout_ms (
      zlink::detail::resolve_timeout (timeout_, _impl->default_request_timeout));
    return submit_spot_request_callback (
      parts, std::move (callback_), flags_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
           zlink_reply_handler_fn callback, void *state) {
          return zlink_spot_request_spot_part (
            _impl->handle, zlink::detail::routing_id_native (dest_node_rid_),
            zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
            callback, state,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            part_flag_, callback ? timeout_ms : 0u);
      });
}

request_operation_t spot_t::request_to_router (const routing_id_t &peer_rid_)
{
    detail::spot_operation_state_t state;
    state.spot = this;
    state.kind = detail::spot_operation_kind_t::request_to_router;
    state.first_rid = peer_rid_;
    return request_operation_t (std::move (state));
}

async_result_t<std::vector<message_t> >
spot_t::request_to_router (const routing_id_t &peer_rid_,
                           message_t message_,
                           std::chrono::milliseconds timeout_)
{
    std::vector<message_t> parts;
    parts.push_back (std::move (message_));
    const uint32_t timeout_ms = zlink::detail::native_timeout_ms (
      zlink::detail::resolve_timeout (timeout_, _impl->default_request_timeout));
    return submit_spot_request_async (
      _impl->handle, parts,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
           zlink_reply_handler_fn callback, void *state) {
          return zlink_spot_request_router_part (
            _impl->handle, zlink::detail::routing_id_native (peer_rid_),
            part_out_, callback, state, ZLINK_SEND_FLAGS_NONE, part_flag_,
            callback ? timeout_ms : 0u);
      });
}

bool spot_t::request_to_router (
  const routing_id_t &peer_rid_,
  message_t message_,
  std::function<void (request_result_t, std::vector<message_t>)> callback_,
  send_flags_t flags_,
  std::chrono::milliseconds timeout_)
{
    std::vector<message_t> parts;
    parts.push_back (std::move (message_));
    const uint32_t timeout_ms = zlink::detail::native_timeout_ms (
      zlink::detail::resolve_timeout (timeout_, _impl->default_request_timeout));
    return submit_spot_request_callback (
      parts, std::move (callback_), flags_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
           zlink_reply_handler_fn callback, void *state) {
          return zlink_spot_request_router_part (
            _impl->handle, zlink::detail::routing_id_native (peer_rid_),
            part_out_, callback, state,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            part_flag_, callback ? timeout_ms : 0u);
      });
}

async_result_t<std::vector<message_t> >
spot_t::request_channel (const std::string &channel_name_,
                         message_t &part_,
                         std::chrono::milliseconds timeout_)
{
    std::vector<message_t> parts;
    parts.push_back (std::move (part_));
    return request_channel (channel_name_, parts, timeout_);
}

async_result_t<std::vector<message_t> >
spot_t::request_channel (const std::string &channel_name_,
                         std::vector<message_t> &parts_,
                         std::chrono::milliseconds timeout_)
{
    validate_channel_name (channel_name_);
    const uint32_t timeout_ms = zlink::detail::native_timeout_ms (
      zlink::detail::resolve_timeout (timeout_, _impl->default_request_timeout));
    return submit_spot_request_async (
      _impl->handle, parts_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
           zlink_reply_handler_fn callback, void *state) {
          return zlink_spot_request_channel_part (
            _impl->handle, channel_name_.c_str (), part_out_, callback, state,
            ZLINK_SEND_FLAGS_NONE, part_flag_, callback ? timeout_ms : 0u);
      });
}

bool spot_t::request_channel (
  const std::string &channel_name_,
  message_t &part_,
  std::function<void (request_result_t, std::vector<message_t>)> callback_,
  send_flags_t flags_,
  std::chrono::milliseconds timeout_)
{
    std::vector<message_t> parts;
    parts.push_back (std::move (part_));
    const bool submitted = request_channel (
      channel_name_, parts, std::move (callback_), flags_, timeout_);
    if (!submitted && !parts.empty ())
        part_ = std::move (parts.front ());
    return submitted;
}

bool spot_t::request_channel (
  const std::string &channel_name_,
  std::vector<message_t> &parts_,
  std::function<void (request_result_t, std::vector<message_t>)> callback_,
  send_flags_t flags_,
  std::chrono::milliseconds timeout_)
{
    validate_channel_name (channel_name_);
    const uint32_t timeout_ms = zlink::detail::native_timeout_ms (
      zlink::detail::resolve_timeout (timeout_, _impl->default_request_timeout));
    return submit_spot_request_callback (
      parts_, std::move (callback_), flags_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
           zlink_reply_handler_fn callback, void *state) {
          return zlink_spot_request_channel_part (
            _impl->handle, channel_name_.c_str (), part_out_, callback, state,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            part_flag_, callback ? timeout_ms : 0u);
      });
}

} // namespace service
} // namespace zlink
