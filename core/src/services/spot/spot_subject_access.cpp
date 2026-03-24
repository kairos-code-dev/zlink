/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_subject_access.hpp"

#include "api/service_api_internal.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <string.h>
#include <vector>

#include "services/spot/spot_dispatch_internal.hpp"
#include "services/spot/spot_internal_receiver.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"

namespace
{
struct spot_node_handler_entry_t
{
    zlink_subscribe_handler_fn handler;
    void *userdata;
};

struct spot_node_handler_registry_t
{
    zlink::mutex_t sync;
    std::map<zlink::spot_node_t *, spot_node_handler_entry_t> handlers;
};

thread_local void *g_current_spot_dispatch_handle = NULL;
thread_local bool g_current_spot_dispatch_is_node = false;

spot_node_handler_registry_t &spot_node_handler_registry ()
{
    static spot_node_handler_registry_t registry;
    return registry;
}

void close_spot_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

bool is_valid_pubsub_filter (const char *filter_,
                             std::string *raw_filter_out_,
                             bool *is_pattern_out_)
{
    if (!filter_ || filter_[0] == '\0')
        return false;

    const size_t len = strlen (filter_);
    if (len == 0 || len > 255)
        return false;

    const char *star = strchr (filter_, '*');
    if (!star) {
        if (raw_filter_out_)
            *raw_filter_out_ = std::string (filter_, len);
        if (is_pattern_out_)
            *is_pattern_out_ = false;
        return true;
    }

    if (star != filter_ + len - 1 || len < 2)
        return false;

    if (strchr (star + 1, '*'))
        return false;

    if (raw_filter_out_)
        *raw_filter_out_ = std::string (filter_, len - 1);
    if (is_pattern_out_)
        *is_pattern_out_ = true;
    return true;
}

int map_common_to_spot_pub_option (zlink_option_t option_)
{
    switch (option_) {
        case ZLINK_OPT_SNDHWM:
            return ZLINK_SPOT_PUB_OPT_SNDHWM;
        case ZLINK_OPT_SNDTIMEO:
            return ZLINK_SPOT_PUB_OPT_SNDTIMEO;
        case ZLINK_OPT_LINGER:
            return ZLINK_SPOT_PUB_OPT_LINGER;
        case ZLINK_OPT_SNDBUF:
            return ZLINK_SPOT_PUB_OPT_SNDBUF;
        case ZLINK_OPT_RCVBUF:
            return ZLINK_SPOT_PUB_OPT_RCVBUF;
        default:
            return -1;
    }
}

int map_common_to_spot_sub_option (zlink_option_t option_)
{
    switch (option_) {
        case ZLINK_OPT_RCVHWM:
            return ZLINK_SPOT_SUB_OPT_RCVHWM;
        case ZLINK_OPT_LINGER:
            return ZLINK_SPOT_SUB_OPT_LINGER;
        case ZLINK_OPT_SNDBUF:
            return ZLINK_SPOT_SUB_OPT_SNDBUF;
        case ZLINK_OPT_RCVBUF:
            return ZLINK_SPOT_SUB_OPT_RCVBUF;
        case ZLINK_OPT_RCVTIMEO:
            return ZLINK_SPOT_SUB_OPT_RCVTIMEO;
        default:
            return -1;
    }
}

int map_common_to_socket_option (zlink_option_t option_)
{
    switch (option_) {
        case ZLINK_OPT_SNDHWM:
            return ZLINK_INTERNAL_OPT_SNDHWM;
        case ZLINK_OPT_RCVHWM:
            return ZLINK_INTERNAL_OPT_RCVHWM;
        case ZLINK_OPT_LINGER:
            return ZLINK_INTERNAL_OPT_LINGER;
        case ZLINK_OPT_SNDTIMEO:
            return ZLINK_INTERNAL_OPT_SNDTIMEO;
        case ZLINK_OPT_RCVTIMEO:
            return ZLINK_INTERNAL_OPT_RCVTIMEO;
        case ZLINK_OPT_SNDBUF:
            return ZLINK_INTERNAL_OPT_SNDBUF;
        case ZLINK_OPT_RCVBUF:
            return ZLINK_INTERNAL_OPT_RCVBUF;
        default:
            return -1;
    }
}

int map_pub_to_socket_option (zlink_pub_option_t option_)
{
    switch (option_) {
        case ZLINK_PUB_OPT_NODROP:
            return ZLINK_INTERNAL_OPT_XPUB_NODROP;
        case ZLINK_PUB_OPT_TOPICS_COUNT:
            return ZLINK_INTERNAL_OPT_TOPICS_COUNT;
        default:
            return -1;
    }
}

int map_sub_to_socket_option (zlink_sub_option_t option_)
{
    switch (option_) {
        case ZLINK_SUB_OPT_TOPICS_COUNT:
            return ZLINK_INTERNAL_OPT_TOPICS_COUNT;
        default:
            return -1;
    }
}

struct spot_subject_less_t
{
    bool operator() (
      const zlink::spot_sub_t::subject_descriptor_t &lhs_,
      const zlink::spot_sub_t::subject_descriptor_t &rhs_) const
    {
        if (lhs_.subject != rhs_.subject)
            return lhs_.subject < rhs_.subject;
        return lhs_.subject_kind < rhs_.subject_kind;
    }
};

int copy_subscription_subject (
  const zlink::spot_sub_t::subject_descriptor_t &subject_,
  char *filter_out_,
  size_t *filter_len_inout_,
  int *is_pattern_out_)
{
    if (!filter_len_inout_) {
        errno = EFAULT;
        return -1;
    }
    if (!filter_out_ && *filter_len_inout_ != 0) {
        errno = EFAULT;
        return -1;
    }
    if (*filter_len_inout_ < subject_.subject.size ()) {
        *filter_len_inout_ = subject_.subject.size ();
        errno = EINVAL;
        return -1;
    }
    if (filter_out_ && !subject_.subject.empty ())
        memcpy (filter_out_, subject_.subject.data (), subject_.subject.size ());
    *filter_len_inout_ = subject_.subject.size ();
    if (is_pattern_out_)
        *is_pattern_out_ =
          subject_.subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_PATTERN ? 1 : 0;
    return 0;
}

int append_subscription_subjects (
  void *handle_, std::vector<zlink::spot_sub_t::subject_descriptor_t> *out_)
{
    if (!out_) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_)) {
        if (sub->node ()) {
            zlink::service_public_api_scope_t admission (
              sub->node ()->public_api_guard ());
            if (!admission.acquired ())
                return -1;
        }
        sub->append_all_subjects (out_);
        return 0;
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub) {
            errno = ENOTSUP;
            return -1;
        }
        sub->append_all_subjects (out_);
        return 0;
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        node->snapshot_subscription_subjects (out_);
        return 0;
    }

    errno = EFAULT;
    return -1;
}

