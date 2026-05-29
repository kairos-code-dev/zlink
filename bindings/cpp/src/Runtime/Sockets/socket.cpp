/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Sockets/socket_contracts.hpp>

#include <Runtime/Native/native_parts.hpp>
#include <Runtime/Native/socket_handle.hpp>
#include <Runtime/Core/context_access.hpp>
#include <Runtime/Service/discovery_access.hpp>
#include <Runtime/Sockets/socket_access.hpp>

namespace zlink
{

namespace detail
{

void *socket_access_t::native_handle (socket_t &socket_) noexcept
{
    return socket_._socket ? detail::native_handle (*socket_._socket) : NULL;
}

const void *
socket_access_t::native_handle (const socket_t &socket_) noexcept
{
    return socket_._socket ? detail::native_handle (*socket_._socket) : NULL;
}

routing_id_t routing_id_from_native_pointer (const void *native_) noexcept
{
    const zlink_routing_id_t *rid =
      static_cast<const zlink_routing_id_t *> (native_);
    return (rid && rid->size > 0) ? native_routing_id (*rid)
                                  : unchecked_empty_routing_id ();
}

} // namespace detail

socket_t::~socket_t () = default;

socket_t::socket_t (socket_t &&) noexcept = default;

socket_t &
socket_t::operator= (socket_t &&) noexcept = default;

bool socket_t::valid () const noexcept
{
    return _socket && _socket->valid ();
}

void socket_t::close ()
{
    const int rc = _socket ? _socket->close () : 0;
    if (rc != 0)
        throw close_error_t (static_cast<close_result_t> (rc), zlink_errno ());
}

void socket_t::bind (const std::string &endpoint_)
{
    const int rc = zlink_bind (detail::native_handle (*this), endpoint_.c_str ());
    if (rc != 0)
        throw bind_error_t (
          detail::bind_result_from_errno (zlink_errno ()), zlink_errno ());
}

void socket_t::connect (const std::string &endpoint_)
{
    const int rc = zlink_connect (detail::native_handle (*this), endpoint_.c_str ());
    if (rc != 0)
        throw connect_error_t (
          detail::connect_result_from_errno (zlink_errno ()), zlink_errno ());
}

void socket_t::unbind (const std::string &endpoint_)
{
    const int rc = zlink_unbind (detail::native_handle (*this), endpoint_.c_str ());
    if (rc != 0)
        throw connect_error_t (
          detail::connect_result_from_errno (zlink_errno ()), zlink_errno ());
}

void socket_t::disconnect (const std::string &endpoint_)
{
    const int rc = zlink_disconnect (detail::native_handle (*this), endpoint_.c_str ());
    if (rc != 0)
        throw connect_error_t (
          detail::connect_result_from_errno (zlink_errno ()), zlink_errno ());
}

void socket_t::disconnect_rid (const routing_id_t &peer_rid_)
{
    const zlink_routing_id_t native =
      *zlink::detail::routing_id_native (peer_rid_);
    const int rc = zlink_disconnect_rid (detail::native_handle (*this), &native);
    if (rc != 0)
        throw connect_error_t (
          static_cast<connect_result_t> (rc), zlink_errno ());
}

void socket_t::set_tls_server (const std::string &cert_,
                                    const std::string &key_,
                                    bool require_client_cert_)
{
    const int rc = zlink_set_tls_server (
      detail::native_handle (*this), cert_.c_str (), key_.c_str (), require_client_cert_ ? 1 : 0);
    if (rc != 0)
        throw config_error_t (
          detail::config_result_from_errno (zlink_errno ()), zlink_errno ());
}

void socket_t::set_tls_client (const std::string &ca_cert_,
                                    const std::string &hostname_,
                                    bool trust_system_)
{
    const char *ca = ca_cert_.empty () ? NULL : ca_cert_.c_str ();
    const char *hostname = hostname_.empty () ? NULL : hostname_.c_str ();
    const int rc = zlink_set_tls_client (
      detail::native_handle (*this), ca, hostname, trust_system_ ? 1 : 0);
    if (rc != 0)
        throw config_error_t (
          detail::config_result_from_errno (zlink_errno ()), zlink_errno ());
}

int socket_t::attach_discovery (service::discovery_t &discovery_)
{
    return zlink_socket_attach_discovery (
      detail::native_handle (*this), zlink::detail::native_handle (discovery_));
}

socket_t::socket_t () noexcept
    : _socket (new detail::socket_handle_t ())
{
}

socket_t::socket_t (context_t &ctx_, socket_type type_)
    : _socket (new detail::socket_handle_t (
        zlink_socket (detail::native_handle (ctx_),
                      static_cast<zlink_socket_type_t> (type_)),
        true))
{
}

int socket_t::send (message_t &part_, send_flags_t flags_)
{
    return detail::submit_one_message_part (
      part_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
          return zlink_send_part (
            detail::native_handle (*this), part_out_, static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            part_flag_);
      });
}

