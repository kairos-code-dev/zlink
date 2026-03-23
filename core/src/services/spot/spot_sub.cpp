/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_sub.hpp"
#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"

#include "sockets/socket_base.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"

#include <stdio.h>
#include <string.h>

namespace zlink
{
static bool spot_frame_has_more (const zlink_msg_t &msg_)
{
    return (reinterpret_cast<const msg_t *> (&msg_)->flags () & msg_t::more)
           != 0;
}

static const uint32_t spot_sub_tag_value = 0x1e6700da;
static const char spot_ready_probe_prefix[] = "__zlink.ready__/";
static const char spot_ready_probe_marker[] =
  "\x00zlink.ready.probe.v1\x00";

namespace
{
static void emit_monitor_event_batch (service_monitor_hub_t *monitor_,
                                      zlink_service_event_t *events_,
                                      size_t count_)
{
    if (!monitor_ || !events_ || count_ == 0)
        return;
    monitor_->emit_batch (events_, count_);
}

static void close_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

static void close_msgv (std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        zlink_msg_close (&(*parts_)[i]);
    parts_->clear ();
}


static bool is_ready_probe_message (const char *topic_,
                                    size_t topic_len_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    std::string *raw_filter_out_,
                                    std::string *peer_endpoint_out_)
{
    if (!topic_ || topic_len_ == 0 || !parts_ || part_count_ != 2)
        return false;
    if (zlink_msg_size (&parts_[0]) != sizeof (spot_ready_probe_marker) - 1)
        return false;
    if (memcmp (zlink_msg_data (&parts_[0]), spot_ready_probe_marker,
                sizeof (spot_ready_probe_marker) - 1)
        != 0) {
        return false;
    }
    if (zlink_msg_size (&parts_[1]) == 0)
        return false;

    if (raw_filter_out_)
        raw_filter_out_->assign (topic_, topic_len_);
    if (peer_endpoint_out_) {
        peer_endpoint_out_->assign (
          static_cast<const char *> (zlink_msg_data (&parts_[1])),
          zlink_msg_size (&parts_[1]));
    }
    return true;
}

static bool may_be_ready_probe_topic (const char *topic_, size_t topic_len_)
{
    return topic_ && topic_len_ >= sizeof (spot_ready_probe_prefix) - 1
           && memcmp (topic_, spot_ready_probe_prefix,
                      sizeof (spot_ready_probe_prefix) - 1)
                == 0;
}

static std::string make_subject_key (const std::string &subject_,
                                     uint32_t subject_kind_)
{
    char prefix[16];
    snprintf (prefix, sizeof (prefix), "%u:", subject_kind_);
    return std::string (prefix) + subject_;
}

}

spot_sub_t::subject_descriptor_t::subject_descriptor_t () :
    subject_kind (ZLINK_SERVICE_EVENT_SUBJECT_NONE)
{
}

spot_sub_t::spot_sub_t (spot_node_t *node_,
                        socket_base_t *socket_,
                        uint64_t attachment_id_,
                        bool node_owned_default_) :
    _node (node_),
    _socket (socket_),
    _runtime (node_ ? node_->runtime () : NULL),
    _attachment_id (attachment_id_),
    _tag (spot_sub_tag_value),
    _node_owned_default (node_owned_default_),
    _routing_id_locked (false),
    _direct_handler_binding_index (0),
    _active_direct_handler (NULL),
    _handler_state (handler_none),
    _callback_inflight (0),
    _destroying (false),
    _monitor (node_ ? node_->ctx () : NULL),
    _monitor_event_queue (),
    _monitor_event_draining (false),
    _monitor_event_pending (0),
    _raw_monitor_socket (NULL),
    _monitor_stop (0),
    _monitor_thread_started (false)
{
    memset (&_routing_id, 0, sizeof (_routing_id));
    initialize_routing_id (&_routing_id);
}

spot_sub_t::~spot_sub_t ()
{
    _tag = 0xdeadbeef;
}

bool spot_sub_t::check_tag () const
{
    return _tag == spot_sub_tag_value;
}

bool spot_sub_t::is_node_owned_default () const
{
    return _node_owned_default;
}

void spot_sub_t::emit_monitor_event (const zlink_service_event_t &event_)
{
    if (_destroying.load (std::memory_order_acquire))
        return;
    _monitor.emit (event_);
}

socket_base_t *spot_sub_t::socket () const
{
    return _socket;
}

int spot_sub_t::initialize_routing_id (zlink_routing_id_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    const uint32_t value = generate_random ();
    out_->size = sizeof (value);
    memcpy (out_->data, &value, sizeof (value));
    return 0;
}

bool spot_sub_t::is_valid_topic (const char *topic_, std::string *out_)
{
    if (!topic_ || topic_[0] == '\0')
        return false;
    const size_t len = strlen (topic_);
    if (len == 0 || len > 255)
        return false;
    const std::string value (topic_, len);
    if (spot_control_protocol::is_reserved_subject (value))
        return false;
    if (out_)
        *out_ = value;
    return true;
}

bool spot_sub_t::is_valid_pattern (const char *pattern_, std::string *prefix_out_)
{
    if (!pattern_ || pattern_[0] == '\0')
        return false;
    const size_t len = strlen (pattern_);
    if (len < 2 || len > 255 || pattern_[len - 1] != '*')
        return false;
    const char *star = strchr (pattern_, '*');
    if (star != pattern_ + len - 1)
        return false;
    const std::string prefix (pattern_, len - 1);
    if (spot_control_protocol::is_reserved_subject (prefix))
        return false;
    if (prefix_out_)
        *prefix_out_ = prefix;
    return true;
}

void spot_sub_t::lock_routing_id ()
{
    _routing_id_locked = true;
}

int spot_sub_t::subscribe (const char *topic_)
{
    socket_base_t *socket = this->socket ();
    if (!_node || !socket) {
        errno = EFAULT;
        return -1;
    }
    std::string topic;
    if (!is_valid_topic (topic_, &topic)) {
        errno = EINVAL;
        return -1;
    }
    if (_node->ensure_healthy () != 0)
        return -1;

    bool had_filters = false;
    bool has_filters = false;
    {
        scoped_lock_t lock (_sync);
        had_filters = !_topics.empty () || !_patterns.empty ();
        lock_routing_id ();
        if (socket->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, topic.data (), topic.size ())
            != 0)
            return -1;
        _topics.insert (topic);
        _delivery_ready_raw_filters.erase (topic);
        has_filters = !_topics.empty () || !_patterns.empty ();
    }
    if (_node) {
        scoped_lock_t node_lock (_node->_sync);
        _node->_subject_last_changed_ms[make_subject_key (
          topic, ZLINK_SERVICE_EVENT_SUBJECT_TOPIC)] = zlink::clock_t ().now_ms ();
        _node->_summary_last_changed_ms = zlink::clock_t ().now_ms ();
    }
    if (_node) {
        _node->note_local_sub_filters_changed (had_filters, has_filters);
        emit_filter_applied_event (topic.c_str (),
                                   ZLINK_SERVICE_EVENT_SUBJECT_TOPIC);
        if (_node->send_subscription_update (topic, true) != 0)
            return -1;
        if (_node->has_active_peers ())
            _node->notify_subscription_forwarded (topic);
        _node->schedule_subscription_replay ();
        if (_node->replay_subscriptions_if_active_peers () != 0)
            return -1;
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_READY, 0);
    }
    return 0;
}

