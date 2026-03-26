/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/multipart_send_txn.hpp"

#include "core/msg.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/sleep.hpp"

#include <new>
#include <string.h>
#include <vector>

namespace
{
typedef std::vector<zlink::msg_t> msg_vec_t;

static bool is_retryable_send_error (int err_)
{
    return err_ == EAGAIN || err_ == EINTR;
}

static void close_frames (msg_vec_t *frames_)
{
    if (!frames_)
        return;

    for (size_t i = 0; i < frames_->size (); ++i) {
        if ((*frames_)[i].check ())
            (void) (*frames_)[i].close ();
    }
}

static int clone_frames (zlink_msg_t *src_, size_t count_, msg_vec_t *dst_)
{
    if (!dst_) {
        errno = EFAULT;
        return -1;
    }

    dst_->clear ();
    if (count_ == 0)
        return 0;

    try {
        dst_->resize (count_);
        for (size_t i = 0; i < count_; ++i) {
            const int init_rc = (*dst_)[i].init ();
            errno_assert (init_rc == 0);
        }
    } catch (const std::bad_alloc &) {
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i < count_; ++i) {
        zlink::msg_t *src = reinterpret_cast<zlink::msg_t *> (&src_[i]);
        if ((*dst_)[i].copy (*src) != 0) {
            close_frames (dst_);
            dst_->clear ();
            return -1;
        }
    }

    return 0;
}

static void consume_original_frames (zlink_msg_t *parts_, size_t part_count_)
{
    for (size_t i = 0; i < part_count_; ++i) {
        zlink::msg_t *msg = reinterpret_cast<zlink::msg_t *> (&parts_[i]);
        if (!msg->check ())
            continue;

        const int close_rc = msg->close ();
        errno_assert (close_rc == 0);
        const int init_rc = msg->init ();
        errno_assert (init_rc == 0);
    }
}

static int socket_sndtimeo_ms (zlink::socket_base_t *socket_)
{
    int timeout_ms = -1;
    size_t optlen = sizeof (timeout_ms);
    if (socket_->getsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &timeout_ms, &optlen)
        != 0) {
        return -1;
    }

    return timeout_ms;
}

static int send_frames_once (zlink::socket_base_t *socket_,
                             msg_vec_t *frames_,
                             int flags_)
{
    bool started = false;

    for (size_t i = 0; i < frames_->size (); ++i) {
        const bool more = i + 1 < frames_->size ();
        if (socket_->send (&(*frames_)[i], (more ? ZLINK_SNDMORE : 0) | flags_)
            != 0) {
            const int err = errno;
            if (started)
                (void) socket_->rollback ();
            errno = err;
            return -1;
        }
        started = more;
    }

    errno = 0;
    return 0;
}

static int send_prefixed_once (zlink::socket_base_t *socket_,
                               const void *prefix_data_,
                               size_t prefix_size_,
                               msg_vec_t *frames_,
                               int flags_);

static int send_frames_once_routed (zlink::socket_base_t *socket_,
                                    const zlink_routing_id_t *routing_id_,
                                    msg_vec_t *frames_,
                                    int flags_)
{
    return send_prefixed_once (socket_, routing_id_->data, routing_id_->size,
                               frames_, flags_);
}

static int send_publish_once (zlink::socket_base_t *socket_,
                              const char *topic_,
                              msg_vec_t *frames_,
                              int flags_)
{
    if (!topic_) {
        if (frames_->empty ()) {
            errno = EINVAL;
            return -1;
        }
        return send_frames_once (socket_, frames_, flags_);
    }

    zlink::msg_t topic_msg;
    const size_t topic_size = strlen (topic_);
    if (topic_msg.init_size (topic_size) != 0)
        return -1;

    if (topic_size > 0)
        memcpy (topic_msg.data (), topic_, topic_size);

    const bool has_payload = !frames_->empty ();
    if (socket_->send (&topic_msg, (has_payload ? ZLINK_SNDMORE : 0) | flags_)
        != 0) {
        const int err = errno;
        (void) topic_msg.close ();
        errno = err;
        return -1;
    }
    (void) topic_msg.close ();

    if (frames_->empty ()) {
        errno = 0;
        return 0;
    }

    return send_frames_once (socket_, frames_, flags_);
}

static int send_prefixed_once (zlink::socket_base_t *socket_,
                               const void *prefix_data_,
                               size_t prefix_size_,
                               msg_vec_t *frames_,
                               int flags_)
{
    zlink::msg_t prefix_msg;
    if (prefix_msg.init_size (prefix_size_) != 0)
        return -1;

    if (prefix_size_ > 0)
        memcpy (prefix_msg.data (), prefix_data_, prefix_size_);

    const bool has_payload = !frames_->empty ();
    if (socket_->send (&prefix_msg, (has_payload ? ZLINK_SNDMORE : 0) | flags_)
        != 0) {
        const int err = errno;
        (void) prefix_msg.close ();
        errno = err;
        return -1;
    }
    (void) prefix_msg.close ();

    if (frames_->empty ()) {
        errno = 0;
        return 0;
    }

    return send_frames_once (socket_, frames_, flags_);
}

typedef int (*attempt_fn) (zlink::socket_base_t *socket_,
                           msg_vec_t *frames_,
                           int flags_,
                           void *arg_);

struct routed_attempt_arg_t
{
    const zlink_routing_id_t *routing_id;
};

struct publish_attempt_arg_t
{
    const char *topic;
};

static int attempt_send_plain (zlink::socket_base_t *socket_,
                               msg_vec_t *frames_,
                               int flags_,
                               void *)
{
    return send_frames_once (socket_, frames_, flags_);
}

