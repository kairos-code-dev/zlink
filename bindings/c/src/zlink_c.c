/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink_c.h>
#include "../../core/src/api/service_api_c_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if !defined(ZLINK_C_THREAD_LOCAL)
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define ZLINK_C_THREAD_LOCAL _Thread_local
#elif defined(_MSC_VER)
#define ZLINK_C_THREAD_LOCAL __declspec(thread)
#else
#define ZLINK_C_THREAD_LOCAL __thread
#endif
#endif

static void zlink_c_close_parts (zlink_msg_t *parts_, size_t part_count_)
{
    size_t i;

    if (!parts_)
        return;

    for (i = 0; i < part_count_; ++i)
        (void) zlink_msg_close (&parts_[i]);
}

static void zlink_c_close_parts_from (zlink_msg_t *parts_,
                                      size_t start_index_,
                                      size_t part_count_)
{
    size_t i;

    if (!parts_)
        return;

    for (i = start_index_; i < part_count_; ++i)
        (void) zlink_msg_close (&parts_[i]);
}

static void zlink_c_copy_routing_id (zlink_routing_id_t *out_,
                                     const zlink_routing_id_t *in_)
{
    if (!out_)
        return;

    if (in_)
        memcpy (out_, in_, sizeof (*out_));
    else
        memset (out_, 0, sizeof (*out_));
}

static zlink_submit_result_t zlink_c_validate_send_parts (zlink_msg_t *parts_,
                                                          size_t part_count_)
{
    if (!parts_ || part_count_ == 0) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    return ZLINK_SUBMIT_OK;
}

static zlink_recv_result_t zlink_c_validate_recv_parts (zlink_msg_t **parts_out_,
                                                        size_t *part_count_out_)
{
    if (!parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INTERNAL_ERROR;
    }

    *parts_out_ = NULL;
    *part_count_out_ = 0;
    return ZLINK_RECV_OK;
}

static zlink_submit_result_t zlink_c_submit_result_from_rc (int rc_)
{
    const int saved_errno = errno;

    if (rc_ == 0)
        return ZLINK_SUBMIT_OK;

    switch (saved_errno != 0 ? saved_errno : EIO) {
    case EAGAIN:
        return ZLINK_SUBMIT_BACKPRESSURED;
    case ENOTCONN:
    case EHOSTUNREACH:
        return ZLINK_SUBMIT_NOT_CONNECTED;
    case ECONNREFUSED:
        return ZLINK_SUBMIT_NOT_ADMITTED;
    case ENOENT:
        return ZLINK_SUBMIT_NOT_FOUND;
    case ETERM:
    case ESHUTDOWN:
        return ZLINK_SUBMIT_TERMINATED;
    case EFAULT:
        return ZLINK_SUBMIT_INVALID_HANDLE;
    case EINVAL:
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    case ENOTSUP:
#if !defined(EOPNOTSUPP) || EOPNOTSUPP != ENOTSUP
    case EOPNOTSUPP:
#endif
        return ZLINK_SUBMIT_NOT_SUPPORTED;
    case EFSM:
    case EBUSY:
        return ZLINK_SUBMIT_INVALID_STATE;
    case EMTHREAD:
        return ZLINK_SUBMIT_THREAD_VIOLATION;
    case ENOMEM:
    case ENOBUFS:
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    default:
        return ZLINK_SUBMIT_INTERNAL_ERROR;
    }
}

static zlink_recv_result_t zlink_c_recv_result_from_rc (int rc_)
{
    const int saved_errno = errno;

    if (rc_ == 0)
        return ZLINK_RECV_OK;

    switch (saved_errno != 0 ? saved_errno : EIO) {
    case EAGAIN:
        return ZLINK_RECV_NO_DATA;
    case EBUSY:
        return ZLINK_RECV_BUSY;
    case ETERM:
    case ESHUTDOWN:
        return ZLINK_RECV_TERMINATED;
    case EFAULT:
        return ZLINK_RECV_INVALID_HANDLE;
    case ENOTSUP:
#if !defined(EOPNOTSUPP) || EOPNOTSUPP != ENOTSUP
    case EOPNOTSUPP:
#endif
        return ZLINK_RECV_NOT_SUPPORTED;
    default:
        return ZLINK_RECV_INTERNAL_ERROR;
    }
}

