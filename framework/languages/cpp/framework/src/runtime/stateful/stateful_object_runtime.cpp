/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/stateful/stateful_object_runtime.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace zlink::framework::runtime::stateful
{
namespace
{

constexpr std::size_t max_creation_request_bytes = 1024u * 1024u;
constexpr std::size_t max_restored_timers = 4096;

std::uint64_t stable_hash (const std::string &value)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto byte : value) {
        hash ^= static_cast<unsigned char> (byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

} // namespace

bool stateful_object_runtime_t::object_key_t::operator< (
  const object_key_t &other) const noexcept
{
    return std::tie (kind, key) < std::tie (other.kind, other.key);
}

stateful_object_runtime_t::stateful_object_runtime_t (
  std::size_t application_capacity,
  std::size_t infrastructure_capacity) :
    _application_capacity (application_capacity),
    _infrastructure_capacity (infrastructure_capacity)
{
    if (_application_capacity == 0 || _infrastructure_capacity == 0) {
        throw std::invalid_argument ("stateful turn capacity is zero");
    }
}

void stateful_object_runtime_t::configure_relocation_state (
  relocation_state_capture_t capture,
  relocation_state_restore_t restore)
{
    if (!capture || !restore)
        throw std::invalid_argument (
          "Relocation state callbacks must not be empty");
    std::lock_guard lock (_mutex);
    _relocation_state_capture = std::move (capture);
    _relocation_state_restore = std::move (restore);
}

void stateful_object_runtime_t::replace_placement_candidates (
  std::vector<placement_candidate_t> candidates)
{
    if (std::any_of (
          candidates.begin (), candidates.end (),
          [] (const placement_candidate_t &candidate) {
              return candidate.weight < 0
                     || candidate.weight > 10000;
          }))
        throw std::invalid_argument (
          "placement weight must be in range 0..10000");
    std::lock_guard lock (_mutex);
    _candidates = std::move (candidates);
}

create_result_t stateful_object_runtime_t::begin_create (
  const create_request_t &request)
{
    std::lock_guard lock (_mutex);
    if (!valid_text (request.key) || !valid_text (request.stable_type)
        || request.creation_request.size () > max_creation_request_bytes) {
        return {create_status_t::failed, stateful_error_t::invalid, 0, {}, false};
    }
    if (_maintenance_inventory_active) {
        return {
          create_status_t::failed, stateful_error_t::moving, 0, {}, false};
    }
    if (request.kind == object_kind_t::instance_spot
        && !request.instance_intent) {
        return {create_status_t::failed,
                stateful_error_t::instance_manager_create_forbidden,
                0,
                {},
                false};
    }
    if (request.kind != object_kind_t::instance_spot
        && request.instance_intent) {
        return {create_status_t::failed, stateful_error_t::invalid, 0, {}, false};
    }

    const object_key_t key{request.kind, request.key};
    const auto existing = _objects.find (key);
    if (existing != _objects.end ()) {
        const auto &record = existing->second;
        if (record.stable_type != request.stable_type) {
            return {create_status_t::failed,
                    stateful_error_t::type_mismatch,
                    0,
                    {},
                    false};
        }
        if (record.state == object_state_t::creating) {
            return {create_status_t::joined,
                    stateful_error_t::none,
                    record.attempt,
                    record.reference,
                    false};
        }
        if (record.state == object_state_t::moving
            || record.state == object_state_t::recovering) {
            return {create_status_t::failed,
                    stateful_error_t::moving,
                    0,
                    {},
                    false};
        }
        if (request.exclusive) {
            return {create_status_t::failed,
                    stateful_error_t::already_exists,
                    0,
                    {},
                    false};
        }
        return {create_status_t::existing,
                stateful_error_t::none,
                0,
                record.reference,
                false};
    }

    auto candidate = select_candidate_locked (request);
    if (!candidate) {
        return {create_status_t::failed,
                stateful_error_t::backpressured,
                0,
                {},
                false};
    }
    const auto generation = ++_last_generation[key];
    const auto attempt = _next_attempt++;
    if (generation == 0 || generation > std::numeric_limits<std::int64_t>::max ()
        || attempt == 0) {
        return {create_status_t::failed, stateful_error_t::invalid, 0, {}, false};
    }
    object_record_t record;
    record.reference = {request.kind,
                        request.key,
                        generation,
                        1,
                        candidate->mesh_name,
                        candidate->node_id};
    record.stable_type = request.stable_type;
    record.attempt = attempt;
    if (request.kind == object_kind_t::actor) {
        record.membership = "entry:" + candidate->node_id;
    }
    _objects.emplace (key, record);
    _attempts.emplace (attempt, key);
    for (auto &entry : _candidates) {
        if (entry.mesh_name == candidate->mesh_name
            && entry.node_id == candidate->node_id) {
            ++entry.pending_count;
            break;
        }
    }
    return {create_status_t::reserved,
            stateful_error_t::none,
            attempt,
            record.reference,
            true};
}

create_result_t stateful_object_runtime_t::begin_reserved_user_spot (
  const object_ref_t &reserved,
  const std::string &stable_type,
  std::vector<std::uint8_t> creation_request)
{
    std::lock_guard lock (_mutex);
    if (reserved.kind != object_kind_t::user_spot
        || !valid_text (reserved.key) || !valid_text (stable_type)
        || reserved.object_generation == 0
        || reserved.authority_owner_generation == 0
        || !valid_text (reserved.mesh_name)
        || !valid_text (reserved.node_id)
        || creation_request.size () > max_creation_request_bytes) {
        return {
          create_status_t::failed, stateful_error_t::invalid, 0, {}, false};
    }
    if (_maintenance_inventory_active) {
        return {
          create_status_t::failed, stateful_error_t::moving, 0, {}, false};
    }
    const object_key_t key{reserved.kind, reserved.key};
    const auto existing = _objects.find (key);
    if (existing != _objects.end ()) {
        if (existing->second.stable_type != stable_type) {
            return {create_status_t::failed,
                    stateful_error_t::type_mismatch, 0, {}, false};
        }
        if (existing->second.reference.object_generation
              != reserved.object_generation
            || existing->second.reference.authority_owner_generation
                 != reserved.authority_owner_generation) {
            return {create_status_t::failed,
                    stateful_error_t::generation_stale, 0, {}, false};
        }
        if (existing->second.state == object_state_t::creating) {
            return {create_status_t::joined, stateful_error_t::none,
                    existing->second.attempt,
                    existing->second.reference, false};
        }
        if (existing->second.state == object_state_t::moving
            || existing->second.state == object_state_t::recovering
            || existing->second.state == object_state_t::closing) {
            return {create_status_t::failed,
                    stateful_error_t::moving, 0, {}, false};
        }
        return {create_status_t::existing, stateful_error_t::none,
                0, existing->second.reference, false};
    }
    const auto attempt = _next_attempt++;
    if (attempt == 0) {
        return {
          create_status_t::failed, stateful_error_t::invalid, 0, {}, false};
    }
    object_record_t record;
    record.reference = reserved;
    record.stable_type = stable_type;
    record.attempt = attempt;
    _last_generation[key] =
      std::max (_last_generation[key], reserved.object_generation);
    _objects.emplace (key, record);
    _attempts.emplace (attempt, key);
    return {create_status_t::reserved, stateful_error_t::none,
            attempt, reserved, true};
}

stateful_error_t stateful_object_runtime_t::commit_create (
  std::uint64_t attempt)
{
    std::lock_guard lock (_mutex);
    const auto attempt_entry = _attempts.find (attempt);
    if (attempt_entry == _attempts.end ()) {
        return stateful_error_t::conflict;
    }
    auto record = _objects.find (attempt_entry->second);
    if (record == _objects.end ()
        || record->second.state != object_state_t::creating
        || record->second.attempt != attempt) {
        return stateful_error_t::conflict;
    }
    release_pending_capacity_locked (record->second);
    for (auto &candidate : _candidates) {
        if (candidate.mesh_name == record->second.reference.mesh_name
            && candidate.node_id == record->second.reference.node_id) {
            ++candidate.active_count;
            break;
        }
    }
    record->second.state = object_state_t::ready;
    record->second.attempt = 0;
    _attempts.erase (attempt_entry);
    return stateful_error_t::none;
}

stateful_error_t stateful_object_runtime_t::abort_create (
  std::uint64_t attempt)
{
    std::lock_guard lock (_mutex);
    const auto attempt_entry = _attempts.find (attempt);
    if (attempt_entry == _attempts.end ()) {
        return stateful_error_t::conflict;
    }
    const auto record = _objects.find (attempt_entry->second);
    if (record == _objects.end ()
        || record->second.state != object_state_t::creating
        || record->second.attempt != attempt) {
        return stateful_error_t::conflict;
    }
    release_pending_capacity_locked (record->second);
    _objects.erase (record);
    _attempts.erase (attempt_entry);
    return stateful_error_t::none;
}

create_result_t stateful_object_runtime_t::activate_instance (
  create_request_t request,
  const std::function<bool (const object_ref_t &)> &factory)
{
    request.kind = object_kind_t::instance_spot;
    request.instance_intent = true;
    auto result = begin_create (request);
    if (!result.factory_owner) {
        return result;
    }
    bool activated = false;
    try {
        activated = factory && factory (result.object);
    }
    catch (...) {
        activated = false;
    }
    if (!activated) {
        (void) abort_create (result.attempt);
        result.status = create_status_t::failed;
        result.error = stateful_error_t::conflict;
        result.factory_owner = false;
        return result;
    }
    const auto committed = commit_create (result.attempt);
    if (committed != stateful_error_t::none) {
        result.status = create_status_t::failed;
        result.error = committed;
        result.factory_owner = false;
        return result;
    }
    result.object = *find (object_kind_t::instance_spot, request.key);
    return result;
}

std::optional<object_ref_t> stateful_object_runtime_t::find (
  object_kind_t kind,
  const std::string &key) const
{
    std::lock_guard lock (_mutex);
    const auto record = _objects.find ({kind, key});
    if (record == _objects.end ()
        || record->second.state != object_state_t::ready) {
        return std::nullopt;
    }
    return record->second.reference;
}

std::pair<stateful_error_t, membership_token_t>
stateful_object_runtime_t::begin_membership_move (
  const object_ref_t &actor,
  const object_ref_t &target_spot)
{
    std::lock_guard lock (_mutex);
    if (_maintenance_inventory_active)
        return {stateful_error_t::moving, {}};
    stateful_error_t actor_error = stateful_error_t::none;
    auto *actor_record = find_record_locked (actor, actor_error);
    if (actor_record == nullptr) {
        return {actor_error, {}};
    }
    stateful_error_t spot_error = stateful_error_t::none;
    auto *spot_record = find_record_locked (target_spot, spot_error);
    if (spot_record == nullptr) {
        return {spot_error, {}};
    }
    if (actor.kind != object_kind_t::actor
        || target_spot.kind != object_kind_t::user_spot) {
        return {stateful_error_t::invalid, {}};
    }
    if (actor_record->state == object_state_t::moving) {
        return {stateful_error_t::moving, {}};
    }
    if (actor_record->queue.application_active) {
        return {stateful_error_t::conflict, {}};
    }
    if (actor_record->state != object_state_t::ready
        || spot_record->state != object_state_t::ready) {
        return {stateful_error_t::conflict, {}};
    }
    membership_token_t token{
      _next_membership_token++, actor_record->reference,
      spot_record->reference};
    actor_record->state = object_state_t::moving;
    _membership_moves.emplace (
      token.value,
      membership_move_t{token, actor_record->membership});
    return {stateful_error_t::none, token};
}

std::pair<stateful_error_t, membership_token_t>
stateful_object_runtime_t::begin_remote_membership_move (
  const object_ref_t &actor,
  object_ref_t target_spot)
{
    std::lock_guard lock (_mutex);
    if (_maintenance_inventory_active)
        return {stateful_error_t::moving, {}};
    stateful_error_t actor_error = stateful_error_t::none;
    auto *actor_record =
      find_record_locked (actor, actor_error);
    if (actor_record == nullptr)
        return {actor_error, {}};
    if (actor.kind != object_kind_t::actor
        || target_spot.kind != object_kind_t::user_spot
        || target_spot.key.empty ()
        || target_spot.object_generation == 0
        || target_spot.mesh_name.empty ()
        || target_spot.node_id.empty ()
        || target_spot.node_id
             == actor_record->reference.node_id)
        return {stateful_error_t::invalid, {}};
    if (actor_record->state == object_state_t::moving)
        return {stateful_error_t::moving, {}};
    if (actor_record->queue.application_active
        || actor_record->state != object_state_t::ready)
        return {stateful_error_t::conflict, {}};
    membership_token_t token{
      _next_membership_token++,
      actor_record->reference,
      std::move (target_spot)};
    actor_record->state = object_state_t::moving;
    _membership_moves.emplace (
      token.value,
      membership_move_t{
        token, actor_record->membership});
    return {stateful_error_t::none, token};
}

std::pair<stateful_error_t, object_ref_t>
stateful_object_runtime_t::commit_membership_move (
  const membership_token_t &token)
{
    std::lock_guard lock (_mutex);
    const auto move = _membership_moves.find (token.value);
    if (move == _membership_moves.end ()
        || move->second.token != token) {
        return {stateful_error_t::conflict, {}};
    }
    stateful_error_t actor_error = stateful_error_t::none;
    auto *actor_record = find_record_locked (token.actor, actor_error);
    const bool remote_target =
      token.target_spot.node_id
      != token.actor.node_id;
    stateful_error_t spot_error =
      stateful_error_t::none;
    auto *spot_record =
      remote_target
        ? nullptr
        : find_record_locked (
            token.target_spot, spot_error);
    if (actor_record == nullptr
        || actor_record->state
             != object_state_t::moving
        || (!remote_target
            && (spot_record == nullptr
                || spot_record->state
                     != object_state_t::ready))) {
        return {
          actor_record == nullptr
            ? actor_error
            : spot_error,
          {}};
    }
    actor_record->membership = token.target_spot.key;
    if (actor_record->reference.node_id != token.target_spot.node_id
        || actor_record->reference.mesh_name != token.target_spot.mesh_name) {
        actor_record->reference.node_id = token.target_spot.node_id;
        actor_record->reference.mesh_name = token.target_spot.mesh_name;
        ++actor_record->reference.authority_owner_generation;
    }
    actor_record->state = object_state_t::ready;
    while (!actor_record->queue.held_application.empty ()) {
        actor_record->queue.application.push_back (
          std::move (actor_record->queue.held_application.front ()));
        actor_record->queue.held_application.pop_front ();
    }
    const auto current = actor_record->reference;
    _membership_moves.erase (move);
    return {stateful_error_t::none, current};
}

stateful_error_t stateful_object_runtime_t::abort_membership_move (
  const membership_token_t &token)
{
    std::lock_guard lock (_mutex);
    const auto move = _membership_moves.find (token.value);
    if (move == _membership_moves.end ()
        || move->second.token != token) {
        return stateful_error_t::conflict;
    }
    const auto actor_entry = _objects.find (key_for (token.actor));
    if (actor_entry == _objects.end ()
        || !same_exact_ref (actor_entry->second.reference, token.actor)
        || actor_entry->second.state != object_state_t::moving) {
        return stateful_error_t::generation_stale;
    }
    actor_entry->second.membership = move->second.previous_membership;
    actor_entry->second.state = object_state_t::ready;
    while (!actor_entry->second.queue.held_application.empty ()) {
        actor_entry->second.queue.application.push_back (
          std::move (
            actor_entry->second.queue.held_application.front ()));
        actor_entry->second.queue.held_application.pop_front ();
    }
    _membership_moves.erase (move);
    return stateful_error_t::none;
}

std::optional<std::string> stateful_object_runtime_t::actor_membership (
  const object_ref_t &actor) const
{
    std::lock_guard lock (_mutex);
    stateful_error_t error = stateful_error_t::none;
    const auto *record = find_record_locked (actor, error);
    if (record == nullptr || actor.kind != object_kind_t::actor) {
        return std::nullopt;
    }
    return record->membership;
}

stateful_error_t stateful_object_runtime_t::destroy_actor (
  const object_ref_t &actor)
{
    std::lock_guard lock (_mutex);
    if (_maintenance_inventory_active)
        return stateful_error_t::moving;
    stateful_error_t error = stateful_error_t::none;
    auto *record = find_record_locked (actor, error);
    if (record == nullptr) {
        return error;
    }
    if (actor.kind != object_kind_t::actor) {
        return stateful_error_t::invalid;
    }
    if (record->state == object_state_t::moving
        || record->state == object_state_t::recovering) {
        return stateful_error_t::moving;
    }
    if (record->membership.rfind ("entry:", 0) != 0) {
        return stateful_error_t::conflict;
    }
    for (auto &candidate : _candidates) {
        if (candidate.mesh_name == record->reference.mesh_name
            && candidate.node_id == record->reference.node_id
            && candidate.active_count != 0) {
            --candidate.active_count;
            break;
        }
    }
    _objects.erase (key_for (actor));
    return stateful_error_t::none;
}

std::pair<stateful_error_t, bool>
stateful_object_runtime_t::close_spot (const object_ref_t &spot)
{
    const auto [error, token] = begin_close_spot (spot);
    if (error != stateful_error_t::none || !token)
        return {error, false};
    const auto committed = commit_close_spot (*token);
    return {committed, committed == stateful_error_t::none};
}

std::pair<stateful_error_t, std::optional<spot_close_token_t>>
stateful_object_runtime_t::begin_close_spot (const object_ref_t &spot)
{
    std::unique_lock lock (_mutex);
    if (_maintenance_inventory_active)
        return {stateful_error_t::moving, std::nullopt};
    stateful_error_t error = stateful_error_t::none;
    auto *record = find_record_locked (spot, error);
    if (record == nullptr)
        return {error, std::nullopt};
    if (spot.kind == object_kind_t::actor)
        return {stateful_error_t::invalid, std::nullopt};
    if (record->state == object_state_t::moving
        || record->state == object_state_t::recovering
        || record->state == object_state_t::closing)
        return {stateful_error_t::moving, std::nullopt};
    if (spot.kind == object_kind_t::user_spot) {
        for (const auto &[key, candidate] : _objects) {
            (void) key;
            if (candidate.reference.kind == object_kind_t::actor
                && candidate.membership == spot.key)
                return {stateful_error_t::none, std::nullopt};
        }
    }
    if (_next_spot_close_token == 0)
        return {stateful_error_t::invalid, std::nullopt};
    spot_close_token_t token{_next_spot_close_token++, spot};
    record->state = object_state_t::closing;
    record->barrier_generation = token.value;
    _spot_closes.emplace (token.value, token);
    _quiescence.wait (lock, [record] {
        return !record->queue.application_active
               && !record->queue.infrastructure_active
               && !record->queue.yielded_continuation;
    });
    return {stateful_error_t::none, token};
}

stateful_error_t stateful_object_runtime_t::commit_close_spot (
  const spot_close_token_t &token)
{
    std::lock_guard lock (_mutex);
    const auto closing = _spot_closes.find (token.value);
    const auto record = _objects.find (key_for (token.spot));
    if (closing == _spot_closes.end () || closing->second != token
        || record == _objects.end ()
        || !same_exact_ref (record->second.reference, token.spot)
        || record->second.state != object_state_t::closing
        || record->second.barrier_generation != token.value)
        return stateful_error_t::generation_stale;
    for (auto &candidate : _candidates) {
        if (candidate.mesh_name == record->second.reference.mesh_name
            && candidate.node_id == record->second.reference.node_id
            && candidate.active_count != 0) {
            --candidate.active_count;
            break;
        }
    }
    _objects.erase (record);
    _spot_closes.erase (closing);
    return stateful_error_t::none;
}

stateful_error_t stateful_object_runtime_t::abort_close_spot (
  const spot_close_token_t &token)
{
    std::lock_guard lock (_mutex);
    const auto closing = _spot_closes.find (token.value);
    const auto record = _objects.find (key_for (token.spot));
    if (closing == _spot_closes.end () || closing->second != token
        || record == _objects.end ()
        || !same_exact_ref (record->second.reference, token.spot)
        || record->second.state != object_state_t::closing
        || record->second.barrier_generation != token.value)
        return stateful_error_t::generation_stale;
    while (!record->second.queue.held_application.empty ()) {
        record->second.queue.application.push_back (
          std::move (record->second.queue.held_application.front ()));
        record->second.queue.held_application.pop_front ();
    }
    record->second.state = object_state_t::ready;
    record->second.barrier_generation = 0;
    _spot_closes.erase (closing);
    return stateful_error_t::none;
}

stateful_error_t stateful_object_runtime_t::enqueue (
  const object_ref_t &owner,
  turn_domain_t domain,
  turn_record_t record)
{
    std::lock_guard lock (_mutex);
    stateful_error_t error = stateful_error_t::none;
    auto *object = find_record_locked (owner, error);
    if (object == nullptr) {
        return error;
    }
    if ((object->state == object_state_t::moving
         || object->state == object_state_t::recovering
         || object->state == object_state_t::closing)
        && domain == turn_domain_t::application) {
        if (object->queue.held_application.size ()
            >= _application_capacity) {
            return stateful_error_t::backpressured;
        }
        object->queue.held_application.push_back (std::move (record));
        return stateful_error_t::none;
    }
    auto &queue = domain == turn_domain_t::application
                    ? object->queue.application
                    : object->queue.infrastructure;
    const auto capacity = domain == turn_domain_t::application
                            ? _application_capacity
                            : _infrastructure_capacity;
    if (queue.size () >= capacity) {
        return stateful_error_t::backpressured;
    }
    queue.push_back (std::move (record));
    return stateful_error_t::none;
}

std::pair<stateful_error_t, std::optional<turn_record_t>>
stateful_object_runtime_t::try_claim (
  const object_ref_t &owner,
  turn_domain_t domain)
{
    std::lock_guard lock (_mutex);
    stateful_error_t error = stateful_error_t::none;
    auto *object = find_record_locked (owner, error);
    if (object == nullptr) {
        return {error, std::nullopt};
    }
    if (domain == turn_domain_t::application
        && object->queue.application_active
        && object->queue.yielded_continuation) {
        auto continuation =
          std::move (*object->queue.yielded_continuation);
        object->queue.yielded_continuation.reset ();
        return {stateful_error_t::none, std::move (continuation)};
    }
    if ((object->state == object_state_t::moving
         || object->state == object_state_t::recovering
         || object->state == object_state_t::closing)) {
        return {stateful_error_t::moving, std::nullopt};
    }
    auto &queue = domain == turn_domain_t::application
                    ? object->queue.application
                    : object->queue.infrastructure;
    auto &active = domain == turn_domain_t::application
                     ? object->queue.application_active
                     : object->queue.infrastructure_active;
    if (active || queue.empty ()) {
        return {stateful_error_t::none, std::nullopt};
    }
    auto record = std::move (queue.front ());
    queue.pop_front ();
    active = true;
    return {stateful_error_t::none, std::move (record)};
}

stateful_error_t stateful_object_runtime_t::complete_claim (
  const object_ref_t &owner,
  turn_domain_t domain)
{
    std::lock_guard lock (_mutex);
    stateful_error_t error = stateful_error_t::none;
    auto *object = find_record_locked (owner, error);
    if (object == nullptr) {
        return error;
    }
    auto &active = domain == turn_domain_t::application
                     ? object->queue.application_active
                     : object->queue.infrastructure_active;
    if (!active) {
        return stateful_error_t::conflict;
    }
    active = false;
    _quiescence.notify_all ();
    return stateful_error_t::none;
}

stateful_error_t stateful_object_runtime_t::yield_claim (
  const object_ref_t &owner,
  turn_record_t continuation)
{
    std::lock_guard lock (_mutex);
    stateful_error_t error = stateful_error_t::none;
    auto *object = find_record_locked (owner, error);
    if (object == nullptr) {
        return error;
    }
    if (object->state == object_state_t::recovering)
        return stateful_error_t::moving;
    if (!object->queue.application_active) {
        return stateful_error_t::conflict;
    }
    if (object->queue.yielded_continuation)
        return stateful_error_t::conflict;
    object->queue.yielded_continuation = std::move (continuation);
    _quiescence.notify_all ();
    return stateful_error_t::none;
}

std::size_t stateful_object_runtime_t::pending (
  const object_ref_t &owner,
  turn_domain_t domain) const
{
    std::lock_guard lock (_mutex);
    stateful_error_t error = stateful_error_t::none;
    const auto *object = find_record_locked (owner, error);
    if (object == nullptr) {
        return 0;
    }
    return domain == turn_domain_t::application
             ? object->queue.application.size ()
                 + object->queue.held_application.size ()
             : object->queue.infrastructure.size ();
}

stateful_error_t stateful_object_runtime_t::register_timer (
  const object_ref_t &owner,
  logical_timer_t timer)
{
    if (timer.timer_id == 0 || timer.due_after_milliseconds == 0
        || timer.next_tick_sequence == 0) {
        return stateful_error_t::invalid;
    }
    std::lock_guard lock (_mutex);
    stateful_error_t error = stateful_error_t::none;
    auto *object = find_record_locked (owner, error);
    if (object == nullptr) {
        return error;
    }
    if ((object->state == object_state_t::moving
         && object->barrier_generation != 0)
        || object->state == object_state_t::recovering
        || object->state == object_state_t::closing)
        return stateful_error_t::moving;
    if (!object->timers.emplace (timer.timer_id, timer).second) {
        return stateful_error_t::conflict;
    }
    return stateful_error_t::none;
}

stateful_error_t stateful_object_runtime_t::cancel_timer (
  const object_ref_t &owner,
  std::uint64_t timer_id)
{
    std::lock_guard lock (_mutex);
    stateful_error_t error = stateful_error_t::none;
    auto *object = find_record_locked (owner, error);
    if (object == nullptr) {
        return error;
    }
    if ((object->state == object_state_t::moving
         && object->barrier_generation != 0)
        || object->state == object_state_t::recovering
        || object->state == object_state_t::closing)
        return stateful_error_t::moving;
    return object->timers.erase (timer_id) == 1
             ? stateful_error_t::none
             : stateful_error_t::not_found;
}

stateful_error_t stateful_object_runtime_t::enqueue_timer_tick (
  const object_ref_t &owner,
  std::uint64_t timer_id,
  std::vector<std::uint8_t> payload)
{
    std::lock_guard lock (_mutex);
    stateful_error_t error = stateful_error_t::none;
    auto *object = find_record_locked (owner, error);
    if (object == nullptr) {
        return error;
    }
    if ((object->state == object_state_t::moving
         && object->barrier_generation != 0)
        || object->state == object_state_t::recovering
        || object->state == object_state_t::closing)
        return stateful_error_t::moving;
    const auto timer = object->timers.find (timer_id);
    if (timer == object->timers.end ()) {
        return stateful_error_t::not_found;
    }
    auto &queue = object->state == object_state_t::moving
                    ? object->queue.held_application
                    : object->queue.application;
    if (queue.size () >= _application_capacity) {
        return stateful_error_t::backpressured;
    }
    queue.push_back (
      {timer->second.next_tick_sequence++, std::move (payload)});
    return stateful_error_t::none;
}

std::vector<logical_timer_t> stateful_object_runtime_t::timers (
  const object_ref_t &owner) const
{
    std::lock_guard lock (_mutex);
    stateful_error_t error = stateful_error_t::none;
    const auto *object = find_record_locked (owner, error);
    if (object == nullptr) {
        return {};
    }
    std::vector<logical_timer_t> result;
    result.reserve (object->timers.size ());
    for (const auto &[id, timer] : object->timers) {
        (void) id;
        result.push_back (timer);
    }
    return result;
}

std::vector<object_inventory_t>
stateful_object_runtime_t::inventory () const
{
    std::lock_guard lock (_mutex);
    std::vector<object_inventory_t> result;
    result.reserve (_objects.size ());
    for (const auto &[_, object] : _objects) {
        result.push_back (
          {.owner = object.reference,
           .stable_type = object.stable_type,
           .state = object.state,
           .membership = object.membership});
    }
    return result;
}

std::optional<std::vector<object_inventory_t>>
stateful_object_runtime_t::try_begin_maintenance_inventory ()
{
    std::lock_guard lock (_mutex);
    if (_maintenance_inventory_active)
        return std::nullopt;
    _maintenance_inventory_active = true;
    std::vector<object_inventory_t> result;
    result.reserve (_objects.size ());
    for (const auto &[_, object] : _objects) {
        result.push_back (
          {.owner = object.reference,
           .stable_type = object.stable_type,
           .state = object.state,
           .membership = object.membership});
    }
    return result;
}

void stateful_object_runtime_t::end_maintenance_inventory () noexcept
{
    std::lock_guard lock (_mutex);
    _maintenance_inventory_active = false;
}

std::pair<stateful_error_t, relocation_seal_t>
stateful_object_runtime_t::try_seal_relocation (
  const object_ref_t &owner)
{
    const auto [error, aggregate] =
      try_seal_relocation_aggregate ({owner});
    if (error != stateful_error_t::none
        || aggregate.participants.size () != 1) {
        return {error, {}};
    }
    return {
      stateful_error_t::none,
      {aggregate.token, aggregate.participants.front ()}};
}

std::pair<stateful_error_t, aggregate_relocation_seal_t>
stateful_object_runtime_t::try_seal_relocation_aggregate (
  const std::vector<object_ref_t> &participants)
{
    if (participants.empty ())
        return {stateful_error_t::invalid, {}};
    std::unique_lock lock (_mutex);
    if (_next_relocation_token == 0) {
        return {stateful_error_t::conflict, {}};
    }

    std::vector<object_key_t> keys;
    std::vector<object_record_t *> records;
    keys.reserve (participants.size ());
    records.reserve (participants.size ());
    for (const auto &participant : participants) {
        const auto key = key_for (participant);
        if (std::find (keys.begin (), keys.end (), key) != keys.end ())
            return {stateful_error_t::invalid, {}};
        stateful_error_t error = stateful_error_t::none;
        auto *object = find_record_locked (participant, error);
        if (object == nullptr)
            return {error, {}};
        if (object->state != object_state_t::ready) {
            return {object->state == object_state_t::moving
                      ? stateful_error_t::moving
                      : stateful_error_t::conflict,
                    {}};
        }
        keys.push_back (key);
        records.push_back (object);
    }

    const auto token = _next_relocation_token++;
    std::vector<object_ref_t> sources;
    sources.reserve (records.size ());
    for (auto *object : records) {
        object->state = object_state_t::moving;
        object->barrier_generation = token;
        sources.push_back (object->reference);
    }
    _quiescence.wait (lock, [&records] {
        return std::all_of (
          records.begin (), records.end (), [] (const auto *object) {
              return !object->queue.application_active
                     && !object->queue.infrastructure_active
                     && !object->queue.yielded_continuation;
          });
    });

    std::vector<frozen_object_state_t> frozen_participants;
    frozen_participants.reserve (records.size ());
    try {
      for (auto *object : records) {
        frozen_object_state_t frozen{
          .owner = object->reference,
          .stable_type = object->stable_type,
          .application_state = {},
          .pending_application = {},
          .timers = {}};
        if ((object->reference.kind == object_kind_t::user_spot
             || object->reference.kind == object_kind_t::instance_spot)
            && _relocation_state_capture) {
            frozen.application_state =
              _relocation_state_capture (
                object->reference, object->stable_type);
        }
        frozen.pending_application.reserve (
          object->queue.application.size ());
        while (!object->queue.application.empty ()) {
            frozen.pending_application.push_back (
              std::move (object->queue.application.front ()));
            object->queue.application.pop_front ();
        }
        frozen.timers.reserve (object->timers.size ());
        for (const auto &[_, timer] : object->timers)
            frozen.timers.push_back (timer);
        frozen_participants.push_back (std::move (frozen));
      }
    }
    catch (...) {
        for (auto *object : records) {
            object->state = object_state_t::ready;
            object->barrier_generation = 0;
        }
        return {stateful_error_t::conflict, {}};
    }
    _relocation_seals.emplace (
      token,
      relocation_seal_state_t{
        keys, std::move (sources), frozen_participants});
    return {
      stateful_error_t::none,
      aggregate_relocation_seal_t{
        token, std::move (frozen_participants)}};
}

stateful_error_t stateful_object_runtime_t::abort_relocation (
  std::uint64_t token)
{
    std::lock_guard lock (_mutex);
    const auto seal = _relocation_seals.find (token);
    if (seal == _relocation_seals.end ()) {
        return stateful_error_t::not_found;
    }
    for (std::size_t index = 0; index != seal->second.keys.size (); ++index) {
        const auto object = _objects.find (seal->second.keys[index]);
        if (object == _objects.end ()
            || object->second.state != object_state_t::moving
            || object->second.barrier_generation != token
            || !same_exact_ref (object->second.reference,
                                seal->second.sources[index]))
            return stateful_error_t::conflict;
    }
    for (std::size_t index = 0; index != seal->second.keys.size (); ++index) {
        auto &record = _objects.find (seal->second.keys[index])->second;
        for (auto &pending :
             seal->second.frozen[index].pending_application)
            record.queue.application.push_back (std::move (pending));
        while (!record.queue.held_application.empty ()) {
            record.queue.application.push_back (
              std::move (record.queue.held_application.front ()));
            record.queue.held_application.pop_front ();
        }
        record.state = object_state_t::ready;
        record.barrier_generation = 0;
    }
    _relocation_seals.erase (seal);
    return stateful_error_t::none;
}

std::pair<stateful_error_t, object_ref_t>
stateful_object_runtime_t::commit_relocation (
  std::uint64_t token,
  std::string target_node_id)
{
    auto [error, committed] =
      commit_relocation_aggregate (token, std::move (target_node_id));
    if (error != stateful_error_t::none || committed.size () != 1)
        return {error, {}};
    return {stateful_error_t::none, std::move (committed.front ())};
}

std::pair<stateful_error_t, std::vector<object_ref_t>>
stateful_object_runtime_t::commit_relocation_aggregate (
  std::uint64_t token,
  std::string target_node_id)
{
    if (!valid_text (target_node_id)) {
        return {stateful_error_t::invalid, {}};
    }
    std::lock_guard lock (_mutex);
    const auto seal = _relocation_seals.find (token);
    if (seal == _relocation_seals.end ()) {
        return {stateful_error_t::not_found, {}};
    }
    for (std::size_t index = 0; index != seal->second.keys.size (); ++index) {
        const auto object = _objects.find (seal->second.keys[index]);
        if (object == _objects.end ()
            || object->second.state != object_state_t::moving
            || object->second.barrier_generation != token
            || !same_exact_ref (object->second.reference,
                                seal->second.sources[index])
            || object->second.reference.authority_owner_generation
                 == std::numeric_limits<std::uint64_t>::max ())
            return {stateful_error_t::conflict, {}};
    }
    std::vector<object_ref_t> result;
    result.reserve (seal->second.keys.size ());
    for (std::size_t index = 0; index != seal->second.keys.size (); ++index) {
        auto &record = _objects.find (seal->second.keys[index])->second;
        record.reference.node_id = target_node_id;
        ++record.reference.authority_owner_generation;
        for (auto &pending :
             seal->second.frozen[index].pending_application)
            record.queue.application.push_back (std::move (pending));
        while (!record.queue.held_application.empty ()) {
            record.queue.application.push_back (
              std::move (record.queue.held_application.front ()));
            record.queue.held_application.pop_front ();
        }
        record.state = object_state_t::ready;
        record.barrier_generation = 0;
        result.push_back (record.reference);
    }
    _relocation_seals.erase (seal);
    return {stateful_error_t::none, std::move (result)};
}

stateful_error_t stateful_object_runtime_t::restore_relocation (
  frozen_object_state_t frozen,
  object_ref_t target,
  relocation_restore_identity_t identity)
try
{
    if (!valid_text (frozen.stable_type)
        || frozen.owner.kind != target.kind
        || frozen.owner.key != target.key
        || frozen.owner.object_generation != target.object_generation
        || frozen.owner.mesh_name != target.mesh_name
        || target.authority_owner_generation
             <= frozen.owner.authority_owner_generation
        || !valid_text (target.mesh_name)
        || !valid_text (target.node_id)
        || identity.reference.empty ()
        || frozen.pending_application.size () > _application_capacity
        || frozen.timers.size () > max_restored_timers) {
        return stateful_error_t::invalid;
    }
    std::uint64_t previous_sequence = 0;
    for (const auto &pending : frozen.pending_application) {
        if (pending.sequence == 0 || pending.sequence <= previous_sequence)
            return stateful_error_t::invalid;
        previous_sequence = pending.sequence;
    }
    std::uint64_t previous_timer = 0;
    for (const auto &timer : frozen.timers) {
        if (timer.timer_id == 0 || timer.timer_id <= previous_timer
            || timer.due_after_milliseconds == 0
            || timer.next_tick_sequence == 0) {
            return stateful_error_t::invalid;
        }
        previous_timer = timer.timer_id;
    }
    const auto key = key_for (target);
    bool requires_application_restore = false;
    {
        std::lock_guard lock (_mutex);
        requires_application_restore =
          _objects.find (key) == _objects.end ();
    }
    if (requires_application_restore
        && (target.kind == object_kind_t::user_spot
            || target.kind == object_kind_t::instance_spot)
        && _relocation_state_restore
        && !_relocation_state_restore (frozen, target)) {
        return stateful_error_t::conflict;
    }

    std::lock_guard lock (_mutex);
    const auto existing = _objects.find (key);
    if (existing != _objects.end ()) {
        const auto &record = existing->second;
        if (!same_exact_ref (record.reference, target)
            || record.state != object_state_t::recovering
            || record.restore_identity
                 != std::optional<relocation_restore_identity_t>{identity}
            || record.stable_type != frozen.stable_type
            || !record.membership.empty ()
            || record.queue.application.size ()
                 != frozen.pending_application.size ()
            || record.timers.size () != frozen.timers.size ()) {
            return stateful_error_t::conflict;
        }
        auto pending = record.queue.application.begin ();
        for (const auto &expected : frozen.pending_application) {
            if (*pending++ != expected)
                return stateful_error_t::conflict;
        }
        auto timer = record.timers.begin ();
        for (const auto &expected : frozen.timers) {
            if (timer == record.timers.end ()
                || timer->second != expected) {
                return stateful_error_t::conflict;
            }
            ++timer;
        }
        return stateful_error_t::already_exists;
    }
    const auto last = _last_generation.find (key);
    if (last != _last_generation.end ()
        && last->second >= target.object_generation) {
        return stateful_error_t::generation_stale;
    }

    object_record_t record;
    record.reference = std::move (target);
    record.stable_type = std::move (frozen.stable_type);
    record.state = object_state_t::recovering;
    record.restore_identity = std::move (identity);
    for (auto &pending : frozen.pending_application)
        record.queue.application.push_back (std::move (pending));
    for (auto &timer : frozen.timers)
        record.timers.emplace (timer.timer_id, std::move (timer));

    auto next_objects = _objects;
    auto next_generations = _last_generation;
    next_generations[key] = record.reference.object_generation;
    next_objects.emplace (key, std::move (record));
    _objects.swap (next_objects);
    _last_generation.swap (next_generations);
    return stateful_error_t::none;
}
catch (...)
{
    return stateful_error_t::backpressured;
}

stateful_error_t stateful_object_runtime_t::restore_relocation_aggregate (
  std::vector<frozen_object_state_t> frozen,
  std::vector<object_ref_t> targets,
  relocation_restore_identity_t identity)
try
{
    if (frozen.size () < 2 || frozen.size () != targets.size ()
        || identity.reference.empty ())
        return stateful_error_t::invalid;

    std::sort (
      frozen.begin (), frozen.end (),
      [] (const frozen_object_state_t &left,
          const frozen_object_state_t &right) {
          return key_for (left.owner) < key_for (right.owner);
      });
    std::sort (
      targets.begin (), targets.end (),
      [] (const object_ref_t &left, const object_ref_t &right) {
          return key_for (left) < key_for (right);
      });

    std::optional<std::string> user_spot_key;
    std::size_t actor_count = 0;
    for (std::size_t index = 0; index != frozen.size (); ++index) {
        const auto &source = frozen[index];
        const auto &target = targets[index];
        if (!valid_text (source.stable_type)
            || source.owner.kind != target.kind
            || source.owner.key != target.key
            || source.owner.object_generation != target.object_generation
            || source.owner.mesh_name != target.mesh_name
            || target.authority_owner_generation
                 <= source.owner.authority_owner_generation
            || !valid_text (target.mesh_name)
            || !valid_text (target.node_id)
            || source.pending_application.size ()
                 > _application_capacity
            || source.timers.size () > max_restored_timers
            || (index != 0
                && key_for (targets[index - 1]) == key_for (target))) {
            return stateful_error_t::invalid;
        }
        std::uint64_t previous_sequence = 0;
        for (const auto &pending : source.pending_application) {
            if (pending.sequence == 0
                || pending.sequence <= previous_sequence) {
                return stateful_error_t::invalid;
            }
            previous_sequence = pending.sequence;
        }
        std::uint64_t previous_timer = 0;
        for (const auto &timer : source.timers) {
            if (timer.timer_id == 0 || timer.timer_id <= previous_timer
                || timer.due_after_milliseconds == 0
                || timer.next_tick_sequence == 0) {
                return stateful_error_t::invalid;
            }
            previous_timer = timer.timer_id;
        }
        if (target.kind == object_kind_t::user_spot) {
            if (user_spot_key)
                return stateful_error_t::invalid;
            user_spot_key = target.key;
        } else if (target.kind == object_kind_t::actor)
            ++actor_count;
        else
            return stateful_error_t::invalid;
    }
    if (!user_spot_key || actor_count + 1 != targets.size ())
        return stateful_error_t::invalid;
    bool has_existing_target = false;
    {
        std::lock_guard lock (_mutex);
        has_existing_target =
          std::any_of (
            targets.begin (), targets.end (),
            [&] (const object_ref_t &target) {
                return _objects.contains (key_for (target));
            });
    }
    if (!has_existing_target && _relocation_state_restore) {
        for (std::size_t index = 0; index != frozen.size (); ++index) {
            if ((targets[index].kind == object_kind_t::user_spot
                 || targets[index].kind == object_kind_t::instance_spot)
                && !_relocation_state_restore (
                  frozen[index], targets[index])) {
                return stateful_error_t::conflict;
            }
        }
    }

    std::lock_guard lock (_mutex);
    std::size_t exact_existing = 0;
    for (std::size_t index = 0; index != targets.size (); ++index) {
        const auto &target = targets[index];
        const auto &source = frozen[index];
        const auto key = key_for (target);
        const auto existing = _objects.find (key);
        if (existing != _objects.end ()) {
            const auto &record = existing->second;
            const auto expected_membership =
              target.kind == object_kind_t::actor ? *user_spot_key
                                                  : std::string{};
            if (!same_exact_ref (record.reference, target)
                || record.state != object_state_t::recovering
                || record.restore_identity
                     != std::optional<relocation_restore_identity_t>{
                       identity}
                || record.stable_type != source.stable_type
                || record.membership != expected_membership
                || record.queue.application.size ()
                     != source.pending_application.size ()
                || record.timers.size () != source.timers.size ()) {
                return stateful_error_t::conflict;
            }
            auto pending = record.queue.application.begin ();
            for (const auto &expected : source.pending_application) {
                if (*pending++ != expected)
                    return stateful_error_t::conflict;
            }
            auto timer = record.timers.begin ();
            for (const auto &expected : source.timers) {
                if (timer == record.timers.end ()
                    || timer->second != expected) {
                    return stateful_error_t::conflict;
                }
                ++timer;
            }
            ++exact_existing;
        } else {
            const auto last = _last_generation.find (key);
            if (last != _last_generation.end ()
                && last->second >= target.object_generation) {
                return stateful_error_t::generation_stale;
            }
        }
    }
    if (exact_existing != 0)
        return exact_existing == targets.size ()
                 ? stateful_error_t::already_exists
                 : stateful_error_t::conflict;
    std::vector<std::pair<object_key_t, object_record_t>> records;
    records.reserve (targets.size ());
    for (std::size_t index = 0; index != targets.size (); ++index) {
        object_record_t record;
        record.reference = std::move (targets[index]);
        record.stable_type = std::move (frozen[index].stable_type);
        record.state = object_state_t::recovering;
        record.restore_identity = identity;
        if (record.reference.kind == object_kind_t::actor && user_spot_key)
            record.membership = *user_spot_key;
        for (auto &pending : frozen[index].pending_application)
            record.queue.application.push_back (std::move (pending));
        for (auto &timer : frozen[index].timers) {
            if (timer.timer_id == 0
                || !record.timers.emplace (
                     timer.timer_id, std::move (timer)).second) {
                return stateful_error_t::invalid;
            }
        }
        records.emplace_back (
          key_for (record.reference), std::move (record));
    }

    auto next_objects = _objects;
    auto next_generations = _last_generation;
    for (auto &[key, record] : records) {
        next_generations[key] = record.reference.object_generation;
        next_objects.emplace (key, std::move (record));
    }
    _objects.swap (next_objects);
    _last_generation.swap (next_generations);
    return stateful_error_t::none;
}
catch (...)
{
    return stateful_error_t::backpressured;
}

bool stateful_object_runtime_t::valid_text (const std::string &value)
{
    return !value.empty () && value.size () <= 255;
}

bool stateful_object_runtime_t::same_exact_ref (
  const object_ref_t &left,
  const object_ref_t &right)
{
    return left.kind == right.kind && left.key == right.key
           && left.object_generation == right.object_generation
           && left.authority_owner_generation
                == right.authority_owner_generation
           && left.mesh_name == right.mesh_name
           && left.node_id == right.node_id;
}

stateful_object_runtime_t::object_key_t
stateful_object_runtime_t::key_for (const object_ref_t &reference)
{
    return {reference.kind, reference.key};
}

stateful_object_runtime_t::object_record_t *
stateful_object_runtime_t::find_record_locked (
  const object_ref_t &reference,
  stateful_error_t &error)
{
    const auto entry = _objects.find (key_for (reference));
    if (entry == _objects.end ()) {
        error = stateful_error_t::not_found;
        return nullptr;
    }
    if (entry->second.reference.object_generation
        != reference.object_generation) {
        error = stateful_error_t::generation_stale;
        return nullptr;
    }
    if (!same_exact_ref (entry->second.reference, reference)) {
        error =
          entry->second.reference.authority_owner_generation
              != reference.authority_owner_generation
            ? stateful_error_t::generation_stale
            : stateful_error_t::conflict;
        return nullptr;
    }
    error = stateful_error_t::none;
    return &entry->second;
}

const stateful_object_runtime_t::object_record_t *
stateful_object_runtime_t::find_record_locked (
  const object_ref_t &reference,
  stateful_error_t &error) const
{
    const auto entry = _objects.find (key_for (reference));
    if (entry == _objects.end ()) {
        error = stateful_error_t::not_found;
        return nullptr;
    }
    if (entry->second.reference.object_generation
        != reference.object_generation) {
        error = stateful_error_t::generation_stale;
        return nullptr;
    }
    if (!same_exact_ref (entry->second.reference, reference)) {
        error =
          entry->second.reference.authority_owner_generation
              != reference.authority_owner_generation
            ? stateful_error_t::generation_stale
            : stateful_error_t::conflict;
        return nullptr;
    }
    error = stateful_error_t::none;
    return &entry->second;
}

std::optional<placement_candidate_t>
stateful_object_runtime_t::select_candidate_locked (
  const create_request_t &request) const
{
    std::vector<const placement_candidate_t *> eligible;
    std::uint64_t total_weight = 0;
    for (const auto &candidate : _candidates) {
        if (request.mesh_name
            && candidate.mesh_name != *request.mesh_name) {
            continue;
        }
        if (candidate.weight == 0
            || !candidate.stable_types.contains (request.stable_type)
            || candidate.active_count >= candidate.active_capacity
            || candidate.pending_count >= candidate.pending_capacity) {
            continue;
        }
        eligible.push_back (&candidate);
        total_weight += static_cast<std::uint64_t> (
          candidate.weight);
    }
    if (eligible.empty () || total_weight == 0) {
        return std::nullopt;
    }
    auto selection = stable_hash (request.key) % total_weight;
    for (const auto *candidate : eligible) {
        const auto weight = static_cast<std::uint64_t> (
          candidate->weight);
        if (selection < weight) {
            return *candidate;
        }
        selection -= weight;
    }
    return *eligible.back ();
}

void stateful_object_runtime_t::release_pending_capacity_locked (
  const object_record_t &record)
{
    for (auto &candidate : _candidates) {
        if (candidate.mesh_name == record.reference.mesh_name
            && candidate.node_id == record.reference.node_id
            && candidate.pending_count != 0) {
            --candidate.pending_count;
            break;
        }
    }
}

} // namespace zlink::framework::runtime::stateful
