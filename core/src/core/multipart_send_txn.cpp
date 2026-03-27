/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/multipart_send_txn.hpp"

#include "core/msg.hpp"
#include "sockets/socket_base.hpp"

namespace
{
static void consume_frames_from (zlink_msg_t *parts_,
                                 size_t start_index_,
                                 size_t part_count_)
{
    for (size_t i = start_index_; i < part_count_; ++i) {
        zlink::msg_t *msg = reinterpret_cast<zlink::msg_t *> (&parts_[i]);
        if (!msg->check ())
            continue;

        const int close_rc = msg->close ();
        errno_assert (close_rc == 0);
        const int init_rc = msg->init ();
        errno_assert (init_rc == 0);
    }
}

static int send_frames_once (zlink::socket_base_t *socket_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             int flags_)
{
    bool started = false;

    for (size_t i = 0; i < part_count_; ++i) {
        const bool more = i + 1 < part_count_;
        if (socket_->send (reinterpret_cast<zlink::msg_t *> (&parts_[i]),
                           (more ? ZLINK_SNDMORE : 0) | flags_)
            != 0) {
            const int err = errno;
            if (started)
                (void) socket_->rollback ();
            consume_frames_from (parts_, i, part_count_);
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
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               int flags_);

static int send_frames_once_routed (zlink::socket_base_t *socket_,
                                    const zlink_routing_id_t *routing_id_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    int flags_)
{
    return send_prefixed_once (socket_, routing_id_->data, routing_id_->size,
                               parts_, part_count_, flags_);
}

static int send_publish_once (zlink::socket_base_t *socket_,
                              const char *topic_,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                              int flags_)
{
    if (!topic_) {
        if (part_count_ == 0) {
            errno = EINVAL;
            return -1;
        }
        return send_frames_once (socket_, parts_, part_count_, flags_);
    }

    zlink::msg_t topic_msg;
    const size_t topic_size = strlen (topic_);
    if (topic_msg.init_size (topic_size) != 0)
        return -1;

    if (topic_size > 0)
        memcpy (topic_msg.data (), topic_, topic_size);

    const bool has_payload = part_count_ > 0;
    if (socket_->send (&topic_msg, (has_payload ? ZLINK_SNDMORE : 0) | flags_)
        != 0) {
        const int err = errno;
        (void) topic_msg.close ();
        errno = err;
        return -1;
    }
    (void) topic_msg.close ();

    if (part_count_ == 0) {
        errno = 0;
        return 0;
    }

    const int rc = send_frames_once (socket_, parts_, part_count_, flags_);
    if (rc != 0)
        (void) socket_->rollback ();
    return rc;
}

static int send_prefixed_once (zlink::socket_base_t *socket_,
                               const void *prefix_data_,
                               size_t prefix_size_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               int flags_)
{
    zlink::msg_t prefix_msg;
    if (prefix_msg.init_size (prefix_size_) != 0)
        return -1;

    if (prefix_size_ > 0)
        memcpy (prefix_msg.data (), prefix_data_, prefix_size_);

    const bool has_payload = part_count_ > 0;
    if (socket_->send (&prefix_msg, (has_payload ? ZLINK_SNDMORE : 0) | flags_)
        != 0) {
        const int err = errno;
        (void) prefix_msg.close ();
        errno = err;
        return -1;
    }
    (void) prefix_msg.close ();

    if (part_count_ == 0) {
        errno = 0;
        return 0;
    }

    const int rc = send_frames_once (socket_, parts_, part_count_, flags_);
    if (rc != 0)
        (void) socket_->rollback ();
    return rc;
}

typedef int (*attempt_fn) (zlink::socket_base_t *socket_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
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
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               int flags_,
                               void *)
{
    return send_frames_once (socket_, parts_, part_count_, flags_);
}

static int attempt_send_routed (zlink::socket_base_t *socket_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                int flags_,
                                void *arg_)
{
    const routed_attempt_arg_t *arg =
      static_cast<const routed_attempt_arg_t *> (arg_);
    return send_frames_once_routed (socket_, arg->routing_id, parts_,
                                    part_count_, flags_);
}

static int attempt_send_publish (zlink::socket_base_t *socket_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 int flags_,
                                 void *arg_)
{
    const publish_attempt_arg_t *arg =
      static_cast<const publish_attempt_arg_t *> (arg_);
    return send_publish_once (socket_, arg->topic, parts_, part_count_, flags_);
}

static int logical_send_once (zlink::socket_base_t *socket_,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                              int flags_,
                              attempt_fn attempt_,
                              void *attempt_arg_)
{
    const int rc = attempt_ (socket_, parts_, part_count_, flags_, attempt_arg_);
    if (rc != 0)
        return -1;

    errno = 0;
    return 0;
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

    return logical_send_once (socket_, parts_, part_count_, flags_,
                              &attempt_send_plain, NULL);
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
    return logical_send_once (socket_, parts_, part_count_, flags_,
                              &attempt_send_routed, &arg);
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

    (void) route_ready_retry_ms_;
    return send_prefixed_once (socket_, prefix_data_, prefix_size_, parts_,
                               part_count_, flags_);
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
    (void) fallback_on_missing_sndtimeo_;
    return logical_send_once (socket_, parts_, part_count_, flags_,
                              &attempt_send_publish, &arg);
}