typedef zlink_recv_result_t (*zlink_c_recv_more_fn) (void *socket_,
                                                     zlink_msg_t *part_out_,
                                                     zlink_part_flag_t *has_more_out_);

static zlink_recv_result_t zlink_c_recv_more_plain (void *socket_,
                                                    zlink_msg_t *part_out_,
                                                    zlink_part_flag_t *has_more_out_)
{
    const zlink_routing_id_t *dummy_rid_ = NULL;

    return zlink_recv_part (
      socket_, &dummy_rid_, part_out_, has_more_out_,
      ZLINK_RECV_FLAGS_DONTWAIT);
}

static zlink_recv_result_t zlink_c_recv_more_subscribe (
  void *socket_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_)
{
    const zlink_routing_id_t *dummy_rid_ = NULL;
    size_t topic_len_ = 0;

    return zlink_subscribe_part (
      socket_, &dummy_rid_, NULL, 0, &topic_len_, part_out_, has_more_out_,
      ZLINK_RECV_FLAGS_DONTWAIT);
}

static zlink_recv_result_t zlink_c_recv_more_router (
  void *socket_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_)
{
    const zlink_routing_id_t *source_node_rid_ = NULL;
    const zlink_routing_id_t *source_spot_rid_ = NULL;
    uint64_t request_seq_ = 0;

    return zlink_router_recv_part (
      socket_, &source_node_rid_, &source_spot_rid_, &request_seq_, part_out_,
      has_more_out_, ZLINK_RECV_FLAGS_DONTWAIT);
}

static zlink_recv_result_t zlink_c_recv_more_spot_subscribe (
  void *socket_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_)
{
    const zlink_routing_id_t *dummy_rid_ = NULL;
    size_t service_name_len_ = 0;
    size_t topic_id_len_ = 0;

    return zlink_spot_subscribe_part (
      socket_, &dummy_rid_, NULL, 0, &service_name_len_, NULL, 0,
      &topic_id_len_, part_out_, has_more_out_, ZLINK_RECV_FLAGS_DONTWAIT);
}

static zlink_recv_result_t zlink_c_recv_more_spot (
  void *socket_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_)
{
    const zlink_routing_id_t *source_rid_ = NULL;
    const zlink_routing_id_t *spot_rid_ = NULL;
    uint64_t request_seq_ = 0;

    return zlink_spot_recv_part (
      socket_, &source_rid_, &spot_rid_, &request_seq_, part_out_,
      has_more_out_, ZLINK_RECV_FLAGS_DONTWAIT);
}

static zlink_recv_result_t zlink_c_collect_recv_parts (
  void *s_,
  zlink_msg_t first_,
  zlink_part_flag_t has_more_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_c_recv_more_fn recv_more_)
{
    size_t count_ = 1;
    size_t capacity_ = has_more_ ? 4u : 1u;
    zlink_msg_t *parts_ =
      (zlink_msg_t *) malloc (capacity_ * sizeof (zlink_msg_t));

    if (!parts_) {
        (void) zlink_msg_close (&first_);
        errno = ENOMEM;
        return ZLINK_RECV_INTERNAL_ERROR;
    }

    parts_[0] = first_;

    while (has_more_) {
        zlink_msg_t next_;
        zlink_part_flag_t more_ = ZLINK_PART_FINAL;
        const zlink_recv_result_t rc_ =
          recv_more_ (s_, &next_, &more_);

        if (rc_ != ZLINK_RECV_OK) {
            zlink_c_close_parts (parts_, count_);
            free (parts_);
            return rc_;
        }

        if (count_ == capacity_) {
            zlink_msg_t *grown_;
            size_t new_capacity_ = capacity_ * 2u;

            grown_ = (zlink_msg_t *) realloc (
              parts_, new_capacity_ * sizeof (zlink_msg_t));
            if (!grown_) {
                (void) zlink_msg_close (&next_);
                zlink_c_close_parts (parts_, count_);
                free (parts_);
                errno = ENOMEM;
                return ZLINK_RECV_INTERNAL_ERROR;
            }

            parts_ = grown_;
            capacity_ = new_capacity_;
        }

        parts_[count_++] = next_;
        has_more_ = more_;
    }

    *parts_out_ = parts_;
    *part_count_out_ = count_;
    return ZLINK_RECV_OK;
}