void spot_node_sub_handler_adapter (const zlink_routing_id_t *source_rid_,
                                    const char *topic_,
                                    size_t topic_len_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    void *userdata_)
{
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (userdata_);
    if (!node || !node->check_tag ()) {
        close_spot_parts (parts_, part_count_);
        return;
    }

    zlink_subscribe_handler_fn handler = NULL;
    void *userdata = NULL;
    {
        spot_node_handler_registry_t &registry = spot_node_handler_registry ();
        zlink::scoped_lock_t lock (registry.sync);
        std::map<zlink::spot_node_t *, spot_node_handler_entry_t>::iterator it =
          registry.handlers.find (node);
        if (it != registry.handlers.end ()) {
            handler = it->second.handler;
            userdata = it->second.userdata;
        }
    }

    if (!handler) {
        close_spot_parts (parts_, part_count_);
        return;
    }

    g_current_spot_dispatch_handle = node;
    g_current_spot_dispatch_is_node = true;
    handler (source_rid_, topic_, topic_len_, parts_, part_count_, userdata);
    g_current_spot_dispatch_handle = NULL;
    g_current_spot_dispatch_is_node = false;
}

void spot_sub_handler_adapter (const zlink_routing_id_t *source_rid_,
                               const char *topic_,
                               size_t topic_len_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               void *userdata_)
{
    spot_handle_t *spot = static_cast<spot_handle_t *> (userdata_);
    if (!spot || !spot->check_tag () || !spot->handler) {
        close_spot_parts (parts_, part_count_);
        return;
    }

    g_current_spot_dispatch_handle = spot;
    g_current_spot_dispatch_is_node = false;
    spot->handler (source_rid_, topic_, topic_len_, parts_, part_count_,
                   spot->handler_userdata);
    g_current_spot_dispatch_handle = NULL;
    g_current_spot_dispatch_is_node = false;
}
} // namespace

namespace zlink
{
void *current_spot_dispatch_handle ()
{
    return g_current_spot_dispatch_handle;
}

bool current_spot_dispatch_is_node ()
{
    return g_current_spot_dispatch_is_node;
}
}

