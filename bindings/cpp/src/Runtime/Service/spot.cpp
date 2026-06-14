/* SPDX-License-Identifier: MPL-2.0 */

#include <Runtime/Service/spot_impl.hpp>
#include <Runtime/Core/duration_conversion.hpp>
#include <Runtime/Service/actor_model_access.hpp>
#include <Runtime/Service/actor_detail.hpp>
#include <Runtime/Messaging/received_access.hpp>
#include <Runtime/Service/spot_operation_submit.hpp>
#include <Runtime/Service/spot_access.hpp>

namespace zlink
{

service::send_operation_t received_t::send ()
{
    auto state_ptr = service::detail::acquire_state ();
    state_ptr->kind = service::detail::spot_operation_kind_t::received_send;
    state_ptr->received = this;
    return service::send_operation_t (std::move (state_ptr));
}

service::reply_operation_t received_t::reply ()
{
    auto state_ptr = service::detail::acquire_state ();
    state_ptr->kind = service::detail::spot_operation_kind_t::received_reply;
    state_ptr->received = this;
    return service::reply_operation_t (std::move (state_ptr));
}

namespace detail
{

void *spot_access_t::native_handle (service::spot_t &spot_) noexcept
{
    return spot_._impl ? spot_._impl->handle : nullptr;
}

const void *spot_access_t::native_handle (const service::spot_t &spot_) noexcept
{
    return spot_._impl ? spot_._impl->handle : nullptr;
}

service::spot_t spot_access_t::adopt_native_handle (void *handle_) noexcept
{
    service::spot_t spot{service::spot_t::native_handle_ctor_tag_t ()};
    spot._impl->handle = handle_;
    spot._impl->last_error = handle_ ? 0 : (errno != 0 ? errno : EFAULT);
    return spot;
}

} // namespace detail

namespace service
{

namespace
{

template <typename SubmitPart>
void submit_single_reply_message (message_t &message_, SubmitPart submit_part_)
{
    if (!message_.valid ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    zlink_msg_t native;
    detail::move_to_native_or_reject (message_, &native);

    const submit_result_t rc = static_cast<submit_result_t> (submit_part_ (&native));
    if (rc != submit_result_t::ok) {
        (void) zlink_msg_close (&native);
        throw submit_error_t (rc, zlink_errno ());
    }
}

} // namespace

spot_t::~spot_t ()
{
    try {
        close ();
    }
    catch (...) {
    }
}

spot_t::spot_t (spot_t &&other) noexcept : _impl (std::move (other._impl))
{
}

spot_t &spot_t::operator= (spot_t &&other) noexcept
{
    if (this == &other)
        return *this;

    try {
        close ();
    }
    catch (...) {
    }
    _impl = std::move (other._impl);
    return *this;
}

bool spot_t::valid () const noexcept
{
    return _impl && _impl->handle != nullptr;
}

void spot_t::request_timeout (std::chrono::milliseconds timeout_)
{
    const int value = zlink::detail::native_option_ms (timeout_);
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (zlink_set_spot_option (
      _impl->handle, ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS, &value, sizeof (value))));
    _impl->default_request_timeout = timeout_;
}

std::chrono::milliseconds spot_t::request_timeout () const
{
    int value = 0;
    size_t size = sizeof (value);
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_get_spot_option (_impl->handle, ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS, &value, &size)));
    return std::chrono::milliseconds (value);
}

send_operation_t spot_t::publish (const std::string &topic_)
{
    zlink::detail::validate_no_embedded_null (topic_, "topic");
    auto state_ptr = detail::acquire_state ();
    state_ptr->spot = this;
    state_ptr->kind = detail::spot_operation_kind_t::publish;
    state_ptr->topic = topic_;
    return send_operation_t (std::move (state_ptr));
}

send_operation_t spot_t::send_channel (const std::string &channel_name_)
{
    validate_channel_name (channel_name_);
    auto state_ptr = detail::acquire_state ();
    state_ptr->spot = this;
    state_ptr->kind = detail::spot_operation_kind_t::send_channel;
    state_ptr->channel_name = channel_name_;
    return send_operation_t (std::move (state_ptr));
}

request_operation_t spot_t::request_channel (const std::string &channel_name_)
{
    validate_channel_name (channel_name_);
    auto state_ptr = detail::acquire_state ();
    state_ptr->spot = this;
    state_ptr->kind = detail::spot_operation_kind_t::request_channel;
    state_ptr->channel_name = channel_name_;
    return request_operation_t (std::move (state_ptr));
}

spot_t::spot_t (spot_node_t &node_) : _impl (std::make_unique<impl> ())
{
    _impl->handle = zlink_spot_new (zlink::detail::native_handle (node_));
    if (!_impl->handle)
        _impl->last_error = errno != 0 ? errno : EFAULT;
}

spot_t::spot_t (native_handle_ctor_tag_t) noexcept : _impl (std::make_unique<impl> ())
{
}

void spot_t::set_routing_id (const routing_id_t &routing_id_)
{
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_set_routing_id (_impl->handle, routing_id_.data (), routing_id_.size ())));
}

