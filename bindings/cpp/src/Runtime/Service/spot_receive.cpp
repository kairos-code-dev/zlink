/* SPDX-License-Identifier: MPL-2.0 */

#include <Runtime/Service/spot_impl.hpp>
#include <Runtime/Native/native_send.hpp>
#include <Runtime/Service/detail.hpp>
#include <Runtime/Native/subscription_reader.hpp>

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
    return subscribe_part_impl (source_rid_out_, topic_out_, part_out_, has_more_out_, flags_);
}

int spot_t::subscribe_part (std::string &topic_out_, message_t &part_out_, bool &has_more_out_, recv_flags_t flags_)
{
    return subscribe_part_impl (nullptr, topic_out_, part_out_, has_more_out_, flags_);
}

int spot_t::receive_subscription_event (subscription_event_t &out_, recv_flags_t flags_)
{
    std::string topic;
    routing_id_t source_rid = zlink::detail::unchecked_empty_routing_id ();
    bool subscribed = false;
    const recv_result_t rc =
      static_cast<recv_result_t> (subscription_event_impl (source_rid, subscribed, topic, flags_));
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
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_set_subscription (_impl->handle, filter_.c_str ())));
}

void spot_t::unset_subscription (const std::string &filter_)
{
    zlink::detail::validate_no_embedded_null (filter_, "filter");
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_unset_subscription (_impl->handle, filter_.c_str ())));
}

void spot_t::subscription_at (size_t index_, std::string &filter_out_, bool *is_pattern_out_) const
{
    int is_pattern = 0;
    config_result_t result = config_result_t::ok;
    const int rc = zlink::detail::read_growing_string (
      [&] (char *buffer_, size_t, size_t *length_out_) {
          result = static_cast<config_result_t> (
            zlink_subscription_at (_impl->handle, index_, buffer_, length_out_, &is_pattern));
          return result == config_result_t::ok ? 0 : -1;
      },
      256u, filter_out_);
    if (rc != 0)
        throw config_error_t (result, zlink_errno ());
    if (is_pattern_out_)
        *is_pattern_out_ = is_pattern != 0;
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
    const handler_result_t rc = static_cast<handler_result_t> (zlink_send_ready_handler (
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
    zlink::detail::validate_bounded_c_string (channel_name_, 255u, "channel_name");
    if (channel_name_.empty ()) {
        errno = EINVAL;
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

[[nodiscard]] int spot_t::publish_impl (const char *topic_, std::vector<message_t> &parts_, send_flags_t flags_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
        return -1;
    }

    if (parts_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    const int rc =
      detail::submit_message_parts (parts_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return zlink_spot_publish_part (_impl->handle, topic_, part_out_,
                                          static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_);
      });
    return rc;
}

[[nodiscard]] int spot_t::publish_impl (const char *topic_, message_t &part_, send_flags_t flags_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
        return -1;
    }

    if (!part_.valid ()) {
        errno = EINVAL;
        return -1;
    }

    return zlink::detail::submit_single_message_part_restore (part_, [&] (zlink_msg_t *native_) {
        return zlink_spot_publish_part (_impl->handle, topic_, native_,
                                        static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), ZLINK_PART_FINAL);
    });
}

[[nodiscard]] int
spot_t::send_channel_impl (const char *channel_name_, std::vector<message_t> &parts_, send_flags_t flags_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
        return -1;
    }

    if (parts_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    const int rc =
      detail::submit_message_parts (parts_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          return zlink_spot_send_channel_part (_impl->handle, channel_name_, part_out_,
                                               static_cast<zlink_send_flags_t> (static_cast<int> (flags_)), part_flag_);
      });
    return rc;
}

[[nodiscard]] int
spot_t::send_channel_no_wait_result_impl (send_result_t &result_out_, const char *channel_name_, message_t &part_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
        return -1;
    }

    if (!part_.valid ()) {
        errno = EINVAL;
        return -1;
    }

    return zlink::detail::submit_single_message_part_no_wait_result (result_out_, part_, [&] (zlink_msg_t *native_) {
        return zlink_spot_send_channel_part (_impl->handle, channel_name_, native_, ZLINK_DONTWAIT, ZLINK_PART_FINAL);
    });
}

