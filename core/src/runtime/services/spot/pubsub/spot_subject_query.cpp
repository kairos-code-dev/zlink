/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/pubsub/spot_subject_access.hpp"

#include "api/service/service_handle_internal.hpp"
#include "api/service/service_mode_internal.hpp"
#include "api/service/service_option_surface_internal.hpp"
#include "core/c_api_copy_internal.hpp"
#include "core/recv_tls_view.hpp"

#include <algorithm>
#include <string>
#include <string.h>
#include <vector>

#include "services/spot/dispatch/spot_dispatch_internal.hpp"
#include "services/spot/dispatch/spot_internal_receiver.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/pubsub/spot_sub.hpp"
#include "services/spot/pubsub/spot_subject_subscription_internal.hpp"

namespace
{
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

struct spot_subject_less_t
{
    bool operator() (const zlink::spot_sub_t::subject_descriptor_t &lhs_,
                     const zlink::spot_sub_t::subject_descriptor_t &rhs_) const
    {
        if (lhs_.subject != rhs_.subject)
            return lhs_.subject < rhs_.subject;
        return lhs_.subject_kind < rhs_.subject_kind;
    }
};

int copy_subscription_subject (const zlink::spot_sub_t::subject_descriptor_t &subject_,
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
    if (is_pattern_out_) {
        *is_pattern_out_ = subject_.subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_PATTERN ? 1 : 0;
    }
    return 0;
}

void append_logical_subscription_subjects (
  const std::shared_ptr<spot_logical_state_t> &state_,
  std::vector<zlink::spot_sub_t::subject_descriptor_t> *out_)
{
    if (!state_ || !out_)
        return;

    zlink::scoped_lock_t lock (state_->pubsub_sync);
    out_->reserve (out_->size () + state_->subscription_topics.size ()
                   + state_->subscription_patterns.size ());
    for (std::set<std::string>::const_iterator it = state_->subscription_topics.begin ();
         it != state_->subscription_topics.end (); ++it) {
        zlink::spot_sub_t::subject_descriptor_t subject;
        subject.subject = *it;
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
        out_->push_back (subject);
    }
    for (std::set<std::string>::const_iterator it = state_->subscription_patterns.begin ();
         it != state_->subscription_patterns.end (); ++it) {
        zlink::spot_sub_t::subject_descriptor_t subject;
        subject.subject = *it + "*";
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_PATTERN;
        out_->push_back (subject);
    }
}

bool logical_subscription_insert (const std::shared_ptr<spot_logical_state_t> &state_,
                                  const std::string &raw_filter_,
                                  bool is_pattern_)
{
    if (!state_)
        return false;

    zlink::scoped_lock_t lock (state_->pubsub_sync);
    return is_pattern_ ? state_->subscription_patterns.insert (raw_filter_).second
                       : state_->subscription_topics.insert (raw_filter_).second;
}

bool logical_subscription_erase (const std::shared_ptr<spot_logical_state_t> &state_,
                                 const std::string &raw_filter_,
                                 bool is_pattern_)
{
    if (!state_)
        return false;

    zlink::scoped_lock_t lock (state_->pubsub_sync);
    return is_pattern_ ? state_->subscription_patterns.erase (raw_filter_) != 0
                       : state_->subscription_topics.erase (raw_filter_) != 0;
}

int copy_topic_to_output (const std::string &topic_, char *topic_id_out_, size_t *topic_id_len_out_)
{
    return zlink::copy_bytes_to_sized_output (topic_.data (), topic_.size (), topic_id_out_,
                                              topic_id_len_out_);
}

int recv_logical_spot_subscription_impl (spot_handle_t *spot_,
                                         zlink_routing_id_t *source_rid_out_,
                                         zlink_msg_t **parts_out_,
                                         size_t *part_count_out_,
                                         char *topic_id_out_,
                                         size_t *topic_id_len_out_)
{
    if (!spot_ || !spot_->logical_state || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<spot_logical_pubsub_message_t> message;
    bool drain_signal = false;
    bool resignal = false;
    {
        zlink::scoped_lock_t lock (spot_->logical_state->pubsub_sync);
        if (spot_->logical_state->subscribe_queue.empty ()) {
            errno = EAGAIN;
            return -1;
        }
        message = spot_->logical_state->subscribe_queue.front ();
        spot_->logical_state->subscribe_queue.pop_front ();
        drain_signal = spot_->logical_state->subscribe_signal_armed;
        if (spot_->logical_state->subscribe_queue.empty ()) {
            spot_->logical_state->subscribe_signal_armed = false;
        } else if (spot_->logical_state->subscribe_signal_armed) {
            resignal = true;
        }
    }
    if (drain_signal && spot_->logical_state->subscribe_signaler.valid ())
        (void) spot_->logical_state->subscribe_signaler.recv_failable ();
    if (resignal && spot_->logical_state->subscribe_signaler.valid ())
        spot_->logical_state->subscribe_signaler.send ();
    if (!message) {
        errno = EAGAIN;
        return -1;
    }

    if (zlink::recv_tls_view::begin (parts_out_, part_count_out_) != 0)
        return -1;
    for (size_t i = 0; i < message->parts.size (); ++i) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (zlink_msg_init_size (&frame, message->parts[i].size ()) != 0) {
            zlink_msg_close (&frame);
            zlink::recv_tls_view::abort ();
            return -1;
        }
        if (!message->parts[i].empty ())
            memcpy (zlink_msg_data (&frame), message->parts[i].data (), message->parts[i].size ());
        if (zlink::recv_tls_view::push (&frame) != 0) {
            zlink_msg_close (&frame);
            zlink::recv_tls_view::abort ();
            return -1;
        }
    }
    if (zlink::recv_tls_view::commit (parts_out_, part_count_out_) != 0) {
        zlink::recv_tls_view::abort ();
        return -1;
    }
    if (source_rid_out_)
        *source_rid_out_ = message->source_rid;
    if (copy_topic_to_output (message->topic_id, topic_id_out_, topic_id_len_out_) != 0) {
        zlink::recv_tls_view::abort ();
        return -1;
    }
    return 0;
}
} // namespace