int spot_sub_t::subscribe_pattern (const char *pattern_)
{
    socket_base_t *socket = this->socket ();
    if (!_node || !socket) {
        errno = EFAULT;
        return -1;
    }
    std::string prefix;
    if (!is_valid_pattern (pattern_, &prefix)) {
        errno = EINVAL;
        return -1;
    }
    if (_node->ensure_healthy () != 0)
        return -1;

    bool had_filters = false;
    bool has_filters = false;
    {
        scoped_lock_t lock (_sync);
        had_filters = !_topics.empty () || !_patterns.empty ();
        lock_routing_id ();
        if (socket->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, prefix.data (), prefix.size ())
            != 0)
            return -1;
        _patterns.insert (prefix);
        _delivery_ready_raw_filters.erase (prefix);
        has_filters = !_topics.empty () || !_patterns.empty ();
    }
    if (_node) {
        scoped_lock_t node_lock (_node->_sync);
        _node->_subject_last_changed_ms[make_subject_key (
          prefix + "*", ZLINK_SERVICE_EVENT_SUBJECT_PATTERN)] =
          zlink::clock_t ().now_ms ();
        _node->_summary_last_changed_ms = zlink::clock_t ().now_ms ();
    }
    if (_node) {
        _node->note_local_sub_filters_changed (had_filters, has_filters);
        std::string subject = prefix + "*";
        emit_filter_applied_event (subject.c_str (),
                                   ZLINK_SERVICE_EVENT_SUBJECT_PATTERN);
        if (_node->send_subscription_update (prefix, true) != 0)
            return -1;
        if (_node->has_active_peers ())
            _node->notify_subscription_forwarded (prefix);
        _node->schedule_subscription_replay ();
        if (_node->replay_subscriptions_if_active_peers () != 0)
            return -1;
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_READY, 0);
    }
    return 0;
}