[[nodiscard]] int spot_t::subscribe_impl (topic_message_t &message_out_, recv_flags_t flags_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
        return -1;
    }

    return zlink::detail::read_subscription_message (message_out_, [&] (const zlink_routing_id_t **source_rid_out_,
                                                                        char *topic_out_, size_t topic_capacity_,
                                                                        size_t *topic_size_out_, zlink_msg_t *part_out_,
                                                                        zlink_part_flag_t *has_more_out_) {
        const zlink_recv_flags_t native_flags =
          topic_out_ ? static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)) : ZLINK_RECV_FLAGS_DONTWAIT;
        return static_cast<int> (zlink_spot_subscribe_part (_impl->handle, source_rid_out_, topic_out_, topic_capacity_,
                                                            topic_size_out_, part_out_, has_more_out_, native_flags));
    });
}

[[nodiscard]] int spot_t::subscribe_part_impl (std::optional<routing_id_t> &source_rid_out_,
                                               std::string &topic_out_,
                                               message_t &part_out_,
                                               bool &has_more_out_,
                                               recv_flags_t flags_)
{
    source_rid_out_ = std::nullopt;
    return subscribe_part_impl (&source_rid_out_, topic_out_, part_out_, has_more_out_, flags_);
}

[[nodiscard]] int spot_t::subscribe_part_impl (std::optional<routing_id_t> *source_rid_out_,
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

    zlink::detail::scoped_native_message_t native_part;
    if (!native_part.init ())
        return -1;

    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const int rc = zlink_spot_subscribe_part (_impl->handle, &source_rid, topic_buffer, sizeof (topic_buffer),
                                              &topic_length, native_part.get (), &has_more,
                                              static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
    if (rc != ZLINK_RECV_OK)
        return rc;

    zlink::detail::assign_subscription_part (source_rid_out_, topic_out_, part_out_, has_more_out_, source_rid,
                                             topic_buffer, topic_length, sizeof (topic_buffer), native_part.get (),
                                             has_more);
    return 0;
}

[[nodiscard]] int spot_t::subscription_event_impl (routing_id_t &source_rid_out_,
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
    const int rc =
      zlink_spot_recv_subscription_event (_impl->handle, &source_rid, &subscribed, topic_buffer, sizeof (topic_buffer),
                                          &topic_length, static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
    if (rc != 0)
        return rc;

    source_rid_out_ = zlink::detail::routing_id_or_empty (source_rid);

    const size_t topic_size = zlink::detail::bounded_topic_size (topic_length, sizeof (topic_buffer));
    topic_.assign (topic_buffer, topic_size);
    subscribed_out_ = subscribed != 0;
    return 0;
}

[[nodiscard]] int
spot_t::publish_no_wait_result_impl (send_result_t &result_out_, const char *topic_, std::vector<message_t> &parts_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
        return -1;
    }

    if (parts_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    return zlink::detail::send_parts_no_wait_result (
      result_out_, parts_, [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_) {
          return zlink_spot_publish_part (_impl->handle, topic_, part_out_, ZLINK_DONTWAIT, part_flag_);
      });
}

[[nodiscard]] int spot_t::publish_no_wait_result_impl (send_result_t &result_out_, const char *topic_, message_t &part_)
{
    if (!_impl->handle) {
        errno = _impl->last_error != 0 ? _impl->last_error : EFAULT;
        return -1;
    }

    if (!part_.valid ()) {
        errno = EINVAL;
        return -1;
    }

    return zlink::detail::submit_single_message_part_no_wait_result (result_out_, part_, [&] (zlink_msg_t *native_) {
        return zlink_spot_publish_part (_impl->handle, topic_, native_, ZLINK_DONTWAIT, ZLINK_PART_FINAL);
    });
}

} // namespace service
} // namespace zlink
