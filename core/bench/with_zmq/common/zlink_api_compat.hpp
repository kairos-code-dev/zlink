#ifndef BENCH_WITH_ZMQ_ZLINK_API_COMPAT_HPP
#define BENCH_WITH_ZMQ_ZLINK_API_COMPAT_HPP

#include <zlink.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

typedef int zlink_socket_type_t;
typedef int zlink_option_t;
typedef int zlink_pub_option_t;
typedef int zlink_send_flags_t;
#ifndef ZLINK_SOCKET_MONITOR_EVENT_MASK_T_DEFINED
typedef uint64_t zlink_socket_monitor_event_mask_t;
#endif

#ifndef ZLINK_SOCKET_MONITOR_OPEN_OPTIONS_T_DEFINED
typedef struct zlink_socket_monitor_open_options_t
{
    zlink_socket_monitor_event_mask_t events;
} zlink_socket_monitor_open_options_t;
#endif

#ifndef ZLINK_MONITOR_SNAPSHOT_T_DEFINED
typedef struct zlink_monitor_snapshot_t
{
    uint32_t source_kind;
    uint32_t state_flags;
    uint32_t detail_flags;
    uint32_t ready_count;
    uint64_t snd_pending_msgs;
    uint64_t rcv_pending_msgs;
} zlink_monitor_snapshot_t;
#endif

#ifndef ZLINK_SOCKET_PAIR
#define ZLINK_SOCKET_PAIR ZLINK_PAIR
#endif
#ifndef ZLINK_SOCKET_PUB
#define ZLINK_SOCKET_PUB ZLINK_PUB
#endif
#ifndef ZLINK_SOCKET_SUB
#define ZLINK_SOCKET_SUB ZLINK_SUB
#endif
#ifndef ZLINK_SOCKET_DEALER
#define ZLINK_SOCKET_DEALER ZLINK_DEALER
#endif
#ifndef ZLINK_SOCKET_ROUTER
#define ZLINK_SOCKET_ROUTER ZLINK_ROUTER
#endif
#ifndef ZLINK_SOCKET_STREAM
#define ZLINK_SOCKET_STREAM ZLINK_STREAM
#endif

#ifndef ZLINK_OPT_LINGER
#define ZLINK_OPT_LINGER ZLINK_LINGER
#endif
#ifndef ZLINK_OPT_SNDHWM
#define ZLINK_OPT_SNDHWM ZLINK_SNDHWM
#endif
#ifndef ZLINK_OPT_RCVHWM
#define ZLINK_OPT_RCVHWM ZLINK_RCVHWM
#endif
#ifndef ZLINK_OPT_SNDTIMEO
#define ZLINK_OPT_SNDTIMEO ZLINK_SNDTIMEO
#endif
#ifndef ZLINK_OPT_RCVTIMEO
#define ZLINK_OPT_RCVTIMEO ZLINK_RCVTIMEO
#endif
#ifndef ZLINK_OPT_SNDBUF
#define ZLINK_OPT_SNDBUF ZLINK_SNDBUF
#endif
#ifndef ZLINK_OPT_RCVBUF
#define ZLINK_OPT_RCVBUF ZLINK_RCVBUF
#endif
#ifndef ZLINK_OPT_EVENTS
#define ZLINK_OPT_EVENTS ZLINK_EVENTS
#endif
#ifndef ZLINK_OPT_LAST_ENDPOINT
#define ZLINK_OPT_LAST_ENDPOINT ZLINK_LAST_ENDPOINT
#endif
#ifndef ZLINK_OPT_TCP_NODELAY
#define ZLINK_OPT_TCP_NODELAY ZLINK_TCP_NODELAY
#endif
#ifndef ZLINK_OPT_TLS_CERT
#define ZLINK_OPT_TLS_CERT ZLINK_TLS_CERT
#endif
#ifndef ZLINK_OPT_TLS_KEY
#define ZLINK_OPT_TLS_KEY ZLINK_TLS_KEY
#endif
#ifndef ZLINK_OPT_TLS_CA
#define ZLINK_OPT_TLS_CA ZLINK_TLS_CA
#endif
#ifndef ZLINK_OPT_TLS_HOSTNAME
#define ZLINK_OPT_TLS_HOSTNAME ZLINK_TLS_HOSTNAME
#endif
#ifndef ZLINK_OPT_TLS_TRUST_SYSTEM
#define ZLINK_OPT_TLS_TRUST_SYSTEM ZLINK_TLS_TRUST_SYSTEM
#endif

