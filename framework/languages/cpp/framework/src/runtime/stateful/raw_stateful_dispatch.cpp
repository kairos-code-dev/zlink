/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/stateful/raw_stateful_dispatch.hpp"
#include "runtime/locations/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <limits>
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

bool nonzero (const protocol::wire_operation_id_t &value) noexcept
{
    return value.high != 0 || value.low != 0;
}

bool nonzero (const protocol::relocation_id_t &value) noexcept
{
    return value.high != 0 || value.low != 0;
}

bool valid_source (const protocol::request_source_fence_t &value) noexcept
{
    return !value.owner_id.empty () && value.lease_generation != 0
           && !value.node_routing_id.empty () && value.node_generation != 0;
}

bool valid_coordinator (
  const protocol::relocation_coordinator_fence_t &value) noexcept
{
    return !value.owner_id.empty () && value.lease_generation != 0
           && !value.node_routing_id.empty () && value.node_generation != 0
           && !value.expected_authority_store_version.empty ();
}

std::array<std::byte, 32> digest_bytes (
  const std::vector<std::uint8_t> &bytes)
{
    std::vector<std::byte> public_bytes;
    public_bytes.reserve (bytes.size ());
    for (const auto value : bytes)
        public_bytes.push_back (static_cast<std::byte> (value));
    return runtime::sha256 (public_bytes);
}

std::size_t retained_bytes (
  const raw_relocation_terminal_target_registration_t &registration)
{
    auto total = protocol::encode_reply_relay (registration.relay).size ();
    if (registration.application_reply)
        total += protocol::encode_application_payload (
                   *registration.application_reply).size ();
    total += registration.request_source.owner_id.size ()
             + registration.request_source.node_routing_id.size ();
    return total;
}

bool frozen_matches_registration (
  const protocol::frozen_record_t &frozen,
  const raw_relocation_target_registration_t &registration)
{
    if (!frozen.target || frozen.source != registration.source)
        return false;
    const auto &target = *frozen.target;
    return target.kind == registration.object.kind
           && target.object_id == registration.object.object_id
           && target.object_generation
                == registration.object.object_generation
           && target.authority_owner_generation
                == registration.object.expected_authority_owner_generation;
}

}

accepted_record_authority_resolver_t
make_location_store_authority_resolver (
  zlink::framework::location_store_t &store)
{
    return [&store] (const accepted_record_authority_query_t &query)
      -> std::optional<accepted_record_authority_t> {
        try {
            const auto kind = query.target.kind == object_kind_t::actor
              ? placement_object_kind_t::actor
              : query.target.kind == object_kind_t::user_spot
                  ? placement_object_kind_t::user_spot
                  : placement_object_kind_t::instance_spot;
            const auto authority = store.read_authority (
              {std::to_string (static_cast<int> (kind)) + ":"
               + query.target.key}).result ().value ();
            const auto *snapshot =
              std::get_if<authority_snapshot_t> (&authority);
            const auto target_rid = node_rid_t::from_string (
              zlink::routing_id_t::from (
                query.target_node_routing_id).to_string ());
            if (!snapshot
                || snapshot->object_generation
                     != query.target.object_generation
                || snapshot->authority_owner_generation
                     != query.target.authority_owner_generation
                || snapshot->allocation.target.mesh_name
                     != query.target.mesh_name
                || snapshot->allocation.target.node_rid.value ()
                     != target_rid.value ()
                || snapshot->allocation.target.node_lifecycle_generation
                     != query.target_node_generation)
                return std::nullopt;

            const auto nodes = store.list_mesh_nodes (
              query.target.mesh_name).result ().value ();
            const auto source_rid = zlink::routing_id_t::from (
              query.source_node_routing_id);
            const auto source = std::find_if (
              nodes.items.begin (), nodes.items.end (),
              [&] (const mesh_node_descriptor_t &node) {
                  return node.rid == source_rid
                         && node.lifecycle_generation
                              == query.source_node_generation;
              });
            if (source == nodes.items.end () || source->owner_id.empty ()
                || source->lease_generation <= 0)
                return std::nullopt;
            const auto lease = store.read_owner_lease (
              source->owner_id).result ().value ();
            const auto *live = std::get_if<owner_lease_found_t> (&lease);
            if (!live || live->token.lease_generation
                           != source->lease_generation
                || live->lease_expires_at <= live->store_now)
                return std::nullopt;
            return accepted_record_authority_t{
              {source->owner_id,
               static_cast<std::uint64_t> (source->lease_generation),
               query.source_node_routing_id,
               query.source_node_generation},
              static_cast<std::uint64_t> (
                snapshot->allocation.target.owner.lease_generation)};
        }
        catch (...) {
            return std::nullopt;
        }
    };
}

