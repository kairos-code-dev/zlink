/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/socket/part_helper_internal.hpp"
#include "api/service/service_mode_internal.hpp"
#include "api/spot/subject/service_spot_subject_surface_internal.hpp"
#include "api/socket/socket_message_api_internal.hpp"
#include "api/message/recv_result_internal.hpp"
#include "api/message/submit_result_internal.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/pubsub/spot_subject_access.hpp"

#include <mutex>
#include <string>

namespace
{
zlink_routing_id_t &spot_service_event_source_rid_tls ()
{
    static thread_local zlink_routing_id_t rid;
    return rid;
}

zlink_recv_result_t try_dequeue_logical_subscribe (spot_handle_t *spot_,
                                                   zlink_routing_id_t *source_rid_out_,
                                                   zlink_msg_t **parts_out_,
                                                   size_t *part_count_out_,
                                                   char *topic_id_out_,
                                                   size_t *topic_id_len_out_)
{
    if (!spot_ || !spot_->logical_state)
        return ZLINK_RECV_NO_DATA;

    return zlink::recv_result_internal::from_rc (
      recv_logical_spot_subscription (spot_, source_rid_out_, parts_out_, part_count_out_,
                                      topic_id_out_, topic_id_len_out_));
}

bool send_sequence_active (void *handle_)
{
    std::shared_ptr<zlink::part_helper_internal::handle_state_t> state =
      zlink::part_helper_internal::find_handle_state (handle_);
    if (!state)
        return false;

    std::lock_guard<std::mutex> lock (state->mutex);
    return state->send.active;
}

}

zlink_submit_result_t spot_publish_no_sequence_check (void *spot_,
                                                      const char *topic_id_,
                                                      zlink_msg_t *parts_,
                                                      size_t part_count_,
                                                      zlink_send_flags_t flags_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return ZLINK_SUBMIT_INVALID_HANDLE;
    if (zlink::spot_node_access_t::is_shutting_down (spot->node)) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_TERMINATED;
    }
    return zlink::submit_result_internal::from_rc (
      spot_subject_publish (spot_, topic_id_, parts_, part_count_, flags_));
}

zlink_submit_result_t spot_publish_impl (void *spot_,
                                         const char *topic_id_,
                                         zlink_msg_t *parts_,
                                         size_t part_count_,
                                         zlink_send_flags_t flags_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    return spot_publish_no_sequence_check (spot_, topic_id_, parts_, part_count_, flags_);
}

zlink_submit_result_t zlink_spot_publish_part (void *spot_,
                                               const char *topic_id_,
                                               zlink_msg_t *part_,
                                               zlink_send_flags_t flags_,
                                               zlink_part_flag_t part_flag_)
{
    if (zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || zlink::part_helper_internal::validate_send_flags (flags_) != 0) {
        return zlink::submit_result_internal::from_errno (errno);
    }

    if (part_flag_ == ZLINK_PART_FINAL && !send_sequence_active (spot_))
        return spot_publish_no_sequence_check (spot_, topic_id_, part_, 1u, flags_);

    zlink::part_helper_internal::send_sequence_spec_t spec;
    spec.family = zlink::part_helper_internal::send_family_spot_publish;
    spec.flags = flags_;
    spec.has_text1 = topic_id_ != NULL;
    spec.text1 = topic_id_ ? topic_id_ : "";

    std::shared_ptr<zlink::part_helper_internal::handle_state_t> state;
    bool first_part = false;
    if (zlink::part_helper_internal::prepare_staged_send_step (spot_, spec, &state, &first_part)
        != 0) {
        return zlink::submit_result_internal::from_errno (errno);
    }

    if (part_flag_ == ZLINK_PART_MORE) {
        if (zlink::part_helper_internal::stage_staged_send_part (state.get (), part_) != 0) {
            const int saved_errno = errno;
            zlink::part_helper_internal::abort_send_step (state);
            zlink::part_helper_internal::consume_send_part (part_);
            errno = saved_errno;
            return zlink::submit_result_internal::from_errno (saved_errno);
        }
        return ZLINK_SUBMIT_OK;
    }

    std::vector<zlink_msg_t> parts;
    if (zlink::part_helper_internal::move_staged_parts_for_submit (state, part_, &parts) != 0) {
        const int saved_errno = errno;
        zlink::part_helper_internal::abort_send_step (state);
        zlink_multipart_close (parts.data (), parts.size ());
        zlink::part_helper_internal::consume_send_part (part_);
        errno = saved_errno;
        return zlink::submit_result_internal::from_errno (saved_errno);
    }

    zlink::part_helper_internal::complete_send_step (state, part_flag_);
    return spot_publish_impl (spot_, topic_id_, parts.data (), parts.size (), flags_);
}