int socket_t::send (std::vector<message_t> &parts_, send_flags_t flags_)
{
    return detail::submit_message_parts (
      parts_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return zlink_send_part (
            detail::native_handle (*this), part_out_, static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            part_flag_);
      });
}

int socket_t::send (const routing_id_t &target_rid_, message_t &part_,
                         send_flags_t flags_)
{
    return detail::submit_one_message_part (
      part_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
          return zlink_send_part_rid (
            detail::native_handle (*this), zlink::detail::routing_id_native (target_rid_),
            part_out_, static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_);
      });
}

int socket_t::send (const routing_id_t &target_rid_,
                         std::vector<message_t> &parts_,
                         send_flags_t flags_)
{
    return detail::submit_message_parts (
      parts_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return zlink_send_part_rid (
            detail::native_handle (*this), zlink::detail::routing_id_native (target_rid_),
            part_out_, static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_);
      });
}

int socket_t::send_no_wait_result (send_result_t &result_,
                                        message_t &part_)
{
    if (!part_.valid ()) {
        errno = EINVAL;
        return -1;
    }

    const int rc = zlink_send_part (
      detail::native_handle (*this), detail::native_handle (part_), ZLINK_DONTWAIT,
      ZLINK_PART_FINAL);
    if (rc == 0) {
        detail::mark_sent (part_);
        result_ = send_result_t::sent;
        return 0;
    }

    const int err = errno;
    if (detail::classify_nonblocking_send_errno (err, result_))
        return 0;
    errno = err;
    return -1;
}

int socket_t::send_no_wait_result (
  send_result_t &result_, std::vector<message_t> &parts_)
{
    return detail::submit_message_parts_no_wait (
      result_, parts_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return zlink_send_part (
            detail::native_handle (*this), part_out_, ZLINK_DONTWAIT, part_flag_);
      });
}

int socket_t::send_no_wait_result (
  send_result_t &result_, const routing_id_t &target_rid_, message_t &part_)
{
    if (!part_.valid ()) {
        errno = EINVAL;
        return -1;
    }

    const int rc = zlink_send_part_rid (
      detail::native_handle (*this), zlink::detail::routing_id_native (target_rid_),
      detail::native_handle (part_), ZLINK_DONTWAIT, ZLINK_PART_FINAL);
    if (rc == 0) {
        detail::mark_sent (part_);
        result_ = send_result_t::sent;
        return 0;
    }

    const int err = errno;
    if (detail::classify_nonblocking_send_errno (err, result_))
        return 0;
    errno = err;
    return -1;
}

int socket_t::send_no_wait_result (
  send_result_t &result_, const routing_id_t &target_rid_,
  std::vector<message_t> &parts_)
{
    return detail::submit_message_parts_no_wait (
      result_, parts_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return zlink_send_part_rid (
            detail::native_handle (*this), zlink::detail::routing_id_native (target_rid_),
            part_out_, ZLINK_DONTWAIT, part_flag_);
      });
}