int spot_sub_t::unsubscribe (const char *topic_or_pattern_)
{
    socket_base_t *socket = this->socket ();
    if (!_node || !socket) {
        errno = EFAULT;
        return -1;
    }
    std::string topic;
    std::string prefix;
    const bool is_pattern = is_valid_pattern (topic_or_pattern_, &prefix);
    const bool is_topic = !is_pattern && is_valid_topic (topic_or_pattern_, &topic);
    if (!is_pattern && !is_topic) {
        errno = EINVAL;
        return -1;
    }
    if (_node->ensure_healthy () != 0)
        return -1;

    bool had_filters = false;
    bool has_filters_after = false;
    int first_error = 0;
    std::vector<std::string> ready_ack_endpoints;
    subject_descriptor_t subject;
    subject.subject_kind = is_pattern ? ZLINK_SERVICE_EVENT_SUBJECT_PATTERN
                                      : ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
    subject.subject = is_pattern ? prefix + "*" : topic;
    const std::string filter = is_pattern ? prefix : topic;
    {
        scoped_lock_t lock (_sync);
        had_filters = !_topics.empty () || !_patterns.empty ();
        lock_routing_id ();
        if (socket->setsockopt (ZLINK_INTERNAL_OPT_UNSUBSCRIBE, filter.data (), filter.size ())
            != 0)
            return -1;
        if (is_pattern)
            _patterns.erase (prefix);
        else
            _topics.erase (topic);
        _delivery_ready_raw_filters.erase (filter);
        has_filters_after = !_topics.empty () || !_patterns.empty ();
    }
    if (_node) {
        scoped_lock_t node_lock (_node->_sync);
        _node->_subject_last_changed_ms[make_subject_key (
          subject.subject, subject.subject_kind)] = zlink::clock_t ().now_ms ();
        _node->_summary_last_changed_ms = zlink::clock_t ().now_ms ();
    }
    if (_node)
        _node->note_local_sub_filters_changed (had_filters, has_filters_after);
    if (_node && _node->send_subscription_update (filter, false) != 0)
        first_error = errno != 0 ? errno : EIO;
    release_ready_ack_endpoints (filter, &ready_ack_endpoints);
    if (_node) {
        const std::string ack_source_id = ready_ack_source_id ();
        for (size_t i = 0; i < ready_ack_endpoints.size (); ++i) {
            (void) _node->send_ready_ack_update (ready_ack_endpoints[i], filter,
                                                 ack_source_id, false);
        }
    }
    if (_node) {
        _node->submit_sub_summary (this, has_filters_after
                                           ? ZLINK_TOPOLOGY_STATE_READY
                                           : ZLINK_TOPOLOGY_STATE_CONNECTING,
                                   0);
    }
    mark_subject_lost (subject, NULL);
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}