static int attempt_send_routed (zlink::socket_base_t *socket_,
                                msg_vec_t *frames_,
                                int flags_,
                                void *arg_)
{
    const routed_attempt_arg_t *arg =
      static_cast<const routed_attempt_arg_t *> (arg_);
    return send_frames_once_routed (socket_, arg->routing_id, frames_, flags_);
}

static int attempt_send_publish (zlink::socket_base_t *socket_,
                                 msg_vec_t *frames_,
                                 int flags_,
                                 void *arg_)
{
    const publish_attempt_arg_t *arg =
      static_cast<const publish_attempt_arg_t *> (arg_);
    return send_publish_once (socket_, arg->topic, frames_, flags_);
}

static int logical_send_with_retry (zlink::socket_base_t *socket_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    int flags_,
                                    attempt_fn attempt_,
                                    void *attempt_arg_,
                                    bool fallback_on_missing_sndtimeo_)
{
    const bool dontwait = (flags_ & ZLINK_DONTWAIT) != 0;
    const int send_flags = dontwait ? (flags_ & ZLINK_DONTWAIT)
                                    : ((flags_ & ZLINK_DONTWAIT)
                                       | ZLINK_DONTWAIT);

    int sndtimeo_ms = 0;
    if (!dontwait) {
        sndtimeo_ms = socket_sndtimeo_ms (socket_);
        if (sndtimeo_ms < 0) {
            if (errno == 0 && !fallback_on_missing_sndtimeo_)
                return -1;

            msg_vec_t frames;
            if (clone_frames (parts_, part_count_, &frames) != 0)
                return -1;

            const int rc = attempt_ (socket_, &frames, flags_, attempt_arg_);
            const int saved_errno = rc == 0 ? 0 : errno;
            close_frames (&frames);
            if (rc != 0) {
                errno = saved_errno;
                return -1;
            }

            consume_original_frames (parts_, part_count_);
            errno = 0;
            return 0;
        }
    }

    const uint64_t deadline_ms =
      !dontwait && sndtimeo_ms > 0
        ? zlink::clock_t ().now_ms () + static_cast<uint64_t> (sndtimeo_ms)
        : 0;

    while (true) {
        msg_vec_t frames;
        if (clone_frames (parts_, part_count_, &frames) != 0)
            return -1;

        const int rc = attempt_ (socket_, &frames, send_flags, attempt_arg_);
        const int saved_errno = rc == 0 ? 0 : errno;
        close_frames (&frames);
        if (rc == 0) {
            consume_original_frames (parts_, part_count_);
            errno = 0;
            return 0;
        }

        if (dontwait || !is_retryable_send_error (saved_errno)) {
            errno = saved_errno;
            return -1;
        }
        if (sndtimeo_ms == 0) {
            errno = EAGAIN;
            return -1;
        }
        if (sndtimeo_ms > 0 && zlink::clock_t ().now_ms () >= deadline_ms) {
            errno = EAGAIN;
            return -1;
        }

        zlink::sleep_ms (1);
    }
}
}

int zlink::logical_multipart_send (socket_base_t *socket_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   int flags_)
{
    if (!socket_ || !parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }

    return logical_send_with_retry (socket_, parts_, part_count_, flags_,
                                    &attempt_send_plain, NULL, false);
}

int zlink::logical_multipart_send_routed (socket_base_t *socket_,
                                          const zlink_routing_id_t *routing_id_,
                                          zlink_msg_t *parts_,
                                          size_t part_count_,
                                          int flags_)
{
    if (!socket_ || !routing_id_ || !parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }

    routed_attempt_arg_t arg = {routing_id_};
    return logical_send_with_retry (socket_, parts_, part_count_, flags_,
                                    &attempt_send_routed, &arg, false);
}

int zlink::logical_multipart_send_prefixed (socket_base_t *socket_,
                                            const void *prefix_data_,
                                            size_t prefix_size_,
                                            zlink_msg_t *parts_,
                                            size_t part_count_,
                                            int flags_,
                                            int route_ready_retry_ms_)
{
    if (!socket_ || !parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }

    const bool dontwait = (flags_ & ZLINK_DONTWAIT) != 0;
    const uint64_t deadline_ms =
      !dontwait && route_ready_retry_ms_ > 0
        ? zlink::clock_t ().now_ms ()
            + static_cast<uint64_t> (route_ready_retry_ms_)
        : 0;

    while (true) {
        msg_vec_t frames;
        if (clone_frames (parts_, part_count_, &frames) != 0)
            return -1;

        const int rc = send_prefixed_once (socket_, prefix_data_, prefix_size_,
                                           &frames, flags_ & ZLINK_DONTWAIT);
        const int saved_errno = rc == 0 ? 0 : errno;
        close_frames (&frames);
        if (rc == 0) {
            consume_original_frames (parts_, part_count_);
            errno = 0;
            return 0;
        }

        if (!dontwait
            && (saved_errno == EHOSTUNREACH || saved_errno == ENOTCONN)
            && route_ready_retry_ms_ > 0
            && zlink::clock_t ().now_ms () < deadline_ms) {
            zlink::sleep_ms (1);
            continue;
        }

        errno = saved_errno;
        return -1;
    }
}

int zlink::logical_multipart_publish (socket_base_t *socket_,
                                      const char *topic_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_,
                                      int flags_,
                                      bool fallback_on_missing_sndtimeo_)
{
    if (!socket_ || (part_count_ > 0 && !parts_)
        || (!topic_ && part_count_ == 0)) {
        errno = EINVAL;
        return -1;
    }

    publish_attempt_arg_t arg = {topic_};
    return logical_send_with_retry (socket_, parts_, part_count_, flags_,
                                    &attempt_send_publish, &arg,
                                    fallback_on_missing_sndtimeo_);
}