#ifndef ZLINK_EVENT_CONNECTION_READY_CHANGED
#define ZLINK_EVENT_CONNECTION_READY_CHANGED ZLINK_EVENT_CONNECTION_READY
#endif

#ifndef ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID
#define ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID ZLINK_CONNECT_ROUTING_ID
#endif

enum
{
    ZLINK_PUB_OPT_NODROP = 1
};

inline int zlink_set_option (void *socket_,
                             zlink_option_t option_,
                             const void *optval_,
                             size_t optvallen_)
{
    return ::zlink_setsockopt (socket_, option_, optval_, optvallen_);
}

inline int zlink_get_option (void *socket_,
                             zlink_option_t option_,
                             void *optval_,
                             size_t *optvallen_)
{
    return ::zlink_getsockopt (socket_, option_, optval_, optvallen_);
}

inline int zlink_set_pub_option (void *socket_,
                                 zlink_pub_option_t option_,
                                 const void *optval_,
                                 size_t optvallen_)
{
    if (option_ == ZLINK_PUB_OPT_NODROP) {
#ifdef ZLINK_XPUB_NODROP
        return zlink_set_option (
          socket_, ZLINK_XPUB_NODROP, optval_, optvallen_);
#else
        (void) socket_;
        (void) optval_;
        (void) optvallen_;
        return 0;
#endif
    }

    (void) socket_;
    (void) option_;
    (void) optval_;
    (void) optvallen_;
    errno = ENOTSUP;
    return -1;
}

inline int zlink_set_subscription (void *socket_, const char *filter_)
{
    const char *filter = filter_ ? filter_ : "";
    return ::zlink_setsockopt (
      socket_, ZLINK_SUBSCRIBE, filter, std::strlen (filter));
}

inline int zlink_unset_subscription (void *socket_, const char *filter_)
{
    const char *filter = filter_ ? filter_ : "";
    return ::zlink_setsockopt (
      socket_, ZLINK_UNSUBSCRIBE, filter, std::strlen (filter));
}

inline int zlink_set_routing_id (void *socket_,
                                 const void *data_,
                                 size_t size_)
{
    return ::zlink_setsockopt (socket_, ZLINK_ROUTING_ID, data_, size_);
}

inline int zlink_set_tls_server (void *socket_,
                                 const char *cert_,
                                 const char *key_,
                                 int trust_system_)
{
    if (cert_ && cert_[0] != '\0'
        && zlink_set_option (
             socket_, ZLINK_OPT_TLS_CERT, cert_, std::strlen (cert_))
             != 0) {
        return -1;
    }
    if (key_ && key_[0] != '\0'
        && zlink_set_option (
             socket_, ZLINK_OPT_TLS_KEY, key_, std::strlen (key_))
             != 0) {
        return -1;
    }
    if (trust_system_ != 0
        && zlink_set_option (socket_, ZLINK_OPT_TLS_TRUST_SYSTEM,
                             &trust_system_, sizeof (trust_system_))
             != 0) {
        return -1;
    }
    return 0;
}

inline int zlink_set_tls_client (void *socket_,
                                 const char *ca_,
                                 const char *hostname_,
                                 int trust_system_)
{
    if (ca_ && ca_[0] != '\0'
        && zlink_set_option (
             socket_, ZLINK_OPT_TLS_CA, ca_, std::strlen (ca_))
             != 0) {
        return -1;
    }
    if (hostname_ && hostname_[0] != '\0'
        && zlink_set_option (
             socket_, ZLINK_OPT_TLS_HOSTNAME, hostname_,
             std::strlen (hostname_))
             != 0) {
        return -1;
    }
    if (zlink_set_option (socket_, ZLINK_OPT_TLS_TRUST_SYSTEM,
                          &trust_system_, sizeof (trust_system_))
        != 0) {
        return -1;
    }
    return 0;
}

inline int bench_zlink_close_msg_range (zlink_msg_t *msgs_, size_t count_)
{
    if (!msgs_)
        return 0;
    for (size_t i = 0; i < count_; ++i)
        ::zlink_msg_close (&msgs_[i]);
    return 0;
}

