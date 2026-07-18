/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_DISPATCH_ACCESS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_DISPATCH_ACCESS_HPP_INCLUDED

#include "actor_model_access.hpp"
#include "../Core/routing_id_access.hpp"
#include <zlink/Contracts/Service/dispatch.hpp>

#include <zlink.h>

#include <cstring>

namespace zlink
{
namespace service
{
namespace detail
{

struct dispatch_access_t
{
    // reply_token_t <-> zlink_mesh_reply_token_t
    static void set_token (reply_token_t &token_, const zlink_mesh_reply_token_t &native_) noexcept
    {
        std::memcpy (token_._opaque.data (), native_.opaque, sizeof (native_.opaque));
    }

    static zlink_mesh_reply_token_t native_token (const reply_token_t &token_) noexcept
    {
        zlink_mesh_reply_token_t out;
        std::memcpy (out.opaque, token_._opaque.data (), sizeof (out.opaque));
        return out;
    }

    // claim_t <-> zlink_mesh_claim_t
    static void set_claim (claim_t &claim_, const zlink_mesh_claim_t &native_) noexcept
    {
        std::memcpy (claim_._opaque.data (), native_.opaque, sizeof (native_.opaque));
        claim_._valid = true;
    }

    static zlink_mesh_claim_t native_claim (const claim_t &claim_) noexcept
    {
        zlink_mesh_claim_t out;
        std::memcpy (out.opaque, claim_._opaque.data (), sizeof (out.opaque));
        return out;
    }

    static void store_claim (claim_t &claim_, const zlink_mesh_claim_t &native_) noexcept
    {
        std::memcpy (claim_._opaque.data (), native_.opaque, sizeof (native_.opaque));
    }

    // batch handle access
    static void *ready_handle (ready_batch_t &batch_) noexcept { return batch_._handle; }
    static void *receive_handle (receive_batch_t &batch_) noexcept { return batch_._handle; }
    static const void *receive_handle (const receive_batch_t &batch_) noexcept
    {
        return batch_._handle;
    }

    static ready_record_t ready_from_native (const zlink_mesh_ready_record_t &native_)
    {
        ready_record_t out;
        out.owner_kind = static_cast<owner_kind_t> (native_.owner_kind);
        out.domain = static_cast<ready_domain_t> (native_.domain);
        out.spot_rid = zlink::detail::native_routing_id (native_.spot_rid);
        out.actor = zlink::detail::actor_model_access_t::from_native (native_.actor);
        return out;
    }

    static receive_record_t receive_from_native (const zlink_mesh_receive_record_t &native_)
    {
        receive_record_t out;
        out.kind = static_cast<record_kind_t> (native_.kind);
        out.domain = static_cast<ready_domain_t> (native_.domain);
        out.source_node_rid = zlink::detail::native_routing_id (native_.source_node_rid);
        out.source_spot_rid = zlink::detail::native_routing_id (native_.source_spot_rid);
        out.source_actor = zlink::detail::actor_model_access_t::from_native (native_.source_actor);
        out.operation_id.high = native_.operation_id.high;
        out.operation_id.low = native_.operation_id.low;
        out.operation_kind = static_cast<operation_kind_t> (native_.operation_kind);
        set_token (out.reply_token, native_.reply_token);
        if (native_.channel_name && native_.channel_name_size > 0)
            out.channel_name.assign (native_.channel_name, native_.channel_name_size);
        if (native_.topic && native_.topic_size > 0)
            out.topic.assign (native_.topic, native_.topic_size);
        if (native_.application_metadata && native_.application_metadata_size > 0)
            out.application_metadata.assign (
              native_.application_metadata,
              native_.application_metadata + native_.application_metadata_size);
        out.terminal_result = native_.terminal_result;
        out.failure_errno = native_.failure_errno;
        out.part_offset = native_.part_offset;
        out.part_count = native_.part_count;
        decode_kind_data (out, native_);
        return out;
    }