int spot_sub_t::set_option (int option_,
                            const void *optval_,
                            size_t optvallen_)
{
    socket_base_t *socket = this->socket ();
    if (!socket) {
        errno = EFAULT;
        return -1;
    }
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    int socket_option = -1;
    switch (option_) {
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
            socket_option = ZLINK_INTERNAL_OPT_RCVHWM;
            break;
        case ZLINK_SPOT_SUB_OPT_LINGER:
            socket_option = ZLINK_INTERNAL_OPT_LINGER;
            break;
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
            socket_option = ZLINK_INTERNAL_OPT_SNDBUF;
            break;
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
            socket_option = ZLINK_INTERNAL_OPT_RCVBUF;
            break;
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            socket_option = ZLINK_INTERNAL_OPT_RCVTIMEO;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    scoped_lock_t lock (_sync);
    return socket->setsockopt (socket_option, optval_, optvallen_);
}

int spot_sub_t::set_routing_id (const void *data_, size_t size_)
{
    if (!data_ || size_ == 0 || size_ > sizeof (_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_routing_id_locked) {
        errno = EFSM;
        return -1;
    }
    _routing_id.size = static_cast<uint8_t> (size_);
    memcpy (_routing_id.data, data_, size_);
    return 0;
}

int spot_sub_t::routing_id (zlink_routing_id_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    *out_ = _routing_id;
    return 0;
}

int spot_sub_t::fill_monitor_snapshot (zlink_monitor_snapshot_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    socket_base_t *socket = this->socket ();
    if (!socket) {
        errno = EFAULT;
        return -1;
    }
    if (socket->monitor_snapshot (out_) != 0)
        return -1;
    out_->source_kind = ZLINK_MONITOR_SOURCE_SPOT_SUB;
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        const uint32_t ready_count =
          static_cast<uint32_t> (_ready_peer_endpoints.size ());
        if (out_->ready_count < ready_count)
            out_->ready_count = ready_count;
    }
    out_->detail_flags |= ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_COUNT;
    if (out_->ready_count > 0)
        out_->state_flags |= ZLINK_MONITOR_STATE_READY;
    else
        out_->state_flags &= ~ZLINK_MONITOR_STATE_READY;
    return 0;
}

void *spot_sub_t::monitor_open (int events_)
{
    lock_routing_id ();
    if (ensure_monitor_bridge_started () != 0)
        return NULL;
    return _monitor.open (events_);
}

bool spot_sub_t::has_filters () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return !_topics.empty () || !_patterns.empty ();
}

int spot_sub_t::set_direct_handler (spot_sub_direct_handler_fn handler_,
                                    void *userdata_)
{
    socket_base_t *socket = this->socket ();
    if (!socket) {
        errno = EFAULT;
        return -1;
    }
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }
    if (_node && _node->ensure_healthy () != 0)
        return -1;
    {
        scoped_lock_t lock (_sync);
        if (_handler_state.load (std::memory_order_acquire) == handler_active) {
            const unsigned int next_binding =
              (_direct_handler_binding_index + 1) % 2;
            _direct_handler_bindings[next_binding].handler = handler_;
            _direct_handler_bindings[next_binding].userdata = userdata_;
            _active_direct_handler.store (&_direct_handler_bindings[next_binding],
                                          std::memory_order_release);
            _direct_handler_binding_index = next_binding;
            return 0;
        }
        if (_handler_state.load (std::memory_order_acquire) != handler_none) {
            errno = EBUSY;
            return -1;
        }
        _direct_handler_bindings[0].handler = handler_;
        _direct_handler_bindings[0].userdata = userdata_;
        _direct_handler_binding_index = 0;
        _active_direct_handler.store (&_direct_handler_bindings[0],
                                      std::memory_order_release);
        _handler_state.store (handler_active, std::memory_order_release);
    }

    if (socket->sub_dispatch_start (&spot_sub_t::dispatch_from_io, this) == 0) {
        return 0;
    }

    {
        scoped_lock_t lock (_sync);
        _active_direct_handler.store (NULL, std::memory_order_release);
        _handler_state.store (handler_none, std::memory_order_release);
    }
    return -1;
}