raw_stateful_dispatch_t::raw_stateful_dispatch_t (
  stateful_object_runtime_t &objects,
  mesh::raw_mesh_node_owner_t &transport,
  accepted_record_authority_resolver_t authority_resolver) :
    _objects (&objects), _transport (&transport),
    _authority_resolver (std::move (authority_resolver))
{
}

stateful_error_t raw_stateful_dispatch_t::ingest (
  const object_ref_t &owner)
{
    auto claim = _transport->mailbox ().try_claim_owner (
      mesh::service_mailbox_domain_t::application,
      mailbox_owner (owner), 1, 16u * 1024u * 1024u);
    if (!claim)
        return stateful_error_t::not_found;

    auto record = std::move (claim->records.front ());
    stateful_error_t validation = stateful_error_t::none;
    protocol::frozen_application_record_t frozen;
    protocol::application_payload_t payload;
    try {
        const auto header = protocol::decode_header (
          record.parts.front ());
        if (record.parts.size () != 2 || !_authority_resolver
            || record.source_routing_id.empty ()
            || !record.source_node_generation)
            validation = stateful_error_t::invalid;

        accepted_record_authority_query_t query;
        query.target = owner;
        query.source_node_routing_id = record.source_routing_id;
        query.source_node_generation =
          record.source_node_generation;

        if (validation == stateful_error_t::none
            && owner.kind == object_kind_t::actor) {
            if (header.kind != protocol::command::actorSend
                && header.kind != protocol::command::actorRequest) {
                validation = stateful_error_t::invalid;
            }
            else {
                const auto actor = protocol::decode_actor_message_header (
                  record.parts.front (), header.kind);
                if (!exact_fence (owner, actor.target)) {
                    validation =
                      owner.object_generation
                          != actor.target.object_generation
                        ? stateful_error_t::generation_stale
                        : stateful_error_t::conflict;
                }
                else {
                    query.target_node_routing_id =
                      actor.target.target_node_routing_id;
                    query.target_node_generation =
                      actor.target.target_node_generation;
                    query.source_kind = actor.source_actor
                      ? protocol::frozen_source_kind_t::actor
                      : protocol::frozen_source_kind_t::node;
                    query.source_actor = actor.source_actor;
                    const auto authority = _authority_resolver (query);
                    if (!authority) {
                        validation = stateful_error_t::conflict;
                    }
                    else {
                        payload = protocol::decode_application_payload (
                          record.parts[1]);
                        frozen.kind =
                          header.kind == protocol::command::actorRequest
                            ? protocol::frozen_record_kind_t::actor_request
                            : protocol::frozen_record_kind_t::actor_send;
                        frozen.source_kind = query.source_kind;
                        frozen.source = authority->source;
                        frozen.source_actor = actor.source_actor;
                        frozen.operation = actor.operation;
                        frozen.operation_kind =
                          header.kind == protocol::command::actorRequest
                            ? 4u : 0u;
                        frozen.reply_route_id = actor.correlation;
                        auto accepted_target = actor.target;
                        accepted_target.owner_lease_generation =
                          authority->target_owner_lease_generation;
                        frozen.body =
                          protocol::frozen_actor_application_body_t{
                            std::move (accepted_target), payload};
                    }
                }
            }
        }
        else if (validation == stateful_error_t::none) {
            if (header.kind != protocol::command::spotSend
                && header.kind != protocol::command::spotRequest) {
                validation = stateful_error_t::invalid;
            }
            else {
                const auto spot = protocol::decode_spot_message_header (
                  record.parts.front (), header.kind);
                if (!exact_fence (owner, spot.target)) {
                    validation =
                      owner.object_generation
                          != spot.target.object_generation
                        ? stateful_error_t::generation_stale
                        : stateful_error_t::conflict;
                }
                else {
                    query.target_node_routing_id =
                      spot.target.target_node_routing_id;
                    query.target_node_generation =
                      spot.target.target_node_generation;
                    query.source_kind = spot.source_spot_id.empty ()
                      ? protocol::frozen_source_kind_t::node
                      : protocol::frozen_source_kind_t::spot;
                    if (!spot.source_spot_id.empty ())
                        query.source_spot_id = spot.source_spot_id;
                    const auto authority = _authority_resolver (query);
                    if (!authority) {
                        validation = stateful_error_t::conflict;
                    }
                    else {
                        payload = protocol::decode_application_payload (
                          record.parts[1]);
                        frozen.kind =
                          header.kind == protocol::command::spotRequest
                            ? protocol::frozen_record_kind_t::spot_request
                            : protocol::frozen_record_kind_t::spot_send;
                        frozen.source_kind = query.source_kind;
                        frozen.source = authority->source;
                        frozen.source_spot_id = query.source_spot_id;
                        frozen.operation = spot.operation;
                        frozen.operation_kind =
                          header.kind == protocol::command::spotRequest
                            ? 3u : 0u;
                        frozen.reply_route_id = spot.correlation;
                        frozen.body =
                          protocol::frozen_spot_application_body_t{
                            spot.target,
                            authority->target_owner_lease_generation,
                            payload};
                    }
                }
            }
        }
        if (validation == stateful_error_t::none
            && !nonzero (frozen.operation))
            validation = stateful_error_t::invalid;
    }
    catch (const protocol::service_wire_error_t &) {
        validation = stateful_error_t::invalid;
    }
    catch (...) {
        validation = stateful_error_t::conflict;
    }

    if (validation != stateful_error_t::none) {
        if (record.request_sequence && record.correlation) {
            (void) _transport->reply_failure (
              record,
              validation == stateful_error_t::generation_stale
                || validation == stateful_error_t::conflict
                ? terminal_conflict
                : terminal_protocol_error,
              validation == stateful_error_t::generation_stale
                ? static_cast<std::uint32_t> (
                    protocol::framework_error_code::actorLocationStale)
                : validation == stateful_error_t::conflict
                    ? static_cast<std::uint32_t> (
                        protocol::framework_error_code::requestRejected)
                    : static_cast<std::uint32_t> (
                        protocol::framework_error_code::requestProtocolError));
        }
        (void) _transport->mailbox ().release (*claim);
        return validation;
    }

    std::uint64_t sequence = 0;
    {
        std::lock_guard lock (_mutex);
        auto &next = _next_sequence[{owner.kind, owner.key}];
        if (next == 0)
            next = 1;
        sequence = next++;
    }
    const auto canonical =
      protocol::encode_frozen_application_record (frozen);
    const auto enqueued = _objects->enqueue (
      owner, turn_domain_t::application,
      {sequence, canonical.canonical_bytes});
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
    return "spot:" + owner.key;
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
           && owner.key == fence.spot_id
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


raw_relocation_replay_coordinator_t::raw_relocation_replay_coordinator_t (
  mesh::raw_mesh_node_owner_t &transport,
  std::size_t terminal_record_limit,
  std::size_t terminal_byte_limit,
  std::chrono::milliseconds relay_retry_interval,
  std::chrono::milliseconds terminal_tombstone_retention) :
    _transport (&transport),
    _terminal_record_limit (terminal_record_limit),
    _terminal_byte_limit (terminal_byte_limit),
    _relay_retry_interval (relay_retry_interval),
    _terminal_tombstone_retention (terminal_tombstone_retention)
{
    if (_terminal_record_limit == 0 || _terminal_byte_limit == 0
        || _relay_retry_interval <= std::chrono::milliseconds::zero ()
        || _terminal_tombstone_retention
             <= std::chrono::milliseconds::zero ())
        throw std::invalid_argument (
          "relocation replay limits must be positive");
}

bool raw_relocation_replay_coordinator_t::key_t::operator< (
  const key_t &other) const noexcept
{
    return std::tie (relocation.high, relocation.low,
                     target_attempt_generation, participant_id)
           < std::tie (other.relocation.high, other.relocation.low,
                       other.target_attempt_generation,
                       other.participant_id);
}

bool raw_relocation_replay_coordinator_t::terminal_key_t::operator< (
  const terminal_key_t &other) const noexcept
{
    return std::tie (relocation.high, relocation.low,
                     operation.high, operation.low)
           < std::tie (other.relocation.high, other.relocation.low,
                       other.operation.high, other.operation.low);
}

raw_relocation_replay_coordinator_t::key_t
raw_relocation_replay_coordinator_t::key (
  const protocol::relocation_id_t &relocation,
  std::uint64_t target_attempt_generation,
  std::uint64_t participant_id)
{
    return {relocation, target_attempt_generation, participant_id};
}

raw_relocation_replay_coordinator_t::terminal_key_t
raw_relocation_replay_coordinator_t::terminal_key (
  const protocol::relocation_id_t &relocation,
  const protocol::wire_operation_id_t &operation)
{
    return {relocation, operation};
}

std::string raw_relocation_replay_coordinator_t::mailbox_owner (
  const std::vector<std::uint8_t> &rid)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill ('0');
    for (const auto byte : rid)
        stream << std::setw (2) << static_cast<unsigned int> (byte);
    return stream.str ();
}