zlink_recv_result_t spot_subscribe_impl (void *spot_,
                                         zlink_routing_id_t *source_rid_out_,
                                         zlink_msg_t **parts_out_,
                                         size_t *part_count_out_,
                                         char *topic_id_out_,
                                         size_t *topic_id_len_out_,
                                         zlink_recv_flags_t flags_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return ZLINK_RECV_INVALID_HANDLE;

    const zlink_recv_result_t logical_recv = try_dequeue_logical_subscribe (
      spot, source_rid_out_, parts_out_, part_count_out_, topic_id_out_, topic_id_len_out_);
    if (logical_recv != ZLINK_RECV_NO_DATA)
        return logical_recv;
    if (spot->logical_state) {
        errno = EAGAIN;
        return ZLINK_RECV_NO_DATA;
    }

    const zlink_recv_result_t dispatch_recv = zlink::recv_result_internal::from_rc (
      spot_dispatch_subscribe_recv_internal (spot_, source_rid_out_, parts_out_, part_count_out_,
                                             topic_id_out_, topic_id_len_out_, flags_));
    if (dispatch_recv != ZLINK_RECV_NO_DATA)
        return dispatch_recv;

    const zlink_recv_result_t service_recv =
      zlink::recv_result_internal::from_rc (zlink::spot_node_access_t::service_subscribe_recv (
        spot->node, source_rid_out_, parts_out_, part_count_out_, topic_id_out_, topic_id_len_out_,
        flags_));
    if (service_recv == ZLINK_RECV_OK)
        return service_recv;
    if (service_recv != ZLINK_RECV_NO_DATA)
        return service_recv;

    const zlink_recv_result_t recv_rc = zlink::recv_result_internal::from_rc (
      spot_subject_recv (spot_, source_rid_out_, parts_out_, part_count_out_, topic_id_out_,
                         topic_id_len_out_, flags_));
    if (recv_rc != ZLINK_RECV_OK)
        return recv_rc;

    return ZLINK_RECV_OK;
}

