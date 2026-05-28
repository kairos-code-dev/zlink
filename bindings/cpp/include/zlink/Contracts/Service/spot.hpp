/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_SPOT_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_SPOT_HPP_INCLUDED

#define ZLINK_CPP_SERVICES_SPOT_NODE_NO_SPOT_INCLUDE
#include "spot_node.hpp"
#undef ZLINK_CPP_SERVICES_SPOT_NODE_NO_SPOT_INCLUDE
#include "../Sockets/pair.hpp"
#include "../Sockets/dealer.hpp"
#include "../Sockets/router.hpp"
#include "../Sockets/stream.hpp"
#include "../Sockets/pub.hpp"
#include "../Sockets/xpub.hpp"
#include "actor_ops.hpp"
#include "../../Runtime/Service/spot_submit.hpp"

namespace zlink
{
namespace service
{

class spot_t
{
  public:
    ~spot_t ()
    {
        try {
            close ();
        } catch (...) {
        }
    }

    spot_t (spot_t &&other) noexcept
        : _spot (other._spot), _last_error (other._last_error),
          _default_request_timeout (other._default_request_timeout)
    {
        other._spot = NULL;
        other._last_error = 0;
        other._default_request_timeout = std::chrono::milliseconds ();
    }

    spot_t &operator= (spot_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        try {
            close ();
        } catch (...) {
        }
        _spot = other._spot;
        _last_error = other._last_error;
        _default_request_timeout = other._default_request_timeout;
        other._spot = NULL;
        other._last_error = 0;
        other._default_request_timeout = std::chrono::milliseconds ();
        return *this;
    }

    spot_t (const spot_t &) = delete;
    spot_t &operator= (const spot_t &) = delete;

    bool valid () const noexcept { return _spot != NULL; }

    void request_timeout (std::chrono::milliseconds timeout_)
    {
        const int value = static_cast<int> (timeout_.count ());
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_spot_option (
              _spot, ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS, &value,
              sizeof (value))));
        _default_request_timeout = timeout_;
    }