bool raw_relocation_replay_coordinator_t::register_target (
  raw_relocation_target_registration_t registration)
{
    if (!nonzero (registration.relocation)
        || registration.target_attempt_generation == 0
        || !valid_coordinator (registration.coordinator)
        || registration.participant_id == 0
        || !valid_source (registration.source)
        || registration.object.object_id.empty ()
        || registration.object.object_generation == 0
        || registration.object.expected_authority_owner_generation == 0
        || !registration.stage)
        return false;
    std::lock_guard lock (_gate);
    return _targets.emplace (
      key (registration.relocation,
           registration.target_attempt_generation,
           registration.participant_id),
      target_state_t{std::move (registration)}).second;
}

bool raw_relocation_replay_coordinator_t::register_source (
  raw_relocation_source_registration_t registration)
{
    if (!nonzero (registration.relocation)
        || registration.target_attempt_generation == 0
        || !valid_coordinator (registration.coordinator)
        || registration.participant_id == 0
        || registration.target_node_routing_id.empty ()
        || registration.target_node_generation == 0
        || registration.sent_high_water == 0
        || !registration.acknowledged)
        return false;
    if (!registration.records.empty ()) {
        if (registration.records.size () != registration.sent_high_water)
            return false;
        for (std::size_t index = 0;
             index != registration.records.size (); ++index) {
            const auto &record = registration.records[index];
            if (record.relocation != registration.relocation
                || record.target_attempt_generation
                     != registration.target_attempt_generation
                || record.coordinator != registration.coordinator
                || record.sender_role != protocol::relocation_role_t::source
                || record.participant_id != registration.participant_id
                || record.sequence != index + 1
                || !valid_source (record.source)
                || !record.frozen_record
                || record.frozen_record->source != record.source
                || record.phase != protocol::relocation_phase_t::prepared)
                return false;
        }
    }
    std::lock_guard lock (_gate);
    return _sources.emplace (
      key (registration.relocation,
           registration.target_attempt_generation,
           registration.participant_id),
      source_state_t{std::move (registration)}).second;
}