zlink::spot_pub_t *as_spot_pub_side_handle (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_pub_t *pub = static_cast<zlink::spot_pub_t *> (handle_);
    return pub->check_tag () ? pub : NULL;
}

zlink::spot_sub_t *as_spot_sub_side_handle (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_sub_t *sub = static_cast<zlink::spot_sub_t *> (handle_);
    return sub->check_tag () ? sub : NULL;
}

zlink::spot_node_t *as_spot_node_handle (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
    return node->check_tag () ? node : NULL;
}

spot_handle_t *as_spot_handle (void *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return NULL;
    }

    spot_handle_t *spot = static_cast<spot_handle_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return spot;
}

zlink::spot_pub_t *ensure_spot_pub (spot_handle_t *spot_)
{
    if (!spot_ || !spot_->node) {
        errno = EFAULT;
        return NULL;
    }

    if (spot_->pub)
        return spot_->pub;

    spot_->pub = spot_->node->create_spot_pub ();
    return spot_->pub;
}

zlink::spot_sub_t *ensure_spot_sub (spot_handle_t *spot_)
{
    if (!spot_ || !spot_->node) {
        errno = EFAULT;
        return NULL;
    }

    if (spot_->sub)
        return spot_->sub;

    zlink::spot_sub_t *sub = spot_->node->create_spot_sub ();
    if (!sub)
        return NULL;

    if (spot_->handler
        && sub->set_direct_handler (&spot_sub_handler_adapter, spot_) != 0) {
        const int err = errno;
        (void) sub->destroy ();
        delete sub;
        errno = err;
        return NULL;
    }

    const zlink::spot_node_t::sub_defaults_t &defaults =
      spot_->pending_sub_defaults;
    if ((defaults.rcvhwm.enabled
         && sub->set_option (ZLINK_SPOT_SUB_OPT_RCVHWM, &defaults.rcvhwm.value,
                             defaults.rcvhwm.size)
              != 0)
        || (defaults.linger.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_LINGER,
                                &defaults.linger.value,
                                defaults.linger.size)
                 != 0)
        || (defaults.sndbuf.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_SNDBUF,
                                &defaults.sndbuf.value,
                                defaults.sndbuf.size)
                 != 0)
        || (defaults.rcvbuf.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_RCVBUF,
                                &defaults.rcvbuf.value,
                                defaults.rcvbuf.size)
                 != 0)
        || (defaults.rcvtimeo.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_RCVTIMEO,
                                &defaults.rcvtimeo.value,
                                defaults.rcvtimeo.size)
                 != 0)) {
        const int err = errno;
        (void) sub->destroy ();
        delete sub;
        errno = err;
        return NULL;
    }

    spot_->sub = sub;
    return spot_->sub;
}

zlink::socket_base_t *spot_pub_poller_socket (void *spot_pub_)
{
    zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_pub_);
    if (!pub) {
        errno = EFAULT;
        return NULL;
    }
    return pub->poller_socket ();
}

zlink::socket_base_t *spot_sub_poller_socket (void *spot_sub_)
{
    zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_sub_);
    if (!sub) {
        errno = EFAULT;
        return NULL;
    }
    return sub->poller_socket ();
}

zlink::socket_base_t *resolve_spot_pub_subject_poller_socket (
  void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return NULL;
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        return pub ? pub->poller_socket () : NULL;
    }
    if (is_registered_spot_node_handle (spot_or_node_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        return pub ? pub->poller_socket () : NULL;
    }
    errno = EFAULT;
    return NULL;
}

zlink::socket_base_t *resolve_spot_sub_subject_poller_socket (
  void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return NULL;
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        return sub ? sub->poller_socket () : NULL;
    }
    if (is_registered_spot_node_handle (spot_or_node_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
        zlink::spot_internal_receiver_t *receiver =
          zlink::spot_node_access_t::ensure_internal_receiver (node);
        if (receiver && receiver->impl ())
            return receiver->impl ()->poller_socket ();
        zlink::spot_sub_t *sub = node->ensure_default_sub ();
        return sub ? sub->poller_socket () : NULL;
    }
    errno = EFAULT;
    return NULL;
}

