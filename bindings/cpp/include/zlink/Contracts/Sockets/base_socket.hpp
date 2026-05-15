/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_BASE_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_BASE_SOCKET_HPP_INCLUDED

#include "../Core/context.hpp"
#include "../Messaging/message.hpp"
#include "../../Runtime/Native/socket_handle.hpp"
#include "../../Runtime/Native/native_parts.hpp"
#include "../Monitoring/monitor.hpp"
#include "../Core/types.hpp"

#include <cerrno>
#include <cstring>
#include <functional>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>


namespace zlink
{

namespace service
{
class discovery_t;
} // namespace service
namespace detail
{
inline void *native_handle (service::discovery_t &discovery_) noexcept;
inline const void *
native_handle (const service::discovery_t &discovery_) noexcept;
} // namespace detail


class base_socket_t : public socket_handle_t
{
  public:
    bool valid () const noexcept { return socket_handle_t::valid (); }

    void close ()
    {
        const int rc = socket_handle_t::close ();
        if (rc != 0)
            throw close_error_t (static_cast<close_result_t> (rc),
                                 zlink_errno ());
    }

    void bind (const std::string &endpoint_)
    {
        const int rc = zlink_bind (handle (), endpoint_.c_str ());
        if (rc != 0)
            throw bind_error_t (
              detail::bind_result_from_errno (zlink_errno ()), zlink_errno ());
    }

    void connect (const std::string &endpoint_)
    {
        const int rc = zlink_connect (handle (), endpoint_.c_str ());
        if (rc != 0)
            throw connect_error_t (
              detail::connect_result_from_errno (zlink_errno ()), zlink_errno ());
    }

    void unbind (const std::string &endpoint_)
    {
        const int rc = zlink_unbind (handle (), endpoint_.c_str ());
        if (rc != 0)
            throw connect_error_t (
              detail::connect_result_from_errno (zlink_errno ()), zlink_errno ());
    }

    void disconnect (const std::string &endpoint_)
    {
        const int rc = zlink_disconnect (handle (), endpoint_.c_str ());
        if (rc != 0)
            throw connect_error_t (
              detail::connect_result_from_errno (zlink_errno ()), zlink_errno ());
    }

    void disconnect_rid (const routing_id_t &peer_rid_)
    {
        const zlink_routing_id_t native =
          *zlink::detail::routing_id_native (peer_rid_);
        const int rc = zlink_disconnect_rid (handle (), &native);
        if (rc != 0)
            throw connect_error_t (
              static_cast<connect_result_t> (rc), zlink_errno ());
    }

    monitor_handle_t
    monitor_handle (monitor_event events_ = monitor_event::all) const
    {
        return monitor_handle_t::open (
          *this, events_);
    }

    common_socket_options_t options () { return common_socket_options_t (handle ()); }

    void set_tls_server (const std::string &cert_,
                         const std::string &key_,
                         bool require_client_cert_ = false)
    {
        const int rc = zlink_set_tls_server (
          handle (), cert_.c_str (), key_.c_str (),
          require_client_cert_ ? 1 : 0);
        if (rc != 0)
            throw config_error_t (
              detail::config_result_from_errno (zlink_errno ()), zlink_errno ());
    }

    void set_tls_client (const std::string &ca_cert_,
                         const std::string &hostname_,
                         bool trust_system_ = false)
    {
        const char *ca = ca_cert_.empty () ? NULL : ca_cert_.c_str ();
        const char *hostname =
          hostname_.empty () ? NULL : hostname_.c_str ();
        const int rc = zlink_set_tls_client (
          handle (), ca, hostname, trust_system_ ? 1 : 0);
        if (rc != 0)
            throw config_error_t (
              detail::config_result_from_errno (zlink_errno ()), zlink_errno ());
    }

  protected:
    template<typename DiscoveryT>
    ZLINK_CPP_NODISCARD int attach_discovery (DiscoveryT &discovery_)
    {
        return zlink_socket_attach_discovery (handle (), zlink::detail::native_handle (discovery_));
    }