int spot_sub_t::recv (zlink_routing_id_t *source_rid_out_,
                      zlink_msg_t **parts_,
                      size_t *part_count_,
                      int flags_,
                      char *topic_out_,
                      size_t *topic_len_)
{
    socket_base_t *socket = this->socket ();
    if (!parts_ || !part_count_) {
        errno = EINVAL;
        return -1;
    }
    if (!_node || !socket) {
        errno = EFAULT;
        return -1;
    }
    if (!_runtime || _runtime->ensure_healthy () != 0)
        return -1;

    if (source_rid_out_)
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    {
        scoped_lock_t lock (_sync);
        if (_handler_state.load (std::memory_order_acquire) != handler_none) {
            errno = EBUSY;
            return -1;
        }
        lock_routing_id ();
    }

    while (true) {
        *parts_ = NULL;
        *part_count_ = 0;

        std::vector<zlink_msg_t> frames;
        frames.reserve (2);

        int rc = 0;
        zlink_msg_t topic_frame;
        zlink_msg_init (&topic_frame);
        rc = socket->recv (reinterpret_cast<msg_t *> (&topic_frame), flags_);
        if (rc != 0) {
            zlink_msg_close (&topic_frame);
            return -1;
        }
        frames.push_back (topic_frame);

        if (spot_frame_has_more (topic_frame)) {
            zlink_msg_t first_payload_frame;
            zlink_msg_init (&first_payload_frame);
            rc = socket->recv (reinterpret_cast<msg_t *> (&first_payload_frame), 0);
            if (rc != 0) {
                zlink_msg_close (&first_payload_frame);
                close_msgv (&frames);
                return -1;
            }

            if (!spot_frame_has_more (first_payload_frame)) {
                const char *topic_data = static_cast<const char *> (
                  zlink_msg_data (&frames[0]));
                const size_t topic_size = zlink_msg_size (&frames[0]);

                if (topic_len_) {
                    const size_t capacity = *topic_len_;
                    *topic_len_ = topic_size;
                    if (topic_out_ && capacity < topic_size) {
                        if (source_rid_out_)
                            socket->copy_last_recv_source_rid (source_rid_out_);
                        zlink_msg_close (&first_payload_frame);
                        close_msgv (&frames);
                        *topic_len_ = topic_size;
                        errno = EMSGSIZE;
                        return -1;
                    }
                    if (topic_out_ && topic_size > 0)
                        memcpy (topic_out_, topic_data, topic_size);
                    if (topic_out_ && capacity > topic_size)
                        topic_out_[topic_size] = '\0';
                } else if (topic_out_) {
                    if (topic_size > 0)
                        memcpy (topic_out_, topic_data, topic_size);
                    topic_out_[topic_size] = '\0';
                }

                zlink_msg_t *payload =
                  static_cast<zlink_msg_t *> (malloc (sizeof (zlink_msg_t)));
                if (!payload) {
                    zlink_msg_close (&first_payload_frame);
                    close_msgv (&frames);
                    errno = ENOMEM;
                    return -1;
                }
                memset (payload, 0, sizeof (zlink_msg_t));

                msg_t *dst = reinterpret_cast<msg_t *> (payload);
                if (dst->init () != 0
                    || dst->move (
                         *reinterpret_cast<msg_t *> (&first_payload_frame))
                         != 0) {
                    zlink_msg_close (&first_payload_frame);
                    zlink_msg_close (payload);
                    free (payload);
                    close_msgv (&frames);
                    errno = EFAULT;
                    return -1;
                }

                zlink_msg_close (&frames[0]);
                zlink_msg_close (&first_payload_frame);
                if (source_rid_out_)
                    socket->copy_last_recv_source_rid (source_rid_out_);
                *parts_ = payload;
                *part_count_ = 1;
                return 0;
            }

            frames.push_back (first_payload_frame);
            while (true) {
                zlink_msg_t frame;
                zlink_msg_init (&frame);
                rc = socket->recv (reinterpret_cast<msg_t *> (&frame), 0);
                if (rc != 0) {
                    zlink_msg_close (&frame);
                    close_msgv (&frames);
                    return -1;
                }
                frames.push_back (frame);
                if (!spot_frame_has_more (frame))
                    break;
            }
        }

        if (rc != 0)
            return -1;
        if (frames.empty ()) {
            errno = EPROTO;
            return -1;
        }

        zlink_msg_t &topic = frames[0];
        const char *topic_data =
          static_cast<const char *> (zlink_msg_data (&topic));
        const size_t topic_size = zlink_msg_size (&topic);

        if (may_be_ready_probe_topic (topic_data, topic_size)) {
            std::string raw_filter;
            std::string peer_endpoint;
            if (is_ready_probe_message (topic_data,
                                        topic_size,
                                        frames.size () > 1 ? &frames[1] : NULL,
                                        frames.size () - 1,
                                        &raw_filter,
                                        &peer_endpoint)) {
                close_msgv (&frames);
                handle_ready_probe (raw_filter, peer_endpoint);
                continue;
            }
        }

        if (topic_len_) {
            const size_t capacity = *topic_len_;
            *topic_len_ = topic_size;
            if (topic_out_ && capacity < topic_size) {
                if (source_rid_out_)
                    socket->copy_last_recv_source_rid (source_rid_out_);
                close_msgv (&frames);
                *topic_len_ = topic_size;
                errno = EMSGSIZE;
                return -1;
            }
            if (topic_out_ && topic_size > 0)
                memcpy (topic_out_, topic_data, topic_size);
            if (topic_out_ && capacity > topic_size)
                topic_out_[topic_size] = '\0';
        } else if (topic_out_) {
            if (topic_size > 0)
                memcpy (topic_out_, topic_data, topic_size);
            topic_out_[topic_size] = '\0';
        }

        const size_t payload_count = frames.size () - 1;
        if (payload_count == 0) {
            close_msgv (&frames);
            return 0;
        }

        zlink_msg_t *payload = static_cast<zlink_msg_t *> (
          malloc (payload_count * sizeof (zlink_msg_t)));
        if (!payload) {
            close_msgv (&frames);
            errno = ENOMEM;
            return -1;
        }
        memset (payload, 0, payload_count * sizeof (zlink_msg_t));

        for (size_t i = 0; i < payload_count; ++i) {
            msg_t *dst = reinterpret_cast<msg_t *> (&payload[i]);
            if (dst->init () != 0
                || dst->move (*reinterpret_cast<msg_t *> (&frames[i + 1])) != 0) {
                for (size_t j = 0; j <= i; ++j)
                    zlink_msg_close (&payload[j]);
                free (payload);
                close_msgv (&frames);
                errno = EFAULT;
                return -1;
            }
        }

        zlink_msg_close (&frames[0]);
        if (source_rid_out_)
            socket->copy_last_recv_source_rid (source_rid_out_);
        *parts_ = payload;
        *part_count_ = payload_count;
        return 0;
    }
}