int socket_t::receive (received_t &received_, recv_flags_t flags_)
{
    detail::recv_envelope_t envelope;
    const int rc = detail::recv_envelope (detail::native_handle (*this), flags_, envelope);
    if (rc != 0)
        return rc;

    std::function<bool(std::vector<message_t> &, send_flags_t)> send_fn;
    if (!zlink::detail::routing_id_empty (envelope.source_rid)) {
        void *raw_handle = detail::native_handle (*this);
        const routing_id_t send_rid (envelope.source_rid);
        send_fn = [raw_handle, send_rid] (
                    std::vector<message_t> &send_parts_,
                    send_flags_t flags_) {
            std::vector<zlink_msg_t> native;
            if (detail::move_parts_to_native (send_parts_, native) != 0)
                throw last_error ();
            size_t failed_index = 0;
            const submit_result_t result = static_cast<submit_result_t> (
              detail::submit_native_parts (
                native, failed_index,
                [&] (zlink_msg_t *part_out_,
                     zlink_part_flag_t part_flag_, bool) {
                    return zlink_send_part_rid (
                      raw_handle, zlink::detail::routing_id_native (send_rid),
                      part_out_, static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
                      part_flag_);
                }));
            if (result == submit_result_t::ok)
                return true;
            detail::restore_parts_from_native (
              send_parts_, native, failed_index);
            if (flags_ == send_flags_t::dontwait
                && result == submit_result_t::backpressured)
                return false;
            throw submit_error_t (result, zlink_errno ());
        };
    }

    const std::optional<routing_id_t> source_rid =
      zlink::detail::routing_id_empty (envelope.source_rid)
        ? std::nullopt
        : std::optional<routing_id_t> (envelope.source_rid);
    const std::optional<routing_id_t> source_spot_rid =
      zlink::detail::routing_id_empty (envelope.source_spot_rid)
        ? std::nullopt
        : std::optional<routing_id_t> (envelope.source_spot_rid);
    const std::optional<uint64_t> request_seq =
      envelope.has_request_seq ? std::optional<uint64_t> (envelope.request_seq)
                               : std::nullopt;

    if (envelope.single_part.has_value ()) {
        received_ = received_t (
          source_rid, source_spot_rid, request_seq,
          std::move (*envelope.single_part),
          std::function<void(std::vector<message_t> &, send_flags_t)> (),
          std::move (send_fn));
    } else if (envelope.parts.size () == 1u) {
        received_ = received_t (
          source_rid, source_spot_rid, request_seq,
          std::move (envelope.parts[0]),
          std::function<void(std::vector<message_t> &, send_flags_t)> (),
          std::move (send_fn));
    } else {
        received_ = received_t (
          source_rid, source_spot_rid, request_seq,
          std::move (envelope.parts),
          std::function<void(std::vector<message_t> &, send_flags_t)> (),
          std::move (send_fn));
    }
    if (source_rid.has_value () && !source_spot_rid.has_value ())
        received_.set_send_context (
          reinterpret_cast<std::uintptr_t> (detail::native_handle (*this)),
          received_t::send_context_kind_t::socket_rid);
    return 0;
}

int socket_t::publish (const std::string &topic_id_, message_t &part_,
                            send_flags_t flags_)
{
    detail::validate_no_embedded_null (topic_id_, "topic");
    return detail::submit_one_message_part (
      part_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
          return zlink_publish_part (
            detail::native_handle (*this), topic_id_.c_str (), part_out_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_);
      });
}

int socket_t::publish (const std::string &topic_id_,
                            std::vector<message_t> &parts_,
                            send_flags_t flags_)
{
    detail::validate_no_embedded_null (topic_id_, "topic");
    return detail::submit_message_parts (
      parts_,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return zlink_publish_part (
            detail::native_handle (*this), topic_id_.c_str (), part_out_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_);
      });
}

int socket_t::publish_no_wait_result (
  send_result_t &result_, const std::string &topic_id_, message_t &part_)
{
    detail::validate_no_embedded_null (topic_id_, "topic");
    if (!part_.valid ()) {
        errno = EINVAL;
        return -1;
    }

    const int rc = zlink_publish_part (
      detail::native_handle (*this), topic_id_.c_str (), detail::native_handle (part_),
      ZLINK_DONTWAIT, ZLINK_PART_FINAL);
    if (rc == 0) {
        detail::mark_sent (part_);
        result_ = send_result_t::sent;
        return 0;
    }

    const int err = errno;
    if (detail::classify_nonblocking_send_errno (err, result_))
        return 0;

    errno = err;
    return -1;
}

int socket_t::publish_no_wait_result (
  send_result_t &result_, const std::string &topic_id_,
  std::vector<message_t> &parts_)
{
    std::vector<zlink_msg_t> native_parts;
    if (detail::move_parts_to_native (parts_, native_parts) != 0)
        return -1;

    size_t failed_index = 0;
    const int rc = detail::submit_native_parts (
      native_parts, failed_index,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return zlink_publish_part (
            detail::native_handle (*this), topic_id_.c_str (), part_out_, ZLINK_DONTWAIT,
            part_flag_);
      });
    if (rc == 0) {
        result_ = send_result_t::sent;
        return 0;
    }

    const int err = errno;
    if (detail::classify_nonblocking_send_errno (err, result_)) {
        if (result_ != send_result_t::sent)
            detail::restore_parts_from_native (
              parts_, native_parts, failed_index);
        return 0;
    }

    detail::restore_parts_from_native (parts_, native_parts, failed_index);
    errno = err;
    return -1;
}