inline int bench_zlink_recv_parts (void *socket_,
                                   zlink_routing_id_t *source_rid_out_,
                                   zlink_msg_t **parts_out_,
                                   size_t *part_count_out_,
                                   zlink_send_flags_t flags_)
{
    if (!parts_out_ || !part_count_out_) {
        errno = EINVAL;
        return -1;
    }

    *parts_out_ = NULL;
    *part_count_out_ = 0;
    if (source_rid_out_)
        std::memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    int socket_type = 0;
    size_t socket_type_size = sizeof (socket_type);
    (void) ::zlink_getsockopt (
      socket_, ZLINK_TYPE, &socket_type, &socket_type_size);

    size_t frame_capacity = 4;
    zlink_msg_t *frames = static_cast<zlink_msg_t *> (
      std::malloc (sizeof (zlink_msg_t) * frame_capacity));
    if (!frames) {
        errno = ENOMEM;
        return -1;
    }

    size_t frame_count = 0;
    int recv_flags = static_cast<int> (flags_);
    while (true) {
        if (frame_count == frame_capacity) {
            const size_t next_capacity = frame_capacity * 2;
            zlink_msg_t *grown = static_cast<zlink_msg_t *> (
              std::realloc (frames, sizeof (zlink_msg_t) * next_capacity));
            if (!grown) {
                bench_zlink_close_msg_range (frames, frame_count);
                std::free (frames);
                errno = ENOMEM;
                return -1;
            }
            frames = grown;
            frame_capacity = next_capacity;
        }

        if (::zlink_msg_init (&frames[frame_count]) != 0) {
            bench_zlink_close_msg_range (frames, frame_count);
            std::free (frames);
            return -1;
        }

        if (::zlink_msg_recv (&frames[frame_count], socket_, recv_flags) < 0) {
            const int saved_errno = errno;
            ::zlink_msg_close (&frames[frame_count]);
            bench_zlink_close_msg_range (frames, frame_count);
            std::free (frames);
            errno = saved_errno;
            return -1;
        }

        const int more = ::zlink_msg_more (&frames[frame_count]);
        ++frame_count;
        if (!more)
            break;
        recv_flags |= ZLINK_DONTWAIT;
    }

    size_t start_index = 0;
    if (frame_count > 0
        && (socket_type == ZLINK_ROUTER || socket_type == ZLINK_STREAM)) {
        if (source_rid_out_) {
            const size_t rid_size =
              std::min (static_cast<size_t> (255),
                        ::zlink_msg_size (&frames[0]));
            source_rid_out_->size = static_cast<uint8_t> (rid_size);
            if (rid_size > 0) {
                std::memcpy (
                  source_rid_out_->data, ::zlink_msg_data (&frames[0]), rid_size);
            }
        }
        ::zlink_msg_close (&frames[0]);
        start_index = 1;
    }

    const size_t payload_count = frame_count - start_index;
    if (payload_count == 0) {
        std::free (frames);
        return source_rid_out_ ? static_cast<int> (source_rid_out_->size) : 0;
    }

    zlink_msg_t *parts = static_cast<zlink_msg_t *> (
      std::malloc (sizeof (zlink_msg_t) * payload_count));
    if (!parts) {
        bench_zlink_close_msg_range (frames + start_index, payload_count);
        std::free (frames);
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i < payload_count; ++i) {
        if (::zlink_msg_init (&parts[i]) != 0) {
            bench_zlink_close_msg_range (parts, i);
            std::free (parts);
            bench_zlink_close_msg_range (frames + start_index, payload_count);
            std::free (frames);
            return -1;
        }
        if (::zlink_msg_move (&parts[i], &frames[start_index + i]) != 0) {
            const int saved_errno = errno;
            bench_zlink_close_msg_range (parts, i + 1);
            std::free (parts);
            bench_zlink_close_msg_range (frames + start_index + i,
                                         payload_count - i);
            std::free (frames);
            errno = saved_errno;
            return -1;
        }
        ::zlink_msg_close (&frames[start_index + i]);
    }

    std::free (frames);

    *parts_out_ = parts;
    *part_count_out_ = payload_count;
    if (source_rid_out_ && source_rid_out_->size > 0)
        return static_cast<int> (source_rid_out_->size);
    return static_cast<int> (payload_count);
}

inline int zlink_recv (void *socket_,
                       zlink_routing_id_t *source_rid_out_,
                       zlink_msg_t **parts_out_,
                       size_t *part_count_out_,
                       zlink_send_flags_t flags_)
{
    return bench_zlink_recv_parts (
      socket_, source_rid_out_, parts_out_, part_count_out_, flags_);
}

inline int zlink_send (void *socket_,
                       zlink_msg_t *parts_,
                       size_t part_count_,
                       zlink_send_flags_t flags_)
{
    if (!parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }

    const size_t send_count = part_count_;
    for (size_t i = 0; i < send_count; ++i) {
        const int send_flags =
          static_cast<int> (flags_)
          | ((i + 1 < send_count) ? ZLINK_SNDMORE : 0);
        if (::zlink_msg_send (&parts_[i], socket_, send_flags) < 0)
            return -1;
    }

    return static_cast<int> (send_count);
}