#define ZLINK_C_SEND_LOOP(CALL_EXPR)                                            \
    do {                                                                        \
        zlink_submit_result_t validate_rc_ =                                    \
          zlink_c_validate_send_parts (parts_, part_count_);                    \
        if (validate_rc_ != ZLINK_SUBMIT_OK)                                    \
            return validate_rc_;                                                \
        for (size_t i = 0; i < part_count_; ++i) {                              \
            zlink_part_flag_t part_flag_ =                                      \
              (i + 1u < part_count_) ? ZLINK_PART_MORE : ZLINK_PART_FINAL;      \
            zlink_submit_result_t rc_ = (CALL_EXPR);                            \
            if (rc_ != ZLINK_SUBMIT_OK) {                                       \
                zlink_c_close_parts_from (parts_, i + 1u, part_count_);         \
                return rc_;                                                     \
            }                                                                   \
        }                                                                       \
        return ZLINK_SUBMIT_OK;                                                 \
    } while (0)

#define ZLINK_C_REQUEST_LOOP(CALL_EXPR)                                         \
    do {                                                                        \
        zlink_submit_result_t validate_rc_ =                                    \
          zlink_c_validate_send_parts (parts_, part_count_);                    \
        if (validate_rc_ != ZLINK_SUBMIT_OK)                                    \
            return validate_rc_;                                                \
        for (size_t i = 0; i < part_count_; ++i) {                              \
            int is_final_ = (i + 1u == part_count_);                            \
            zlink_part_flag_t part_flag_ =                                      \
              is_final_ ? ZLINK_PART_FINAL : ZLINK_PART_MORE;                   \
            zlink_submit_result_t rc_ = (CALL_EXPR);                            \
            if (rc_ != ZLINK_SUBMIT_OK) {                                       \
                zlink_c_close_parts_from (parts_, i + 1u, part_count_);         \
                return rc_;                                                     \
            }                                                                   \
        }                                                                       \
        return ZLINK_SUBMIT_OK;                                                 \
    } while (0)

ZLINK_C_EXPORT zlink_submit_result_t zlink_send (void *s_,
                                                 zlink_msg_t *parts_,
                                                 size_t part_count_,
                                                 zlink_send_flags_t flags_)
{
    ZLINK_C_SEND_LOOP (zlink_send_part (s_, &parts_[i], flags_, part_flag_));
}

ZLINK_C_EXPORT zlink_submit_result_t zlink_send_rid (
  void *s_,
  const zlink_routing_id_t *target_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_)
{
    ZLINK_C_SEND_LOOP (
      zlink_send_part_rid (s_, target_rid_, &parts_[i], flags_, part_flag_));
}

ZLINK_C_EXPORT zlink_submit_result_t zlink_publish (void *subject_,
                                                    const char *topic_id_,
                                                    zlink_msg_t *parts_,
                                                    size_t part_count_,
                                                    zlink_send_flags_t flags_)
{
    zlink_submit_result_t validate_rc_ =
      zlink_c_validate_send_parts (parts_, part_count_);
    if (validate_rc_ != ZLINK_SUBMIT_OK)
        return validate_rc_;

    {
        const int service_rc = zlink_service_publish_internal (
          subject_, topic_id_, parts_, part_count_, flags_);
        const int saved_errno = errno;
        if (service_rc == 0 || saved_errno != EFAULT) {
            zlink_c_close_parts (parts_, part_count_);
            errno = saved_errno;
            return zlink_c_submit_result_from_rc (service_rc);
        }
    }

    ZLINK_C_SEND_LOOP (
      zlink_publish_part (subject_, topic_id_, &parts_[i], flags_, part_flag_));
}

ZLINK_C_EXPORT zlink_submit_result_t zlink_dealer_request (
  void *dealer_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    ZLINK_C_REQUEST_LOOP (zlink_dealer_request_part (
      dealer_, &parts_[i], flags_, part_flag_, is_final_ ? timeout_ms_ : 0u,
      is_final_ ? handler_ : NULL, is_final_ ? userdata_ : NULL));
}

ZLINK_C_EXPORT zlink_submit_result_t zlink_router_request (
  void *router_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    ZLINK_C_REQUEST_LOOP (zlink_router_request_part (
      router_, peer_rid_, &parts_[i], flags_, part_flag_,
      is_final_ ? timeout_ms_ : 0u, is_final_ ? handler_ : NULL,
      is_final_ ? userdata_ : NULL));
}

