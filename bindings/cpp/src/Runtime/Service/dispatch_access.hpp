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

    static void invalidate_claim (claim_t &claim_) noexcept { claim_._valid = false; }

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
        return out;
    }
};

} // namespace detail
} // namespace service
} // namespace zlink

#endif