    base_socket_t () noexcept {}

    base_socket_t (context_t &ctx_, socket_type type_)
        : socket_handle_t (
            zlink_socket (detail::native_handle (ctx_),
                          static_cast<zlink_socket_type_t> (type_)),
            true)
    {
    }

    ZLINK_CPP_NODISCARD int send (message_t &part_,
                                  send_flags_t flags_ = send_flags_t::none)
    {
        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native_part;
        detail::move_to_native(part_, &native_part);
        if (part_.valid ())
            return -1;

        const int rc = zlink_send_part (
          handle (), &native_part, static_cast<zlink_send_flags_t> (flags_),
          ZLINK_PART_FINAL);
        if (rc != 0) {
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (detail::native_handle(part_), &native_part);
            (void) zlink_msg_close (&native_part);
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int send (std::vector<message_t> &parts_,
                                  send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native_parts, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_send_part (
                handle (), part_out_, static_cast<zlink_send_flags_t> (flags_),
                part_flag_);
          });
        if (rc != 0)
            detail::restore_parts_from_native (parts_, native_parts, failed_index);
        return rc;
    }

    ZLINK_CPP_NODISCARD int send (const routing_id_t &target_rid_,
                                  message_t &part_,
                                  send_flags_t flags_ = send_flags_t::none)
    {
        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native_part;
        detail::move_to_native(part_, &native_part);
        if (part_.valid ())
            return -1;

        const int rc = zlink_send_part_rid (
          handle (), zlink::detail::routing_id_native (target_rid_), &native_part,
          static_cast<zlink_send_flags_t> (flags_), ZLINK_PART_FINAL);
        if (rc != 0) {
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (detail::native_handle(part_), &native_part);
            (void) zlink_msg_close (&native_part);
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    send (const routing_id_t &target_rid_,
          std::vector<message_t> &parts_,
          send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native_parts, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_send_part_rid (
                handle (), zlink::detail::routing_id_native (target_rid_), part_out_,
                static_cast<zlink_send_flags_t> (flags_), part_flag_);
          });
        if (rc != 0)
            detail::restore_parts_from_native (parts_, native_parts, failed_index);
        return rc;
    }