    // Copies kind_data into a batch-independent buffer and, for the record
    // kinds that carry a known control payload, decodes it into a typed value.
    static void decode_kind_data (receive_record_t &out_,
                                  const zlink_mesh_receive_record_t &native_)
    {
        if (!native_.kind_data || native_.kind_data_size == 0)
            return;
        const uint8_t *bytes = static_cast<const uint8_t *> (native_.kind_data);
        out_.kind_data.assign (bytes, bytes + native_.kind_data_size);

        if (native_.kind == ZLINK_MESH_RECORD_SEND_READY
            && native_.kind_data_size >= sizeof (zlink_mesh_send_ready_data_t)) {
            const auto *d = static_cast<const zlink_mesh_send_ready_data_t *> (native_.kind_data);
            send_ready_data_t sr;
            sr.destination_kind = static_cast<destination_kind_t> (d->destination_kind);
            sr.target_node_rid = zlink::detail::native_routing_id (d->target_node_rid);
            sr.target_spot_rid = zlink::detail::native_routing_id (d->target_spot_rid);
            sr.target_actor = zlink::detail::actor_model_access_t::from_native (d->target_actor);
            if (d->channel_name && d->channel_name_size > 0)
                sr.channel_name.assign (d->channel_name, d->channel_name_size);
            out_.send_ready = std::move (sr);
        } else if (native_.kind == ZLINK_MESH_RECORD_TRANSFER_CONTROL
                   && native_.kind_data_size >= sizeof (zlink_actor_transfer_control_t)) {
            const auto *d = static_cast<const zlink_actor_transfer_control_t *> (native_.kind_data);
            transfer_control_t tc;
            tc.phase = static_cast<transfer_control_phase_t> (d->phase);
            tc.role = static_cast<transfer_control_role_t> (d->role);
            tc.transfer_id_high = d->transfer_id.high;
            tc.transfer_id_low = d->transfer_id.low;
            tc.actor = zlink::detail::actor_model_access_t::from_native (d->actor);
            tc.membership_epoch = d->membership_epoch;
            tc.final_sequence = d->final_sequence;
            tc.result_code = d->result_code;
            tc.failure_errno = d->failure_errno;
            out_.transfer_control = std::move (tc);
        } else if (native_.kind == ZLINK_MESH_RECORD_SPOT_CONTROL
                   && native_.kind_data_size >= sizeof (zlink_actor_control_record_t)) {
            const auto *d = static_cast<const zlink_actor_control_record_t *> (native_.kind_data);
            actor_control_t ac;
            ac.kind = static_cast<lifecycle_kind_t> (d->kind);
            ac.previous_actor = zlink::detail::actor_model_access_t::from_native (d->previous_actor);
            ac.current_actor = zlink::detail::actor_model_access_t::from_native (d->current_actor);
            ac.previous_spot_rid = zlink::detail::native_routing_id (d->previous_spot_rid);
            ac.current_spot_rid = zlink::detail::native_routing_id (d->current_spot_rid);
            ac.previous_spot_generation = d->previous_spot_generation;
            ac.current_spot_generation = d->current_spot_generation;
            ac.previous_membership_epoch = d->previous_membership_epoch;
            ac.current_membership_epoch = d->current_membership_epoch;
            ac.result_code = d->result_code;
            out_.actor_control = std::move (ac);
        } else if (native_.operation_kind == ZLINK_MESH_OPERATION_ACTOR_JOIN
                   && native_.kind_data_size >= sizeof (zlink_actor_join_completion_t)) {
            const auto *d = static_cast<const zlink_actor_join_completion_t *> (native_.kind_data);
            join_completion_t jc;
            jc.join_result = static_cast<join_admission_t> (d->join_result);
            jc.actor = zlink::detail::actor_model_access_t::from_native (d->actor);
            jc.location = zlink::detail::actor_model_access_t::from_native (d->location);
            out_.join_completion = std::move (jc);
        }
    }
};

} // namespace detail
} // namespace service
} // namespace zlink

#endif