int recv_logical_spot_subscription (spot_handle_t *spot_,
                                    zlink_routing_id_t *source_rid_out_,
                                    zlink_msg_t **parts_out_,
                                    size_t *part_count_out_,
                                    char *topic_id_out_,
                                    size_t *topic_id_len_out_)
{
    return recv_logical_spot_subscription_impl (spot_, source_rid_out_, parts_out_,
                                                part_count_out_, topic_id_out_,
                                                topic_id_len_out_);
}

int spot_append_subscription_subjects (void *handle_,
                                       std::vector<zlink::spot_sub_t::subject_descriptor_t> *out_)
{
    if (!out_) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_)) {
        if (sub->node ()) {
            zlink::service_public_api_scope_t admission (sub->node ()->public_api_guard ());
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
        append_logical_subscription_subjects (spot->logical_state, out_);
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

int spot_subject_recv (void *subject_,
                       zlink_routing_id_t *source_rid_out_,
                       zlink_msg_t **parts_out_,
                       size_t *part_count_out_,
                       char *topic_id_out_,
                       size_t *topic_id_len_out_,
                       zlink_recv_flags_t flags_)
{
    if (validate_recv_flags (flags_) != 0)
        return -1;

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (subject_))
        return sub->recv (source_rid_out_, parts_out_, part_count_out_, flags_, topic_id_out_,
                          topic_id_len_out_);

    if (spot_handle_t *spot = as_spot_handle (subject_)) {
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        if (spot_require_recv_model (spot) != 0)
            return -1;
        return recv_logical_spot_subscription (spot, source_rid_out_, parts_out_, part_count_out_,
                                               topic_id_out_, topic_id_len_out_);
    }

    if (is_registered_spot_node_handle (subject_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (subject_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        if (spot_node_require_recv_model (node) != 0)
            return -1;
        zlink::spot_internal_receiver_t *receiver =
          zlink::spot_node_access_t::ensure_internal_receiver (node);
        zlink::spot_sub_t *sub = receiver ? receiver->impl () : NULL;
        if (!sub) {
            errno = ENOTSUP;
            return -1;
        }
        return sub->recv (source_rid_out_, parts_out_, part_count_out_, flags_, topic_id_out_,
                          topic_id_len_out_);
    }

    errno = EFAULT;
    return -1;
}

int spot_subject_set_routing_id (void *handle_, const void *data_, size_t size_)
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
        if (!spot->node
            || zlink::spot_node_access_t::update_spot_routing_id (spot->node, spot, data_, size_)
                 != 0)
            return -1;
        return 0;
    }
    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        if (node->set_node_routing_id (data_, size_) != 0)
            return -1;
        (void) zlink_service_spot_node_refresh_routed_router_identity (handle_);
        return 0;
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
        if (!out_) {
            errno = EINVAL;
            return -1;
        }
        *out_ = spot->spot_routing_id;
        return 0;
    }
    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        return node->node_routing_id (out_);
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

    if (as_spot_pub_side_handle (handle_)) {
        errno = ENOTSUP;
        return -1;
    }

    if (is_registered_spot_handle (handle_)) {
        errno = ENOTSUP;
        return -1;
    }

    if (is_registered_spot_node_handle (handle_))
        return static_cast<zlink::spot_node_t *> (handle_)->set_tls_server (cert_, key_);

    errno = EFAULT;
    return -1;
}

