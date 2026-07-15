/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "actor_transfer_coordinator.hpp"

#include <algorithm>

namespace zlink::framework::detail
{

bool actor_transfer_coordinator_t::try_begin_local (const std::string &actor_key)
{
    std::lock_guard lock (_mutex);
    return _moves.emplace (actor_key, move_state_t{actor_move_phase_t::local, std::string{}})
      .second;
}

bool actor_transfer_coordinator_t::try_begin_source_remote (const std::string &actor_key,
                                                            std::string transfer_id)
{
    std::lock_guard lock (_mutex);
    return _moves
      .emplace (actor_key, move_state_t{actor_move_phase_t::source_remote, std::move (transfer_id),
                                        std::chrono::steady_clock::now ()})
      .second;
}

void actor_transfer_coordinator_t::cancel_move (const std::string &actor_key)
{
    std::lock_guard lock (_mutex);
    _moves.erase (actor_key);
}

void actor_transfer_coordinator_t::mark_reconcile (const std::string &actor_key)
{
    std::lock_guard lock (_mutex);
    auto found = _moves.find (actor_key);
    if (found == _moves.end ()) {
        _moves.emplace (actor_key, move_state_t{actor_move_phase_t::reconcile, std::string{}});
        return;
    }
    found->second.phase = actor_move_phase_t::reconcile;
}

std::optional<std::chrono::steady_clock::duration>
actor_transfer_coordinator_t::complete_move (const std::string &actor_key)
{
    std::lock_guard lock (_mutex);
    const auto found = _moves.find (actor_key);
    if (found == _moves.end ()) {
        return std::nullopt;
    }
    std::optional<std::chrono::steady_clock::duration> elapsed;
    if (found->second.transfer_started_at) {
        elapsed = std::chrono::steady_clock::now () - *found->second.transfer_started_at;
    }
    _moves.erase (found);
    return elapsed;
}

bool actor_transfer_coordinator_t::try_append_backlog (const std::string &actor_key,
                                                       handoff_packet_t packet)
{
    std::lock_guard lock (_mutex);
    const auto moving = _moves.find (actor_key);
    if (moving == _moves.end ()) {
        return false;
    }
    // Only the source side of a move preserves packets; the target side keeps
    // rejecting until the commit installs the actor (§3.4).
    if (moving->second.phase != actor_move_phase_t::local
        && moving->second.phase != actor_move_phase_t::source_remote
        && moving->second.phase != actor_move_phase_t::reconcile) {
        return false;
    }
    auto &backlog = _backlogs[actor_key];
    if (packet.is_request) {
        const auto request_id = packet.metadata.find ("__zlink.actorRequestId");
        if (request_id != packet.metadata.end () && !request_id->second.empty ()) {
            const auto duplicate = std::find_if (
              backlog.begin (), backlog.end (), [&request_id] (const handoff_packet_t &queued) {
                  const auto queued_id = queued.metadata.find ("__zlink.actorRequestId");
                  return queued.is_request && queued_id != queued.metadata.end ()
                         && queued_id->second == request_id->second;
              });
            if (duplicate != backlog.end ()) {
                return false;
            }
        }
    }
    backlog.push_back (std::move (packet));
    return true;
}

std::vector<handoff_packet_t>
actor_transfer_coordinator_t::take_backlog (const std::string &actor_key)
{
    std::lock_guard lock (_mutex);
    const auto found = _backlogs.find (actor_key);
    if (found == _backlogs.end ()) {
        return {};
    }
    auto backlog = std::move (found->second);
    _backlogs.erase (found);
    return backlog;
}

void actor_transfer_coordinator_t::activate_forwarding (
  const std::string &actor_key,
  std::uint64_t old_generation,
  actor_ref_t target_actor,
  spot_route_t target_route,
  std::chrono::steady_clock::time_point evict_at,
  std::string transfer_id)
{
    std::lock_guard lock (_mutex);
    // At most one entry per actor: a re-transfer refreshes the entry toward the
    // new hop and restarts the window instead of accumulating entries (§10.4-4).
    _forwardings[actor_key] = forwarding_entry_t{
      old_generation, std::move (target_actor), std::move (target_route), evict_at,
      std::move (transfer_id)};
}

bool actor_transfer_coordinator_t::forwards_stale_generation (const std::string &actor_key,
                                                              std::uint64_t generation) const
{
    std::lock_guard lock (_mutex);
    const auto found = _forwardings.find (actor_key);
    return found != _forwardings.end () && generation <= found->second.old_generation;
}

std::optional<actor_forwarding_target_t>
actor_transfer_coordinator_t::forwarding_target (const std::string &actor_key,
                                                 std::uint64_t generation) const
{
    std::lock_guard lock (_mutex);
    const auto found = _forwardings.find (actor_key);
    if (found == _forwardings.end () || found->second.old_generation != generation
        || found->second.evict_at <= std::chrono::steady_clock::now ()) {
        return std::nullopt;
    }
    return actor_forwarding_target_t{found->second.target_actor, found->second.target_route};
}

std::vector<evicted_actor_forwarding_t>
actor_transfer_coordinator_t::evict_expired_forwarding (std::chrono::steady_clock::time_point now)
{
    std::lock_guard lock (_mutex);
    std::vector<evicted_actor_forwarding_t> evicted;
    for (auto found = _forwardings.begin (); found != _forwardings.end ();) {
        if (found->second.evict_at <= now) {
            evicted.push_back (
              evicted_actor_forwarding_t{found->first, found->second.old_generation,
                                          found->second.transfer_id});
            found = _forwardings.erase (found);
        } else {
            ++found;
        }
    }
    return evicted;
}

bool actor_transfer_coordinator_t::blocks_dispatch (const std::string &actor_key) const
{
    std::lock_guard lock (_mutex);
    return _moves.contains (actor_key);
}

std::optional<actor_move_phase_t>
actor_transfer_coordinator_t::phase (const std::string &actor_key) const
{
    std::lock_guard lock (_mutex);
    const auto found = _moves.find (actor_key);
    return found == _moves.end () ? std::nullopt : std::make_optional (found->second.phase);
}

std::optional<std::string>
actor_transfer_coordinator_t::transfer_id (const std::string &actor_key) const
{
    std::lock_guard lock (_mutex);
    const auto found = _moves.find (actor_key);
    return found == _moves.end () || found->second.transfer_id.empty ()
             ? std::nullopt
             : std::make_optional (found->second.transfer_id);
}

bool actor_transfer_coordinator_t::try_add_admission (std::string transfer_id,
                                                      pending_actor_admission_t admission)
{
    std::lock_guard lock (_mutex);
    if (_admissions.contains (transfer_id) || _moves.contains (admission.actor_key)) {
        return false;
    }
    const auto actor_key = admission.actor_key;
    _moves.emplace (actor_key, move_state_t{actor_move_phase_t::target_pending, transfer_id});
    _admissions.emplace (std::move (transfer_id), std::move (admission));
    return true;
}

std::optional<pending_actor_admission_t>
actor_transfer_coordinator_t::begin_commit (const std::string &transfer_id,
                                            const actor_ref_t &source_actor,
                                            const spot_rid_t &target_spot_rid)
{
    std::lock_guard lock (_mutex);
    const auto found = _admissions.find (transfer_id);
    if (found == _admissions.end ()) {
        return std::nullopt;
    }
    if (found->second.deadline <= std::chrono::steady_clock::now ()) {
        _moves.erase (found->second.actor_key);
        _admissions.erase (found);
        return std::nullopt;
    }
    if (found->second.source_actor.actor_id () != source_actor.actor_id ()
        || found->second.source_actor.actor_type () != source_actor.actor_type ()
        || found->second.source_actor.generation () != source_actor.generation ()
        || found->second.source_actor.node_rid ().value () != source_actor.node_rid ().value ()
        || found->second.target_spot_rid.value () != target_spot_rid.value ()) {
        return std::nullopt;
    }
    auto moving = _moves.find (found->second.actor_key);
    if (moving == _moves.end () || moving->second.transfer_id != transfer_id
        || moving->second.phase != actor_move_phase_t::target_pending) {
        return std::nullopt;
    }
    moving->second.phase = actor_move_phase_t::target_committing;
    return found->second;
}

std::optional<pending_actor_admission_t>
actor_transfer_coordinator_t::pending_commit (const std::string &transfer_id,
                                              const actor_ref_t &source_actor,
                                              const spot_rid_t &target_spot_rid) const
{
    std::lock_guard lock (_mutex);
    const auto found = _admissions.find (transfer_id);
    if (found == _admissions.end ()) {
        return std::nullopt;
    }
    const auto moving = _moves.find (found->second.actor_key);
    if (moving == _moves.end () || moving->second.transfer_id != transfer_id
        || moving->second.phase != actor_move_phase_t::target_committing
        || found->second.source_actor.actor_id () != source_actor.actor_id ()
        || found->second.source_actor.actor_type () != source_actor.actor_type ()
        || found->second.source_actor.generation () != source_actor.generation ()
        || found->second.source_actor.node_rid ().value () != source_actor.node_rid ().value ()
        || found->second.target_spot_rid.value () != target_spot_rid.value ()) {
        return std::nullopt;
    }
    return found->second;
}

void actor_transfer_coordinator_t::fail_commit (const std::string &transfer_id, bool reconcile)
{
    std::lock_guard lock (_mutex);
    const auto found = _admissions.find (transfer_id);
    if (found == _admissions.end ()) {
        return;
    }
    const auto actor_key = found->second.actor_key;
    _admissions.erase (found);
    if (reconcile) {
        auto &move = _moves[actor_key];
        move.phase = actor_move_phase_t::reconcile;
        move.transfer_id.clear ();
    } else {
        _moves.erase (actor_key);
    }
}

void actor_transfer_coordinator_t::complete_commit (const std::string &transfer_id)
{
    std::lock_guard lock (_mutex);
    const auto found = _admissions.find (transfer_id);
    if (found == _admissions.end ()) {
        return;
    }
    _moves.erase (found->second.actor_key);
    _admissions.erase (found);
}

std::vector<expired_actor_admission_t>
actor_transfer_coordinator_t::cleanup_expired (std::chrono::steady_clock::time_point now)
{
    std::lock_guard lock (_mutex);
    std::vector<expired_actor_admission_t> removed;
    for (auto found = _admissions.begin (); found != _admissions.end ();) {
        const auto moving = _moves.find (found->second.actor_key);
        const bool can_expire =
          moving != _moves.end () && moving->second.phase == actor_move_phase_t::target_pending;
        if (can_expire && found->second.deadline <= now) {
            _moves.erase (moving);
            removed.push_back (expired_actor_admission_t{found->first, found->second});
            found = _admissions.erase (found);
        } else {
            ++found;
        }
    }
    return removed;
}

std::size_t actor_transfer_coordinator_t::pending_count () const
{
    std::lock_guard lock (_mutex);
    return _admissions.size ();
}

std::string actor_transfer_coordinator_t::next_transfer_id (const std::string &node_rid)
{
    std::lock_guard lock (_mutex);
    return node_rid + ":" + std::to_string (_next_transfer_id++);
}

} // namespace zlink::framework::detail
