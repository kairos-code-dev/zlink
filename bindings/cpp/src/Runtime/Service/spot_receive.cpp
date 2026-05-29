/* SPDX-License-Identifier: MPL-2.0 */

#include <Runtime/Service/spot_impl.hpp>
#include <Runtime/Service/detail.hpp>

namespace zlink
{
namespace service
{

int spot_t::subscribe (topic_message_t &out_, recv_flags_t flags_)
{
    return subscribe_impl (out_, flags_);
}

int spot_t::subscribe_part (std::optional<routing_id_t> &source_rid_out_,
                            std::string &topic_out_,
                            message_t &part_out_,
                            bool &has_more_out_,
                            recv_flags_t flags_)
{
    return subscribe_part_impl (source_rid_out_, topic_out_, part_out_,
                                has_more_out_, flags_);
}

int spot_t::subscribe_part (std::string &topic_out_,
                            message_t &part_out_,
                            bool &has_more_out_,
                            recv_flags_t flags_)
{
    return subscribe_part_impl (nullptr, topic_out_, part_out_, has_more_out_,
                                flags_);
}

int spot_t::receive_subscription_event (subscription_event_t &out_,
                                        recv_flags_t flags_)
{
    std::string topic;
    routing_id_t source_rid = zlink::detail::unchecked_empty_routing_id ();
    bool subscribed = false;
    const recv_result_t rc = static_cast<recv_result_t> (
      subscription_event_impl (source_rid, subscribed, topic, flags_));
    if (rc != recv_result_t::ok)
        return static_cast<int> (rc);
    out_ = subscription_event_t ();
    if (!zlink::detail::routing_id_empty (source_rid))
        out_.routing_id = source_rid;
    out_.topic = std::move (topic);
    out_.subscribed = subscribed;
    return static_cast<int> (recv_result_t::ok);
}

void spot_t::set_subscription (const std::string &filter_)
{
    zlink::detail::validate_no_embedded_null (filter_, "filter");
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_set_subscription (_impl->handle, filter_.c_str ())));
}

void spot_t::unset_subscription (const std::string &filter_)
{
    zlink::detail::validate_no_embedded_null (filter_, "filter");
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_unset_subscription (_impl->handle, filter_.c_str ())));
}

void spot_t::subscription_at (size_t index_,
                              std::string &filter_out_,
                              bool *is_pattern_out_) const
{
    size_t capacity = 256u;
    const size_t max_capacity = 64u * 1024u;

    while (capacity <= max_capacity) {
        std::vector<char> buffer (capacity);
        size_t length = capacity;
        int is_pattern = 0;
        const config_result_t rc =
          static_cast<config_result_t> (zlink_subscription_at (
            _impl->handle, index_, buffer.data (), &length, &is_pattern));
        if (rc == config_result_t::ok) {
            const size_t bounded =
              length <= buffer.size () ? length : buffer.size ();
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

subscription_filter_t spot_t::subscription_at (size_t index_) const
{
    subscription_filter_t filter;
    subscription_at (index_, filter.filter, &filter.is_pattern);
    return filter;
}

void spot_t::set_send_ready_handler (std::function<void ()> handler_)
{
    _impl->send_ready_handler = std::move (handler_);
    const handler_result_t rc =
      static_cast<handler_result_t> (zlink_send_ready_handler (
        _impl->handle,
        [] (void *, void *userdata_) {
            spot_t *self = static_cast<spot_t *> (userdata_);
            if (self && self->_impl->send_ready_handler)
                self->_impl->send_ready_handler ();
        },
        this));
    if (rc != handler_result_t::ok)
        throw handler_error_t (rc, zlink_errno ());
}

void spot_t::validate_channel_name (const std::string &channel_name_)
{
    zlink::detail::validate_bounded_c_string (channel_name_, 255u,
                                              "channel_name");
    if (channel_name_.empty ()) {
        errno = EINVAL;
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

[[nodiscard]] int spot_t::publish_impl (const char *topic_,
                                        std::vector<message_t> &parts_,
                                        send_flags_t flags_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
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
            _impl->handle, topic_, part_out_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            part_flag_);
      });
    if (rc != 0) {
        detail::restore_parts_from_native (parts_, native, failed_index);
    }
    return rc;
}

[[nodiscard]] int
spot_t::publish_impl (const char *topic_, message_t &part_, send_flags_t flags_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
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
      _impl->handle, topic_, &native,
      static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
      ZLINK_PART_FINAL);
    if (rc != 0) {
        const int err = errno;
        part_.init ();
        if (part_.valid ())
            (void) zlink_msg_move (zlink::detail::native_handle (part_),
                                   &native);
        (void) zlink_msg_close (&native);
        errno = err;
    }
    return rc;
}

[[nodiscard]] int spot_t::send_channel_impl (const char *channel_name_,
                                             std::vector<message_t> &parts_,
                                             send_flags_t flags_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
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
            _impl->handle, channel_name_, part_out_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            part_flag_);
      });
    if (rc != 0) {
        detail::restore_parts_from_native (parts_, native, failed_index);
    }
    return rc;
}