int infer_spot_monitor_role (void *target_, uint32_t events_)
{
    const bool want_pub = (events_ & (ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_FULL
                                      | ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_DRAINED
                                      | ZLINK_SPOT_MONITOR_EVENT_PUB_DELIVERY_READY_CHANGED
                                      | ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED))
                           != 0;
    const bool want_sub =
      (events_ & (ZLINK_SPOT_MONITOR_EVENT_SUB_FILTER_APPLIED
                  | ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED
                  | ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED))
      != 0;
    if (want_pub && want_sub) {
        errno = EINVAL;
        return -1;
    }
    if (want_pub)
        return ZLINK_SPOT_ROLE_PUB;
    if (want_sub)
        return ZLINK_SPOT_ROLE_SUB;

    if (as_spot_pub_side_handle (target_))
        return ZLINK_SPOT_ROLE_PUB;
    if (as_spot_sub_side_handle (target_))
        return ZLINK_SPOT_ROLE_SUB;

    spot_handle_t *spot = as_spot_handle (target_);
    if (!spot) {
        errno = EFAULT;
        return -1;
    }
    if (spot->pub && !spot->sub)
        return ZLINK_SPOT_ROLE_PUB;
    if (spot->sub && !spot->pub)
        return ZLINK_SPOT_ROLE_SUB;

    return ZLINK_SPOT_ROLE_SUB;
}

void *spot_pub_monitor_open (void *spot_pub_, int events_)
{
    zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_pub_);
    if (!pub) {
        errno = EFAULT;
        return NULL;
    }
    if (pub->node ()) {
        zlink::service_public_api_scope_t admission (
          pub->node ()->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
    }
    return pub->monitor_open (events_);
}

void *spot_sub_monitor_open (void *spot_sub_, int events_)
{
    zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_sub_);
    if (!sub) {
        errno = EFAULT;
        return NULL;
    }
    if (sub->node ()) {
        zlink::service_public_api_scope_t admission (
          sub->node ()->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
    }
    return sub->monitor_open (events_);
}

void *spot_handle_monitor_open (void *spot_,
                                zlink_spot_role_t role_,
                                int events_,
                                void **snapshot_subject_out_)
{
    if (snapshot_subject_out_)
        *snapshot_subject_out_ = NULL;

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return NULL;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return NULL;

    if (role_ == ZLINK_SPOT_ROLE_PUB) {
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        if (!pub) {
            errno = ENOTSUP;
            return NULL;
        }
        if (snapshot_subject_out_)
            *snapshot_subject_out_ = pub;
        return pub->monitor_open (events_);
    }
    if (role_ == ZLINK_SPOT_ROLE_SUB) {
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub) {
            errno = ENOTSUP;
            return NULL;
        }
        if (snapshot_subject_out_)
            *snapshot_subject_out_ = sub;
        return sub->monitor_open (events_);
    }

    errno = EINVAL;
    return NULL;
}

int spot_subject_publish (void *subject_,
                          const char *topic_id_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          zlink_send_flags_t flags_)
{
    if (!topic_id_) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (subject_))
        return pub->publish (topic_id_, parts_, part_count_, flags_);

    if (spot_handle_t *spot = as_spot_handle (subject_)) {
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        if (!pub) {
            errno = ENOTSUP;
            return -1;
        }
        return pub->publish (topic_id_, parts_, part_count_, flags_);
    }

    if (zlink::spot_node_t *node = as_spot_node_handle (subject_)) {
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        if (!pub)
            return -1;
        return pub->publish (topic_id_, parts_, part_count_, flags_);
    }

    errno = EFAULT;
    return -1;
}

int spot_subject_recv (void *subject_,
                       zlink_routing_id_t *source_rid_out_,
                       zlink_msg_t **parts_out_,
                       size_t *part_count_out_,
                       char *topic_id_out_,
                       size_t *topic_id_len_out_,
                       zlink_send_flags_t flags_)
{
    if (validate_recv_flags (flags_) != 0)
        return -1;

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (subject_))
        return sub->recv (source_rid_out_, parts_out_, part_count_out_, flags_,
                          topic_id_out_, topic_id_len_out_);

    if (spot_handle_t *spot = as_spot_handle (subject_)) {
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        if (spot_require_recv_model (spot) != 0)
            return -1;
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub) {
            errno = ENOTSUP;
            return -1;
        }
        return sub->recv (source_rid_out_, parts_out_, part_count_out_, flags_,
                          topic_id_out_, topic_id_len_out_);
    }

    if (zlink::spot_node_t *node = as_spot_node_handle (subject_)) {
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        if (spot_node_require_recv_model (node) != 0)
            return -1;
        zlink::spot_internal_receiver_t *receiver =
          zlink::spot_node_access_t::ensure_internal_receiver (node);
        if (!receiver || !receiver->impl ()) {
            errno = ENOTSUP;
            return -1;
        }
        return receiver->impl ()->recv (source_rid_out_, parts_out_,
                                        part_count_out_, flags_, topic_id_out_,
                                        topic_id_len_out_);
    }

    errno = EFAULT;
    return -1;
}