void spot_sub_t::dispatch_from_io (const zlink_routing_id_t *source_rid_,
                                   const char *topic_,
                                   size_t topic_len_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   void *userdata_)
{
    spot_sub_t *self = static_cast<spot_sub_t *> (userdata_);
    if (!self) {
        close_parts (parts_, part_count_);
        return;
    }

    if (may_be_ready_probe_topic (topic_, topic_len_)) {
        std::string raw_filter;
        std::string peer_endpoint;
        if (is_ready_probe_message (topic_, topic_len_, parts_, part_count_,
                                    &raw_filter, &peer_endpoint)) {
            self->handle_ready_probe (raw_filter, peer_endpoint);
            close_parts (parts_, part_count_);
            return;
        }
    }

    direct_handler_binding_t *binding =
      self->_active_direct_handler.load (std::memory_order_acquire);
    if (self->_handler_state.load (std::memory_order_acquire) != handler_active
        || !binding || !binding->handler) {
        close_parts (parts_, part_count_);
        return;
    }

    self->_callback_inflight.add (1);
    binding->handler (source_rid_, topic_, topic_len_, parts_, part_count_,
                      binding->userdata);

    const bool callbacks_remaining = self->_callback_inflight.sub (1);
    if (!callbacks_remaining
        && self->_handler_state.load (std::memory_order_acquire)
             != handler_active) {
        scoped_lock_t lock (self->_sync);
        self->_callback_cv.broadcast ();
    }
}

}