[[nodiscard]] int spot_t::send_channel_no_wait_result_impl (
  send_result_t &result_out_, const char *channel_name_, message_t &part_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
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
      _impl->handle, channel_name_, &native, ZLINK_DONTWAIT, ZLINK_PART_FINAL);
    if (rc == 0) {
        result_out_ = send_result_t::sent;
        return 0;
    }
    if (rc == ZLINK_SUBMIT_BACKPRESSURED || rc == ZLINK_SUBMIT_NOT_CONNECTED) {
        result_out_ = zlink::detail::to_send_result (rc);
        part_.init ();
        if (part_.valid ())
            (void) zlink_msg_move (zlink::detail::native_handle (part_),
                                   &native);
        (void) zlink_msg_close (&native);
        return 0;
    }

    const int err = errno;
    if (detail::classify_nonblocking_send_errno (err, result_out_)) {
        if (result_out_ != send_result_t::sent) {
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (zlink::detail::native_handle (part_),
                                       &native);
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

[[nodiscard]] int spot_t::subscribe_impl (topic_message_t &message_out_,
                                          recv_flags_t flags_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
        return -1;
    }

    char topic_buffer[256];
    size_t topic_length = sizeof (topic_buffer);
    const zlink_routing_id_t *source_rid = nullptr;

    zlink_msg_t first_part;
    if (zlink_msg_init (&first_part) != 0)
        return -1;

    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const zlink_routing_id_t *part_source_rid = nullptr;
    size_t part_topic_length = sizeof (topic_buffer);
    const int first_rc = zlink_spot_subscribe_part (
      _impl->handle, &part_source_rid, topic_buffer, sizeof (topic_buffer),
      &part_topic_length, &first_part, &has_more,
      static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
    if (first_rc != ZLINK_RECV_OK) {
        const int err = errno;
        (void) zlink_msg_close (&first_part);
        errno = err;
        return first_rc;
    }

    source_rid = part_source_rid;
    topic_length = part_topic_length;

    const size_t topic_size = topic_length < sizeof (topic_buffer)
                                ? topic_length
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
        message_out_ =
          topic_message_t (source, std::move (topic), std::move (part));
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
              _impl->handle, &part_source_rid, nullptr, 0u, &ignored_topic_length,
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
        message_out_ =
          topic_message_t (source, std::move (topic), std::move (parts));
    }
    return 0;
}

[[nodiscard]] int
spot_t::subscribe_part_impl (std::optional<routing_id_t> &source_rid_out_,
                             std::string &topic_out_,
                             message_t &part_out_,
                             bool &has_more_out_,
                             recv_flags_t flags_)
{
    source_rid_out_ = std::nullopt;
    return subscribe_part_impl (&source_rid_out_, topic_out_, part_out_,
                                has_more_out_, flags_);
}

[[nodiscard]] int
spot_t::subscribe_part_impl (std::optional<routing_id_t> *source_rid_out_,
                             std::string &topic_out_,
                             message_t &part_out_,
                             bool &has_more_out_,
                             recv_flags_t flags_)
{
    if (source_rid_out_)
        *source_rid_out_ = std::nullopt;
    topic_out_.clear ();
    has_more_out_ = false;
    part_out_.close ();

    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
        return -1;
    }

    char topic_buffer[256];
    size_t topic_length = sizeof (topic_buffer);
    const zlink_routing_id_t *source_rid = nullptr;

    zlink_msg_t native_part;
    if (zlink_msg_init (&native_part) != 0)
        return -1;

    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const int rc = zlink_spot_subscribe_part (
      _impl->handle, &source_rid, topic_buffer, sizeof (topic_buffer),
      &topic_length, &native_part, &has_more,
      static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
    if (rc != ZLINK_RECV_OK) {
        const int err = errno;
        (void) zlink_msg_close (&native_part);
        errno = err;
        return rc;
    }

    const size_t topic_size = topic_length < sizeof (topic_buffer)
                                ? topic_length
                                : sizeof (topic_buffer) - 1u;
    topic_out_.assign (topic_buffer, topic_size);
    if (source_rid_out_ && source_rid && source_rid->size > 0)
        *source_rid_out_ = zlink::detail::native_routing_id (*source_rid);
    zlink::detail::adopt_native_message (part_out_, &native_part);
    has_more_out_ = has_more != ZLINK_PART_FINAL;
    return 0;
}

[[nodiscard]] int
spot_t::subscription_event_impl (routing_id_t &source_rid_out_,
                                 bool &subscribed_out_,
                                 std::string &topic_,
                                 recv_flags_t flags_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
        return -1;
    }

    char topic_buffer[256];
    size_t topic_length = 0;
    int subscribed = 0;
    const zlink_routing_id_t *source_rid = nullptr;
    const int rc = zlink_spot_recv_subscription_event (
      _impl->handle, &source_rid, &subscribed, topic_buffer,
      sizeof (topic_buffer), &topic_length,
      static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
    if (rc != 0)
        return rc;

    if (source_rid && source_rid->size > 0)
        source_rid_out_ = zlink::detail::native_routing_id (*source_rid);
    else
        source_rid_out_ = zlink::detail::unchecked_empty_routing_id ();

    const size_t topic_size = topic_length < sizeof (topic_buffer)
                                ? topic_length
                                : sizeof (topic_buffer) - 1u;
    topic_.assign (topic_buffer, topic_size);
    subscribed_out_ = subscribed != 0;
    return 0;
}

[[nodiscard]] int
spot_t::publish_no_wait_result_impl (send_result_t &result_out_,
                                     const char *topic_,
                                     std::vector<message_t> &parts_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
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
          return zlink_spot_publish_part (_impl->handle, topic_, part_out_,
                                          ZLINK_DONTWAIT, part_flag_);
      });
    if (rc == 0) {
        result_out_ = send_result_t::sent;
        return 0;
    }
    if (rc == ZLINK_SUBMIT_BACKPRESSURED || rc == ZLINK_SUBMIT_NOT_CONNECTED) {
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

[[nodiscard]] int spot_t::publish_no_wait_result_impl (
  send_result_t &result_out_, const char *topic_, message_t &part_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
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

    const int rc = zlink_spot_publish_part (_impl->handle, topic_, &native,
                                            ZLINK_DONTWAIT, ZLINK_PART_FINAL);
    if (rc == 0) {
        result_out_ = send_result_t::sent;
        return 0;
    }
    if (rc == ZLINK_SUBMIT_BACKPRESSURED || rc == ZLINK_SUBMIT_NOT_CONNECTED) {
        result_out_ = zlink::detail::to_send_result (rc);
        part_.init ();
        if (part_.valid ())
            (void) zlink_msg_move (zlink::detail::native_handle (part_),
                                   &native);
        (void) zlink_msg_close (&native);
        return 0;
    }

    const int err = errno;
    if (detail::classify_nonblocking_send_errno (err, result_out_)) {
        if (result_out_ != send_result_t::sent) {
            part_.init ();
            if (part_.valid ())
                (void) zlink_msg_move (zlink::detail::native_handle (part_),
                                       &native);
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

} // namespace service
} // namespace zlink