int spot_subject_set_tls_client (void *handle_,
                                 const char *ca_cert_,
                                 const char *hostname_,
                                 int trust_system_)
{
    if (as_spot_pub_side_handle (handle_)) {
        errno = ENOTSUP;
        return -1;
    }
    if (as_spot_sub_side_handle (handle_)) {
        errno = ENOTSUP;
        return -1;
    }
    if (is_registered_spot_handle (handle_)) {
        errno = ENOTSUP;
        return -1;
    }
    if (is_registered_spot_node_handle (handle_))
        return static_cast<zlink::spot_node_t *> (handle_)->set_tls_client (ca_cert_, hostname_,
                                                                            trust_system_);
    errno = EFAULT;
    return -1;
}

int spot_subject_set_subscription (void *handle_, const char *filter_)
{
    std::string raw_filter;
    bool is_pattern = false;
    if (!is_valid_pubsub_filter (filter_, &raw_filter, &is_pattern) || raw_filter.empty ()) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_)) {
        if (sub->node ()) {
            zlink::service_public_api_scope_t admission (sub->node ()->public_api_guard ());
            if (!admission.acquired ())
                return -1;
        }
        return is_pattern ? sub->subscribe_pattern (filter_) : sub->subscribe (filter_);
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        if (!logical_subscription_insert (spot->logical_state, raw_filter, is_pattern))
            return 0;
        if (!spot->node
            || spot->node->update_logical_spot_subscription (raw_filter, is_pattern, true) != 0) {
            logical_subscription_erase (spot->logical_state, raw_filter, is_pattern);
            return -1;
        }
        return 0;
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_internal_receiver_t *receiver =
          zlink::spot_node_access_t::internal_receiver (node);
        if (receiver) {
            return is_pattern ? receiver->subscribe_pattern (filter_)
                              : receiver->subscribe (filter_);
        }
        zlink::spot_sub_t *sub = node->ensure_default_sub ();
        if (!sub) {
            errno = ENOTSUP;
            return -1;
        }
        return is_pattern ? sub->subscribe_pattern (filter_) : sub->subscribe (filter_);
    }

    errno = EFAULT;
    return -1;
}

int spot_subject_unset_subscription (void *handle_, const char *filter_)
{
    std::string raw_filter;
    bool is_pattern = false;
    if (!is_valid_pubsub_filter (filter_, &raw_filter, &is_pattern) || raw_filter.empty ()) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_)) {
        if (sub->node ()) {
            zlink::service_public_api_scope_t admission (sub->node ()->public_api_guard ());
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
        if (!logical_subscription_erase (spot->logical_state, raw_filter, is_pattern))
            return 0;
        if (!spot->node
            || spot->node->update_logical_spot_subscription (raw_filter, is_pattern, false) != 0) {
            logical_subscription_insert (spot->logical_state, raw_filter, is_pattern);
            return -1;
        }
        return 0;
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_internal_receiver_t *receiver =
          zlink::spot_node_access_t::internal_receiver (node);
        if (receiver)
            return receiver->unsubscribe (filter_);
        zlink::spot_sub_t *sub = node->ensure_default_sub ();
        if (!sub) {
            errno = ENOTSUP;
            return -1;
        }
        return sub->unsubscribe (filter_);
    }

    errno = EFAULT;
    return -1;
}

int spot_subject_subscription_at (
  void *handle_, size_t index_, char *filter_out_, size_t *filter_len_inout_, int *is_pattern_out_)
{
    std::vector<zlink::spot_sub_t::subject_descriptor_t> subjects;
    if (spot_append_subscription_subjects (handle_, &subjects) != 0)
        return -1;

    std::sort (subjects.begin (), subjects.end (), spot_subject_less_t ());
    if (index_ >= subjects.size ()) {
        errno = ENOENT;
        return -1;
    }
    return copy_subscription_subject (subjects[index_], filter_out_, filter_len_inout_,
                                      is_pattern_out_);
}