bool raw_relocation_replay_coordinator_t::arm_source (
  const protocol::relocation_id_t &relocation,
  std::uint64_t target_attempt_generation,
  std::uint64_t participant_id)
{
    std::lock_guard lock (_gate);
    const auto found = _sources.find (
      key (relocation, target_attempt_generation, participant_id));
    if (found == _sources.end () || found->second.registration.records.empty ())
        return false;
    found->second.armed = true;
    found->second.next_retry = clock_t::time_point::min ();
    return true;
}

bool raw_relocation_replay_coordinator_t::unregister_source (
  const protocol::relocation_id_t &relocation,
  std::uint64_t target_attempt_generation,
  std::uint64_t participant_id)
{
    std::lock_guard lock (_gate);
    return _sources.erase (
             key (relocation, target_attempt_generation, participant_id))
           != 0;
}

std::size_t raw_relocation_replay_coordinator_t::retry_source_replays (
  clock_t::time_point now)
{
    struct pending_t
    {
        std::vector<std::uint8_t> target;
        protocol::relocation_data_t record;
    };
    std::vector<pending_t> pending;
    {
        std::lock_guard lock (_gate);
        for (auto &[_, state] : _sources) {
            if (!state.armed || state.registration.records.empty ()
                || now < state.next_retry)
                continue;
            state.next_retry = now + _relay_retry_interval;
            for (const auto &record : state.registration.records) {
                if (record.sequence <= state.high_water)
                    continue;
                pending.push_back (
                  {state.registration.target_node_routing_id, record});
            }
        }
    }
    std::size_t sent = 0;
    for (const auto &item : pending) {
        try {
            if (_transport->send_relocation_control (
                  item.target, item.record))
                ++sent;
        }
        catch (...) {
        }
    }
    return sent;
}

raw_relocation_replay_result_t
raw_relocation_replay_coordinator_t::pump_one ()
{
    const auto local = _transport->topology ().local_descriptor ();
    auto claim = _transport->mailbox ().try_claim_owner (
      mesh::service_mailbox_domain_t::infrastructure,
      mailbox_owner (local.node_routing_id), 1, 16u * 1024u * 1024u);
    if (!claim)
        return raw_relocation_replay_result_t::no_data;
    auto record = std::move (claim->records.front ());
    const auto result = process (record);
    (void) _transport->mailbox ().release (*claim);
    return result;
}

raw_relocation_replay_result_t
raw_relocation_replay_coordinator_t::process (
  const mesh::service_mailbox_record_t &record)
{
    if (record.parts.empty ())
        return raw_relocation_replay_result_t::invalid;
    try {
        const auto header = protocol::decode_header (
          record.parts.front ());
        if (header.kind == protocol::command::relocationData) {
            const auto control = protocol::decode_relocation_control (
              record.parts.front ());
            const auto *data =
              std::get_if<protocol::relocation_data_t> (&control);
            return data ? process_data (record, *data)
                        : raw_relocation_replay_result_t::invalid;
        }
        if (header.kind == protocol::command::relocationAck) {
            const auto control = protocol::decode_relocation_control (
              record.parts.front ());
            const auto *ack =
              std::get_if<protocol::relocation_ack_t> (&control);
            return ack ? process_ack (record, *ack)
                       : raw_relocation_replay_result_t::invalid;
        }
        if (header.kind == protocol::command::replyRelay)
            return process_reply_relay (
              record, protocol::decode_reply_relay (
                        record.parts.front ()));
        if (header.kind == protocol::command::replyRelayAck)
            return process_reply_relay_ack (
              record, protocol::decode_reply_relay_ack (
                        record.parts.front ()));
    }
    catch (const protocol::service_wire_error_t &) {
        return raw_relocation_replay_result_t::invalid;
    }
    return raw_relocation_replay_result_t::invalid;
}