inline int zlink_send_rid (void *socket_,
                           const zlink_routing_id_t *target_rid_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           zlink_send_flags_t flags_)
{
    if (!socket_ || !target_rid_ || !parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t rid_msg;
    if (::zlink_msg_init_size (&rid_msg, target_rid_->size) != 0)
        return -1;
    if (target_rid_->size > 0) {
        std::memcpy (
          ::zlink_msg_data (&rid_msg), target_rid_->data, target_rid_->size);
    }

    const int rc =
      ::zlink_msg_send (&rid_msg, socket_, static_cast<int> (flags_ | ZLINK_SNDMORE));
    if (rc < 0) {
        ::zlink_msg_close (&rid_msg);
        return -1;
    }

    return zlink_send (socket_, parts_, part_count_, flags_);
}

inline int zlink_msg_send_rid (zlink_msg_t *msg_,
                               void *socket_,
                               const zlink_routing_id_t *target_rid_,
                               zlink_send_flags_t flags_)
{
    if (!msg_) {
        errno = EINVAL;
        return -1;
    }

    const size_t payload_size = ::zlink_msg_size (msg_);
    const int rc = zlink_send_rid (socket_, target_rid_, msg_, 1, flags_);
    if (rc < 0)
        return -1;
    return static_cast<int> (payload_size);
}

inline int zlink_msg_recv_rid (zlink_msg_t *msg_,
                               void *socket_,
                               zlink_routing_id_t *source_rid_out_,
                               zlink_send_flags_t flags_)
{
    if (!msg_) {
        errno = EINVAL;
        return -1;
    }

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int rc = zlink_recv (
      socket_, source_rid_out_, &parts, &part_count, flags_);
    if (rc < 0)
        return -1;

    if (!parts || part_count != 1) {
        if (parts)
            ::zlink_multipart_close (parts, part_count);
        errno = EPROTO;
        return -1;
    }

    const size_t payload_size = ::zlink_msg_size (&parts[0]);
    if (::zlink_msg_move (msg_, &parts[0]) != 0) {
        const int saved_errno = errno;
        ::zlink_multipart_close (parts, part_count);
        errno = saved_errno;
        return -1;
    }

    ::zlink_msg_close (&parts[0]);
    std::free (parts);

    if (source_rid_out_ && source_rid_out_->size > 0)
        return static_cast<int> (source_rid_out_->size);
    return static_cast<int> (payload_size);
}

inline int zlink_publish (void *socket_,
                          const char *topic_id_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          zlink_send_flags_t flags_)
{
    if (!socket_ || !parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }

    if (!topic_id_ || topic_id_[0] == '\0')
        return zlink_send (socket_, parts_, part_count_, flags_);

    zlink_msg_t topic;
    const size_t topic_size = topic_id_ ? std::strlen (topic_id_) : 0;
    if (::zlink_msg_init_size (&topic, topic_size) != 0)
        return -1;
    if (topic_size > 0)
        std::memcpy (::zlink_msg_data (&topic), topic_id_, topic_size);

    if (::zlink_msg_send (&topic, socket_,
                          static_cast<int> (flags_ | ZLINK_SNDMORE))
        < 0) {
        ::zlink_msg_close (&topic);
        return -1;
    }

    return zlink_send (socket_, parts_, part_count_, flags_);
}

inline void *zlink_socket_monitor_open (
  void *socket_, const zlink_socket_monitor_open_options_t *options_)
{
    return ::zlink_socket_monitor_open (socket_, options_);
}

inline int zlink_socket_monitor_recv (void *monitor_,
                                      zlink_monitor_event_t *event_out_)
{
    return ::zlink_monitor_recv (monitor_, event_out_, ZLINK_DONTWAIT);
}

inline int zlink_monitor_snapshot (void *monitor_,
                                   zlink_monitor_snapshot_t *out_)
{
    if (!monitor_ || !out_) {
        errno = EINVAL;
        return -1;
    }

    std::memset (out_, 0, sizeof (*out_));
    int events = 0;
    size_t events_size = sizeof (events);
    if (zlink_get_option (monitor_, ZLINK_OPT_EVENTS, &events, &events_size)
        != 0) {
        return -1;
    }

    out_->ready_count = (events & ZLINK_POLLIN) != 0 ? 1u : 0u;
    return 0;
}

inline int zlink_monitor_close (void **monitor_p_)
{
    if (!monitor_p_ || !*monitor_p_)
        return 0;
    void *monitor = *monitor_p_;
    *monitor_p_ = NULL;
    return ::zlink_close (monitor);
}

#endif
