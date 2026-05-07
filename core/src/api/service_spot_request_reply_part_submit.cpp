/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "api/part_helper_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/submit_result_internal.hpp"

zlink_submit_result_t spot_send_channel_impl (void *spot_,
                                              const char *channel_name_,
                                              zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_send_flags_t flags_);
zlink_submit_result_t spot_request_spot_impl (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);
zlink_submit_result_t spot_request_router_impl (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);
zlink_submit_result_t spot_request_channel_impl (
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);
zlink_submit_result_t spot_send_spot_impl (void *spot_,
                                           const zlink_routing_id_t *dest_node_rid_,
                                           const zlink_routing_id_t *dest_spot_rid_,
                                           zlink_msg_t *parts_,
                                           size_t part_count_,
                                           zlink_send_flags_t flags_);
zlink_submit_result_t spot_reply_spot_impl (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_);
zlink_submit_result_t spot_reply_router_impl (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_);
zlink_submit_result_t router_request_spot_impl (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);
zlink_submit_result_t router_reply_spot_impl (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_);
zlink_submit_result_t router_send_spot_impl (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);

namespace
{
using zlink::part_helper_internal::has_valid_routing_id;

int validate_request_send_flags (zlink_send_flags_t flags_)
{
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

int prepare_staged_send_step (
  void *handle_,
  const zlink::part_helper_internal::send_sequence_spec_t &spec_,
  std::shared_ptr<zlink::part_helper_internal::handle_state_t> *state_out_,
  bool *first_part_out_)
{
    if (!state_out_ || !first_part_out_) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<zlink::part_helper_internal::handle_state_t> state =
      zlink::part_helper_internal::find_or_create_handle_state (handle_);
    if (!state)
        return -1;

    std::unique_lock<std::mutex> lock (state->mutex);
    const std::thread::id current_thread = std::this_thread::get_id ();
    while (state->send.active && state->send.owner_thread != current_thread) {
        if (!zlink::part_helper_internal::aggregate_send_mode_active ()) {
            errno = EINVAL;
            return -1;
        }
        state->cv.wait (lock);
    }

    if (!state->send.active) {
        state->send.active = true;
        state->send.spec = spec_;
        state->send.sink_socket = NULL;
        state->send.owner_thread = current_thread;
        *first_part_out_ = true;
    } else {
        if (!zlink::part_helper_internal::send_spec_equals (
              state->send.spec, spec_)) {
            errno = EINVAL;
            return -1;
        }
        *first_part_out_ = false;
    }

    *state_out_ = state;
    return 0;
}

int stage_staged_send_part (
  zlink::part_helper_internal::handle_state_t *state_,
  zlink_msg_t *part_)
{
    if (!state_ || !part_) {
        errno = EFAULT;
        return -1;
    }

    state_->send.buffered_parts.resize (state_->send.buffered_parts.size () + 1);
    zlink_msg_t &slot = state_->send.buffered_parts.back ();
    zlink_msg_init (&slot);
    if (zlink_msg_move (&slot, part_) != 0) {
        zlink_msg_close (&slot);
        state_->send.buffered_parts.pop_back ();
        errno = EFAULT;
        return -1;
    }

    return 0;
}

int move_staged_parts_for_submit (
  const std::shared_ptr<zlink::part_helper_internal::handle_state_t> &state_,
  zlink_msg_t *part_,
  std::vector<zlink_msg_t> *parts_out_)
{
    if (!state_ || !part_ || !parts_out_) {
        errno = EFAULT;
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        parts_out_->swap (state_->send.buffered_parts);
    }

    parts_out_->resize (parts_out_->size () + 1);
    zlink_msg_t &slot = parts_out_->back ();
    zlink_msg_init (&slot);
    if (zlink_msg_move (&slot, part_) != 0) {
        zlink_msg_close (&slot);
        parts_out_->pop_back ();
        errno = EFAULT;
        return -1;
    }

    return 0;
}

template <typename FinalSubmit>
zlink_submit_result_t finalize_staged_spot_submit (
  const std::shared_ptr<zlink::part_helper_internal::handle_state_t> &state_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_,
  FinalSubmit final_submit_)
{
    if (part_flag_ == ZLINK_PART_MORE) {
        if (stage_staged_send_part (state_.get (), part_) != 0) {
            const int saved_errno = errno;
            zlink::part_helper_internal::abort_send_step (state_);
            zlink::part_helper_internal::consume_send_part (part_);
            errno = saved_errno;
            return zlink::submit_result_internal::from_errno (saved_errno);
        }
        return ZLINK_SUBMIT_OK;
    }

    std::vector<zlink_msg_t> parts;
    if (move_staged_parts_for_submit (state_, part_, &parts) != 0) {
        const int saved_errno = errno;
        zlink::part_helper_internal::abort_send_step (state_);
        zlink_multipart_close (parts.data (), parts.size ());
        zlink::part_helper_internal::consume_send_part (part_);
        errno = saved_errno;
        return zlink::submit_result_internal::from_errno (saved_errno);
    }

    zlink::part_helper_internal::complete_send_step (state_, part_flag_);
    return final_submit_ (parts);
}

template <typename FinalSubmit>
zlink_submit_result_t submit_staged_sequence (
  void *handle_,
  const zlink::part_helper_internal::send_sequence_spec_t &spec_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_,
  FinalSubmit final_submit_)
{
    std::shared_ptr<zlink::part_helper_internal::handle_state_t> state;
    bool first_part = false;
    if (prepare_staged_send_step (handle_, spec_, &state, &first_part) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    return finalize_staged_spot_submit (
      state, part_, part_flag_, final_submit_);
}
}

zlink_submit_result_t zlink_spot_send_channel_part (
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_)
{
    if (!channel_name_ || channel_name_[0] == '\0') {
        errno = EINVAL;
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }
    if (zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    zlink::part_helper_internal::send_sequence_spec_t spec;
    spec.family = zlink::part_helper_internal::send_family_spot_send_channel;
    spec.flags = flags_;
    spec.has_text1 = true;
    spec.text1 = channel_name_;

    return submit_staged_sequence (
      spot_, spec, part_, part_flag_,
      [spot_, channel_name_, flags_] (std::vector<zlink_msg_t> &parts_) {
          return spot_send_channel_impl (spot_, channel_name_, parts_.data (),
                                         parts_.size (), flags_);
      });
}

zlink_submit_result_t zlink_spot_request_spot_part (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_)
{
    if (!handler_ || !has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_)
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    zlink::part_helper_internal::send_sequence_spec_t spec;
    spec.family = zlink::part_helper_internal::send_family_spot_request_spot;
    spec.flags = flags_;
    spec.timeout_ms = timeout_ms_;
    spec.handler = handler_;
    spec.userdata = userdata_;
    spec.request_like = true;
    spec.has_rid1 = true;
    spec.has_rid2 = true;
    zlink::part_helper_internal::copy_routing_id (dest_node_rid_, &spec.rid1);
    zlink::part_helper_internal::copy_routing_id (dest_spot_rid_, &spec.rid2);

    return submit_staged_sequence (
      spot_, spec, part_, part_flag_,
      [spot_, dest_node_rid_, dest_spot_rid_, handler_, userdata_, flags_,
       timeout_ms_] (std::vector<zlink_msg_t> &parts_) {
          return spot_request_spot_impl (
            spot_, dest_node_rid_, dest_spot_rid_, parts_.data (),
            parts_.size (), handler_, userdata_, flags_, timeout_ms_);
      });
}

zlink_submit_result_t zlink_spot_request_router_part (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_)
{
    if (!handler_ || !has_valid_routing_id (peer_rid_)
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    zlink::part_helper_internal::send_sequence_spec_t spec;
    spec.family = zlink::part_helper_internal::send_family_spot_request_router;
    spec.flags = flags_;
    spec.timeout_ms = timeout_ms_;
    spec.handler = handler_;
    spec.userdata = userdata_;
    spec.request_like = true;
    spec.has_rid1 = true;
    zlink::part_helper_internal::copy_routing_id (peer_rid_, &spec.rid1);

    return submit_staged_sequence (
      spot_, spec, part_, part_flag_,
      [spot_, peer_rid_, handler_, userdata_, flags_,
       timeout_ms_] (std::vector<zlink_msg_t> &parts_) {
          return spot_request_router_impl (spot_, peer_rid_, parts_.data (),
                                           parts_.size (), handler_, userdata_,
                                           flags_, timeout_ms_);
      });
}

zlink_submit_result_t zlink_spot_request_channel_part (
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_)
{
    if (!handler_ || !channel_name_ || channel_name_[0] == '\0'
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    zlink::part_helper_internal::send_sequence_spec_t spec;
    spec.family = zlink::part_helper_internal::send_family_spot_request_channel;
    spec.flags = flags_;
    spec.timeout_ms = timeout_ms_;
    spec.handler = handler_;
    spec.userdata = userdata_;
    spec.request_like = true;
    spec.has_text1 = true;
    spec.text1 = channel_name_;

    return submit_staged_sequence (
      spot_, spec, part_, part_flag_,
      [spot_, channel_name_, handler_, userdata_, flags_,
       timeout_ms_] (std::vector<zlink_msg_t> &parts_) {
          return spot_request_channel_impl (spot_, channel_name_, parts_.data (),
                                            parts_.size (), handler_, userdata_,
                                            flags_, timeout_ms_);
      });
}

zlink_submit_result_t zlink_spot_send_spot_part (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_)
{
    if (!has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_)
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    zlink::part_helper_internal::send_sequence_spec_t spec;
    spec.family = zlink::part_helper_internal::send_family_spot_send_spot;
    spec.flags = flags_;
    spec.has_rid1 = true;
    spec.has_rid2 = true;
    zlink::part_helper_internal::copy_routing_id (dest_node_rid_, &spec.rid1);
    zlink::part_helper_internal::copy_routing_id (dest_spot_rid_, &spec.rid2);

    return submit_staged_sequence (
      spot_, spec, part_, part_flag_,
      [spot_, dest_node_rid_, dest_spot_rid_, flags_] (
        std::vector<zlink_msg_t> &parts_) {
          return spot_send_spot_impl (spot_, dest_node_rid_, dest_spot_rid_,
                                      parts_.data (), parts_.size (), flags_);
      });
}

zlink_submit_result_t zlink_spot_reply_spot_part (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_)
{
    if (!has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_) || request_seq_ == 0
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    zlink::part_helper_internal::send_sequence_spec_t spec;
    spec.family = zlink::part_helper_internal::send_family_spot_reply_spot;
    spec.request_like = true;
    spec.request_seq = request_seq_;
    spec.has_rid1 = true;
    spec.has_rid2 = true;
    zlink::part_helper_internal::copy_routing_id (dest_node_rid_, &spec.rid1);
    zlink::part_helper_internal::copy_routing_id (dest_spot_rid_, &spec.rid2);

    return submit_staged_sequence (
      spot_, spec, part_, part_flag_,
      [spot_, dest_node_rid_, dest_spot_rid_, request_seq_] (
        std::vector<zlink_msg_t> &parts_) {
          return spot_reply_spot_impl (
            spot_, dest_node_rid_, dest_spot_rid_, request_seq_, parts_.data (),
            parts_.size ());
      });
}

zlink_submit_result_t zlink_spot_reply_router_part (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_)
{
    if (!has_valid_routing_id (peer_rid_) || request_seq_ == 0
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    zlink::part_helper_internal::send_sequence_spec_t spec;
    spec.family = zlink::part_helper_internal::send_family_spot_reply_router;
    spec.request_like = true;
    spec.request_seq = request_seq_;
    spec.has_rid1 = true;
    zlink::part_helper_internal::copy_routing_id (peer_rid_, &spec.rid1);

    return submit_staged_sequence (
      spot_, spec, part_, part_flag_,
      [spot_, peer_rid_, request_seq_] (std::vector<zlink_msg_t> &parts_) {
          return spot_reply_router_impl (spot_, peer_rid_, request_seq_,
                                         parts_.data (), parts_.size ());
      });
}

zlink_submit_result_t zlink_router_request_spot_part (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_)
{
    if (!handler_ || !has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_)
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    zlink::part_helper_internal::send_sequence_spec_t spec;
    spec.family = zlink::part_helper_internal::send_family_router_request_spot;
    spec.flags = flags_;
    spec.timeout_ms = timeout_ms_;
    spec.handler = handler_;
    spec.userdata = userdata_;
    spec.request_like = true;
    spec.has_rid1 = true;
    spec.has_rid2 = true;
    zlink::part_helper_internal::copy_routing_id (dest_node_rid_, &spec.rid1);
    zlink::part_helper_internal::copy_routing_id (dest_spot_rid_, &spec.rid2);

    return submit_staged_sequence (
      router_, spec, part_, part_flag_,
      [router_, dest_node_rid_, dest_spot_rid_, handler_, userdata_, flags_,
       timeout_ms_] (std::vector<zlink_msg_t> &parts_) {
          return router_request_spot_impl (
            router_, dest_node_rid_, dest_spot_rid_, parts_.data (),
            parts_.size (), handler_, userdata_, flags_, timeout_ms_);
      });
}

zlink_submit_result_t zlink_router_reply_spot_part (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_)
{
    if (!has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_) || request_seq_ == 0
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    zlink::part_helper_internal::send_sequence_spec_t spec;
    spec.family = zlink::part_helper_internal::send_family_router_reply_spot;
    spec.request_like = true;
    spec.request_seq = request_seq_;
    spec.has_rid1 = true;
    spec.has_rid2 = true;
    zlink::part_helper_internal::copy_routing_id (dest_node_rid_, &spec.rid1);
    zlink::part_helper_internal::copy_routing_id (dest_spot_rid_, &spec.rid2);

    return submit_staged_sequence (
      router_, spec, part_, part_flag_,
      [router_, dest_node_rid_, dest_spot_rid_, request_seq_] (
        std::vector<zlink_msg_t> &parts_) {
          return router_reply_spot_impl (
            router_, dest_node_rid_, dest_spot_rid_, request_seq_, parts_.data (),
            parts_.size ());
      });
}

zlink_submit_result_t zlink_router_send_spot_part (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_)
{
    if (!has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_)
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    zlink::part_helper_internal::send_sequence_spec_t spec;
    spec.family = zlink::part_helper_internal::send_family_router_send_spot;
    spec.flags = flags_;
    spec.has_rid1 = true;
    spec.has_rid2 = true;
    zlink::part_helper_internal::copy_routing_id (dest_node_rid_, &spec.rid1);
    zlink::part_helper_internal::copy_routing_id (dest_spot_rid_, &spec.rid2);

    return submit_staged_sequence (
      router_, spec, part_, part_flag_,
      [router_, dest_node_rid_, dest_spot_rid_, flags_] (
        std::vector<zlink_msg_t> &parts_) {
          return router_send_spot_impl (router_, dest_node_rid_, dest_spot_rid_,
                                        parts_.data (), parts_.size (), flags_);
      });
}
