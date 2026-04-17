/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"
#include "api/recv_result_internal.hpp"
#include "api/submit_result_internal.hpp"
#include "services/spot/spot_node_access.hpp"

#include <string>

namespace
{
int copy_service_name_to_output (const std::string &service_name_,
                                 char *service_name_out_,
                                 size_t *service_name_len_out_)
{
    if (!service_name_len_out_) {
        errno = EFAULT;
        return -1;
    }

    if (!service_name_out_) {
        *service_name_len_out_ = service_name_.size ();
        return 0;
    }

    if (*service_name_len_out_ < service_name_.size ()) {
        *service_name_len_out_ = service_name_.size ();
        errno = EMSGSIZE;
        return -1;
    }

    if (!service_name_.empty ())
        memcpy (service_name_out_, service_name_.data (), service_name_.size ());
    *service_name_len_out_ = service_name_.size ();
    return 0;
}

int resolve_spot_bound_service_name (spot_handle_t *spot_,
                                     std::string *service_name_out_)
{
    if (!spot_ || !spot_->node || !service_name_out_) {
        errno = EFAULT;
        return -1;
    }

    *service_name_out_ =
      zlink::spot_node_access_t::summary_service_name (spot_->node);
    if (service_name_out_->empty ()) {
        errno = ENOENT;
        return -1;
    }

    return 0;
}

bool service_name_matches_or_unset (const std::string &bound_service_name_,
                                    const char *service_name_)
{
    return !bound_service_name_.empty () && bound_service_name_ == service_name_;
}
}

int spot_publish_internal (void *spot_,
                           const char *topic_id_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           zlink_send_flags_t flags_)
{
    return spot_subject_publish (spot_, topic_id_, parts_, part_count_, flags_);
}

int spot_pub_publish_internal (void *spot_pub_,
                               const char *topic_id_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               zlink_send_flags_t flags_)
{
    return spot_subject_publish (spot_pub_, topic_id_, parts_, part_count_,
                                 flags_);
}

int spot_node_publish_internal (void *node_,
                                const char *topic_id_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_)
{
    return spot_subject_publish (node_, topic_id_, parts_, part_count_, flags_);
}

int spot_pub_send_ready_handler_internal (void *spot_pub_,
                                          zlink_send_ready_handler_fn handler_,
                                          void *userdata_)
{
    return spot_pub_install_send_ready_handler (spot_pub_, handler_, userdata_);
}

int spot_sub_subscribe_internal (void *spot_sub_, const char *topic_id_)
{
    return spot_subject_set_subscription (spot_sub_, topic_id_);
}

int spot_sub_subscribe_pattern_internal (void *spot_sub_,
                                         const char *pattern_)
{
    return spot_subject_set_subscription (spot_sub_, pattern_);
}

int spot_sub_unsubscribe_internal (void *spot_sub_,
                                   const char *topic_id_or_pattern_)
{
    return spot_subject_unset_subscription (spot_sub_, topic_id_or_pattern_);
}

int spot_sub_recv_internal (void *sub_,
                            const zlink_routing_id_t **source_rid_out_,
                            zlink_msg_t **parts_,
                            size_t *part_count_,
                            char *topic_id_out_,
                            size_t *topic_id_len_,
                            zlink_send_flags_t flags_)
{
    static thread_local zlink_routing_id_t source_rid_value;
    zlink_routing_id_t *source_rid_value_out =
      source_rid_out_ ? &source_rid_value : NULL;
    const int rc = spot_subject_recv (sub_, source_rid_value_out, parts_,
                                      part_count_, topic_id_out_,
                                      topic_id_len_, flags_);
    if (source_rid_out_)
        *source_rid_out_ = rc == 0 ? source_rid_value_out : NULL;
    return rc;
}

int spot_node_recv_internal (void *node_,
                             const zlink_routing_id_t **source_rid_out_,
                             zlink_msg_t **parts_,
                             size_t *part_count_,
                             char *topic_id_out_,
                             size_t *topic_id_len_,
                             zlink_send_flags_t flags_)
{
    static thread_local zlink_routing_id_t source_rid_value;
    zlink_routing_id_t *source_rid_value_out =
      source_rid_out_ ? &source_rid_value : NULL;
    const int rc = spot_subject_recv (node_, source_rid_value_out, parts_,
                                      part_count_, topic_id_out_,
                                      topic_id_len_, flags_);
    if (source_rid_out_)
        *source_rid_out_ = rc == 0 ? source_rid_value_out : NULL;
    return rc;
}