int spot_subject_set_common_option (void *handle_,
                                    zlink_option_t option_,
                                    const void *optval_,
                                    size_t optvallen_)
{
    const int pub_option = map_common_to_spot_pub_option (option_);
    const int sub_option = map_common_to_spot_sub_option (option_);
    if (pub_option < 0 && sub_option < 0) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (handle_)) {
        if (pub_option < 0) {
            errno = EINVAL;
            return -1;
        }
        return pub->set_option (pub_option, optval_, optvallen_);
    }

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_)) {
        if (sub_option < 0) {
            errno = EINVAL;
            return -1;
        }
        return sub->set_option (sub_option, optval_, optvallen_);
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0) {
            zlink::spot_pub_t *pub = ensure_spot_pub (spot);
            if (!pub || pub->set_option (pub_option, optval_, optvallen_) != 0)
                return -1;
        }
        if (sub_option >= 0) {
            zlink::spot_sub_t *sub = ensure_spot_sub (spot);
            if (!sub) {
                errno = ENOTSUP;
                return -1;
            }
            if (sub->set_option (sub_option, optval_, optvallen_) != 0)
                return -1;
        }
        return 0;
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0
            && node->set_pub_option (pub_option, optval_, optvallen_) != 0)
            return -1;
        if (sub_option >= 0
            && node->set_sub_option (sub_option, optval_, optvallen_) != 0)
            return -1;
        return 0;
    }

    errno = EFAULT;
    return -1;
}

int spot_subject_get_common_option (void *handle_,
                                    zlink_option_t option_,
                                    void *optval_,
                                    size_t *optvallen_)
{
    const int socket_option = map_common_to_socket_option (option_);
    const int pub_option = map_common_to_spot_pub_option (option_);
    const int sub_option = map_common_to_spot_sub_option (option_);
    if (socket_option < 0 || (pub_option < 0 && sub_option < 0)) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (handle_)) {
        if (pub_option < 0) {
            errno = EINVAL;
            return -1;
        }
        zlink::socket_base_t *socket = pub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (socket_option, optval_, optvallen_);
    }

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_)) {
        if (sub_option < 0) {
            errno = EINVAL;
            return -1;
        }
        zlink::socket_base_t *socket = sub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (socket_option, optval_, optvallen_);
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0) {
            zlink::spot_pub_t *pub = ensure_spot_pub (spot);
            if (!pub || !pub->poller_socket ()) {
                errno = EFAULT;
                return -1;
            }
            return pub->poller_socket ()->getsockopt (socket_option, optval_,
                                                      optvallen_);
        }
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub || !sub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return sub->poller_socket ()->getsockopt (socket_option, optval_,
                                                  optvallen_);
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0) {
            zlink::spot_pub_t *pub = node->ensure_default_pub ();
            if (!pub || !pub->poller_socket ()) {
                errno = EFAULT;
                return -1;
            }
            return pub->poller_socket ()->getsockopt (socket_option, optval_,
                                                      optvallen_);
        }
        zlink::spot_sub_t *sub = node->ensure_default_sub ();
        if (!sub || !sub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return sub->poller_socket ()->getsockopt (socket_option, optval_,
                                                  optvallen_);
    }

    errno = EFAULT;
    return -1;
}

int spot_subject_set_pub_option (void *handle_,
                                 zlink_pub_option_t option_,
                                 const void *optval_,
                                 size_t optvallen_)
{
    if (option_ != ZLINK_PUB_OPT_NODROP) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (handle_))
        return pub->set_option (ZLINK_SPOT_PUB_OPT_NODROP, optval_, optvallen_);

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        if (!pub) {
            errno = ENOTSUP;
            return -1;
        }
        return pub->set_option (ZLINK_SPOT_PUB_OPT_NODROP, optval_, optvallen_);
    }

    if (is_registered_spot_node_handle (handle_))
        return static_cast<zlink::spot_node_t *> (handle_)->set_pub_option (
          ZLINK_SPOT_PUB_OPT_NODROP, optval_, optvallen_);

    errno = EFAULT;
    return -1;
}

