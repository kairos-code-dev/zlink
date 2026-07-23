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

void stateful_object_runtime_t::replace_placement_candidates (
  std::vector<placement_candidate_t> candidates)
{
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
        if (record.state == object_state_t::moving) {
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
    stateful_error_t spot_error = stateful_error_t::none;
    auto *spot_record =
      find_record_locked (token.target_spot, spot_error);
    if (actor_record == nullptr || spot_record == nullptr
        || actor_record->state != object_state_t::moving
        || spot_record->state != object_state_t::ready) {
        return {actor_record == nullptr ? actor_error : spot_error, {}};
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
    stateful_error_t error = stateful_error_t::none;
    auto *record = find_record_locked (actor, error);
    if (record == nullptr) {
        return error;
    }
    if (actor.kind != object_kind_t::actor) {
        return stateful_error_t::invalid;
    }
    if (record->state == object_state_t::moving) {
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
    std::lock_guard lock (_mutex);
    stateful_error_t error = stateful_error_t::none;
    auto *record = find_record_locked (spot, error);
    if (record == nullptr) {
        return {error, false};
    }
    if (spot.kind == object_kind_t::actor) {
        return {stateful_error_t::invalid, false};
    }
    if (record->state == object_state_t::moving) {
        return {stateful_error_t::moving, false};
    }
    if (spot.kind == object_kind_t::user_spot) {
        for (const auto &[key, candidate] : _objects) {
            (void) key;
            if (candidate.reference.kind == object_kind_t::actor
                && candidate.membership == spot.key) {
                return {stateful_error_t::none, false};
            }
        }
    }
    _objects.erase (key_for (spot));
    return {stateful_error_t::none, true};
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
    if (object->state == object_state_t::moving
        && domain == turn_domain_t::infrastructure) {
        return stateful_error_t::moving;
    }
    if (object->state == object_state_t::moving) {
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
    if (!object->queue.application_active) {
        return stateful_error_t::conflict;
    }
    if (object->queue.application.size () >= _application_capacity) {
        return stateful_error_t::backpressured;
    }
    object->queue.application_active = false;
    object->queue.application.push_back (std::move (continuation));
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
        error = stateful_error_t::conflict;
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
        error = stateful_error_t::conflict;
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
        total_weight += candidate.weight;
    }
    if (eligible.empty () || total_weight == 0) {
        return std::nullopt;
    }
    auto selection = stable_hash (request.key) % total_weight;
    for (const auto *candidate : eligible) {
        if (selection < candidate->weight) {
            return *candidate;
        }
        selection -= candidate->weight;
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