ZLINK_C_EXPORT zlink_submit_result_t zlink_router_reply (
  void *router_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    ZLINK_C_SEND_LOOP (zlink_router_reply_part (
      router_, peer_rid_, request_seq_, &parts_[i], part_flag_));
}

ZLINK_C_EXPORT zlink_submit_result_t zlink_router_request_spot (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    ZLINK_C_REQUEST_LOOP (zlink_router_request_spot_part (
      router_, dest_node_rid_, dest_spot_rid_, &parts_[i],
      is_final_ ? handler_ : NULL, is_final_ ? userdata_ : NULL, flags_,
      part_flag_, is_final_ ? timeout_ms_ : 0u));
}

ZLINK_C_EXPORT zlink_submit_result_t zlink_router_reply_spot (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    ZLINK_C_SEND_LOOP (zlink_router_reply_spot_part (
      router_, dest_node_rid_, dest_spot_rid_, request_seq_, &parts_[i],
      part_flag_));
}

ZLINK_C_EXPORT zlink_submit_result_t zlink_router_send_spot (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_)
{
    ZLINK_C_SEND_LOOP (zlink_router_send_spot_part (
      router_, dest_node_rid_, dest_spot_rid_, &parts_[i], flags_, part_flag_));
}

ZLINK_C_EXPORT zlink_recv_result_t zlink_recv (void *s_,
                                               zlink_routing_id_t *source_rid_out_,
                                               zlink_msg_t **parts_out_,
                                               size_t *part_count_out_,
                                               zlink_recv_flags_t flags_)
{
    const zlink_routing_id_t *source_rid_ptr_ = NULL;
    zlink_msg_t first_;
    zlink_part_flag_t has_more_ = ZLINK_PART_FINAL;
    zlink_recv_result_t rc_ =
      zlink_c_validate_recv_parts (parts_out_, part_count_out_);

    if (rc_ != ZLINK_RECV_OK)
        return rc_;

    zlink_c_copy_routing_id (source_rid_out_, NULL);
    rc_ = zlink_recv_part (s_, &source_rid_ptr_, &first_, &has_more_, flags_);
    if (rc_ != ZLINK_RECV_OK)
        return rc_;

    zlink_c_copy_routing_id (source_rid_out_, source_rid_ptr_);
    return zlink_c_collect_recv_parts (
      s_, first_, has_more_, parts_out_, part_count_out_,
      &zlink_c_recv_more_plain);
}

ZLINK_C_EXPORT zlink_recv_result_t zlink_subscribe (
  void *subject_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_)
{
    zlink_routing_id_t service_source_rid_;
    const zlink_routing_id_t *source_rid_ptr_ = NULL;
    size_t topic_id_capacity_ = 0;
    zlink_msg_t first_;
    zlink_part_flag_t has_more_ = ZLINK_PART_FINAL;
    zlink_recv_result_t rc_ =
      zlink_c_validate_recv_parts (parts_out_, part_count_out_);

    if (rc_ != ZLINK_RECV_OK)
        return rc_;
    if (!topic_id_len_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INTERNAL_ERROR;
    }

    topic_id_capacity_ = *topic_id_len_out_;
    zlink_c_copy_routing_id (source_rid_out_, NULL);
    rc_ = zlink_c_recv_result_from_rc (
      zlink_service_subscribe_recv_internal (
        subject_,
        &service_source_rid_,
        parts_out_,
        part_count_out_,
        topic_id_out_,
        topic_id_len_out_,
        flags_));
    if (rc_ == ZLINK_RECV_OK) {
        zlink_c_copy_routing_id (source_rid_out_, &service_source_rid_);
        return ZLINK_RECV_OK;
    }
    if (rc_ != ZLINK_RECV_INVALID_HANDLE)
        return rc_;

    rc_ = zlink_subscribe_part (subject_, &source_rid_ptr_, topic_id_out_,
                                topic_id_capacity_, topic_id_len_out_, &first_,
                                &has_more_, flags_);
    if (rc_ != ZLINK_RECV_OK)
        return rc_;

    zlink_c_copy_routing_id (source_rid_out_, source_rid_ptr_);
    return zlink_c_collect_recv_parts (
      subject_, first_, has_more_, parts_out_, part_count_out_,
      &zlink_c_recv_more_subscribe);
}