raw_relocation_replay_result_t
raw_relocation_replay_coordinator_t::process_data (
  const mesh::service_mailbox_record_t &record,
  const protocol::relocation_data_t &data)
{
    if (record.parts.size () != 1 || !data.frozen_record
        || data.sender_role != protocol::relocation_role_t::source
        || data.phase != protocol::relocation_phase_t::prepared
        || data.sequence == 0)
        return raw_relocation_replay_result_t::invalid;

    const auto state_key = key (
      data.relocation, data.target_attempt_generation,
      data.participant_id);
    const auto digest = digest_bytes (
      protocol::encode_relocation_control (data));
    std::function<bool (const protocol::relocation_data_t &)> stage;
    {
        std::unique_lock lock (_gate);
        const auto found = _targets.find (state_key);
        if (found == _targets.end ())
            return raw_relocation_replay_result_t::not_registered;
        auto &state = found->second;
        const auto &registration = state.registration;
        if (registration.relocation != data.relocation
            || registration.target_attempt_generation
                 != data.target_attempt_generation
            || registration.coordinator != data.coordinator
            || registration.participant_id != data.participant_id
            || registration.source != data.source
            || record.source_routing_id != registration.source.node_routing_id
            || record.source_node_generation
                 != registration.source.node_generation
            || !frozen_matches_registration (
                 *data.frozen_record, registration))
            return raw_relocation_replay_result_t::stale_fence;

        const auto accepted = state.accepted.find (data.sequence);
        if (accepted != state.accepted.end ()) {
            if (accepted->second != digest)
                return raw_relocation_replay_result_t::conflicting_duplicate;
            const auto source_node =
              registration.source.node_routing_id;
            const protocol::relocation_ack_t duplicate_ack{
              data.relocation,
              data.target_attempt_generation,
              data.coordinator,
              protocol::relocation_role_t::target,
              data.participant_id,
              state.high_water};
            lock.unlock ();
            try {
                return _transport->send_relocation_control (
                  source_node, duplicate_ack)
                  ? raw_relocation_replay_result_t::duplicate
                  : raw_relocation_replay_result_t::transport_failed;
            }
            catch (...) {
                return raw_relocation_replay_result_t::transport_failed;
            }
        }
        if (data.sequence != state.high_water + 1
            || state.staging_sequence)
            return raw_relocation_replay_result_t::sequence_gap;

        const auto operation_key = std::pair{
          data.frozen_record->operation.high,
          data.frozen_record->operation.low};
        if (!nonzero (data.frozen_record->operation))
            return raw_relocation_replay_result_t::invalid;
        for (const auto &[candidate_key, candidate] : _targets) {
            if (candidate.registration.source != registration.source)
                continue;
            const auto duplicate =
              candidate.accepted_operations.find (operation_key);
            if (duplicate != candidate.accepted_operations.end ()
                && (candidate_key.relocation != state_key.relocation
                    || candidate_key.target_attempt_generation
                         != state_key.target_attempt_generation
                    || candidate_key.participant_id
                         != state_key.participant_id
                    || duplicate->second != data.sequence))
                return raw_relocation_replay_result_t::conflicting_duplicate;
        }
        state.staging_sequence = data.sequence;
        stage = registration.stage;
    }

    bool staged = false;
    try {
        staged = stage (data);
    }
    catch (...) {
        staged = false;
    }

    std::vector<std::uint8_t> source_node;
    std::uint64_t high_water = 0;
    {
        std::lock_guard lock (_gate);
        const auto found = _targets.find (state_key);
        if (found == _targets.end ()
            || found->second.staging_sequence != data.sequence)
            return raw_relocation_replay_result_t::stale_fence;
        auto &state = found->second;
        state.staging_sequence.reset ();
        if (!staged)
            return raw_relocation_replay_result_t::restore_failed;
        state.accepted.emplace (data.sequence, digest);
        state.accepted_operations.emplace (
          std::pair{data.frozen_record->operation.high,
                    data.frozen_record->operation.low},
          data.sequence);
        state.high_water = data.sequence;
        source_node = state.registration.source.node_routing_id;
        high_water = state.high_water;
    }
    try {
        return _transport->send_relocation_control (
          source_node,
          protocol::relocation_ack_t{
            data.relocation,
            data.target_attempt_generation,
            data.coordinator,
            protocol::relocation_role_t::target,
            data.participant_id,
            high_water})
          ? raw_relocation_replay_result_t::applied
          : raw_relocation_replay_result_t::transport_failed;
    }
    catch (...) {
        return raw_relocation_replay_result_t::transport_failed;
    }
}