zlink_recv_result_t zlink_spot_subscribe_part (void *spot_,
                                               const zlink_routing_id_t **source_rid_out_,
                                               char *topic_id_buf_,
                                               size_t topic_id_capacity_,
                                               size_t *topic_id_len_out_,
                                               zlink_msg_t *part_out_,
                                               zlink_part_flag_t *has_more_out_,
                                               zlink_recv_flags_t flags_)
{
    if (!spot_ || !topic_id_len_out_ || !part_out_ || !has_more_out_) {
        errno = EFAULT;
        return zlink::recv_result_internal::from_errno (errno);
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::recv_result_internal::from_errno (errno);

    std::shared_ptr<zlink::part_helper_internal::handle_state_t> helper_state =
      zlink::part_helper_internal::find_or_create_handle_state (spot_);
    if (!helper_state)
        return zlink::recv_result_internal::from_errno (errno);

    bool first_part = false;
    zlink::socket_base_t *source_socket = NULL;
    if (zlink::part_helper_internal::prepare_recv_step (
          spot_, zlink::part_helper_internal::recv_family_spot_subscribe, source_socket,
          &helper_state, &first_part, &source_socket)
        != 0) {
        return zlink::recv_result_internal::from_errno (errno);
    }

    if (first_part) {
        zlink_routing_id_t source_rid;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        const bool use_caller_topic_buf = topic_id_buf_ != NULL && topic_id_capacity_ > 0;
        size_t topic_id_len = use_caller_topic_buf ? topic_id_capacity_ : 65536;
        std::string topic_id;
        std::vector<char> topic_id_storage;
        char *recv_topic_buf = topic_id_buf_;
        if (!use_caller_topic_buf) {
            topic_id_storage.resize (topic_id_len);
            recv_topic_buf = topic_id_storage.data ();
        }
        memset (&source_rid, 0, sizeof (source_rid));

        const zlink_recv_result_t rc = spot_subscribe_impl (spot_, &source_rid, &parts, &part_count,
                                                            recv_topic_buf, &topic_id_len, flags_);
        if (rc != ZLINK_RECV_OK) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            return rc;
        }

        topic_id.assign (recv_topic_buf, topic_id_len);

        bool move_failed = false;
        {
            std::lock_guard<std::mutex> lock (helper_state->mutex);
            helper_state->recv.source_node_rid = source_rid;
            helper_state->recv.return_source_rid_as_null =
              helper_state->recv.source_node_rid.size == 0;
            helper_state->recv.topic_id.swap (topic_id);
            helper_state->recv.buffered_parts.resize (part_count);
            helper_state->recv.next_part_index = 0;
            for (size_t i = 0; i < part_count; ++i) {
                zlink_msg_init (&helper_state->recv.buffered_parts[i]);
                if (zlink_msg_move (&helper_state->recv.buffered_parts[i], &parts[i]) != 0) {
                    move_failed = true;
                    break;
                }
            }
        }
        zlink_multipart_close (parts, part_count);
        if (move_failed) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EFAULT;
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (helper_state->recv.buffered_parts.empty ()) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EPROTO;
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (zlink_msg_move (part_out_, &helper_state->recv.buffered_parts[0]) != 0) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EFAULT;
            return zlink::recv_result_internal::from_errno (errno);
        }
        helper_state->recv.next_part_index = 1;
    } else {
        bool range_failed = false;
        bool move_failed = false;
        {
            std::lock_guard<std::mutex> lock (helper_state->mutex);
            if (helper_state->recv.next_part_index >= helper_state->recv.buffered_parts.size ()) {
                range_failed = true;
            } else {
                move_failed =
                  zlink_msg_move (
                    part_out_,
                    &helper_state->recv.buffered_parts[helper_state->recv.next_part_index])
                  != 0;
                if (!move_failed)
                    ++helper_state->recv.next_part_index;
            }
        }
        if (range_failed) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EPROTO;
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (move_failed) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EFAULT;
            return zlink::recv_result_internal::from_errno (errno);
        }
    }

    int copy_errno = 0;
    {
        std::lock_guard<std::mutex> lock (helper_state->mutex);
        *topic_id_len_out_ = helper_state->recv.topic_id.size ();
        if (topic_id_capacity_ > 0) {
            if (!topic_id_buf_) {
                copy_errno = EFAULT;
            } else if (topic_id_capacity_ < helper_state->recv.topic_id.size ()) {
                copy_errno = EMSGSIZE;
            } else if (!helper_state->recv.topic_id.empty ()) {
                memcpy (topic_id_buf_, helper_state->recv.topic_id.data (),
                        helper_state->recv.topic_id.size ());
            }
        }

        if (source_rid_out_) {
            *source_rid_out_ = helper_state->recv.return_source_rid_as_null
                                 ? NULL
                                 : &helper_state->recv.source_node_rid;
        }
        *has_more_out_ =
          (helper_state->recv.next_part_index < helper_state->recv.buffered_parts.size ())
            ? ZLINK_PART_MORE
            : ZLINK_PART_FINAL;
    }

    zlink::part_helper_internal::complete_recv_step (helper_state, *has_more_out_);
    if (copy_errno != 0) {
        errno = copy_errno;
        return zlink::recv_result_internal::from_errno (errno);
    }
    return ZLINK_RECV_OK;
}

zlink_recv_result_t zlink_spot_recv_subscription_event (void *spot_,
                                                        const zlink_routing_id_t **source_rid_out_,
                                                        int *subscribed_out_,
                                                        char *topic_id_buf_,
                                                        size_t topic_id_capacity_,
                                                        size_t *topic_id_len_out_,
                                                        zlink_recv_flags_t flags_)
{
    if (!spot_ || !subscribed_out_ || !topic_id_len_out_) {
        errno = EFAULT;
        return zlink::recv_result_internal::from_errno (errno);
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::recv_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return ZLINK_RECV_INVALID_HANDLE;

    *topic_id_len_out_ = topic_id_capacity_;

    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));

    const zlink_recv_result_t service_rc = zlink::recv_result_internal::from_rc (
      zlink::spot_node_access_t::service_subscription_event_recv (
        spot->node, &source_rid, subscribed_out_, topic_id_buf_, topic_id_len_out_, flags_));
    if (service_rc == ZLINK_RECV_OK) {
        if (source_rid_out_) {
            zlink_routing_id_t &stored_source_rid = spot_service_event_source_rid_tls ();
            stored_source_rid = source_rid;
            *source_rid_out_ = &stored_source_rid;
        }
        return ZLINK_RECV_OK;
    }
    return service_rc;
}