ZLINK_C_EXPORT zlink_recv_result_t zlink_router_recv (
  void *router_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_)
{
    static ZLINK_C_THREAD_LOCAL zlink_routing_id_t source_node_rid_copy_;
    static ZLINK_C_THREAD_LOCAL zlink_routing_id_t source_spot_rid_copy_;
    const zlink_routing_id_t *source_node_rid_local_ = NULL;
    const zlink_routing_id_t *source_spot_rid_local_ = NULL;
    const zlink_routing_id_t **source_node_rid_ptr_ =
      source_node_rid_out_ ? source_node_rid_out_ : &source_node_rid_local_;
    const zlink_routing_id_t **source_spot_rid_ptr_ =
      source_spot_rid_out_ ? source_spot_rid_out_ : &source_spot_rid_local_;
    uint64_t request_seq_local_ = 0;
    uint64_t *request_seq_ptr_ =
      request_seq_out_ ? request_seq_out_ : &request_seq_local_;
    zlink_msg_t first_;
    zlink_part_flag_t has_more_ = ZLINK_PART_FINAL;
    zlink_recv_result_t rc_ =
      zlink_c_validate_recv_parts (parts_out_, part_count_out_);

    if (rc_ != ZLINK_RECV_OK)
        return rc_;

    if (source_node_rid_out_)
        *source_node_rid_out_ = NULL;
    if (source_spot_rid_out_)
        *source_spot_rid_out_ = NULL;
    if (request_seq_out_)
        *request_seq_out_ = 0;

    rc_ = zlink_router_recv_part (router_, source_node_rid_ptr_,
                                  source_spot_rid_ptr_, request_seq_ptr_,
                                  &first_, &has_more_, flags_);
    if (rc_ != ZLINK_RECV_OK)
        return rc_;

    zlink_c_copy_routing_id (&source_node_rid_copy_, *source_node_rid_ptr_);
    zlink_c_copy_routing_id (&source_spot_rid_copy_, *source_spot_rid_ptr_);

    rc_ = zlink_c_collect_recv_parts (
      router_, first_, has_more_, parts_out_, part_count_out_,
      &zlink_c_recv_more_router);
    if (rc_ != ZLINK_RECV_OK)
        return rc_;

    if (source_node_rid_out_) {
        *source_node_rid_out_ =
          *source_node_rid_ptr_ ? &source_node_rid_copy_ : NULL;
    }
    if (source_spot_rid_out_) {
        *source_spot_rid_out_ =
          *source_spot_rid_ptr_ ? &source_spot_rid_copy_ : NULL;
    }
    return ZLINK_RECV_OK;
}

ZLINK_C_EXPORT zlink_recv_result_t zlink_subscription_event (
  void *subject_,
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_)
{
    const zlink_routing_id_t *source_rid_ptr_ = NULL;
    size_t topic_id_capacity_ = 0;
    zlink_recv_result_t rc_;

    if (!subscribed_out_ || !topic_id_len_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INTERNAL_ERROR;
    }

    topic_id_capacity_ = *topic_id_len_out_;
    zlink_c_copy_routing_id (source_rid_out_, NULL);
    rc_ = zlink_xpub_recv_part (subject_, &source_rid_ptr_, subscribed_out_,
                                topic_id_out_, topic_id_capacity_,
                                topic_id_len_out_, flags_);
    if (rc_ != ZLINK_RECV_OK)
        return rc_;

    zlink_c_copy_routing_id (source_rid_out_, source_rid_ptr_);
    return ZLINK_RECV_OK;
}

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_send_channel (
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_)
{
    ZLINK_C_SEND_LOOP (zlink_spot_send_channel_part (
      spot_, channel_name_, &parts_[i], flags_, part_flag_));
}

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_publish (
  void *spot_,
  const char *service_name_,
  const char *topic_id_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_)
{
    ZLINK_C_SEND_LOOP (zlink_spot_publish_part (
      spot_, service_name_, topic_id_, &parts_[i], flags_, part_flag_));
}