void spot_t::get_routing_id (routing_id_t &out_) const
{
    zlink_routing_id_t native;
    std::memset (&native, 0, sizeof (native));
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_get_routing_id (_impl->handle, &native)));
    out_ = zlink::detail::native_routing_id (native);
}

routing_id_t spot_t::routing_id () const
{
    routing_id_t value = zlink::detail::unchecked_empty_routing_id ();
    get_routing_id (value);
    return value;
}

void spot_t::reply_to_spot (const routing_id_t &dest_node_rid_,
                            const routing_id_t &dest_spot_rid_,
                            uint64_t request_seq_,
                            message_t message_,
                            send_flags_t flags_)
{
    zlink::detail::throw_if_reply_flags_unsupported (flags_);
    submit_single_reply_message (message_, [&] (zlink_msg_t *part_out_) {
        return zlink_spot_reply_spot_part (_impl->handle,
                                           zlink::detail::routing_id_native (dest_node_rid_),
                                           zlink::detail::routing_id_native (dest_spot_rid_),
                                           request_seq_, part_out_, ZLINK_PART_FINAL);
    });
}

reply_operation_t spot_t::reply_to_spot (const routing_id_t &dest_node_rid_,
                                         const routing_id_t &dest_spot_rid_,
                                         uint64_t request_seq_)
{
    auto state_ptr = detail::acquire_state ();
    state_ptr->spot = this;
    state_ptr->kind = detail::spot_operation_kind_t::reply_to_spot;
    state_ptr->first_rid = dest_node_rid_;
    state_ptr->second_rid = dest_spot_rid_;
    state_ptr->request_seq = request_seq_;
    return reply_operation_t (std::move (state_ptr));
}

void spot_t::reply_to_router (const routing_id_t &peer_rid_,
                              uint64_t request_seq_,
                              message_t message_,
                              send_flags_t flags_)
{
    zlink::detail::throw_if_reply_flags_unsupported (flags_);
    submit_single_reply_message (message_, [&] (zlink_msg_t *part_out_) {
        return zlink_spot_reply_router_part (_impl->handle,
                                             zlink::detail::routing_id_native (peer_rid_),
                                             request_seq_, part_out_, ZLINK_PART_FINAL);
    });
}

reply_operation_t spot_t::reply_to_router (const routing_id_t &peer_rid_, uint64_t request_seq_)
{
    auto state_ptr = detail::acquire_state ();
    state_ptr->spot = this;
    state_ptr->kind = detail::spot_operation_kind_t::reply_to_router;
    state_ptr->first_rid = peer_rid_;
    state_ptr->request_seq = request_seq_;
    return reply_operation_t (std::move (state_ptr));
}

std::optional<received_t> spot_t::recv_routed_optional (recv_flags_t flags_)
{
    const zlink_routing_id_t *source_node_rid = nullptr;
    const zlink_routing_id_t *source_spot_rid = nullptr;
    uint64_t request_seq = 0;
    std::vector<zlink_msg_t> parts_native;
    for (;;) {
        parts_native.emplace_back ();
        zlink_msg_t &native_part = parts_native.back ();
        if (zlink_msg_init (&native_part) != 0) {
            parts_native.pop_back ();
            detail::close_native_parts (parts_native);
            throw last_error ();
        }

        const zlink_routing_id_t *part_source_node_rid = nullptr;
        const zlink_routing_id_t *part_source_spot_rid = nullptr;
        uint64_t part_request_seq = 0;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const recv_result_t rc = static_cast<recv_result_t> (zlink_spot_recv_part (
          _impl->handle, &part_source_node_rid, &part_source_spot_rid, &part_request_seq,
          &native_part, &has_more, static_cast<zlink_recv_flags_t> (static_cast<int> (flags_))));
        if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait
            && parts_native.size () == 1u) {
            (void) zlink_msg_close (&native_part);
            parts_native.pop_back ();
            return std::nullopt;
        }
        if (rc != recv_result_t::ok) {
            (void) zlink_msg_close (&native_part);
            parts_native.pop_back ();
            detail::close_native_parts (parts_native);
            throw recv_error_t (rc, zlink_errno ());
        }

        if (parts_native.size () == 1u) {
            source_node_rid = part_source_node_rid;
            source_spot_rid = part_source_spot_rid;
            request_seq = part_request_seq;
        }
        if (!has_more)
            break;
    }

    std::vector<message_t> parts;
    if (detail::assign_parts_from_native (parts_native, parts) != 0)
        throw last_error ();

    const bool has_node_rid = source_node_rid && source_node_rid->size > 0;
    const bool has_spot_rid = source_spot_rid && source_spot_rid->size > 0;

    received_t received = zlink::detail::received_access_t::make (
      has_node_rid
        ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_node_rid))
        : std::nullopt,
      has_spot_rid
        ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_spot_rid))
        : std::nullopt,
      request_seq != 0u ? std::optional<uint64_t> (request_seq) : std::nullopt, std::move (parts));

    // send()/reply() reconstruct the spot-spot native call lazily from the
    // stored routing ids and request sequence, so no per-receive closures are
    // built here. The spot send/reply path requires both routing ids.
    if (has_node_rid && has_spot_rid)
        zlink::detail::received_access_t::set_spot_spot_send_context (received, _impl->handle);

    return std::optional<received_t> (std::move (received));
}

