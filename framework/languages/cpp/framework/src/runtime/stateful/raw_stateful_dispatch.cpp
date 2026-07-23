/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/stateful/raw_stateful_dispatch.hpp"

#include <iomanip>
#include <sstream>
#include <tuple>
#include <utility>

namespace zlink::framework::runtime::stateful
{

namespace
{

constexpr std::uint32_t terminal_protocol_error = 104;
constexpr std::uint32_t terminal_internal_error = 105;
constexpr std::uint32_t terminal_rejected = 106;
constexpr std::uint32_t terminal_conflict = 107;

}

raw_stateful_dispatch_t::raw_stateful_dispatch_t (
  stateful_object_runtime_t &objects,
  mesh::raw_mesh_node_owner_t &transport) :
    _objects (&objects), _transport (&transport)
{
}

stateful_error_t raw_stateful_dispatch_t::ingest (
  const object_ref_t &owner)
{
    auto claim = _transport->mailbox ().try_claim_owner (
      mesh::service_mailbox_domain_t::application,
      mailbox_owner (owner), 1, 16u * 1024u * 1024u);
    if (!claim) {
        return stateful_error_t::not_found;
    }
    auto record = std::move (claim->records.front ());
    stateful_error_t validation = stateful_error_t::none;
    try {
        const auto header =
          protocol::decode_header (record.parts.front ());
        if (owner.kind == object_kind_t::actor) {
            if (header.kind != protocol::command::actorSend
                && header.kind != protocol::command::actorRequest) {
                validation = stateful_error_t::invalid;
            } else {
                const auto actor =
                  protocol::decode_actor_message_header (
                    record.parts.front (), header.kind);
                if (!exact_fence (owner, actor.target)) {
                    validation =
                      owner.object_generation
                          != actor.target.object_generation
                        ? stateful_error_t::generation_stale
                        : stateful_error_t::conflict;
                }
            }
        } else {
            if (header.kind != protocol::command::spotSend
                && header.kind != protocol::command::spotRequest) {
                validation = stateful_error_t::invalid;
            } else {
                const auto spot =
                  protocol::decode_spot_message_header (
                    record.parts.front (), header.kind);
                if (!exact_fence (owner, spot.target)) {
                    validation =
                      owner.object_generation
                          != spot.target.object_generation
                        ? stateful_error_t::generation_stale
                        : stateful_error_t::conflict;
                }
            }
        }
        if (record.parts.size () != 2) {
            validation = stateful_error_t::invalid;
        }
    }
    catch (const protocol::service_wire_error_t &) {
        validation = stateful_error_t::invalid;
    }
    if (validation != stateful_error_t::none) {
        if (record.request_sequence && record.correlation) {
            (void) _transport->reply_failure (
              record,
              validation == stateful_error_t::generation_stale
                ? terminal_conflict
                : terminal_protocol_error,
              validation == stateful_error_t::generation_stale
                ? static_cast<std::uint32_t> (
                    protocol::framework_error_code::actorLocationStale)
                : static_cast<std::uint32_t> (
                    protocol::framework_error_code::requestProtocolError));
        }
        (void) _transport->mailbox ().release (*claim);
        return validation;
    }

    protocol::application_payload_t payload;
    try {
        payload =
          protocol::decode_application_payload (record.parts[1]);
    }
    catch (const protocol::service_wire_error_t &) {
        if (record.request_sequence && record.correlation) {
            (void) _transport->reply_failure (
              record, terminal_protocol_error,
              static_cast<std::uint32_t> (
                protocol::framework_error_code::payloadDecodeFailed));
        }
        (void) _transport->mailbox ().release (*claim);
        return stateful_error_t::invalid;
    }

    std::uint64_t sequence = 0;
    {
        std::lock_guard lock (_mutex);
        auto &next = _next_sequence[{owner.kind, owner.key}];
        if (next == 0) {
            next = 1;
        }
        sequence = next++;
    }
    const auto enqueued = _objects->enqueue (
      owner, turn_domain_t::application,
      {sequence, record.parts[1]});
    if (enqueued != stateful_error_t::none) {
        if (record.request_sequence && record.correlation) {
            (void) _transport->reply_failure (
              record, terminal_rejected,
              static_cast<std::uint32_t> (
                protocol::framework_error_code::requestRejected));
        }
        (void) _transport->mailbox ().release (*claim);
        return enqueued;
    }
    {
        std::lock_guard lock (_mutex);
        _pending.emplace (
          delivery_key (owner, sequence),
          pending_delivery_t{
            std::move (payload), std::move (record)});
    }
    (void) _transport->mailbox ().release (*claim);
    return stateful_error_t::none;
}

std::pair<stateful_error_t, std::optional<stateful_delivery_t>>
raw_stateful_dispatch_t::try_claim (const object_ref_t &owner)
{
    auto [error, turn] =
      _objects->try_claim (owner, turn_domain_t::application);
    if (error != stateful_error_t::none || !turn) {
        return {error, std::nullopt};
    }
    std::lock_guard lock (_mutex);
    const auto pending =
      _pending.find (delivery_key (owner, turn->sequence));
    if (pending == _pending.end ()) {
        (void) _objects->complete_claim (
          owner, turn_domain_t::application);
        return {stateful_error_t::conflict, std::nullopt};
    }
    return {
      stateful_error_t::none,
      stateful_delivery_t{
        owner, *turn, pending->second.payload,
        pending->second.transport.request_sequence.has_value ()}};
}

stateful_error_t raw_stateful_dispatch_t::complete (
  const stateful_delivery_t &delivery,
  std::optional<protocol::application_payload_t> reply)
{
    pending_delivery_t pending;
    {
        std::lock_guard lock (_mutex);
        const auto found = _pending.find (
          delivery_key (delivery.owner, delivery.turn.sequence));
        if (found == _pending.end ()) {
            return stateful_error_t::conflict;
        }
        pending = std::move (found->second);
        _pending.erase (found);
    }
    if (delivery.request) {
        if (!reply) {
            (void) _transport->reply_failure (
              pending.transport, terminal_internal_error,
              static_cast<std::uint32_t> (
                protocol::framework_error_code::requestFailed));
        } else if (!_transport->reply (pending.transport, *reply)) {
            (void) _objects->complete_claim (
              delivery.owner, turn_domain_t::application);
            return stateful_error_t::conflict;
        }
    }
    return _objects->complete_claim (
      delivery.owner, turn_domain_t::application);
}

bool raw_stateful_dispatch_t::delivery_key_t::operator< (
  const delivery_key_t &other) const noexcept
{
    return std::tie (kind, key, sequence)
           < std::tie (other.kind, other.key, other.sequence);
}

std::string raw_stateful_dispatch_t::mailbox_owner (
  const object_ref_t &owner)
{
    if (owner.kind == object_kind_t::actor) {
        return "actor:" + owner.key;
    }
    std::ostringstream stream;
    stream << "spot:" << std::hex << std::setfill ('0');
    for (const auto byte : owner.key) {
        stream << std::setw (2)
               << static_cast<unsigned int> (
                    static_cast<unsigned char> (byte));
    }
    return stream.str ();
}

bool raw_stateful_dispatch_t::exact_fence (
  const object_ref_t &owner,
  const protocol::actor_route_fence_t &fence)
{
    return owner.kind == object_kind_t::actor
           && owner.key == fence.actor_id
           && owner.object_generation == fence.object_generation
           && owner.authority_owner_generation
                == fence.authority_owner_generation;
}

bool raw_stateful_dispatch_t::exact_fence (
  const object_ref_t &owner,
  const protocol::spot_route_fence_t &fence)
{
    return owner.kind != object_kind_t::actor
           && std::vector<std::uint8_t> (
                owner.key.begin (), owner.key.end ())
                == fence.spot_routing_id
           && owner.object_generation == fence.object_generation
           && owner.authority_owner_generation
                == fence.authority_owner_generation;
}

raw_stateful_dispatch_t::delivery_key_t
raw_stateful_dispatch_t::delivery_key (
  const object_ref_t &owner,
  std::uint64_t sequence)
{
    return {owner.kind, owner.key, sequence};
}

} // namespace zlink::framework::runtime::stateful