ZLINK_C_EXPORT zlink_recv_result_t zlink_spot_subscribe (
  void *spot_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_)
{
    const zlink_routing_id_t *source_rid_ptr_ = NULL;
    size_t service_name_capacity_ = 0;
    size_t topic_id_capacity_ = 0;
    zlink_msg_t first_;
    zlink_part_flag_t has_more_ = ZLINK_PART_FINAL;
    zlink_recv_result_t rc_ =
      zlink_c_validate_recv_parts (parts_out_, part_count_out_);

    if (rc_ != ZLINK_RECV_OK)
        return rc_;
    if (!service_name_len_out_ || !topic_id_len_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INTERNAL_ERROR;
    }

    service_name_capacity_ = *service_name_len_out_;
    topic_id_capacity_ = *topic_id_len_out_;
    zlink_c_copy_routing_id (source_rid_out_, NULL);
    rc_ = zlink_spot_subscribe_part (
      spot_, &source_rid_ptr_, service_name_out_, service_name_capacity_,
      service_name_len_out_, topic_id_out_, topic_id_capacity_,
      topic_id_len_out_, &first_, &has_more_, flags_);
    if (rc_ != ZLINK_RECV_OK)
        return rc_;

    zlink_c_copy_routing_id (source_rid_out_, source_rid_ptr_);
    return zlink_c_collect_recv_parts (
      spot_, first_, has_more_, parts_out_, part_count_out_,
      &zlink_c_recv_more_spot_subscribe);
}

ZLINK_C_EXPORT zlink_recv_result_t zlink_spot_subscription_event (
  void *spot_,
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_)
{
    (void) spot_;
    (void) service_name_out_;
    (void) topic_id_out_;
    (void) flags_;

    zlink_c_copy_routing_id (source_rid_out_, NULL);
    if (subscribed_out_)
        *subscribed_out_ = 0;
    if (service_name_len_out_)
        *service_name_len_out_ = 0;
    if (topic_id_len_out_)
        *topic_id_len_out_ = 0;

    /* No clear *_part substrate equivalent exists in the current zlink.h. */
    errno = ENOTSUP;
    return ZLINK_RECV_NOT_SUPPORTED;
}

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_request_channel (
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    ZLINK_C_REQUEST_LOOP (zlink_spot_request_channel_part (
      spot_, channel_name_, &parts_[i], is_final_ ? handler_ : NULL,
      is_final_ ? userdata_ : NULL, flags_, part_flag_,
      is_final_ ? timeout_ms_ : 0u));
}

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_reply_spot (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    ZLINK_C_SEND_LOOP (zlink_spot_reply_spot_part (
      spot_, dest_node_rid_, dest_spot_rid_, request_seq_, &parts_[i],
      part_flag_));
}

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_reply_router (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    ZLINK_C_SEND_LOOP (zlink_spot_reply_router_part (
      spot_, peer_rid_, request_seq_, &parts_[i], part_flag_));
}

ZLINK_C_EXPORT zlink_recv_result_t zlink_spot_recv (
  void *spot_,
  const zlink_routing_id_t **source_rid_out_,
  const zlink_routing_id_t **spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_)
{
    const zlink_routing_id_t *source_rid_local_ = NULL;
    const zlink_routing_id_t *spot_rid_local_ = NULL;
    const zlink_routing_id_t **source_rid_ptr_ =
      source_rid_out_ ? source_rid_out_ : &source_rid_local_;
    const zlink_routing_id_t **spot_rid_ptr_ =
      spot_rid_out_ ? spot_rid_out_ : &spot_rid_local_;
    uint64_t request_seq_local_ = 0;
    uint64_t *request_seq_ptr_ =
      request_seq_out_ ? request_seq_out_ : &request_seq_local_;
    zlink_msg_t first_;
    zlink_part_flag_t has_more_ = ZLINK_PART_FINAL;
    zlink_recv_result_t rc_ =
      zlink_c_validate_recv_parts (parts_out_, part_count_out_);

    if (rc_ != ZLINK_RECV_OK)
        return rc_;

    if (source_rid_out_)
        *source_rid_out_ = NULL;
    if (spot_rid_out_)
        *spot_rid_out_ = NULL;
    if (request_seq_out_)
        *request_seq_out_ = 0;

    rc_ = zlink_spot_recv_part (spot_, source_rid_ptr_, spot_rid_ptr_,
                                request_seq_ptr_, &first_, &has_more_, flags_);
    if (rc_ != ZLINK_RECV_OK)
        return rc_;

    return zlink_c_collect_recv_parts (
      spot_, first_, has_more_, parts_out_, part_count_out_,
      &zlink_c_recv_more_spot);
}
