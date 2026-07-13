/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "api/socket/part_helper_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "api/message/submit_result_internal.hpp"
#include "utils/routing_id.hpp"

zlink_submit_result_t spot_send_channel_impl (void *spot_,
                                              const char *channel_name_,
                                              zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_send_flags_t flags_);
zlink_submit_result_t spot_request_spot_impl (void *spot_,
                                              const zlink_routing_id_t *dest_node_rid_,
                                              const zlink_routing_id_t *dest_spot_rid_,
                                              zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_reply_handler_fn handler_,
                                              void *userdata_,
                                              zlink_send_flags_t flags_,
                                              uint32_t timeout_ms_);
zlink_submit_result_t spot_request_router_impl (void *spot_,
                                                const zlink_routing_id_t *peer_rid_,
                                                zlink_msg_t *parts_,
                                                size_t part_count_,
                                                zlink_reply_handler_fn handler_,
                                                void *userdata_,
                                                zlink_send_flags_t flags_,
                                                uint32_t timeout_ms_);
zlink_submit_result_t spot_request_channel_impl (void *spot_,
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
zlink_submit_result_t spot_reply_spot_impl (void *spot_,
                                            const zlink_routing_id_t *dest_node_rid_,
                                            const zlink_routing_id_t *dest_spot_rid_,
                                            uint64_t request_seq_,
                                            zlink_msg_t *parts_,
                                            size_t part_count_);
zlink_submit_result_t spot_reply_router_impl (void *spot_,
                                              const zlink_routing_id_t *peer_rid_,
                                              uint64_t request_seq_,
                                              zlink_msg_t *parts_,
                                              size_t part_count_);
zlink_submit_result_t router_request_spot_impl (void *router_,
                                                const zlink_routing_id_t *dest_node_rid_,
                                                const zlink_routing_id_t *dest_spot_rid_,
                                                zlink_msg_t *parts_,
                                                size_t part_count_,
                                                zlink_reply_handler_fn handler_,
                                                void *userdata_,
                                                zlink_send_flags_t flags_,
                                                uint32_t timeout_ms_);
zlink_submit_result_t router_reply_spot_impl (void *router_,
                                              const zlink_routing_id_t *dest_node_rid_,
                                              const zlink_routing_id_t *dest_spot_rid_,
                                              uint64_t request_seq_,
                                              zlink_msg_t *parts_,
                                              size_t part_count_);
zlink_submit_result_t router_send_spot_impl (void *router_,
                                             const zlink_routing_id_t *dest_node_rid_,
                                             const zlink_routing_id_t *dest_spot_rid_,
                                             zlink_msg_t *parts_,
                                             size_t part_count_,
                                             zlink_send_flags_t flags_);

namespace
{
using zlink::part_helper_internal::send_sequence_spec_t;

int validate_request_send_flags (zlink_send_flags_t flags_)
{
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

int validate_request_part_handler (zlink_part_flag_t part_flag_,
                                   zlink_reply_handler_fn handler_)
{
    if ((part_flag_ == ZLINK_PART_FINAL) != (handler_ != NULL)) {
        errno = EINVAL;
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
        if (zlink::part_helper_internal::stage_staged_send_part (state_.get (), part_) != 0) {
            const int saved_errno = errno;
            zlink::part_helper_internal::abort_send_step (state_);
            zlink::part_helper_internal::consume_send_part (part_);
            errno = saved_errno;
            return zlink::submit_result_internal::from_errno (saved_errno);
        }
        return ZLINK_SUBMIT_OK;
    }

    std::vector<zlink_msg_t> parts;
    if (zlink::part_helper_internal::move_staged_parts_for_submit (state_, part_, &parts) != 0) {
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
zlink_submit_result_t
submit_staged_sequence (void *handle_,
                        const zlink::part_helper_internal::send_sequence_spec_t &spec_,
                        zlink_msg_t *part_,
                        zlink_part_flag_t part_flag_,
                        FinalSubmit final_submit_)
{
    std::shared_ptr<zlink::part_helper_internal::handle_state_t> state;
    bool first_part = false;
    if (zlink::part_helper_internal::prepare_staged_send_step (handle_, spec_, &state, &first_part)
        != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    return finalize_staged_spot_submit (state, part_, part_flag_, final_submit_);
}

send_sequence_spec_t make_send_spec (zlink::part_helper_internal::send_family_t family_,
                                     zlink_send_flags_t flags_)
{
    send_sequence_spec_t spec;
    spec.family = family_;
    spec.flags = flags_;
    return spec;
}

send_sequence_spec_t make_request_spec (zlink::part_helper_internal::send_family_t family_,
                                        zlink_send_flags_t flags_,
                                        uint32_t timeout_ms_,
                                        zlink_reply_handler_fn handler_,
                                        void *userdata_)
{
    send_sequence_spec_t spec = make_send_spec (family_, flags_);
    spec.timeout_ms = timeout_ms_;
    spec.handler = handler_;
    spec.userdata = userdata_;
    spec.request_like = true;
    return spec;
}

send_sequence_spec_t make_reply_spec (zlink::part_helper_internal::send_family_t family_,
                                      uint64_t request_seq_)
{
    send_sequence_spec_t spec = make_send_spec (family_, ZLINK_SEND_FLAGS_NONE);
    spec.request_like = true;
    spec.request_seq = request_seq_;
    return spec;
}

void set_text1 (send_sequence_spec_t *spec_, const char *text_)
{
    spec_->has_text1 = true;
    spec_->text1 = text_;
}

void set_rid1 (send_sequence_spec_t *spec_, const zlink_routing_id_t *rid_)
{
    spec_->has_rid1 = true;
    zlink::part_helper_internal::copy_routing_id (rid_, &spec_->rid1);
}

void set_rid_pair (send_sequence_spec_t *spec_,
                   const zlink_routing_id_t *rid1_,
                   const zlink_routing_id_t *rid2_)
{
    set_rid1 (spec_, rid1_);
    spec_->has_rid2 = true;
    zlink::part_helper_internal::copy_routing_id (rid2_, &spec_->rid2);
}
}

zlink_submit_result_t zlink_spot_send_channel_part (void *spot_,
                                                    const char *channel_name_,
                                                    zlink_msg_t *part_,
                                                    zlink_send_flags_t flags_,
                                                    zlink_part_flag_t part_flag_)
{
    if (!channel_name_ || channel_name_[0] == '\0') {
        errno = EINVAL;
        return zlink::submit_result_internal::from_errno (errno);
    }
    if (zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_send_flags (flags_) != 0) {
        return zlink::submit_result_internal::from_errno (errno);
    }

    if (part_flag_ == ZLINK_PART_FINAL
        && !zlink::part_helper_internal::send_sequence_active (spot_)) {
        return spot_send_channel_impl (spot_, channel_name_, part_, 1u, flags_);
    }

    send_sequence_spec_t spec =
      make_send_spec (zlink::part_helper_internal::send_family_spot_send_channel, flags_);
    set_text1 (&spec, channel_name_);

    return submit_staged_sequence (
      spot_, spec, part_, part_flag_,
      [spot_, channel_name_, flags_] (std::vector<zlink_msg_t> &parts_) {
          return spot_send_channel_impl (spot_, channel_name_, parts_.data (), parts_.size (),
                                         flags_);
      });
}

zlink_submit_result_t zlink_spot_request_spot_part (void *spot_,
                                                    const zlink_routing_id_t *dest_node_rid_,
                                                    const zlink_routing_id_t *dest_spot_rid_,
                                                    zlink_msg_t *part_,
                                                    zlink_reply_handler_fn handler_,
                                                    void *userdata_,
                                                    zlink_send_flags_t flags_,
                                                    zlink_part_flag_t part_flag_,
                                                    uint32_t timeout_ms_)
{
    if (!zlink::valid_routing_id (dest_node_rid_)
        || !zlink::valid_routing_id (dest_spot_rid_)
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_part_handler (part_flag_, handler_) != 0
        || validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    send_sequence_spec_t spec =
      make_request_spec (zlink::part_helper_internal::send_family_spot_request_spot, flags_,
                         timeout_ms_, handler_, userdata_);
    set_rid_pair (&spec, dest_node_rid_, dest_spot_rid_);

    return submit_staged_sequence (spot_, spec, part_, part_flag_,
                                   [spot_, dest_node_rid_, dest_spot_rid_, handler_, userdata_,
                                    flags_, timeout_ms_] (std::vector<zlink_msg_t> &parts_) {
                                       return spot_request_spot_impl (
                                         spot_, dest_node_rid_, dest_spot_rid_, parts_.data (),
                                         parts_.size (), handler_, userdata_, flags_, timeout_ms_);
                                   });
}

zlink_submit_result_t zlink_spot_request_router_part (void *spot_,
                                                      const zlink_routing_id_t *peer_rid_,
                                                      zlink_msg_t *part_,
                                                      zlink_reply_handler_fn handler_,
                                                      void *userdata_,
                                                      zlink_send_flags_t flags_,
                                                      zlink_part_flag_t part_flag_,
                                                      uint32_t timeout_ms_)
{
    if (!zlink::valid_routing_id (peer_rid_)
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_part_handler (part_flag_, handler_) != 0
        || validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    send_sequence_spec_t spec =
      make_request_spec (zlink::part_helper_internal::send_family_spot_request_router, flags_,
                         timeout_ms_, handler_, userdata_);
    set_rid1 (&spec, peer_rid_);

    return submit_staged_sequence (spot_, spec, part_, part_flag_,
                                   [spot_, peer_rid_, handler_, userdata_, flags_,
                                    timeout_ms_] (std::vector<zlink_msg_t> &parts_) {
                                       return spot_request_router_impl (
                                         spot_, peer_rid_, parts_.data (), parts_.size (), handler_,
                                         userdata_, flags_, timeout_ms_);
                                   });
}

zlink_submit_result_t zlink_spot_request_channel_part (void *spot_,
                                                       const char *channel_name_,
                                                       zlink_msg_t *part_,
                                                       zlink_reply_handler_fn handler_,
                                                       void *userdata_,
                                                       zlink_send_flags_t flags_,
                                                       zlink_part_flag_t part_flag_,
                                                       uint32_t timeout_ms_)
{
    if (!channel_name_ || channel_name_[0] == '\0'
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_part_handler (part_flag_, handler_) != 0
        || validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    send_sequence_spec_t spec =
      make_request_spec (zlink::part_helper_internal::send_family_spot_request_channel, flags_,
                         timeout_ms_, handler_, userdata_);
    set_text1 (&spec, channel_name_);

    return submit_staged_sequence (spot_, spec, part_, part_flag_,
                                   [spot_, channel_name_, handler_, userdata_, flags_,
                                    timeout_ms_] (std::vector<zlink_msg_t> &parts_) {
                                       return spot_request_channel_impl (
                                         spot_, channel_name_, parts_.data (), parts_.size (),
                                         handler_, userdata_, flags_, timeout_ms_);
                                   });
}

zlink_submit_result_t zlink_spot_send_spot_part (void *spot_,
                                                 const zlink_routing_id_t *dest_node_rid_,
                                                 const zlink_routing_id_t *dest_spot_rid_,
                                                 zlink_msg_t *part_,
                                                 zlink_send_flags_t flags_,
                                                 zlink_part_flag_t part_flag_)
{
    if (!zlink::valid_routing_id (dest_node_rid_) || !zlink::valid_routing_id (dest_spot_rid_)
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    if (part_flag_ == ZLINK_PART_FINAL
        && !zlink::part_helper_internal::send_sequence_active (spot_)) {
        zlink_msg_t *parts = part_;
        const zlink_submit_result_t result =
          spot_send_spot_impl (spot_, dest_node_rid_, dest_spot_rid_, parts, 1u, flags_);
        if (result != ZLINK_SUBMIT_OK)
            zlink::part_helper_internal::consume_send_part (part_);
        return result;
    }

    send_sequence_spec_t spec =
      make_send_spec (zlink::part_helper_internal::send_family_spot_send_spot, flags_);
    set_rid_pair (&spec, dest_node_rid_, dest_spot_rid_);

    return submit_staged_sequence (
      spot_, spec, part_, part_flag_,
      [spot_, dest_node_rid_, dest_spot_rid_, flags_] (std::vector<zlink_msg_t> &parts_) {
          return spot_send_spot_impl (spot_, dest_node_rid_, dest_spot_rid_, parts_.data (),
                                      parts_.size (), flags_);
      });
}

zlink_submit_result_t zlink_spot_reply_spot_part (void *spot_,
                                                  const zlink_routing_id_t *dest_node_rid_,
                                                  const zlink_routing_id_t *dest_spot_rid_,
                                                  uint64_t request_seq_,
                                                  zlink_msg_t *part_,
                                                  zlink_part_flag_t part_flag_)
{
    if (!zlink::valid_routing_id (dest_node_rid_) || !zlink::valid_routing_id (dest_spot_rid_)
        || request_seq_ == 0 || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    send_sequence_spec_t spec =
      make_reply_spec (zlink::part_helper_internal::send_family_spot_reply_spot, request_seq_);
    set_rid_pair (&spec, dest_node_rid_, dest_spot_rid_);

    return submit_staged_sequence (
      spot_, spec, part_, part_flag_,
      [spot_, dest_node_rid_, dest_spot_rid_, request_seq_] (std::vector<zlink_msg_t> &parts_) {
          return spot_reply_spot_impl (spot_, dest_node_rid_, dest_spot_rid_, request_seq_,
                                       parts_.data (), parts_.size ());
      });
}

zlink_submit_result_t zlink_spot_reply_router_part (void *spot_,
                                                    const zlink_routing_id_t *peer_rid_,
                                                    uint64_t request_seq_,
                                                    zlink_msg_t *part_,
                                                    zlink_part_flag_t part_flag_)
{
    if (!zlink::valid_routing_id (peer_rid_) || request_seq_ == 0
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    send_sequence_spec_t spec =
      make_reply_spec (zlink::part_helper_internal::send_family_spot_reply_router, request_seq_);
    set_rid1 (&spec, peer_rid_);

    return submit_staged_sequence (
      spot_, spec, part_, part_flag_,
      [spot_, peer_rid_, request_seq_] (std::vector<zlink_msg_t> &parts_) {
          return spot_reply_router_impl (spot_, peer_rid_, request_seq_, parts_.data (),
                                         parts_.size ());
      });
}

zlink_submit_result_t zlink_router_request_spot_part (void *router_,
                                                      const zlink_routing_id_t *dest_node_rid_,
                                                      const zlink_routing_id_t *dest_spot_rid_,
                                                      zlink_msg_t *part_,
                                                      zlink_reply_handler_fn handler_,
                                                      void *userdata_,
                                                      zlink_send_flags_t flags_,
                                                      zlink_part_flag_t part_flag_,
                                                      uint32_t timeout_ms_)
{
    if (!zlink::valid_routing_id (dest_node_rid_)
        || !zlink::valid_routing_id (dest_spot_rid_)
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_part_handler (part_flag_, handler_) != 0
        || validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    send_sequence_spec_t spec =
      make_request_spec (zlink::part_helper_internal::send_family_router_request_spot, flags_,
                         timeout_ms_, handler_, userdata_);
    set_rid_pair (&spec, dest_node_rid_, dest_spot_rid_);

    return submit_staged_sequence (router_, spec, part_, part_flag_,
                                   [router_, dest_node_rid_, dest_spot_rid_, handler_, userdata_,
                                    flags_, timeout_ms_] (std::vector<zlink_msg_t> &parts_) {
                                       return router_request_spot_impl (
                                         router_, dest_node_rid_, dest_spot_rid_, parts_.data (),
                                         parts_.size (), handler_, userdata_, flags_, timeout_ms_);
                                   });
}

zlink_submit_result_t zlink_router_reply_spot_part (void *router_,
                                                    const zlink_routing_id_t *dest_node_rid_,
                                                    const zlink_routing_id_t *dest_spot_rid_,
                                                    uint64_t request_seq_,
                                                    zlink_msg_t *part_,
                                                    zlink_part_flag_t part_flag_)
{
    if (!zlink::valid_routing_id (dest_node_rid_) || !zlink::valid_routing_id (dest_spot_rid_)
        || request_seq_ == 0 || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    send_sequence_spec_t spec =
      make_reply_spec (zlink::part_helper_internal::send_family_router_reply_spot, request_seq_);
    set_rid_pair (&spec, dest_node_rid_, dest_spot_rid_);

    return submit_staged_sequence (
      router_, spec, part_, part_flag_,
      [router_, dest_node_rid_, dest_spot_rid_, request_seq_] (std::vector<zlink_msg_t> &parts_) {
          return router_reply_spot_impl (router_, dest_node_rid_, dest_spot_rid_, request_seq_,
                                         parts_.data (), parts_.size ());
      });
}

zlink_submit_result_t zlink_router_send_spot_part (void *router_,
                                                   const zlink_routing_id_t *dest_node_rid_,
                                                   const zlink_routing_id_t *dest_spot_rid_,
                                                   zlink_msg_t *part_,
                                                   zlink_send_flags_t flags_,
                                                   zlink_part_flag_t part_flag_)
{
    if (!zlink::valid_routing_id (dest_node_rid_) || !zlink::valid_routing_id (dest_spot_rid_)
        || zlink::part_helper_internal::validate_part_flag (part_flag_) != 0
        || validate_request_send_flags (flags_) != 0) {
        zlink::part_helper_internal::consume_send_part (part_);
        return zlink::submit_result_internal::from_errno (errno);
    }

    send_sequence_spec_t spec =
      make_send_spec (zlink::part_helper_internal::send_family_router_send_spot, flags_);
    set_rid_pair (&spec, dest_node_rid_, dest_spot_rid_);

    return submit_staged_sequence (
      router_, spec, part_, part_flag_,
      [router_, dest_node_rid_, dest_spot_rid_, flags_] (std::vector<zlink_msg_t> &parts_) {
          return router_send_spot_impl (router_, dest_node_rid_, dest_spot_rid_, parts_.data (),
                                        parts_.size (), flags_);
      });
}