int socket_t::subscribe (topic_message_t &message_, recv_flags_t flags_)
{
    char topic_buffer[256];
    size_t topic_size = sizeof (topic_buffer);
    const zlink_routing_id_t *source_rid = NULL;
    std::string topic;
    message_t first_part;

    for (;;) {
        zlink_msg_t native_part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        if (zlink_msg_init (&native_part) != 0)
            return -1;

        topic_size = sizeof (topic_buffer);
        const int rc = zlink_subscribe_part (
          detail::native_handle (*this), &source_rid, topic_buffer, sizeof (topic_buffer),
          &topic_size, &native_part, &has_more,
          static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
        if (rc != ZLINK_RECV_OK) {
            (void) zlink_msg_close (&native_part);
            return static_cast<int> (rc);
        }

        topic.assign (topic_buffer, topic_size);

        if (zlink_msg_move (detail::native_handle (first_part), &native_part)
            != 0) {
            (void) zlink_msg_close (&native_part);
            return -1;
        }
        (void) zlink_msg_close (&native_part);

        if (has_more == ZLINK_PART_FINAL) {
            message_ = topic_message_t (
              source_rid && source_rid->size > 0
                ? std::optional<routing_id_t> (
                    zlink::detail::native_routing_id (*source_rid))
                : std::nullopt,
              std::move (topic), std::move (first_part));
            break;
        }

        std::vector<message_t> parts;
        parts.push_back (std::move (first_part));

        for (;;) {
            if (zlink_msg_init (&native_part) != 0)
                return -1;

            topic_size = sizeof (topic_buffer);
            const int more_rc = zlink_subscribe_part (
              detail::native_handle (*this), &source_rid, topic_buffer, sizeof (topic_buffer),
              &topic_size, &native_part, &has_more,
              static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
            if (more_rc != ZLINK_RECV_OK) {
                (void) zlink_msg_close (&native_part);
                return static_cast<int> (more_rc);
            }

            parts.emplace_back ();
            if (zlink_msg_move (
                  detail::native_handle (parts.back ()), &native_part)
                != 0) {
                (void) zlink_msg_close (&native_part);
                return -1;
            }
            (void) zlink_msg_close (&native_part);

            if (has_more == ZLINK_PART_FINAL) {
                message_ = topic_message_t (
                  source_rid && source_rid->size > 0
                    ? std::optional<routing_id_t> (
                        zlink::detail::native_routing_id (*source_rid))
                    : std::nullopt,
                  std::move (topic), std::move (parts));
                break;
            }
        }
        break;
    }

    return 0;
}

int socket_t::subscribe_part (
  std::optional<routing_id_t> &source_rid_out_, std::string &topic_out_,
  message_t &part_out_, bool &has_more_out_, recv_flags_t flags_)
{
    char topic_buffer[256];
    size_t topic_size = sizeof (topic_buffer);
    const zlink_routing_id_t *source_rid = NULL;
    zlink_msg_t native_part;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    if (zlink_msg_init (&native_part) != 0)
        return -1;

    const int rc = zlink_subscribe_part (
      detail::native_handle (*this), &source_rid, topic_buffer, sizeof (topic_buffer),
      &topic_size, &native_part, &has_more,
      static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
    if (rc != ZLINK_RECV_OK) {
        (void) zlink_msg_close (&native_part);
        return static_cast<int> (rc);
    }

    source_rid_out_ =
      source_rid && source_rid->size > 0
        ? std::optional<routing_id_t> (
            zlink::detail::native_routing_id (*source_rid))
        : std::nullopt;
    topic_out_.assign (topic_buffer, topic_size);
    zlink::detail::adopt_native_message (part_out_, &native_part);
    has_more_out_ = has_more != ZLINK_PART_FINAL;
    return 0;
}

int socket_t::subscription_event (
  routing_id_t &source_rid_out_, bool &subscribed_out_,
  std::string &topic_id_out_, recv_flags_t flags_)
{
    zlink_msg_t part;
    if (zlink_msg_init (&part) != 0)
        return -1;

    const zlink_routing_id_t *source_rid = NULL;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const int rc = zlink_recv_part (
      detail::native_handle (*this), &source_rid, &part, &has_more,
      static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
    if (rc != 0) {
        (void) zlink_msg_close (&part);
        return rc;
    }

    if (source_rid && source_rid->size > 0)
        source_rid_out_ = zlink::detail::native_routing_id (*source_rid);
    else
        source_rid_out_ = zlink::detail::unchecked_empty_routing_id ();

    const unsigned char *data =
      static_cast<const unsigned char *> (zlink_msg_data (&part));
    const size_t size = zlink_msg_size (&part);
    if (has_more) {
        (void) zlink_msg_close (&part);
        errno = EMSGSIZE;
        return -1;
    }

    subscribed_out_ = size > 0 && data[0] != 0;
    topic_id_out_.assign (
      size > 1 ? reinterpret_cast<const char *> (data + 1) : "",
      size > 0 ? size - 1 : 0);
    (void) zlink_msg_close (&part);
    return 0;
}

int socket_t::set_subscription (const std::string &filter_)
{
    detail::validate_no_embedded_null (filter_, "filter");
    return zlink_set_subscription (detail::native_handle (*this), filter_.c_str ());
}

int socket_t::unset_subscription (const std::string &filter_)
{
    detail::validate_no_embedded_null (filter_, "filter");
    return zlink_unset_subscription (detail::native_handle (*this), filter_.c_str ());
}

int socket_t::subscription_at (size_t index_,
                                    std::string &filter_,
                                    bool *is_pattern_)
{
    size_t cap = 256;
    const size_t max_cap = 64u * 1024u;
    while (cap <= max_cap) {
        std::vector<char> buffer (cap);
        size_t size = cap;
        int pattern = 0;
        const int rc = zlink_subscription_at (
          detail::native_handle (*this), index_, buffer.data (), &size, &pattern);
        if (rc == 0) {
            const size_t bounded = size <= buffer.size () ? size : buffer.size ();
            filter_.assign (buffer.data (), bounded);
            if (is_pattern_)
                *is_pattern_ = pattern != 0;
            return 0;
        }

        if (errno != EINVAL || cap == max_cap)
            return -1;

        cap *= 2u;
        if (cap > max_cap)
            cap = max_cap;
    }

    errno = EINVAL;
    return -1;
}

int socket_t::subscription_event (subscription_event_t &event_,
                                       recv_flags_t flags_)
{
    event_.routing_id = std::nullopt;
    event_.topic.clear ();
    event_.subscribed = false;
    routing_id_t source_rid = zlink::detail::unchecked_empty_routing_id ();
    const int rc = subscription_event (
      source_rid, event_.subscribed, event_.topic, flags_);
    if (rc != 0)
        return rc;
    if (!zlink::detail::routing_id_empty (source_rid))
        event_.routing_id = source_rid;
    return 0;
}

void socket_t::set_send_ready_handler (std::function<void()> handler_)
{
    _send_ready_handler = std::move (handler_);
    auto trampoline = [] (void *, void *userdata_) {
        socket_t *self = static_cast<socket_t *> (userdata_);
        if (self && self->_send_ready_handler)
            self->_send_ready_handler ();
    };
    if (zlink_send_ready_handler (
          detail::native_handle (*this),
          static_cast<zlink_send_ready_handler_fn> (+trampoline), this)
        != 0)
        detail::throw_if_failed<handler_error_t> (
          static_cast<handler_result_t> (
            detail::handler_result_from_errno (zlink_errno ())));
}

int socket_t::set_routing_id_raw (std::span<const std::byte> data_)
{
    return zlink_set_routing_id (
      detail::native_handle (*this), data_.data (), data_.size ());
}

int socket_t::get_routing_id_raw (routing_id_t &routing_id_) const
{
    zlink_routing_id_t native;
    std::memset (&native, 0, sizeof (native));
    const int rc = zlink_get_routing_id (const_cast<void *> (detail::native_handle (*this)), &native);
    if (rc == 0)
        zlink::detail::assign_routing_id_native (routing_id_, native);
    return rc;
}

} // namespace zlink