int spot_subject_get_pub_option (void *handle_,
                                 zlink_pub_option_t option_,
                                 void *optval_,
                                 size_t *optvallen_)
{
    const int socket_option = map_pub_to_socket_option (option_);
    if (socket_option < 0) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (handle_)) {
        zlink::socket_base_t *socket = pub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (socket_option, optval_, optvallen_);
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        if (!pub || !pub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return pub->poller_socket ()->getsockopt (socket_option, optval_,
                                                  optvallen_);
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        if (!pub || !pub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return pub->poller_socket ()->getsockopt (socket_option, optval_,
                                                  optvallen_);
    }

    errno = EFAULT;
    return -1;
}

int spot_subject_set_sub_option (void *handle_,
                                 zlink_sub_option_t option_,
                                 const void *optval_,
                                 size_t optvallen_)
{
    LIBZLINK_UNUSED (handle_);

    if (option_ != ZLINK_SUB_OPT_TOPICS_COUNT) {
        errno = EINVAL;
        return -1;
    }
    if (optval_ || optvallen_ != 0) {
        errno = EINVAL;
        return -1;
    }
    errno = EINVAL;
    return -1;
}

int spot_subject_get_sub_option (void *handle_,
                                 zlink_sub_option_t option_,
                                 void *optval_,
                                 size_t *optvallen_)
{
    if (option_ == ZLINK_SUB_OPT_TOPICS_COUNT) {
        if (!optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
            if (optvallen_)
                *optvallen_ = sizeof (int);
            errno = EINVAL;
            return -1;
        }
        std::vector<zlink::spot_sub_t::subject_descriptor_t> subjects;
        if (append_subscription_subjects (handle_, &subjects) != 0)
            return -1;
        *static_cast<int *> (optval_) = static_cast<int> (subjects.size ());
        *optvallen_ = sizeof (int);
        return 0;
    }

    const int socket_option = map_sub_to_socket_option (option_);
    if (socket_option < 0) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_)) {
        zlink::socket_base_t *socket = sub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (socket_option, optval_, optvallen_);
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub || !sub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return sub->poller_socket ()->getsockopt (socket_option, optval_,
                                                  optvallen_);
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_sub_t *sub = node->ensure_default_sub ();
        if (!sub || !sub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return sub->poller_socket ()->getsockopt (socket_option, optval_,
                                                  optvallen_);
    }

    errno = EFAULT;
    return -1;
}

int spot_subject_set_routing_id (void *handle_,
                                 const void *data_,
                                 size_t size_)
{
    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (handle_))
        return pub->set_routing_id (data_, size_);
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_))
        return sub->set_routing_id (data_, size_);
    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        return pub ? pub->set_routing_id (data_, size_) : -1;
    }
    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        return pub ? pub->set_routing_id (data_, size_) : -1;
    }
    errno = EFAULT;
    return -1;
}

int spot_subject_get_routing_id (void *handle_, zlink_routing_id_t *out_)
{
    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (handle_))
        return pub->routing_id (out_);
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_))
        return sub->routing_id (out_);
    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        return pub ? pub->routing_id (out_) : -1;
    }
    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        return pub ? pub->routing_id (out_) : -1;
    }
    errno = EFAULT;
    return -1;
}

int spot_subject_set_tls_server (void *handle_,
                                 const char *cert_,
                                 const char *key_,
                                 int require_client_cert_)
{
    LIBZLINK_UNUSED (require_client_cert_);

    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (handle_))
        return pub->node () ? pub->node ()->set_tls_server (cert_, key_) : -1;

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        if (!spot->node) {
            errno = EFAULT;
            return -1;
        }
        return spot->node->set_tls_server (cert_, key_);
    }

    if (is_registered_spot_node_handle (handle_))
        return static_cast<zlink::spot_node_t *> (handle_)->set_tls_server (
          cert_, key_);

    errno = EFAULT;
    return -1;
}

int spot_subject_set_tls_client (void *handle_,
                                 const char *ca_cert_,
                                 const char *hostname_,
                                 int trust_system_)
{
    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (handle_))
        return pub->node ()
                 ? pub->node ()->set_tls_client (ca_cert_, hostname_,
                                                trust_system_)
                 : -1;
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_))
        return sub->node ()
                 ? sub->node ()->set_tls_client (ca_cert_, hostname_,
                                                trust_system_)
                 : -1;
    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        if (!spot->node) {
            errno = EFAULT;
            return -1;
        }
        return spot->node->set_tls_client (ca_cert_, hostname_, trust_system_);
    }
    if (is_registered_spot_node_handle (handle_))
        return static_cast<zlink::spot_node_t *> (handle_)->set_tls_client (
          ca_cert_, hostname_, trust_system_);
    errno = EFAULT;
    return -1;
}