raw_relocation_replay_result_t
raw_relocation_replay_coordinator_t::process_ack (
  const mesh::service_mailbox_record_t &record,
  const protocol::relocation_ack_t &ack)
{
    if (record.parts.size () != 1
        || ack.sender_role != protocol::relocation_role_t::target
        || ack.high_water == 0)
        return raw_relocation_replay_result_t::invalid;
    std::function<void (std::uint64_t)> callback;
    {
        std::lock_guard lock (_gate);
        const auto found = _sources.find (
          key (ack.relocation, ack.target_attempt_generation,
               ack.participant_id));
        if (found == _sources.end ())
            return raw_relocation_replay_result_t::not_registered;
        auto &state = found->second;
        const auto &registration = state.registration;
        if (ack.relocation != registration.relocation
            || ack.target_attempt_generation
                 != registration.target_attempt_generation
            || ack.coordinator != registration.coordinator
            || ack.participant_id != registration.participant_id
            || record.source_routing_id
                 != registration.target_node_routing_id
            || record.source_node_generation
                 != registration.target_node_generation
            || ack.high_water > registration.sent_high_water)
            return raw_relocation_replay_result_t::stale_fence;
        if (ack.high_water <= state.high_water)
            return raw_relocation_replay_result_t::ack_ignored;
        state.high_water = ack.high_water;
        callback = registration.acknowledged;
    }
    try {
        callback (ack.high_water);
    }
    catch (...) {
        return raw_relocation_replay_result_t::persistence_failed;
    }
    {
        std::lock_guard lock (_gate);
        const auto found = _sources.find (
          key (ack.relocation, ack.target_attempt_generation,
               ack.participant_id));
        if (found != _sources.end ()
            && found->second.high_water
                 == found->second.registration.sent_high_water) {
            found->second.registration.records.clear ();
            found->second.armed = false;
        }
    }
    return raw_relocation_replay_result_t::ack_advanced;
}

std::uint64_t raw_relocation_replay_coordinator_t::target_high_water (
  const protocol::relocation_id_t &relocation,
  std::uint64_t target_attempt_generation,
  std::uint64_t participant_id) const
{
    std::lock_guard lock (_gate);
    const auto found = _targets.find (
      key (relocation, target_attempt_generation, participant_id));
    return found == _targets.end () ? 0 : found->second.high_water;
}

std::uint64_t raw_relocation_replay_coordinator_t::source_ack_high_water (
  const protocol::relocation_id_t &relocation,
  std::uint64_t target_attempt_generation,
  std::uint64_t participant_id) const
{
    std::lock_guard lock (_gate);
    const auto found = _sources.find (
      key (relocation, target_attempt_generation, participant_id));
    return found == _sources.end () ? 0 : found->second.high_water;
}

std::size_t
raw_relocation_replay_coordinator_t::target_retained_identity_bytes (
  const protocol::relocation_id_t &relocation,
  std::uint64_t target_attempt_generation,
  std::uint64_t participant_id) const
{
    std::lock_guard lock (_gate);
    const auto found = _targets.find (
      key (relocation, target_attempt_generation, participant_id));
    return found == _targets.end ()
      ? 0
      : found->second.accepted.size ()
          * sizeof (std::array<std::byte, 32>);
}

bool raw_relocation_replay_coordinator_t::register_terminal_source (
  raw_relocation_terminal_source_registration_t registration)
{
    if (!nonzero (registration.relocation)
        || !valid_coordinator (registration.coordinator)
        || !nonzero (registration.operation)
        || !valid_source (registration.request_source)
        || registration.target_node_routing_id.empty ()
        || registration.target_node_generation == 0
        || registration.target_attempt_generation == 0
        || registration.participant_id == 0
        || registration.sequence == 0
        || registration.reply_route_id == 0
        || !registration.complete)
        return false;
    const auto local = _transport->topology ().local_descriptor ();
    if (registration.request_source.node_routing_id
          != local.node_routing_id
        || registration.request_source.node_generation
             != local.lifecycle_generation)
        return false;
    std::lock_guard lock (_gate);
    if (_terminal_sources.size () >= _terminal_record_limit)
        return false;
    return _terminal_sources.emplace (
      terminal_key (registration.relocation,
                    registration.operation),
      terminal_source_state_t{std::move (registration)}).second;
}

