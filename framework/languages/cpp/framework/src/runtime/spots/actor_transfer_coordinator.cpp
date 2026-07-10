/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_transfer_coordinator.hpp"

namespace zlink::framework::detail
{

bool actor_transfer_coordinator_t::try_begin_local (const std::string &actor_key)
{
    std::lock_guard lock (_mutex);
    return _moves.emplace (actor_key, move_state_t{actor_move_phase_t::local, std::string{}})
      .second;
}

bool actor_transfer_coordinator_t::try_begin_source_remote (const std::string &actor_key)
{
    std::lock_guard lock (_mutex);
    return _moves
      .emplace (actor_key, move_state_t{actor_move_phase_t::source_remote, std::string{}})
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

void actor_transfer_coordinator_t::complete_move (const std::string &actor_key)
{
    cancel_move (actor_key);
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

std::size_t
actor_transfer_coordinator_t::cleanup_expired (std::chrono::steady_clock::time_point now)
{
    std::lock_guard lock (_mutex);
    std::size_t removed = 0;
    for (auto found = _admissions.begin (); found != _admissions.end ();) {
        const auto moving = _moves.find (found->second.actor_key);
        const bool can_expire =
          moving != _moves.end () && moving->second.phase == actor_move_phase_t::target_pending;
        if (can_expire && found->second.deadline <= now) {
            _moves.erase (moving);
            found = _admissions.erase (found);
            ++removed;
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