int spot_subject_set_subscription (void *handle_, const char *filter_)
{
    std::string raw_filter;
    bool is_pattern = false;
    if (!is_valid_pubsub_filter (filter_, &raw_filter, &is_pattern)
        || raw_filter.empty ()) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_)) {
        if (sub->node ()) {
            zlink::service_public_api_scope_t admission (
              sub->node ()->public_api_guard ());
            if (!admission.acquired ())
                return -1;
        }
        return is_pattern ? sub->subscribe_pattern (filter_)
                          : sub->subscribe (filter_);
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub) {
            errno = ENOTSUP;
            return -1;
        }
        return is_pattern ? sub->subscribe_pattern (filter_)
                          : sub->subscribe (filter_);
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_internal_receiver_t *receiver =
          zlink::spot_node_access_t::ensure_internal_receiver (node);
        if (!receiver)
            return -1;
        return is_pattern ? receiver->subscribe_pattern (filter_)
                          : receiver->subscribe (filter_);
    }

    errno = EFAULT;
    return -1;
}

int spot_subject_unset_subscription (void *handle_, const char *filter_)
{
    std::string raw_filter;
    bool is_pattern = false;
    if (!is_valid_pubsub_filter (filter_, &raw_filter, &is_pattern)
        || raw_filter.empty ()) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_)) {
        if (sub->node ()) {
            zlink::service_public_api_scope_t admission (
              sub->node ()->public_api_guard ());
            if (!admission.acquired ())
                return -1;
        }
        return sub->unsubscribe (filter_);
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub) {
            errno = ENOTSUP;
            return -1;
        }
        return sub->unsubscribe (filter_);
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_internal_receiver_t *receiver =
          zlink::spot_node_access_t::ensure_internal_receiver (node);
        if (!receiver)
            return -1;
        return receiver->unsubscribe (filter_);
    }

    errno = EFAULT;
    return -1;
}

int spot_subject_subscription_at (void *handle_,
                                  size_t index_,
                                  char *filter_out_,
                                  size_t *filter_len_inout_,
                                  int *is_pattern_out_)
{
    std::vector<zlink::spot_sub_t::subject_descriptor_t> subjects;
    if (append_subscription_subjects (handle_, &subjects) != 0)
        return -1;

    std::sort (subjects.begin (), subjects.end (), spot_subject_less_t ());
    if (index_ >= subjects.size ()) {
        errno = ENOENT;
        return -1;
    }
    return copy_subscription_subject (subjects[index_], filter_out_,
                                      filter_len_inout_, is_pattern_out_);
}

int spot_pub_install_send_ready_handler (void *spot_pub_,
                                         zlink_send_ready_handler_fn handler_,
                                         void *userdata_)
{
    zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_pub_);
    if (!pub) {
        errno = EFAULT;
        return -1;
    }
    if (pub->node ()) {
        zlink::service_public_api_scope_t admission (
          pub->node ()->public_api_guard ());
        if (!admission.acquired ())
            return -1;
    }
    void *subject = spot_pub_;
    if (pub->is_node_owned_default () && pub->node ())
        subject = pub->node ();
    return pub->set_send_ready_handler (handler_, subject, userdata_);
}

bool in_spot_node_send_ready_callback (zlink::spot_node_t *node_)
{
    if (!node_)
        return false;

    zlink::socket_base_t *dispatch_socket =
      zlink::socket_base_t::current_send_ready_dispatch_socket ();
    if (!dispatch_socket)
        return false;

    zlink::spot_pub_t *pub = node_->default_pub ();
    return pub && pub->owns_socket (dispatch_socket);
}

void clear_spot_node_handler_registration (zlink::spot_node_t *node_)
{
    if (!node_)
        return;

    spot_node_handler_registry_t &registry = spot_node_handler_registry ();
    zlink::scoped_lock_t lock (registry.sync);
    registry.handlers.erase (node_);
}

