/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_api_internal.hpp"

#include <algorithm>
#include <string.h>
#include <string>
#include <vector>

#include "services/spot/spot_internal_receiver.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_sub.hpp"

namespace
{
static bool is_valid_pubsub_filter (const char *filter_,
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

static int spot_node_subscribe_internal (void *node_, const char *topic_id_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::ensure_internal_receiver (node);
    if (!receiver)
        return -1;
    return receiver->subscribe (topic_id_);
}

static int spot_node_subscribe_pattern_internal (void *node_,
                                                 const char *pattern_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::ensure_internal_receiver (node);
    if (!receiver)
        return -1;
    return receiver->subscribe_pattern (pattern_);
}

static int spot_node_unsubscribe_internal (void *node_,
                                           const char *topic_id_or_pattern_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::ensure_internal_receiver (node);
    if (!receiver)
        return -1;
    return receiver->unsubscribe (topic_id_or_pattern_);
}

static int spot_subscribe_internal (void *spot_, const char *topic_id_)
{
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_))
        return sub->subscribe (topic_id_);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    zlink::spot_sub_t *sub = ensure_spot_sub (spot);
    if (!sub) {
        errno = ENOTSUP;
        return -1;
    }
    return sub->subscribe (topic_id_);
}

static int spot_subscribe_pattern_internal (void *spot_, const char *pattern_)
{
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_))
        return sub->subscribe_pattern (pattern_);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    zlink::spot_sub_t *sub = ensure_spot_sub (spot);
    if (!sub) {
        errno = ENOTSUP;
        return -1;
    }
    return sub->subscribe_pattern (pattern_);
}

static int spot_unsubscribe_internal (void *spot_,
                                      const char *topic_id_or_pattern_)
{
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_))
        return sub->unsubscribe (topic_id_or_pattern_);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    zlink::spot_sub_t *sub = ensure_spot_sub (spot);
    if (!sub) {
        errno = ENOTSUP;
        return -1;
    }
    return sub->unsubscribe (topic_id_or_pattern_);
}

struct spot_subscription_less_t
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

static int map_sub_to_socket_option (zlink_sub_option_t option_)
{
    switch (option_) {
        case ZLINK_SUB_OPT_TOPICS_COUNT:
            return ZLINK_INTERNAL_OPT_TOPICS_COUNT;
        default:
            return -1;
    }
}

static int copy_subscription_subject (
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
}

int zlink_spot_subject_get_sub_option_internal (void *handle_,
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
            sub->append_all_subjects (&subjects);
        } else if (is_registered_spot_node_handle (handle_)) {
            zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
            zlink::service_public_api_scope_t admission (
              node->public_api_guard ());
            if (!admission.acquired ())
                return -1;
            node->snapshot_subscription_subjects (&subjects);
        } else {
            errno = EFAULT;
            return -1;
        }

        *static_cast<int *> (optval_) = static_cast<int> (subjects.size ());
        *optvallen_ = sizeof (int);
        return 0;
    }

    const int socket_option = map_sub_to_socket_option (option_);
    if (socket_option < 0) {
        errno = EINVAL;
        return -1;
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

int zlink_spot_subject_set_subscription_internal (void *handle_,
                                                  const char *filter_)
{
    std::string raw_filter;
    bool is_pattern = false;
    if (!is_valid_pubsub_filter (filter_, &raw_filter, &is_pattern)
        || raw_filter.empty ()) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_spot_handle (handle_))
        return is_pattern ? spot_subscribe_pattern_internal (handle_, filter_)
                          : spot_subscribe_internal (handle_, filter_);
    if (is_registered_spot_node_handle (handle_))
        return is_pattern ? spot_node_subscribe_pattern_internal (handle_,
                                                                  filter_)
                          : spot_node_subscribe_internal (handle_, filter_);

    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_unset_subscription_internal (void *handle_,
                                                    const char *filter_)
{
    std::string raw_filter;
    bool is_pattern = false;
    if (!is_valid_pubsub_filter (filter_, &raw_filter, &is_pattern)
        || raw_filter.empty ()) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_spot_handle (handle_))
        return spot_unsubscribe_internal (handle_, filter_);
    if (is_registered_spot_node_handle (handle_))
        return spot_node_unsubscribe_internal (handle_, filter_);

    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_subscription_at_internal (void *handle_,
                                                 size_t index_,
                                                 char *filter_out_,
                                                 size_t *filter_len_inout_,
                                                 int *is_pattern_out_)
{
    std::vector<zlink::spot_sub_t::subject_descriptor_t> subjects;

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
        sub->append_all_subjects (&subjects);
    } else if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        node->snapshot_subscription_subjects (&subjects);
    } else {
        errno = EFAULT;
        return -1;
    }

    std::sort (subjects.begin (), subjects.end (), spot_subscription_less_t ());
    if (index_ >= subjects.size ()) {
        errno = ENOENT;
        return -1;
    }
    return copy_subscription_subject (subjects[index_], filter_out_,
                                      filter_len_inout_, is_pattern_out_);
}