int spot_t::recv_routed (received_t &out_, recv_flags_t flags_)
{
    try {
        std::optional<received_t> received = recv_routed_optional (flags_);
        if (!received.has_value ())
            return static_cast<int> (recv_result_t::no_data);
        out_ = std::move (*received);
        return static_cast<int> (recv_result_t::ok);
    }
    catch (const recv_error_t &err) {
        return static_cast<int> (err.result ());
    }
    catch (const binding_error_t &err) {
        errno = err.internal_errno () != 0 ? err.internal_errno () : EFAULT;
        return -1;
    }
    catch (...) {
        errno = EFAULT;
        return -1;
    }
}

void spot_t::set_dispatch_handler (
  std::function<void (spot_t &, const spot_dispatch_info_t &)> handler_)
{
    _impl->dispatch_event_handler = std::move (handler_);
    const handler_result_t rc = static_cast<handler_result_t> (zlink_spot_dispatch_event_handler (
      _impl->handle,
      [] (void *spot_, const zlink_spot_dispatch_info_t *info_, void *userdata_) {
          (void) spot_;
          spot_t *self = static_cast<spot_t *> (userdata_);
          if (!self || !self->_impl->dispatch_event_handler || !info_)
              return;
          const spot_dispatch_info_t info =
            zlink::detail::actor_model_access_t::from_native (*info_);
          self->_impl->dispatch_event_handler (*self, info);
      },
      this));
    if (rc != handler_result_t::ok)
        throw handler_error_t (rc, zlink_errno ());
}

void spot_t::set_dispatch_handler (std::function<void (const spot_dispatch_info_t &)> handler_)
{
    set_dispatch_handler (
      [handler = std::move (handler_)] (spot_t &, const spot_dispatch_info_t &info_) mutable {
          handler (info_);
      });
}

std::optional<spot_actor_lifecycle_event_t> spot_t::recv_actor_lifecycle (recv_flags_t flags_)
{
    zlink_spot_actor_lifecycle_event_t native_event;
    std::memset (&native_event, 0, sizeof (native_event));
    const recv_result_t rc = static_cast<recv_result_t> (zlink_spot_recv_actor_lifecycle (
      _impl->handle, &native_event, static_cast<zlink_recv_flags_t> (static_cast<int> (flags_))));
    if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
        return std::nullopt;
    if (rc != recv_result_t::ok)
        throw recv_error_t (rc, zlink_errno ());
    return std::optional<spot_actor_lifecycle_event_t> (
      zlink::detail::actor_model_access_t::from_native (native_event));
}

std::optional<actor_join_request_t> spot_t::recv_actor_join (recv_flags_t flags_)
{
    zlink_actor_join_info_t native_info;
    std::memset (&native_info, 0, sizeof (native_info));
    zlink_msg_t *parts = nullptr;
    size_t part_count = 0;
    const recv_result_t rc = static_cast<recv_result_t> (
      zlink_spot_actor_join_recv (_impl->handle, &native_info, &parts, &part_count,
                                  static_cast<zlink_recv_flags_t> (static_cast<int> (flags_))));
    if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
        return std::nullopt;
    if (rc != recv_result_t::ok)
        throw recv_error_t (rc, zlink_errno ());
    message_t message;
    if (part_count > 0) {
        if (zlink_msg_move (zlink::detail::native_handle (message), &parts[0]) != 0) {
            zlink_multipart_close (parts, part_count);
            throw last_error ();
        }
    }
    zlink_multipart_close (parts, part_count);
    return std::optional<actor_join_request_t> (actor_join_request_t (
      zlink::detail::actor_model_access_t::from_native (native_info), std::move (message)));
}

actor_join_reply_operation_t spot_t::reply_actor_join (const actor_join_request_t &request_,
                                                       int32_t join_result_code_)
{
    detail::actor_join_reply_state_t state;
    state.spot = _impl->handle;
    state.info = request_.info ();
    state.join_result_code = join_result_code_;
    return actor_join_reply_operation_t (std::move (state));
}

std::vector<actor_ref_t> spot_t::actors () const
{
    size_t count = 0;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_spot_actors (_impl->handle, nullptr, &count)));
    std::vector<zlink_actor_ref_t> native (count);
    if (count > 0) {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (zlink_spot_actors (_impl->handle, native.data (), &count)));
        native.resize (count);
    }
    std::vector<actor_ref_t> entries;
    entries.reserve (native.size ());
    for (size_t i = 0; i < native.size (); ++i)
        entries.push_back (zlink::detail::actor_model_access_t::from_native (native[i]));
    return entries;
}

void spot_t::close ()
{
    if (!_impl || !_impl->handle)
        return;

    void *tmp = _impl->handle;
    detail::throw_if_failed<close_error_t> (
      static_cast<close_result_t> (zlink_spot_destroy (&tmp)));
    _impl->handle = nullptr;
}

} // namespace service
} // namespace zlink