zlink_submit_result_t zlink_spot_publish (void *spot_,
                                          const char *service_name_,
                                          const char *topic_id_,
                                          zlink_msg_t *parts_,
                                          size_t part_count_,
                                          zlink_send_flags_t flags_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return ZLINK_SUBMIT_INVALID_HANDLE;
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    std::string bound_service_name;
    if (resolve_spot_bound_service_name (spot, &bound_service_name) == 0
        && service_name_matches_or_unset (bound_service_name, service_name_)) {
        return zlink_publish (spot_, topic_id_, parts_, part_count_, flags_);
    }

    zlink::socket_base_t *service_pub =
      zlink::spot_node_access_t::service_pub_socket (spot->node, service_name_);
    if (service_pub) {
        return zlink_publish (service_pub, topic_id_, parts_, part_count_, flags_);
    }

    const int service_pub_errno = errno;

    errno = service_pub_errno == ENOTCONN ? ENOTCONN : ENOENT;
    return service_pub_errno == ENOTCONN ? ZLINK_SUBMIT_NOT_CONNECTED
                                         : ZLINK_SUBMIT_NOT_FOUND;
}

zlink_recv_result_t zlink_spot_subscribe (void *spot_,
                                          const zlink_routing_id_t **source_rid_out_,
                                          zlink_msg_t **parts_out_,
                                          size_t *part_count_out_,
                                          char *service_name_out_,
                                          size_t *service_name_len_out_,
                                          char *topic_id_out_,
                                          size_t *topic_id_len_out_,
                                          zlink_recv_flags_t flags_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return ZLINK_RECV_INVALID_HANDLE;

    const zlink_recv_result_t service_recv =
      zlink::recv_result_internal::from_rc (
        zlink::spot_node_access_t::service_subscribe_recv (
          spot->node, source_rid_out_, parts_out_, part_count_out_,
          service_name_out_, service_name_len_out_, topic_id_out_,
          topic_id_len_out_, flags_));
    if (service_recv == ZLINK_RECV_OK)
        return service_recv;
    if (service_recv != ZLINK_RECV_NO_DATA)
        return service_recv;

    const zlink_routing_id_t *source_rid = NULL;
    const zlink_recv_result_t recv_rc =
      zlink_subscribe (spot_, &source_rid, parts_out_, part_count_out_,
                       topic_id_out_, topic_id_len_out_, flags_);
    if (recv_rc != ZLINK_RECV_OK)
        return recv_rc;
    if (source_rid_out_)
        *source_rid_out_ = source_rid;

    std::string bound_service_name;
    if (resolve_spot_bound_service_name (spot, &bound_service_name) != 0)
        return zlink::recv_result_internal::from_errno (errno);
    return zlink::recv_result_internal::from_rc (copy_service_name_to_output (
      bound_service_name, service_name_out_, service_name_len_out_));
}

zlink_recv_result_t zlink_spot_subscription_event (
  void *spot_,
  const zlink_routing_id_t **source_rid_out_,
  int *subscribed_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return ZLINK_RECV_INVALID_HANDLE;

    const zlink_recv_result_t service_recv =
      zlink::recv_result_internal::from_rc (
        zlink::spot_node_access_t::service_subscription_event_recv (
          spot->node, source_rid_out_, subscribed_out_, service_name_out_,
          service_name_len_out_, topic_id_out_, topic_id_len_out_, flags_));
    if (service_recv == ZLINK_RECV_OK)
        return service_recv;
    if (service_recv != ZLINK_RECV_NO_DATA)
        return service_recv;

    const zlink_routing_id_t *source_rid = NULL;
    const zlink_recv_result_t recv_rc =
      zlink_subscription_event (spot_, &source_rid, subscribed_out_,
                                topic_id_out_, topic_id_len_out_, flags_);
    if (recv_rc != ZLINK_RECV_OK)
        return recv_rc;
    if (source_rid_out_)
        *source_rid_out_ = source_rid;

    std::string bound_service_name;
    if (resolve_spot_bound_service_name (spot, &bound_service_name) != 0)
        return zlink::recv_result_internal::from_errno (errno);
    return zlink::recv_result_internal::from_rc (copy_service_name_to_output (
      bound_service_name, service_name_out_, service_name_len_out_));
}