  protected:
    ZLINK_CPP_NODISCARD int send_no_wait_result (send_result_t &result_,
                                      message_t &part_)
    {
        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        // Direct send: pass the wrapper's native msg handle straight to
        // zlink. On success zlink takes ownership of the data; we only
        // drop the wrapper's valid flag. On failure zlink leaves the msg
        // untouched, so no rollback is needed and the caller can retry.
        const int rc = zlink_send_part (
          handle (), detail::native_handle (part_),
          ZLINK_DONTWAIT, ZLINK_PART_FINAL);
        if (rc == 0) {
            detail::mark_sent (part_);
            result_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_)) {
            return 0;
        }
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    send_no_wait_result (send_result_t &result_, std::vector<message_t> &parts_)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native_parts, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_send_part (
                handle (), part_out_, ZLINK_DONTWAIT, part_flag_);
          });
        if (rc == 0) {
            result_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_)) {
            if (result_ != send_result_t::sent)
                detail::restore_parts_from_native (parts_, native_parts, failed_index);
            return 0;
        }

        detail::restore_parts_from_native (parts_, native_parts, failed_index);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    send_no_wait_result (send_result_t &result_,
              const routing_id_t &target_rid_,
              message_t &part_)
    {
        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        // Direct send: pass the wrapper's native msg handle straight to
        // zlink. On success zlink takes ownership of the data; we only
        // drop the wrapper's valid flag. On failure zlink leaves the msg
        // untouched, so no rollback is needed and the caller can retry.
        const int rc = zlink_send_part_rid (
          handle (), zlink::detail::routing_id_native (target_rid_),
          detail::native_handle (part_),
          ZLINK_DONTWAIT, ZLINK_PART_FINAL);
        if (rc == 0) {
            detail::mark_sent (part_);
            result_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_)) {
            return 0;
        }
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    send_no_wait_result (send_result_t &result_,
              const routing_id_t &target_rid_,
              std::vector<message_t> &parts_)
    {
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native_parts, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_send_part_rid (
                handle (), zlink::detail::routing_id_native (target_rid_), part_out_,
                ZLINK_DONTWAIT, part_flag_);
          });
        if (rc == 0) {
            result_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_)) {
            if (result_ != send_result_t::sent)
                detail::restore_parts_from_native (parts_, native_parts, failed_index);
            return 0;
        }

        detail::restore_parts_from_native (parts_, native_parts, failed_index);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    receive (received_t &received_, recv_flags_t flags_ = recv_flags_t::none)
    {
        detail::recv_envelope_t envelope;
        const int rc = detail::recv_envelope (handle (), flags_, envelope);
        if (rc != 0)
            return rc;

        std::function<bool(std::vector<message_t> &, send_flags_t)> send_fn;
        if (!zlink::detail::routing_id_empty (envelope.source_rid)) {
            void *raw_handle = handle ();
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
                          part_out_, static_cast<zlink_send_flags_t> (flags_),
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

        if (envelope.parts.size () == 1u) {
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
              handle (), received_t::send_context_kind_t::socket_rid);
        return 0;
    }

    ZLINK_CPP_NODISCARD int publish (const std::string &topic_id_,
                                     message_t &part_,
                                     send_flags_t flags_ = send_flags_t::none)
    {
        detail::validate_no_embedded_null (topic_id_, "topic");
        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native_part;
        detail::move_to_native(part_, &native_part);
        if (part_.valid ())
            return -1;

        const int rc = zlink_publish_part (
          handle (), topic_id_.c_str (), &native_part,
          static_cast<zlink_send_flags_t> (flags_), ZLINK_PART_FINAL);
        if (rc != 0) {
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (detail::native_handle(part_), &native_part);
            (void) zlink_msg_close (&native_part);
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int publish (const std::string &topic_id_,
                                     std::vector<message_t> &parts_,
                                     send_flags_t flags_ = send_flags_t::none)
    {
        detail::validate_no_embedded_null (topic_id_, "topic");
        std::vector<zlink_msg_t> native_parts;
        if (detail::move_parts_to_native (parts_, native_parts) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native_parts, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_publish_part (
                handle (), topic_id_.c_str (), part_out_,
                static_cast<zlink_send_flags_t> (flags_), part_flag_);
          });
        if (rc != 0)
            detail::restore_parts_from_native (parts_, native_parts, failed_index);
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    publish_no_wait_result (send_result_t &result_,
                 const std::string &topic_id_,
                 message_t &part_)
    {
        detail::validate_no_embedded_null (topic_id_, "topic");
        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native_part;
        detail::move_to_native(part_, &native_part);
        if (part_.valid ())
            return -1;

        const int rc = zlink_publish_part (
          handle (), topic_id_.c_str (), &native_part, ZLINK_DONTWAIT,
          ZLINK_PART_FINAL);
        if (rc == 0) {
            result_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_)) {
            if (result_ != send_result_t::sent) {
                part_.init ();
                if (part_.valid ())
                    (void) zlink_msg_move (detail::native_handle(part_), &native_part);
                (void) zlink_msg_close (&native_part);
            }
            return 0;
        }

        part_.init ();
        if (part_.valid ())
            (void) zlink_msg_move (detail::native_handle(part_), &native_part);
        (void) zlink_msg_close (&native_part);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    publish_no_wait_result (send_result_t &result_,
                 const std::string &topic_id_,
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
                handle (), topic_id_.c_str (), part_out_, ZLINK_DONTWAIT,
                part_flag_);
          });
        if (rc == 0) {
            result_ = send_result_t::sent;
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_)) {
            if (result_ != send_result_t::sent)
                detail::restore_parts_from_native (parts_, native_parts, failed_index);
            return 0;
        }

        detail::restore_parts_from_native (parts_, native_parts, failed_index);
        errno = err;
        return -1;
    }

  public:
    ZLINK_CPP_NODISCARD int set_subscription (const std::string &filter_)
    {
        detail::validate_no_embedded_null (filter_, "filter");
        return zlink_set_subscription (handle (), filter_.c_str ());
    }

    ZLINK_CPP_NODISCARD int unset_subscription (const std::string &filter_)
    {
        detail::validate_no_embedded_null (filter_, "filter");
        return zlink_unset_subscription (handle (), filter_.c_str ());
    }

    ZLINK_CPP_NODISCARD int
    subscription_at (size_t index_, std::string &filter_, bool *is_pattern_ = NULL)
    {
        size_t cap = 256;
        const size_t max_cap = 64u * 1024u;
        while (cap <= max_cap) {
            std::vector<char> buffer (cap);
            size_t size = cap;
            int pattern = 0;
            const int rc = zlink_subscription_at (
              handle (), index_, buffer.data (), &size, &pattern);
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

    ZLINK_CPP_NODISCARD int
    subscribe (topic_message_t &message_, recv_flags_t flags_ = recv_flags_t::none)
    {
        char topic_buffer[256];
        size_t topic_size = sizeof (topic_buffer);
        const zlink_routing_id_t *source_rid = NULL;
        std::string topic;
        std::vector<message_t> parts;

        for (;;) {
            zlink_msg_t native_part;
            zlink_part_flag_t has_more = ZLINK_PART_FINAL;
            if (zlink_msg_init (&native_part) != 0)
                return -1;

            topic_size = sizeof (topic_buffer);
            const int rc = zlink_subscribe_part (
              handle (), &source_rid, topic_buffer, sizeof (topic_buffer),
              &topic_size, &native_part, &has_more,
              static_cast<zlink_recv_flags_t> (flags_));
            if (rc != ZLINK_RECV_OK) {
                (void) zlink_msg_close (&native_part);
                return static_cast<int> (rc);
            }

            if (parts.empty ())
                topic.assign (topic_buffer, topic_size);

            parts.emplace_back ();
            if (zlink_msg_move (detail::native_handle (parts.back ()),
                                &native_part)
                != 0) {
                (void) zlink_msg_close (&native_part);
                return -1;
            }
            (void) zlink_msg_close (&native_part);

            if (has_more == ZLINK_PART_FINAL)
                break;
        }

        message_ = topic_message_t (
          source_rid && source_rid->size > 0
            ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_rid))
            : std::nullopt,
          std::move (topic), std::move (parts));
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    subscribe (routing_id_t &source_rid_out_,
               std::string &topic_id_out_,
               std::vector<message_t> &parts_out_,
               recv_flags_t flags_ = recv_flags_t::none)
    {
        topic_message_t message;
        const int rc = subscribe (message, flags_);
        if (rc != 0)
            return rc;

        if (message.routing_id ())
            source_rid_out_ = *message.routing_id ();
        else
            source_rid_out_ = zlink::detail::unchecked_empty_routing_id ();
        topic_id_out_ = message.topic ();
        parts_out_ = std::move (message.parts ());
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    subscription_event (subscription_event_t &event_,
                        recv_flags_t flags_ = recv_flags_t::none)
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

    ZLINK_CPP_NODISCARD int
    subscription_event (routing_id_t &source_rid_out_,
                        bool &subscribed_out_,
                        std::string &topic_id_out_,
                        recv_flags_t flags_ = recv_flags_t::none)
    {
        zlink_msg_t part;
        if (zlink_msg_init (&part) != 0)
            return -1;

        const zlink_routing_id_t *source_rid = NULL;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const int rc = zlink_recv_part (
          handle (), &source_rid, &part, &has_more,
          static_cast<zlink_recv_flags_t> (flags_));
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

    void on_send_ready (std::function<void()> handler_)
    {
        _send_ready_handler = std::move (handler_);
        if (on_send_ready_native (&base_socket_t::send_ready_trampoline, this)
            != 0)
            detail::throw_if_failed<handler_error_t> (
              static_cast<handler_result_t> (
                detail::handler_result_from_errno (zlink_errno ())));
    }

  protected:
    ZLINK_CPP_NODISCARD int
    on_send_ready_native (zlink_send_ready_handler_fn handler_,
                          void *userdata_ = NULL)
    {
        return zlink_send_ready_handler (handle (), handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int on_receive (zlink_socket_msg_handler_fn handler_,
                                        void *userdata_ = NULL)
    {
        return zlink_recv_handler (handle (), handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    on_packet (zlink_stream_packet_handler_fn handler_, void *userdata_ = NULL)
    {
        return zlink_stream_packet_handler (handle (), handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    set_option (compat::options::socket_option option_, const void *value_, size_t size_)
    {
        return zlink_set_option (
          handle (), static_cast<zlink_option_t> (option_), value_, size_);
    }

    static void send_ready_trampoline (void *, void *userdata_)
    {
        base_socket_t *self = static_cast<base_socket_t *> (userdata_);
        if (self && self->_send_ready_handler)
            self->_send_ready_handler ();
    }

    std::function<void()> _send_ready_handler;

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set_option (compat::options::socket_option option_, const T &value_)
    {
        return set_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_option (compat::options::socket_option option_, const std::string &value_)
    {
        if (!detail::is_common_string_option (option_)) {
            errno = EINVAL;
            return -1;
        }
        return set_option (option_, value_.data (), value_.size ());
    }

    ZLINK_CPP_NODISCARD int
    get_option (compat::options::socket_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    get_option (compat::options::socket_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_option (option_, value_, &size);
    }

    ZLINK_CPP_NODISCARD int
    get_option (compat::options::socket_option option_, std::string &value_) const
    {
        return detail::get_string_option (
          [](void *socket_, compat::options::socket_option option_, void *value_, size_t *size_) {
              return zlink_get_option (
                socket_, static_cast<zlink_option_t> (option_), value_, size_);
          },
          const_cast<void *> (handle ()), option_,
          option_ == compat::options::socket_option::last_endpoint ? 1024u : 512u, value_);
    }

    ZLINK_CPP_NODISCARD int set_routing_id_raw (const void *data_, size_t size_)
    {
        return zlink_set_routing_id (handle (), data_, size_);
    }

    ZLINK_CPP_NODISCARD int set_routing_id_raw (const std::string &routing_id_)
    {
        return set_routing_id_raw (routing_id_.data (), routing_id_.size ());
    }

    ZLINK_CPP_NODISCARD int
    get_routing_id_raw (routing_id_t &routing_id_) const
    {
        return zlink_get_routing_id (
          const_cast<void *> (handle ()), zlink::detail::routing_id_native (routing_id_));
    }

    ZLINK_CPP_NODISCARD int get_routing_id_raw (std::string &routing_id_) const
    {
        routing_id_t native_rid = zlink::detail::unchecked_empty_routing_id ();
        if (get_routing_id_raw (native_rid) != 0)
            return -1;
        routing_id_ = native_rid.to_string ();
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    set_router_option (compat::options::router_option option_, const void *value_, size_t size_)
    {
        return zlink_set_router_option (
          handle (), static_cast<zlink_router_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_router_option (compat::options::router_option option_,
                                               const T &value_)
    {
        return set_router_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_router_option (compat::options::router_option_key_t<std::string> key_,
                       const std::string &value_)
    {
        return set_router_option (key_.option, value_.data (), value_.size ());
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_router_option (compat::options::router_option_key_t<T> key_,
                                               const T &value_)
    {
        return set_router_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_router_option (compat::options::router_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_router_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_router_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_router_option (compat::options::router_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_router_option (option_, value_, &size);
    }

    ZLINK_CPP_NODISCARD int
    get_router_option (compat::options::router_option option_, std::string &value_) const
    {
        return detail::get_string_option (
          zlink_get_router_option, const_cast<void *> (handle ()),
          static_cast<zlink_router_option_t> (option_), 256u, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_router_option (compat::options::router_option_key_t<std::string> key_,
                       std::string &value_) const
    {
        return get_router_option (key_.option, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_router_option (compat::options::router_option_key_t<T> key_, T *value_) const
    {
        return get_router_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_dealer_option (compat::options::dealer_option option_, const void *value_, size_t size_)
    {
        return zlink_set_dealer_option (
          handle (), static_cast<zlink_dealer_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_dealer_option (compat::options::dealer_option option_,
                                               const T &value_)
    {
        return set_dealer_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_dealer_option (compat::options::dealer_option_key_t<std::string> key_,
                       const std::string &value_)
    {
        return set_dealer_option (key_.option, value_.data (), value_.size ());
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_dealer_option (compat::options::dealer_option_key_t<T> key_,
                                               const T &value_)
    {
        return set_dealer_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_pub_option (compat::options::pub_option option_, const void *value_, size_t size_)
    {
        return zlink_set_pub_option (
          handle (), static_cast<zlink_pub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_pub_option (compat::options::pub_option option_,
                                            const T &value_)
    {
        return set_pub_option (option_, &value_, sizeof (value_));
    }

    ZLINK_CPP_NODISCARD int
    set_pub_option (compat::options::pub_option_key_t<std::string> key_,
                    const std::string &value_)
    {
        return set_pub_option (key_.option, value_.data (), value_.size ());
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_pub_option (compat::options::pub_option_key_t<T> key_,
                                            const T &value_)
    {
        return set_pub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_pub_option (compat::options::pub_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_pub_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_pub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_pub_option (compat::options::pub_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_pub_option (option_, value_, &size);
    }

    ZLINK_CPP_NODISCARD int
    get_pub_option (compat::options::pub_option option_, std::string &value_) const
    {
        return detail::get_string_option (
          zlink_get_pub_option, const_cast<void *> (handle ()),
          static_cast<zlink_pub_option_t> (option_), 256u, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_pub_option (compat::options::pub_option_key_t<std::string> key_,
                    std::string &value_) const
    {
        return get_pub_option (key_.option, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_pub_option (compat::options::pub_option_key_t<T> key_,
                                            T *value_) const
    {
        return get_pub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_sub_option (compat::options::sub_option option_, const void *value_, size_t size_)
    {
        return zlink_set_sub_option (
          handle (), static_cast<zlink_sub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_sub_option (compat::options::sub_option option_, const T &value_)
    {
        return set_sub_option (option_, &value_, sizeof (value_));
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_sub_option (compat::options::sub_option_key_t<T> key_,
                                            const T &value_)
    {
        return set_sub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_sub_option (compat::options::sub_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_sub_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_sub_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_sub_option (compat::options::sub_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_sub_option (option_, value_, &size);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_sub_option (compat::options::sub_option_key_t<T> key_,
                                            T *value_) const
    {
        return get_sub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_stream_option (compat::options::stream_option option_, const void *value_, size_t size_)
    {
        return zlink_set_stream_option (
          handle (), static_cast<zlink_stream_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_stream_option (compat::options::stream_option option_,
                                               const T &value_)
    {
        return set_stream_option (option_, &value_, sizeof (value_));
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_stream_option (compat::options::stream_option_key_t<T> key_,
                                               const T &value_)
    {
        return set_stream_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_stream_option (compat::options::stream_option option_, void *value_, size_t *size_) const
    {
        return zlink_get_stream_option (
          const_cast<void *> (handle ()),
          static_cast<zlink_stream_option_t> (option_), value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_stream_option (compat::options::stream_option option_, T *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }
        size_t size = sizeof (*value_);
        return get_stream_option (option_, value_, &size);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_stream_option (compat::options::stream_option_key_t<T> key_, T *value_) const
    {
        return get_stream_option (key_.option, value_);
    }
};

} // namespace zlink

#endif