    std::chrono::milliseconds request_timeout () const
    {
        int value = 0;
        size_t size = sizeof (value);
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_get_spot_option (
              _spot, ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS, &value, &size)));
        return std::chrono::milliseconds (value);
    }

    send_op_t publish (const std::string &topic_)
    {
        zlink::detail::validate_no_embedded_null (topic_, "topic");
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::publish;
        state.topic = topic_;
        return send_op_t (std::move (state));
    }

    send_op_t send_channel (const std::string &channel_name_)
    {
        validate_channel_name (channel_name_);
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::send_channel;
        state.channel_name = channel_name_;
        return send_op_t (std::move (state));
    }

    request_op_t request_channel (const std::string &channel_name_)
    {
        validate_channel_name (channel_name_);
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::request_channel;
        state.channel_name = channel_name_;
        return request_op_t (std::move (state));
    }

  private:
    bool publish (const std::string &topic_,
                  std::vector<message_t> &parts_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        const int rc = publish_impl (topic_.c_str (), parts_, flags_);
        if (rc != 0)
        {
            if (flags_ == send_flags_t::dontwait
                && static_cast<submit_result_t> (rc)
                    == submit_result_t::backpressured)
                return false;
            throw submit_error_t (
              static_cast<submit_result_t> (rc), zlink_errno ());
        }
        return true;
    }

    bool publish (const std::string &topic_,
                  message_t &part_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        if (flags_ == send_flags_t::dontwait) {
            send_result_t result = send_result_t::sent;
            if (publish_no_wait_result_impl (
                  result, topic_.c_str (), part_)
                != 0) {
                const int err = zlink_errno ();
                throw submit_error_t (
                  zlink::detail::submit_result_from_errno (err), err);
            }
            if (result == send_result_t::not_ready)
                throw submit_error_t (
                  submit_result_t::not_connected, zlink_errno ());
            return result == send_result_t::sent;
        }

        const int rc = publish_impl (topic_.c_str (), part_, flags_);
        if (rc != 0)
        {
            if (flags_ == send_flags_t::dontwait
                && static_cast<submit_result_t> (rc)
                    == submit_result_t::backpressured)
                return false;
            throw submit_error_t (
              static_cast<submit_result_t> (rc), zlink_errno ());
        }
        return true;
    }

    bool publish_discard_on_backpressure (const std::string &topic_,
                                          message_t &part_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
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

        const int rc = zlink_spot_publish_part (
          _spot, topic_.c_str (), &native, ZLINK_DONTWAIT, ZLINK_PART_FINAL);
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
        throw submit_error_t (
          rc == ZLINK_SUBMIT_NOT_CONNECTED
            ? submit_result_t::not_connected
            : static_cast<submit_result_t> (rc),
          err);
    }

    bool send_channel (const std::string &channel_name_,
                       message_t &part_,
                       send_flags_t flags_ = send_flags_t::none)
    {
        validate_channel_name (channel_name_);
        if (flags_ == send_flags_t::dontwait) {
            send_result_t result = send_result_t::sent;
            if (send_channel_no_wait_result_impl (
                  result, channel_name_.c_str (), part_)
                != 0) {
                const int err = zlink_errno ();
                throw submit_error_t (
                  zlink::detail::submit_result_from_errno (err), err);
            }
            if (result == send_result_t::not_ready)
                throw submit_error_t (
                  submit_result_t::not_connected, zlink_errno ());
            return result == send_result_t::sent;
        }

        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        const bool submitted = send_channel (channel_name_, parts, flags_);
        if (!submitted && !parts.empty ())
            part_ = std::move (parts.front ());
        return submitted;
    }

    bool send_channel (const std::string &channel_name_,
                       std::vector<message_t> &parts_,
                       send_flags_t flags_ = send_flags_t::none)
    {
        validate_channel_name (channel_name_);
        const int rc = send_channel_impl (
          channel_name_.c_str (), parts_, flags_);
        if (rc != 0)
        {
            if (flags_ == send_flags_t::dontwait
                && static_cast<submit_result_t> (rc)
                    == submit_result_t::backpressured)
                return false;
            throw submit_error_t (
              static_cast<submit_result_t> (rc), zlink_errno ());
        }
        return true;
    }

    bool send_to_spot (const routing_id_t &dest_node_rid_,
                       const routing_id_t &dest_spot_rid_,
                       message_t message_,
                       send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        return send_to_spot (dest_node_rid_, dest_spot_rid_, parts, flags_);
    }

    bool send_to_spot (const routing_id_t &dest_node_rid_,
                       const routing_id_t &dest_spot_rid_,
                       std::vector<message_t> &parts_,
                       send_flags_t flags_ = send_flags_t::none)
    {
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0)
            throw last_error ();
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
                return zlink_spot_send_spot_part (
                  _spot, zlink::detail::routing_id_native (dest_node_rid_),
                  zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
                  static_cast<zlink_send_flags_t> (flags_), part_flag_);
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

  public:
    send_op_t send_to_spot (const routing_id_t &dest_node_rid_,
                            const routing_id_t &dest_spot_rid_)
    {
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::send_to_spot;
        state.first_rid = dest_node_rid_;
        state.second_rid = dest_spot_rid_;
        return send_op_t (std::move (state));
    }

    request_op_t request_to_spot (const routing_id_t &dest_node_rid_,
                                  const routing_id_t &dest_spot_rid_)
    {
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::request_to_spot;
        state.first_rid = dest_node_rid_;
        state.second_rid = dest_spot_rid_;
        return request_op_t (std::move (state));
    }

  private:
    async_result_t<std::vector<message_t>>
    request_to_spot (const routing_id_t &dest_node_rid_,
                     const routing_id_t &dest_spot_rid_,
                     message_t message_,
                     std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
                 bool is_final_) {
                return zlink_spot_request_spot_part (
                  _spot, zlink::detail::routing_id_native (dest_node_rid_),
                  zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL, ZLINK_SEND_FLAGS_NONE, part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        zlink::detail::resolve_timeout (
                          timeout_, _default_request_timeout).count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return async_result_t<std::vector<message_t>> (
          std::move (future), detail::make_spot_request_progress (_spot));
    }

    bool request_to_spot (
      const routing_id_t &dest_node_rid_,
      const routing_id_t &dest_spot_rid_,
      message_t message_,
      std::function<void(request_result_t, std::vector<message_t>)> callback_,
      send_flags_t flags_ = send_flags_t::none,
      std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
                 bool is_final_) {
                return zlink_spot_request_spot_part (
                  _spot, zlink::detail::routing_id_native (dest_node_rid_),
                  zlink::detail::routing_id_native (dest_spot_rid_), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL,
                  static_cast<zlink_send_flags_t> (flags_), part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        zlink::detail::resolve_timeout (
                          timeout_, _default_request_timeout).count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

  public:
    request_op_t request_to_router (const routing_id_t &peer_rid_)
    {
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::request_to_router;
        state.first_rid = peer_rid_;
        return request_op_t (std::move (state));
    }

  private:
    async_result_t<std::vector<message_t>>
    request_to_router (const routing_id_t &peer_rid_,
                       message_t message_,
                       std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
                 bool is_final_) {
                return zlink_spot_request_router_part (
                  _spot, zlink::detail::routing_id_native (peer_rid_), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL, ZLINK_SEND_FLAGS_NONE, part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        zlink::detail::resolve_timeout (
                          timeout_, _default_request_timeout).count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return async_result_t<std::vector<message_t>> (
          std::move (future), detail::make_spot_request_progress (_spot));
    }

    bool request_to_router (
      const routing_id_t &peer_rid_,
      message_t message_,
      std::function<void(request_result_t, std::vector<message_t>)> callback_,
      send_flags_t flags_ = send_flags_t::none,
      std::chrono::milliseconds timeout_ = {})
    {
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
                 bool is_final_) {
                return zlink_spot_request_router_part (
                  _spot, zlink::detail::routing_id_native (peer_rid_), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL,
                  static_cast<zlink_send_flags_t> (flags_), part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        zlink::detail::resolve_timeout (
                          timeout_, _default_request_timeout).count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

    async_result_t<std::vector<message_t>>
    request_channel (const std::string &channel_name_,
                     message_t &part_,
                     std::chrono::milliseconds timeout_ = {})
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        return request_channel (channel_name_, parts, timeout_);
    }

    async_result_t<std::vector<message_t>>
    request_channel (const std::string &channel_name_,
                     std::vector<message_t> &parts_,
                     std::chrono::milliseconds timeout_ = {})
    {
        validate_channel_name (channel_name_);
        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool is_final_) {
                return zlink_spot_request_channel_part (
                  _spot, channel_name_.c_str (), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL, ZLINK_SEND_FLAGS_NONE, part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        zlink::detail::resolve_timeout (
                          timeout_, _default_request_timeout).count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return async_result_t<std::vector<message_t>> (
          std::move (future),
          detail::make_spot_request_progress (_spot, channel_name_));
    }

    bool request_channel (
      const std::string &channel_name_,
      message_t &part_,
      std::function<void(request_result_t, std::vector<message_t>)> callback_,
      send_flags_t flags_ = send_flags_t::none,
      std::chrono::milliseconds timeout_ = {})
    {
        std::vector<message_t> parts;
        parts.push_back (std::move (part_));
        const bool submitted = request_channel (
          channel_name_, parts, std::move (callback_), flags_, timeout_);
        if (!submitted && !parts.empty ())
            part_ = std::move (parts.front ());
        return submitted;
    }

    bool request_channel (
      const std::string &channel_name_,
      std::vector<message_t> &parts_,
      std::function<void(request_result_t, std::vector<message_t>)> callback_,
      send_flags_t flags_ = send_flags_t::none,
      std::chrono::milliseconds timeout_ = {})
    {
        validate_channel_name (channel_name_);
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool is_final_) {
                return zlink_spot_request_channel_part (
                  _spot, channel_name_.c_str (), part_out_,
                  is_final_ ? &detail::request_callback_trampoline : NULL,
                  is_final_ ? state : NULL,
                  static_cast<zlink_send_flags_t> (flags_), part_flag_,
                  is_final_
                    ? static_cast<uint32_t> (
                        zlink::detail::resolve_timeout (
                          timeout_, _default_request_timeout).count ())
                    : 0u);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            delete state;
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

  public:
    int subscribe (topic_message_t &out_,
                   recv_flags_t flags_ = recv_flags_t::none)
    {
        return subscribe_impl (out_, flags_);
    }

    int subscribe_part (std::optional<routing_id_t> &source_rid_out_,
                        std::string &topic_out_,
                        message_t &part_out_,
                        bool &has_more_out_,
                        recv_flags_t flags_ = recv_flags_t::none)
    {
        return subscribe_part_impl (
          source_rid_out_, topic_out_, part_out_, has_more_out_, flags_);
    }

    int subscribe_part (std::string &topic_out_,
                        message_t &part_out_,
                        bool &has_more_out_,
                        recv_flags_t flags_ = recv_flags_t::none)
    {
        return subscribe_part_impl (
          NULL, topic_out_, part_out_, has_more_out_, flags_);
    }

    int receive_subscription_event (
      subscription_event_t &out_,
      recv_flags_t flags_ = recv_flags_t::none)
    {
        std::string topic;
        routing_id_t source_rid = zlink::detail::unchecked_empty_routing_id ();
        bool subscribed = false;
        const recv_result_t rc = static_cast<recv_result_t> (
          subscription_event_impl (
            source_rid, subscribed, topic, flags_));
        if (rc != recv_result_t::ok)
            return static_cast<int> (rc);
        out_ = subscription_event_t ();
        if (!zlink::detail::routing_id_empty (source_rid))
            out_.routing_id = source_rid;
        out_.topic = std::move (topic);
        out_.subscribed = subscribed;
        return static_cast<int> (recv_result_t::ok);
    }

    void set_subscription (const std::string &filter_)
    {
        zlink::detail::validate_no_embedded_null (filter_, "filter");
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_subscription (_spot, filter_.c_str ())));
    }

    void unset_subscription (const std::string &filter_)
    {
        zlink::detail::validate_no_embedded_null (filter_, "filter");
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_unset_subscription (_spot, filter_.c_str ())));
    }

    void subscription_at (size_t index_,
                          std::string &filter_out_,
                          bool *is_pattern_out_ = NULL) const
    {
        size_t capacity = 256u;
        const size_t max_capacity = 64u * 1024u;

        while (capacity <= max_capacity) {
            std::vector<char> buffer (capacity);
            size_t length = capacity;
            int is_pattern = 0;
            const config_result_t rc = static_cast<config_result_t> (
              zlink_subscription_at (
                _spot, index_, buffer.data (), &length, &is_pattern));
            if (rc == config_result_t::ok) {
                const size_t bounded = length <= buffer.size () ? length
                                                                : buffer.size ();
                size_t out_size = bounded;
                if (out_size > 0 && buffer[out_size - 1] == '\0')
                    --out_size;
                filter_out_.assign (buffer.data (), out_size);
                if (is_pattern_out_)
                    *is_pattern_out_ = is_pattern != 0;
                return;
            }

            if (errno != EINVAL || capacity == max_capacity)
                throw config_error_t (rc, zlink_errno ());

            capacity *= 2u;
            if (capacity > max_capacity)
                capacity = max_capacity;
        }

        throw config_error_t (config_result_t::invalid_argument, EINVAL);
    }

    subscription_filter_t subscription_at (size_t index_) const
    {
        subscription_filter_t filter;
        subscription_at (index_, filter.filter, &filter.is_pattern);
        return filter;
    }

    void on_send_ready (std::function<void()> handler_)
    {
        _send_ready_handler = std::move (handler_);
        const handler_result_t rc = static_cast<handler_result_t> (
          zlink_send_ready_handler (
            _spot, &spot_t::send_ready_trampoline, this));
        if (rc != handler_result_t::ok)
            throw handler_error_t (rc, zlink_errno ());
    }

  private:
    static void validate_channel_name (const std::string &channel_name_)
    {
        zlink::detail::validate_bounded_c_string (channel_name_, 255u, "channel_name");
        if (channel_name_.empty ()) {
            errno = EINVAL;
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        }
    }

    ZLINK_CPP_NODISCARD int
    publish_impl (const char *topic_,
                  std::vector<message_t> &parts_,
                  send_flags_t flags_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        if (parts_.empty ()) {
            errno = EINVAL;
            return -1;
        }

        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_spot_publish_part (
                _spot, topic_, part_out_,
                static_cast<zlink_send_flags_t> (flags_), part_flag_);
          });
        if (rc != 0) {
            detail::restore_parts_from_native (parts_, native, failed_index);
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    publish_impl (const char *topic_,
                  message_t &part_,
                  send_flags_t flags_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native;
        zlink::detail::move_to_native (part_, &native);
        if (part_.valid ())
            return -1;

        const int rc = zlink_spot_publish_part (
          _spot, topic_, &native,
          static_cast<zlink_send_flags_t> (flags_), ZLINK_PART_FINAL);
        if (rc != 0) {
            const int err = errno;
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (zlink::detail::native_handle (part_), &native);
            (void) zlink_msg_close (&native);
            errno = err;
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    send_channel_impl (const char *channel_name_,
                       std::vector<message_t> &parts_,
                       send_flags_t flags_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        if (parts_.empty ()) {
            errno = EINVAL;
            return -1;
        }

        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_spot_send_channel_part (
                _spot, channel_name_, part_out_,
                static_cast<zlink_send_flags_t> (flags_), part_flag_);
          });
        if (rc != 0) {
            detail::restore_parts_from_native (parts_, native, failed_index);
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD int
    send_channel_no_wait_result_impl (send_result_t &result_out_,
                                      const char *channel_name_,
                                      message_t &part_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native;
        zlink::detail::move_to_native (part_, &native);
        if (part_.valid ())
            return -1;

        const int rc = zlink_spot_send_channel_part (
          _spot, channel_name_, &native, ZLINK_DONTWAIT, ZLINK_PART_FINAL);
        if (rc == 0) {
            result_out_ = send_result_t::sent;
            return 0;
        }
        if (rc == ZLINK_SUBMIT_BACKPRESSURED
            || rc == ZLINK_SUBMIT_NOT_CONNECTED) {
            result_out_ = zlink::detail::to_send_result (rc);
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (zlink::detail::native_handle (part_), &native);
            (void) zlink_msg_close (&native);
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_out_)) {
            if (result_out_ != send_result_t::sent) {
                part_.init ();
                if (part_.valid ())
                    (void) zlink_msg_move (zlink::detail::native_handle (part_), &native);
                (void) zlink_msg_close (&native);
            }
            errno = err;
            return 0;
        }

        part_.init ();
        if (part_.valid ())
            (void) zlink_msg_move (zlink::detail::native_handle (part_), &native);
        (void) zlink_msg_close (&native);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    subscribe_impl (topic_message_t &message_out_,
                    recv_flags_t flags_ = recv_flags_t::none)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        char topic_buffer[256];
        size_t topic_length = sizeof (topic_buffer);
        const zlink_routing_id_t *source_rid = NULL;

        zlink_msg_t first_part;
        if (zlink_msg_init (&first_part) != 0)
            return -1;

        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const zlink_routing_id_t *part_source_rid = NULL;
        size_t part_topic_length = sizeof (topic_buffer);
        const int first_rc = zlink_spot_subscribe_part (
          _spot, &part_source_rid, topic_buffer, sizeof (topic_buffer),
          &part_topic_length, &first_part, &has_more,
          static_cast<zlink_recv_flags_t> (flags_));
        if (first_rc != ZLINK_RECV_OK) {
            const int err = errno;
            (void) zlink_msg_close (&first_part);
            errno = err;
            return first_rc;
        }

        source_rid = part_source_rid;
        topic_length = part_topic_length;

        const size_t topic_size =
          topic_length < sizeof (topic_buffer) ? topic_length
                                               : sizeof (topic_buffer) - 1u;
        std::string topic (topic_buffer, topic_size);
        const std::optional<routing_id_t> source =
          source_rid && source_rid->size > 0
            ? std::optional<routing_id_t> (
                zlink::detail::native_routing_id (*source_rid))
            : std::nullopt;

        if (!has_more) {
            message_t part;
            zlink::detail::adopt_native_message (part, &first_part);
            message_out_ = topic_message_t (
              source, std::move (topic), std::move (part));
        } else {
            std::vector<zlink_msg_t> parts_native;
            parts_native.push_back (first_part);

            for (;;) {
                parts_native.emplace_back ();
                zlink_msg_t &native_part = parts_native.back ();
                if (zlink_msg_init (&native_part) != 0) {
                    parts_native.pop_back ();
                    detail::close_native_parts (parts_native);
                    return -1;
                }

                zlink_part_flag_t more = ZLINK_PART_FINAL;
                size_t ignored_topic_length = 0u;
                const int rc = zlink_spot_subscribe_part (
                  _spot, &part_source_rid, NULL, 0u, &ignored_topic_length,
                  &native_part, &more, ZLINK_RECV_FLAGS_DONTWAIT);
                if (rc != ZLINK_RECV_OK) {
                    const int err = errno;
                    (void) zlink_msg_close (&native_part);
                    parts_native.pop_back ();
                    detail::close_native_parts (parts_native);
                    errno = err;
                    return rc;
                }

                if (!more)
                    break;
            }

            std::vector<message_t> parts;
            if (detail::assign_parts_from_native (parts_native, parts) != 0)
                return -1;
            message_out_ = topic_message_t (
              source, std::move (topic), std::move (parts));
        }
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    subscribe_part_impl (std::optional<routing_id_t> &source_rid_out_,
                         std::string &topic_out_,
                         message_t &part_out_,
                         bool &has_more_out_,
                         recv_flags_t flags_ = recv_flags_t::none)
    {
        source_rid_out_ = std::nullopt;
        return subscribe_part_impl (
          &source_rid_out_, topic_out_, part_out_, has_more_out_, flags_);
    }

    ZLINK_CPP_NODISCARD int
    subscribe_part_impl (std::optional<routing_id_t> *source_rid_out_,
                         std::string &topic_out_,
                         message_t &part_out_,
                         bool &has_more_out_,
                         recv_flags_t flags_ = recv_flags_t::none)
    {
        if (source_rid_out_)
            *source_rid_out_ = std::nullopt;
        topic_out_.clear ();
        has_more_out_ = false;
        part_out_.close ();

        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        char topic_buffer[256];
        size_t topic_length = sizeof (topic_buffer);
        const zlink_routing_id_t *source_rid = NULL;

        zlink_msg_t native_part;
        if (zlink_msg_init (&native_part) != 0)
            return -1;

        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const int rc = zlink_spot_subscribe_part (
          _spot, &source_rid, topic_buffer, sizeof (topic_buffer),
          &topic_length, &native_part, &has_more,
          static_cast<zlink_recv_flags_t> (flags_));
        if (rc != ZLINK_RECV_OK) {
            const int err = errno;
            (void) zlink_msg_close (&native_part);
            errno = err;
            return rc;
        }

        const size_t topic_size =
          topic_length < sizeof (topic_buffer) ? topic_length
                                               : sizeof (topic_buffer) - 1u;
        topic_out_.assign (topic_buffer, topic_size);
        if (source_rid_out_ && source_rid && source_rid->size > 0)
            *source_rid_out_ = zlink::detail::native_routing_id (*source_rid);
        zlink::detail::adopt_native_message (part_out_, &native_part);
        has_more_out_ = has_more != ZLINK_PART_FINAL;
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    subscription_event_impl (routing_id_t &source_rid_out_,
                             bool &subscribed_out_,
                             std::string &topic_,
                             recv_flags_t flags_ = recv_flags_t::none)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        char topic_buffer[256];
        size_t topic_length = 0;
        int subscribed = 0;
        const zlink_routing_id_t *source_rid = NULL;
        const int rc = zlink_spot_recv_subscription_event (
          _spot, &source_rid, &subscribed, topic_buffer, sizeof (topic_buffer),
          &topic_length,
          static_cast<zlink_recv_flags_t> (flags_));
        if (rc != 0)
            return rc;

        if (source_rid && source_rid->size > 0)
            source_rid_out_ = zlink::detail::native_routing_id (*source_rid);
        else
            source_rid_out_ = zlink::detail::unchecked_empty_routing_id ();

        const size_t topic_size =
          topic_length < sizeof (topic_buffer) ? topic_length
                                               : sizeof (topic_buffer) - 1u;
        topic_.assign (topic_buffer, topic_size);
        subscribed_out_ = subscribed != 0;
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    publish_no_wait_result_impl (send_result_t &result_out_,
                      const char *topic_,
                      std::vector<message_t> &parts_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        if (parts_.empty ()) {
            errno = EINVAL;
            return -1;
        }

        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts_, native) != 0)
            return -1;

        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              return zlink_spot_publish_part (
                _spot, topic_, part_out_, ZLINK_DONTWAIT, part_flag_);
          });
        if (rc == 0) {
            result_out_ = send_result_t::sent;
            return 0;
        }
        if (rc == ZLINK_SUBMIT_BACKPRESSURED
            || rc == ZLINK_SUBMIT_NOT_CONNECTED) {
            result_out_ = zlink::detail::to_send_result (rc);
            detail::restore_parts_from_native (parts_, native, failed_index);
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_out_)) {
            if (result_out_ != send_result_t::sent)
                detail::restore_parts_from_native (parts_, native, failed_index);
            return 0;
        }

        detail::restore_parts_from_native (parts_, native, failed_index);
        errno = err;
        return -1;
    }

    ZLINK_CPP_NODISCARD int
    publish_no_wait_result_impl (send_result_t &result_out_,
                      const char *topic_,
                      message_t &part_)
    {
        if (!_spot) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        if (!part_.valid ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t native;
        zlink::detail::move_to_native (part_, &native);
        if (part_.valid ())
            return -1;

        const int rc = zlink_spot_publish_part (
          _spot, topic_, &native, ZLINK_DONTWAIT, ZLINK_PART_FINAL);
        if (rc == 0) {
            result_out_ = send_result_t::sent;
            return 0;
        }
        if (rc == ZLINK_SUBMIT_BACKPRESSURED
            || rc == ZLINK_SUBMIT_NOT_CONNECTED) {
            result_out_ = zlink::detail::to_send_result (rc);
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (zlink::detail::native_handle (part_), &native);
            (void) zlink_msg_close (&native);
            return 0;
        }

        const int err = errno;
        if (detail::classify_nonblocking_send_errno (err, result_out_)) {
            if (result_out_ != send_result_t::sent) {
                part_.init ();
                if (part_.valid ())
                    (void) zlink_msg_move (zlink::detail::native_handle (part_), &native);
                (void) zlink_msg_close (&native);
            }
            return 0;
        }

        part_.init ();
        if (part_.valid ())
            (void) zlink_msg_move (zlink::detail::native_handle (part_), &native);
        (void) zlink_msg_close (&native);
        errno = err;
        return -1;
    }

  private:
    explicit spot_t (spot_node_t &node_)
        : _spot (zlink_spot_new (zlink::detail::native_handle (node_))),
          _last_error (0),
          _default_request_timeout (std::chrono::milliseconds ())
    {
        if (!_spot)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    explicit spot_t (void *handle_) noexcept
        : _spot (handle_), _last_error (handle_ ? 0 : (errno != 0 ? errno : EFAULT)),
          _default_request_timeout (std::chrono::milliseconds ())
    {
    }

    friend class spot_node_t;
    friend class send_ready_op_t;
    friend class request_ready_op_t;
    friend class request_callback_ready_op_t;
    friend class reply_ready_op_t;
    friend void *zlink::detail::native_handle (spot_t &spot_) noexcept;
    friend const void *
    zlink::detail::native_handle (const spot_t &spot_) noexcept;

  public:
    static void send_ready_trampoline (void *, void *userdata_)
    {
        spot_t *self = static_cast<spot_t *> (userdata_);
        if (self && self->_send_ready_handler)
            self->_send_ready_handler ();
    }

    static void routed_receive_trampoline (
      const zlink_routing_id_t *source_rid_,
      const zlink_routing_id_t *spot_rid_,
      uint64_t request_seq_,
      zlink_msg_t *parts_,
      size_t part_count_,
      void *userdata_)
    {
        spot_t *self = static_cast<spot_t *> (userdata_);
        if (!self || !self->_routed_receive_handler)
            return;
        self->_routed_receive_handler (
          zlink::detail::make_received (
            source_rid_, spot_rid_, request_seq_, request_seq_ != 0u,
            parts_, part_count_));
    }

    static void dispatch_event_trampoline (
      void *,
      const zlink_spot_dispatch_info_t *info_,
      void *userdata_)
    {
        spot_t *self = static_cast<spot_t *> (userdata_);
        if (!self || !self->_dispatch_event_handler || !info_)
            return;
        const spot_dispatch_info_t info (*info_);
        self->_dispatch_event_handler (*self, info);
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (zlink_set_routing_id (
            _spot, routing_id_.data (), routing_id_.size ())));
    }

    void get_routing_id (routing_id_t &out_) const
    {
        zlink_routing_id_t native;
        std::memset (&native, 0, sizeof (native));
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (zlink_get_routing_id (_spot, &native)));
        out_ = zlink::detail::native_routing_id (native);
    }

    routing_id_t routing_id () const
    {
        routing_id_t value = zlink::detail::unchecked_empty_routing_id ();
        get_routing_id (value);
        return value;
    }

  private:
    void reply_to_spot (const routing_id_t &dest_node_rid_,
                        const routing_id_t &dest_spot_rid_,
                        uint64_t request_seq_,
                        message_t message_,
                        send_flags_t flags_ = send_flags_t::none)
    {
        zlink::detail::throw_if_reply_flags_unsupported (flags_);
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0)
            throw last_error ();
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
                return zlink_spot_reply_spot_part (
                  _spot, zlink::detail::routing_id_native (dest_node_rid_),
                  zlink::detail::routing_id_native (dest_spot_rid_), request_seq_, part_out_,
                  part_flag_);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            throw submit_error_t (rc, zlink_errno ());
        }
    }

  public:
    reply_op_t reply_to_spot (const routing_id_t &dest_node_rid_,
                              const routing_id_t &dest_spot_rid_,
                              uint64_t request_seq_)
    {
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::reply_to_spot;
        state.first_rid = dest_node_rid_;
        state.second_rid = dest_spot_rid_;
        state.request_seq = request_seq_;
        return reply_op_t (std::move (state));
    }

  private:
    void reply_to_router (const routing_id_t &peer_rid_,
                          uint64_t request_seq_,
                          message_t message_,
                          send_flags_t flags_ = send_flags_t::none)
    {
        zlink::detail::throw_if_reply_flags_unsupported (flags_);
        std::vector<message_t> parts;
        parts.push_back (std::move (message_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (parts, native) != 0)
            throw last_error ();
        size_t failed_index = 0;
        const submit_result_t rc = static_cast<submit_result_t> (
          detail::submit_native_parts (
            native, failed_index,
            [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
                return zlink_spot_reply_router_part (
                  _spot, zlink::detail::routing_id_native (peer_rid_), request_seq_, part_out_,
                  part_flag_);
            }));
        if (rc != submit_result_t::ok) {
            detail::close_native_parts (native, failed_index);
            throw submit_error_t (rc, zlink_errno ());
        }
    }

  public:
    reply_op_t reply_to_router (const routing_id_t &peer_rid_,
                                uint64_t request_seq_)
    {
        detail::spot_op_state_t state;
        state.spot = this;
        state.kind = detail::spot_op_kind_t::reply_to_router;
        state.first_rid = peer_rid_;
        state.request_seq = request_seq_;
        return reply_op_t (std::move (state));
    }

  private:
    std::optional<received_t>
    recv_routed_optional (recv_flags_t flags_ = recv_flags_t::none)
    {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
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

            const zlink_routing_id_t *part_source_node_rid = NULL;
            const zlink_routing_id_t *part_source_spot_rid = NULL;
            uint64_t part_request_seq = 0;
            zlink_part_flag_t has_more = ZLINK_PART_FINAL;
            const recv_result_t rc = static_cast<recv_result_t> (
              zlink_spot_recv_part (
                _spot, &part_source_node_rid, &part_source_spot_rid,
                &part_request_seq, &native_part, &has_more,
                static_cast<zlink_recv_flags_t> (flags_)));
            if (rc == recv_result_t::no_data
                && flags_ == recv_flags_t::dontwait
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

        std::function<void(std::vector<message_t> &, send_flags_t)> reply_fn;
	        if (request_seq != 0u) {
            const std::optional<routing_id_t> node_rid =
              (source_node_rid && source_node_rid->size > 0)
                ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_node_rid))
                : std::nullopt;
            const std::optional<routing_id_t> spot_rid =
              (source_spot_rid && source_spot_rid->size > 0)
                ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_spot_rid))
                : std::nullopt;
            reply_fn = [this, node_rid, spot_rid, request_seq] (
                         std::vector<message_t> &reply_parts_,
                         send_flags_t flags__) {
                if (!node_rid || !spot_rid)
                    throw submit_error_t (
                      submit_result_t::invalid_argument, EINVAL);
                zlink::detail::throw_if_reply_flags_unsupported (flags__);
                std::vector<zlink_msg_t> native_reply;
                if (detail::move_parts_to_native (reply_parts_, native_reply) != 0)
                    throw last_error ();
                size_t failed_index = 0;
                const submit_result_t reply_rc = static_cast<submit_result_t> (
                  detail::submit_native_parts (
                    native_reply, failed_index,
                    [&] (zlink_msg_t *part_out_,
                         zlink_part_flag_t part_flag_,
                         bool) {
                        return zlink_spot_reply_spot_part (
                          _spot, zlink::detail::routing_id_native (*node_rid),
                          zlink::detail::routing_id_native (*spot_rid), request_seq,
                          part_out_, part_flag_);
                    }));
                if (reply_rc != submit_result_t::ok) {
                    detail::restore_parts_from_native (
                      reply_parts_, native_reply, failed_index);
                    throw submit_error_t (reply_rc, zlink_errno ());
                }
	            };
	        }
	        std::function<bool(std::vector<message_t> &, send_flags_t)> send_fn;
	        if (source_node_rid && source_node_rid->size > 0
	            && source_spot_rid && source_spot_rid->size > 0) {
	            const routing_id_t send_node_rid =
	              zlink::detail::native_routing_id (*source_node_rid);
	            const routing_id_t send_spot_rid =
	              zlink::detail::native_routing_id (*source_spot_rid);
	            send_fn = [this, send_node_rid, send_spot_rid] (
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
	                         zlink_part_flag_t part_flag_,
	                         bool) {
	                        return zlink_spot_send_spot_part (
	                          _spot, zlink::detail::routing_id_native (send_node_rid),
	                          zlink::detail::routing_id_native (send_spot_rid),
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

	        return std::optional<received_t> (received_t (
          (source_node_rid && source_node_rid->size > 0)
            ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_node_rid))
            : std::nullopt,
          (source_spot_rid && source_spot_rid->size > 0)
            ? std::optional<routing_id_t> (zlink::detail::native_routing_id (*source_spot_rid))
            : std::nullopt,
	          request_seq != 0u ? std::optional<uint64_t> (request_seq)
	                            : std::nullopt,
	          std::move (parts), std::move (reply_fn), std::move (send_fn)));
    }

  public:
    int recv_routed (received_t &out_,
                     recv_flags_t flags_ = recv_flags_t::none)
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
        catch (const zlink_error_t &err) {
            errno = err.internal_errno () != 0 ? err.internal_errno () : EFAULT;
            return -1;
        }
        catch (...) {
            errno = EFAULT;
            return -1;
        }
    }

    void on_dispatch_event (
      std::function<void(spot_t &, const spot_dispatch_info_t &)> handler_)
    {
        _dispatch_event_handler = std::move (handler_);
        const handler_result_t rc = static_cast<handler_result_t> (
          zlink_spot_dispatch_event_handler (
            _spot, &spot_t::dispatch_event_trampoline, this));
        if (rc != handler_result_t::ok)
            throw handler_error_t (rc, zlink_errno ());
    }

    void on_dispatch_event (
      std::function<void(const spot_dispatch_info_t &)> handler_)
    {
        on_dispatch_event (
          [handler = std::move (handler_)] (
            spot_t &, const spot_dispatch_info_t &info_) mutable {
              handler (info_);
        });
    }

    std::optional<spot_actor_lifecycle_event_t>
    recv_actor_lifecycle (recv_flags_t flags_ = recv_flags_t::none)
    {
        zlink_spot_actor_lifecycle_event_t native_event;
        std::memset (&native_event, 0, sizeof (native_event));
        const recv_result_t rc = static_cast<recv_result_t> (
          zlink_spot_recv_actor_lifecycle (
            _spot, &native_event, static_cast<zlink_recv_flags_t> (flags_)));
        if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
            return std::nullopt;
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return std::optional<spot_actor_lifecycle_event_t> (
          spot_actor_lifecycle_event_t (native_event));
    }

    std::optional<actor_join_request_t>
    recv_actor_join (recv_flags_t flags_ = recv_flags_t::none)
    {
        zlink_actor_join_info_t native_info;
        std::memset (&native_info, 0, sizeof (native_info));
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        const recv_result_t rc = static_cast<recv_result_t> (
          zlink_spot_actor_join_recv (
            _spot, &native_info, &parts, &part_count,
            static_cast<zlink_recv_flags_t> (flags_)));
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
        return std::optional<actor_join_request_t> (
          actor_join_request_t (actor_join_info_t (native_info),
                                std::move (message)));
    }

    actor_join_reply_op_t reply_actor_join (
      const actor_join_request_t &request_, int32_t join_result_code_)
    {
        detail::actor_join_reply_state_t state;
        state.spot = _spot;
        state.info = request_.info ();
        state.join_result_code = join_result_code_;
        return actor_join_reply_op_t (std::move (state));
    }

    std::vector<actor_ref_t> actors () const
    {
        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_actors (_spot, NULL, &count)));
        std::vector<zlink_actor_ref_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_spot_actors (_spot, native.data (), &count)));
            native.resize (count);
        }
        std::vector<actor_ref_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (actor_ref_t (native[i]));
        return entries;
    }

    void close ()
    {
        if (!_spot)
            return;

        void *tmp = _spot;
        detail::throw_if_failed<close_error_t> (
          static_cast<close_result_t> (zlink_spot_destroy (&tmp)));
        _spot = NULL;
    }

  private:
    void *_spot;
    int _last_error;
    std::chrono::milliseconds _default_request_timeout;
    std::function<void()> _send_ready_handler;
    std::function<void(received_t)> _routed_receive_handler;
    std::function<void(spot_t &, const spot_dispatch_info_t &)>
      _dispatch_event_handler;
};