int spot_install_handler (spot_handle_t *spot_,
                          zlink_subscribe_handler_fn handler_,
                          void *userdata_)
{
    if (!spot_ || !handler_) {
        errno = EINVAL;
        return -1;
    }

    zlink::spot_sub_t *sub = ensure_spot_sub (spot_);
    if (!sub)
        return -1;

    spot_->handler = handler_;
    spot_->handler_userdata = userdata_;
    const int rc = sub->set_direct_handler (&spot_sub_handler_adapter, spot_);
    if (rc != 0) {
        spot_->handler = NULL;
        spot_->handler_userdata = NULL;
    }
    return rc;
}

int spot_node_install_handler (zlink::spot_node_t *node_,
                               zlink_subscribe_handler_fn handler_,
                               void *userdata_)
{
    if (!node_ || !handler_) {
        errno = EINVAL;
        return -1;
    }

    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::ensure_internal_receiver (node_);
    if (!receiver)
        return -1;

    spot_node_handler_entry_t previous = {NULL, NULL};
    bool had_previous = false;
    {
        spot_node_handler_registry_t &registry = spot_node_handler_registry ();
        zlink::scoped_lock_t lock (registry.sync);
        std::map<zlink::spot_node_t *, spot_node_handler_entry_t>::iterator it =
          registry.handlers.find (node_);
        if (it != registry.handlers.end ()) {
            previous = it->second;
            had_previous = true;
        }
        spot_node_handler_entry_t entry = {handler_, userdata_};
        registry.handlers[node_] = entry;
    }

    if (receiver->set_direct_handler (&spot_node_sub_handler_adapter, node_)
        == 0)
        return 0;

    {
        spot_node_handler_registry_t &registry = spot_node_handler_registry ();
        zlink::scoped_lock_t lock (registry.sync);
        if (had_previous)
            registry.handlers[node_] = previous;
        else
            registry.handlers.erase (node_);
    }
    return -1;
}

int spot_install_recv_handler (spot_handle_t *spot_,
                               zlink_subscribe_handler_fn handler_,
                               void *userdata_)
{
    if (!spot_ || !handler_) {
        errno = EINVAL;
        return -1;
    }

    zlink::service_public_api_scope_t admission (spot_->public_api);
    if (!admission.acquired ())
        return -1;
    if (spot_transition_to_callback_mode (spot_) != 0)
        return -1;

    const int rc = spot_install_handler (spot_, handler_, userdata_);
    if (rc != 0)
        spot_revert_callback_transition (spot_);
    return rc;
}

int spot_node_install_recv_handler (zlink::spot_node_t *node_,
                                    zlink_subscribe_handler_fn handler_,
                                    void *userdata_)
{
    if (!node_ || !handler_) {
        errno = EINVAL;
        return -1;
    }
    if (!node_->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::service_public_api_scope_t admission (node_->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    if (spot_node_transition_to_callback_mode (node_) != 0)
        return -1;

    const int rc = spot_node_install_handler (node_, handler_, userdata_);
    if (rc != 0)
        spot_node_revert_callback_transition (node_);
    return rc;
}

int spot_install_send_ready_handler (spot_handle_t *spot_,
                                     zlink_send_ready_handler_fn handler_,
                                     void *userdata_)
{
    if (!spot_ || !handler_) {
        errno = EINVAL;
        return -1;
    }

    zlink::service_public_api_scope_t admission (spot_->public_api);
    if (!admission.acquired ())
        return -1;
    bool already_active = false;
    if (spot_activate_send_ready_mode (spot_, &already_active) != 0)
        return -1;
    zlink::spot_pub_t *pub = ensure_spot_pub (spot_);
    if (!pub) {
        if (!already_active)
            spot_revert_send_ready_mode (spot_);
        errno = ENOTSUP;
        return -1;
    }
    const int rc = pub->set_send_ready_handler (handler_, spot_, userdata_);
    if (rc != 0 && !already_active)
        spot_revert_send_ready_mode (spot_);
    return rc;
}

int spot_node_install_send_ready_handler (
  zlink::spot_node_t *node_,
  zlink_send_ready_handler_fn handler_,
  void *userdata_)
{
    if (!node_ || !handler_) {
        errno = EINVAL;
        return -1;
    }
    if (!node_->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::service_public_api_scope_t admission (node_->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    bool already_active = false;
    if (spot_node_activate_send_ready_mode (node_, &already_active) != 0)
        return -1;
    const int rc = node_->set_send_ready_handler (handler_, userdata_);
    if (rc != 0 && !already_active)
        spot_node_revert_send_ready_mode (node_);
    return rc;
}