bool raw_relocation_replay_coordinator_t::register_terminal_target (
  raw_relocation_terminal_target_registration_t registration)
{
    const auto &relay = registration.relay;
    if (!nonzero (relay.relocation) || !nonzero (relay.operation)
        || !valid_coordinator (relay.coordinator)
        || !valid_source (registration.request_source)
        || relay.target_attempt_generation == 0
        || relay.participant_id == 0 || relay.sequence == 0
        || relay.reply_route_id == 0 || !registration.persist_ack
        || !registration.persist_source_lease_expiry
        || (relay.terminal_result != 0
            && registration.application_reply)
        || (relay.terminal_result != 0
            && relay.failure_code
                 == protocol::framework_error_code::none))
        return false;
    const auto bytes = retained_bytes (registration);
    std::lock_guard lock (_gate);
    const auto item_key = terminal_key (
      relay.relocation, relay.operation);
    const auto existing = _terminal_targets.find (item_key);
    if (existing != _terminal_targets.end ())
        return existing->second.registration.relay == relay
               && existing->second.registration.request_source
                    == registration.request_source
               && existing->second.registration.application_reply
                    == registration.application_reply;
    if (_terminal_targets.size () >= _terminal_record_limit
        || bytes > _terminal_byte_limit - std::min (
             _terminal_byte_limit, _terminal_retained_bytes))
        return false;
    terminal_target_state_t state;
    state.registration = std::move (registration);
    state.retained_bytes = bytes;
    state.next_retry = clock_t::time_point::min ();
    _terminal_targets.emplace (item_key, std::move (state));
    _terminal_retained_bytes += bytes;
    return true;
}

std::size_t raw_relocation_replay_coordinator_t::retry_terminal_relays (
  clock_t::time_point now)
{
    struct pending_send_t
    {
        terminal_key_t key;
        std::vector<std::uint8_t> target;
        protocol::reply_relay_t relay;
        std::optional<protocol::application_payload_t> reply;
    };
    std::vector<pending_send_t> pending;
    {
        std::lock_guard lock (_gate);
        for (auto &[item_key, state] : _terminal_targets) {
            if (state.acknowledging || state.next_retry > now)
                continue;
            pending.push_back ({
              item_key,
              state.registration.request_source.node_routing_id,
              state.registration.relay,
              state.registration.application_reply});
            state.next_retry = now + _relay_retry_interval;
        }
    }
    std::size_t sent = 0;
    for (auto &item : pending) {
        try {
            if (_transport->send_reply_relay (
                  item.target, item.relay, std::move (item.reply)))
                ++sent;
        }
        catch (...) {
            // The durable target record remains registered for the next retry.
        }
    }
    return sent;
}

raw_relocation_replay_result_t
raw_relocation_replay_coordinator_t::process_reply_relay (
  const mesh::service_mailbox_record_t &record,
  const protocol::reply_relay_t &relay)
{
    const auto item_key = terminal_key (
      relay.relocation, relay.operation);
    std::optional<protocol::application_payload_t> reply;
    if (record.parts.size () == 2) {
        try {
            reply = protocol::decode_application_payload (
              record.parts[1]);
        }
        catch (const protocol::service_wire_error_t &) {
            return raw_relocation_replay_result_t::invalid;
        }
    }
    else if (record.parts.size () != 1) {
        return raw_relocation_replay_result_t::invalid;
    }
    if ((relay.terminal_result != 0 && reply)
        || (relay.terminal_result != 0
            && relay.failure_code
                 == protocol::framework_error_code::none))
        return raw_relocation_replay_result_t::invalid;

    std::vector<std::uint8_t> fingerprint =
      protocol::encode_reply_relay (relay);
    if (reply) {
        const auto encoded = protocol::encode_application_payload (*reply);
        fingerprint.insert (
          fingerprint.end (), encoded.begin (), encoded.end ());
    }
    const auto digest = digest_bytes (fingerprint);
    std::function<bool (
      const protocol::reply_relay_t &,
      const std::optional<protocol::application_payload_t> &)> complete;
    protocol::request_source_fence_t request_source;
    bool duplicate = false;
    {
        std::lock_guard lock (_gate);
        const auto found = _terminal_sources.find (item_key);
        if (found == _terminal_sources.end ())
            return raw_relocation_replay_result_t::not_registered;
        auto &state = found->second;
        const auto &registration = state.registration;
        if (relay.relocation != registration.relocation
            || relay.coordinator != registration.coordinator
            || relay.operation != registration.operation
            || relay.target_attempt_generation
                 != registration.target_attempt_generation
            || relay.participant_id != registration.participant_id
            || relay.sequence != registration.sequence
            || relay.reply_route_id != registration.reply_route_id
            || record.source_routing_id
                 != registration.target_node_routing_id
            || record.source_node_generation
                 != registration.target_node_generation)
            return raw_relocation_replay_result_t::stale_fence;
        if (state.completed) {
            if (state.digest != digest)
                return raw_relocation_replay_result_t::conflicting_duplicate;
            duplicate = true;
        }
        else if (state.completing) {
            return raw_relocation_replay_result_t::sequence_gap;
        }
        else {
            state.completing = true;
            complete = registration.complete;
        }
        request_source = registration.request_source;
    }

    if (!duplicate) {
        bool persisted = false;
        try {
            persisted = complete (relay, reply);
        }
        catch (...) {
            persisted = false;
        }
        std::lock_guard lock (_gate);
        const auto found = _terminal_sources.find (item_key);
        if (found == _terminal_sources.end ())
            return raw_relocation_replay_result_t::not_registered;
        found->second.completing = false;
        if (!persisted)
            return raw_relocation_replay_result_t::persistence_failed;
        found->second.completed = true;
        found->second.digest = digest;
        found->second.completed_at = clock_t::now ();
    }

    const protocol::reply_relay_ack_t ack{
      relay.relocation,
      relay.coordinator,
      relay.operation,
      request_source,
      duplicate
        ? protocol::reply_relay_ack_status_t::already_terminal
        : protocol::reply_relay_ack_status_t::terminal_received};
    try {
        if (!_transport->send_reply_relay_ack (
              record.source_routing_id, ack))
            return raw_relocation_replay_result_t::transport_failed;
    }
    catch (...) {
        return raw_relocation_replay_result_t::transport_failed;
    }
    return duplicate
      ? raw_relocation_replay_result_t::terminal_duplicate
      : raw_relocation_replay_result_t::terminal_received;
}

