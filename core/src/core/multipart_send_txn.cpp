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
                             int flags_,
                             bool rollback_started_,
                             zlink::socket_public_send_scope_t &scope_)
{
    bool started = rollback_started_;

    for (size_t i = 0; i < part_count_; ++i) {
        const bool more = i + 1 < part_count_;
        if (socket_->send_scoped (reinterpret_cast<zlink::msg_t *> (&parts_[i]),
                                  (more ? ZLINK_SNDMORE : 0) | flags_, scope_)
            != 0) {
            const int err = errno;
            if (started)
                (void) socket_->rollback_scoped (scope_);
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
                               int flags_,
                               zlink::socket_public_send_scope_t &scope_);

static int send_frames_once_routed (zlink::socket_base_t *socket_,
                                    const zlink_routing_id_t *routing_id_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    int flags_,
                                    zlink::socket_public_send_scope_t &scope_)
{
    return send_prefixed_once (socket_, routing_id_->data, routing_id_->size,
                               parts_, part_count_, flags_, scope_);
}

static int send_publish_once (zlink::socket_base_t *socket_,
                              const char *topic_,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                              int flags_,
                              zlink::socket_public_send_scope_t &scope_)
{
    if (!topic_) {
        if (part_count_ == 0) {
            errno = EINVAL;
            return -1;
        }
        return send_frames_once (socket_, parts_, part_count_, flags_, false,
                                 scope_);
    }

    zlink::msg_t topic_msg;
    const size_t topic_size = strlen (topic_);
    if (topic_msg.init_size (topic_size) != 0)
        return -1;

    if (topic_size > 0)
        memcpy (topic_msg.data (), topic_, topic_size);

    const bool has_payload = part_count_ > 0;
    if (socket_->send_scoped (&topic_msg,
                              (has_payload ? ZLINK_SNDMORE : 0) | flags_,
                              scope_)
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

    return send_frames_once (socket_, parts_, part_count_, flags_, true, scope_);
}

static int send_prefixed_once (zlink::socket_base_t *socket_,
                               const void *prefix_data_,
                               size_t prefix_size_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               int flags_,
                               zlink::socket_public_send_scope_t &scope_)
{
    zlink::msg_t prefix_msg;
    if (prefix_msg.init_size (prefix_size_) != 0)
        return -1;

    if (prefix_size_ > 0)
        memcpy (prefix_msg.data (), prefix_data_, prefix_size_);

    const bool has_payload = part_count_ > 0;
    if (socket_->send_scoped (&prefix_msg,
                              (has_payload ? ZLINK_SNDMORE : 0) | flags_,
                              scope_)
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

    return send_frames_once (socket_, parts_, part_count_, flags_, true, scope_);
}

typedef int (*attempt_fn) (zlink::socket_base_t *socket_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           int flags_,
                           void *arg_,
                           zlink::socket_public_send_scope_t &scope_);

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
                               void *,
                               zlink::socket_public_send_scope_t &scope_)
{
    return send_frames_once (socket_, parts_, part_count_, flags_, false,
                             scope_);
}

static int attempt_send_routed (zlink::socket_base_t *socket_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                int flags_,
                                void *arg_,
                                zlink::socket_public_send_scope_t &scope_)
{
    const routed_attempt_arg_t *arg =
      static_cast<const routed_attempt_arg_t *> (arg_);
    return send_frames_once_routed (socket_, arg->routing_id, parts_,
                                    part_count_, flags_, scope_);
}

static int attempt_send_publish (zlink::socket_base_t *socket_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 int flags_,
                                 void *arg_,
                                 zlink::socket_public_send_scope_t &scope_)
{
    const publish_attempt_arg_t *arg =
      static_cast<const publish_attempt_arg_t *> (arg_);
    return send_publish_once (socket_, arg->topic, parts_, part_count_, flags_,
                              scope_);
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

    zlink::socket_public_send_scope_t send_scope (
      socket_->lifecycle_coordinator (),
      socket_->direct_send_needs_public_api_sync ());
    if (!send_scope.acquired ())
        return -1;

    const int rc =
      attempt_send_plain (socket_, parts_, part_count_, flags_, NULL, send_scope);
    if (rc != 0)
        return -1;

    errno = 0;
    return 0;
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
    zlink::socket_public_send_scope_t send_scope (
      socket_->lifecycle_coordinator (), true);
    if (!send_scope.acquired ())
        return -1;

    const int rc = attempt_send_routed (socket_, parts_, part_count_, flags_,
                                        &arg, send_scope);
    if (rc != 0)
        return -1;

    errno = 0;
    return 0;
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
    zlink::socket_public_send_scope_t send_scope (
      socket_->lifecycle_coordinator (),
      socket_->direct_send_needs_public_api_sync ());
    if (!send_scope.acquired ())
        return -1;

    return send_prefixed_once (socket_, prefix_data_, prefix_size_, parts_,
                               part_count_, flags_, send_scope);
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
    zlink::socket_public_send_scope_t send_scope (
      socket_->lifecycle_coordinator (), true);
    if (!send_scope.acquired ())
        return -1;

    const int rc = attempt_send_publish (socket_, parts_, part_count_, flags_,
                                         &arg, send_scope);
    if (rc != 0)
        return -1;

    errno = 0;
    return 0;
}