inline bool send_ready_op_t::submit () &&
{
    if (!detail::has_send_parts (_state))
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    // Op kinds backed by received_t / spot_node_t may have _state.spot == NULL.
    switch (_state.kind) {
    case detail::spot_op_kind_t::raw_send:
    case detail::spot_op_kind_t::raw_routed_send:
    case detail::spot_op_kind_t::raw_publish:
    case detail::spot_op_kind_t::raw_router_send_spot:
        return detail::submit_raw_send_state (_state);
    case detail::spot_op_kind_t::received_send: {
        if (!_state.received || !_state.received->has_send_fn ())
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        if (detail::send_part_count (_state) == 1u
            && _state.received->_runtime
            && _state.received->_runtime->_send_context_handle
            && _state.received->_runtime->_send_context_kind
                 != received_t::send_context_kind_t::none
            && _state.received->_routing_id.has_value ()) {
            message_t &part = detail::send_single_part (_state);
            if (!part.valid ()) {
                detail::restore_single_send_part_to_source (_state);
                throw submit_error_t (
                  submit_result_t::invalid_argument, EINVAL);
            }

            zlink_submit_result_t direct_rc = ZLINK_SUBMIT_INVALID_ARGUMENT;
            if (_state.received->_runtime->_send_context_kind
                == received_t::send_context_kind_t::socket_rid) {
                direct_rc = zlink_send_part_rid (
                  _state.received->_runtime->_send_context_handle,
                  zlink::detail::routing_id_native (
                    *_state.received->_routing_id),
                  zlink::detail::native_handle (part),
                  static_cast<zlink_send_flags_t> (_state.flags),
                  ZLINK_PART_FINAL);
            } else if (_state.received->_spot_rid.has_value ()) {
                direct_rc = zlink_router_send_spot_part (
                  _state.received->_runtime->_send_context_handle,
                  zlink::detail::routing_id_native (
                    *_state.received->_routing_id),
                  zlink::detail::routing_id_native (
                    *_state.received->_spot_rid),
                  zlink::detail::native_handle (part),
                  static_cast<zlink_send_flags_t> (_state.flags),
                  ZLINK_PART_FINAL);
            }

            const submit_result_t result =
              static_cast<submit_result_t> (direct_rc);
            if (result == submit_result_t::ok) {
                zlink::detail::mark_sent (part);
                return true;
            }
            detail::restore_single_send_part_to_source (_state);
            if (_state.flags == send_flags_t::dontwait
                && result == submit_result_t::backpressured)
                return false;
            throw submit_error_t (result, zlink_errno ());
        }
        std::vector<message_t> parts = detail::take_send_parts (_state);
        const bool sent = _state.received->invoke_send_fn (parts, _state.flags);
        if (!sent)
            detail::restore_single_send_part_to_source (_state, parts);
        return sent;
    }
    case detail::spot_op_kind_t::bound_session_send:
        return detail::submit_bound_session_send_state (_state);
    case detail::spot_op_kind_t::stream_bound_actor_send:
        return detail::submit_stream_bound_actor_send_state (_state);
    default:
        break;
    }

    if (!_state.spot)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (_state.kind) {
    case detail::spot_op_kind_t::publish:
        if (detail::send_part_count (_state) == 1u
            && _state.flags == send_flags_t::dontwait
            && _state.discard_single_part_on_backpressure
            && _state.single_part.has_value ()
            && !_state.single_part_source) {
            return _state.spot->publish_discard_on_backpressure (
              _state.topic, *_state.single_part);
        }
        return detail::send_part_count (_state) == 1u
          ? _state.spot->publish (
              _state.topic, detail::send_single_part (_state), _state.flags)
          : _state.spot->publish (
              _state.topic, _state.parts, _state.flags);
    case detail::spot_op_kind_t::send_channel:
        return detail::send_part_count (_state) == 1u
          ? _state.spot->send_channel (
              _state.channel_name, detail::send_single_part (_state),
              _state.flags)
          : _state.spot->send_channel (
              _state.channel_name, _state.parts, _state.flags);
    case detail::spot_op_kind_t::send_to_spot:
        if (!_state.first_rid || !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return detail::send_part_count (_state) == 1u
          ? _state.spot->send_to_spot (
              *_state.first_rid, *_state.second_rid,
              std::move (detail::send_single_part (_state)), _state.flags)
          : _state.spot->send_to_spot (
              *_state.first_rid, *_state.second_rid, _state.parts,
              _state.flags);
    default:
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

inline request_callback_ready_op_t request_ready_op_t::flags (int flags_) &&
{
    _state.flags = send_flags_t (flags_);
    return request_callback_ready_op_t (std::move (_state));
}

inline async_result_t<std::vector<message_t>>
request_ready_op_t::submit_async () &&
{
    if (_state.parts.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    if (_state.kind == detail::spot_op_kind_t::raw_request
        || _state.kind == detail::spot_op_kind_t::raw_routed_request
        || _state.kind == detail::spot_op_kind_t::raw_router_request_spot) {
        if (!_state.raw_socket)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        if (_state.kind != detail::spot_op_kind_t::raw_request
            && !_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        if (_state.kind == detail::spot_op_kind_t::raw_router_request_spot
            && !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (_state.parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
               bool is_final_) {
              const uint32_t timeout =
                is_final_ ? static_cast<uint32_t> (_state.timeout.count ()) : 0u;
              switch (_state.kind) {
              case detail::spot_op_kind_t::raw_request:
                  return zlink_dealer_request_part (
                    _state.raw_socket, part_out_, ZLINK_SEND_FLAGS_NONE,
                    part_flag_, timeout,
                    is_final_ ? &detail::request_callback_trampoline : NULL,
                    is_final_ ? state : NULL);
              case detail::spot_op_kind_t::raw_routed_request:
                  return zlink_router_request_part (
                    _state.raw_socket,
                    zlink::detail::routing_id_native (*_state.first_rid),
                    part_out_, ZLINK_SEND_FLAGS_NONE, part_flag_, timeout,
                    is_final_ ? &detail::request_callback_trampoline : NULL,
                    is_final_ ? state : NULL);
              case detail::spot_op_kind_t::raw_router_request_spot:
                  return zlink_router_request_spot_part (
                    _state.raw_socket,
                    zlink::detail::routing_id_native (*_state.first_rid),
                    zlink::detail::routing_id_native (*_state.second_rid),
                    part_out_,
                    is_final_ ? &detail::request_callback_trampoline : NULL,
                    is_final_ ? state : NULL, ZLINK_SEND_FLAGS_NONE,
                    part_flag_, timeout);
              default:
                  return ZLINK_SUBMIT_INVALID_ARGUMENT;
              }
          });
        if (rc != 0) {
            detail::close_native_parts (native, failed_index);
            delete state;
            throw last_error ();
        }
        return async_result_t<std::vector<message_t>> (
          std::move (future),
          zlink::detail::make_socket_request_progress (_state.raw_socket));
    }

    if (!_state.spot)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (_state.kind) {
    case detail::spot_op_kind_t::request_channel:
        return _state.spot->request_channel (
          _state.channel_name, _state.parts, _state.timeout);
    case detail::spot_op_kind_t::request_to_spot:
        if (!_state.first_rid || !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return _state.spot->request_to_spot (
          *_state.first_rid, *_state.second_rid,
          std::move (_state.parts.front ()), _state.timeout);
    case detail::spot_op_kind_t::request_to_router:
        if (!_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return _state.spot->request_to_router (
          *_state.first_rid, std::move (_state.parts.front ()),
          _state.timeout);
    default:
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

inline bool request_ready_op_t::submit (request_callback_t callback_) &&
{
    request_callback_ready_op_t ready (std::move (_state));
    return std::move (ready).submit (std::move (callback_));
}

inline bool
request_callback_ready_op_t::submit (request_callback_t callback_) &&
{
    if (_state.parts.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    if (_state.kind == detail::spot_op_kind_t::raw_request
        || _state.kind == detail::spot_op_kind_t::raw_routed_request
        || _state.kind == detail::spot_op_kind_t::raw_router_request_spot) {
        if (!_state.raw_socket)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        if (_state.kind != detail::spot_op_kind_t::raw_request
            && !_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        if (_state.kind == detail::spot_op_kind_t::raw_router_request_spot
            && !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (_state.parts, native) != 0) {
            delete state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
               bool is_final_) {
              const uint32_t timeout =
                is_final_ ? static_cast<uint32_t> (_state.timeout.count ()) : 0u;
              switch (_state.kind) {
              case detail::spot_op_kind_t::raw_request:
                  return zlink_dealer_request_part (
                    _state.raw_socket, part_out_,
                    static_cast<zlink_send_flags_t> (_state.flags),
                    part_flag_, timeout,
                    is_final_ ? &detail::request_callback_trampoline : NULL,
                    is_final_ ? state : NULL);
              case detail::spot_op_kind_t::raw_routed_request:
                  return zlink_router_request_part (
                    _state.raw_socket,
                    zlink::detail::routing_id_native (*_state.first_rid),
                    part_out_, static_cast<zlink_send_flags_t> (_state.flags),
                    part_flag_, timeout,
                    is_final_ ? &detail::request_callback_trampoline : NULL,
                    is_final_ ? state : NULL);
              case detail::spot_op_kind_t::raw_router_request_spot:
                  return zlink_router_request_spot_part (
                    _state.raw_socket,
                    zlink::detail::routing_id_native (*_state.first_rid),
                    zlink::detail::routing_id_native (*_state.second_rid),
                    part_out_,
                    is_final_ ? &detail::request_callback_trampoline : NULL,
                    is_final_ ? state : NULL,
                    static_cast<zlink_send_flags_t> (_state.flags),
                    part_flag_, timeout);
              default:
                  return ZLINK_SUBMIT_INVALID_ARGUMENT;
              }
          });
        if (rc != 0) {
            detail::close_native_parts (native, failed_index);
            delete state;
            const submit_error_t err (
              static_cast<submit_result_t> (rc), zlink_errno ());
            if (_state.flags == send_flags_t::dontwait
                && err.result () == submit_result_t::backpressured)
                return false;
            throw err;
        }
        return true;
    }

    if (!_state.spot)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (_state.kind) {
    case detail::spot_op_kind_t::request_channel:
        return _state.spot->request_channel (
          _state.channel_name, _state.parts, std::move (callback_),
          _state.flags, _state.timeout);
    case detail::spot_op_kind_t::request_to_spot:
        if (!_state.first_rid || !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return _state.spot->request_to_spot (
          *_state.first_rid, *_state.second_rid,
          std::move (_state.parts.front ()), std::move (callback_),
          _state.flags, _state.timeout);
    case detail::spot_op_kind_t::request_to_router:
        if (!_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return _state.spot->request_to_router (
          *_state.first_rid, std::move (_state.parts.front ()),
          std::move (callback_), _state.flags, _state.timeout);
    default:
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

inline void reply_ready_op_t::submit () &&
{
    if (_state.parts.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    if (_state.kind == detail::spot_op_kind_t::received_reply) {
        if (!_state.received || !_state.received->has_reply_fn ())
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        _state.received->invoke_reply_fn (_state.parts, _state.flags);
        return;
    }

    if (_state.kind == detail::spot_op_kind_t::raw_reply
        || _state.kind == detail::spot_op_kind_t::raw_router_reply_spot) {
        if (!_state.raw_socket || !_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        if (_state.kind == detail::spot_op_kind_t::raw_router_reply_spot
            && !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        zlink::detail::throw_if_reply_flags_unsupported (_state.flags);
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (_state.parts, native) != 0)
            throw last_error ();
        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              if (_state.kind == detail::spot_op_kind_t::raw_reply) {
                  return zlink_router_reply_part (
                    _state.raw_socket,
                    zlink::detail::routing_id_native (*_state.first_rid),
                    _state.request_seq, part_out_, part_flag_);
              }
              return zlink_router_reply_spot_part (
                _state.raw_socket,
                zlink::detail::routing_id_native (*_state.first_rid),
                zlink::detail::routing_id_native (*_state.second_rid),
                _state.request_seq, part_out_, part_flag_);
          });
        if (rc != 0) {
            detail::restore_parts_from_native (
              _state.parts, native, failed_index);
            throw last_error ();
        }
        return;
    }

    if (!_state.spot)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (_state.kind) {
    case detail::spot_op_kind_t::reply_to_spot:
        if (!_state.first_rid || !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        _state.spot->reply_to_spot (
          *_state.first_rid, *_state.second_rid, _state.request_seq,
          std::move (_state.parts.front ()), _state.flags);
        return;
    case detail::spot_op_kind_t::reply_to_router:
        if (!_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        _state.spot->reply_to_router (
          *_state.first_rid, _state.request_seq,
          std::move (_state.parts.front ()), _state.flags);
        return;
    default:
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

} // namespace service

} // namespace zlink

#include "spot_socket_ops.hpp"
#include "spot_node_ops.hpp"

#endif