raw_relocation_replay_result_t
raw_relocation_replay_coordinator_t::process_reply_relay_ack (
  const mesh::service_mailbox_record_t &record,
  const protocol::reply_relay_ack_t &ack)
{
    if (record.parts.size () != 1)
        return raw_relocation_replay_result_t::invalid;
    const auto item_key = terminal_key (
      ack.relocation, ack.operation);
    std::function<bool (protocol::reply_relay_ack_status_t)> persist;
    {
        std::lock_guard lock (_gate);
        const auto found = _terminal_targets.find (item_key);
        if (found == _terminal_targets.end ())
            return raw_relocation_replay_result_t::not_registered;
        auto &state = found->second;
        const auto &registration = state.registration;
        if (ack.relocation != registration.relay.relocation
            || ack.coordinator != registration.relay.coordinator
            || ack.operation != registration.relay.operation
            || ack.request_source != registration.request_source
            || record.source_routing_id
                 != registration.request_source.node_routing_id
            || record.source_node_generation
                 != registration.request_source.node_generation)
            return raw_relocation_replay_result_t::stale_fence;
        if (state.acknowledging)
            return raw_relocation_replay_result_t::ack_ignored;
        state.acknowledging = true;
        persist = registration.persist_ack;
    }

    bool persisted = false;
    try {
        persisted = persist (ack.status);
    }
    catch (...) {
        persisted = false;
    }
    std::lock_guard lock (_gate);
    const auto found = _terminal_targets.find (item_key);
    if (found == _terminal_targets.end ())
        return raw_relocation_replay_result_t::not_registered;
    found->second.acknowledging = false;
    if (!persisted)
        return raw_relocation_replay_result_t::persistence_failed;
    _terminal_retained_bytes -= found->second.retained_bytes;
    _terminal_targets.erase (found);
    return raw_relocation_replay_result_t::relay_acknowledged;
}

bool raw_relocation_replay_coordinator_t::confirm_terminal_source_lease_expired (
  const protocol::relocation_id_t &relocation,
  const protocol::wire_operation_id_t &operation,
  const protocol::request_source_fence_t &exact_source)
{
    const auto item_key = terminal_key (relocation, operation);
    std::function<bool ()> persist;
    {
        std::lock_guard lock (_gate);
        const auto found = _terminal_targets.find (item_key);
        if (found == _terminal_targets.end ()
            || found->second.registration.request_source
                 != exact_source
            || found->second.acknowledging)
            return false;
        found->second.acknowledging = true;
        persist =
          found->second.registration.persist_source_lease_expiry;
    }
    bool persisted = false;
    try {
        persisted = persist ();
    }
    catch (...) {
        persisted = false;
    }
    std::lock_guard lock (_gate);
    const auto found = _terminal_targets.find (item_key);
    if (found == _terminal_targets.end ())
        return false;
    found->second.acknowledging = false;
    if (!persisted)
        return false;
    _terminal_retained_bytes -= found->second.retained_bytes;
    _terminal_targets.erase (found);
    return true;
}

std::size_t raw_relocation_replay_coordinator_t::reap_terminal_tombstones (
  clock_t::time_point now)
{
    std::lock_guard lock (_gate);
    std::size_t removed = 0;
    for (auto iterator = _terminal_sources.begin ();
         iterator != _terminal_sources.end ();) {
        if (iterator->second.completed
            && iterator->second.completed_at
                 + _terminal_tombstone_retention <= now) {
            iterator = _terminal_sources.erase (iterator);
            ++removed;
        }
        else {
            ++iterator;
        }
    }
    return removed;
}

std::size_t
raw_relocation_replay_coordinator_t::pending_terminal_relays () const
{
    std::lock_guard lock (_gate);
    return _terminal_targets.size ();
}

std::size_t
raw_relocation_replay_coordinator_t::terminal_retained_bytes () const
{
    std::lock_guard lock (_gate);
    return _terminal_retained_bytes;
}


} // namespace zlink::framework::runtime::stateful
