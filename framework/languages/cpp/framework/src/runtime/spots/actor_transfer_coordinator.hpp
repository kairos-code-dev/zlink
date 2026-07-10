/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/actors/actor.hpp>
#include <zlink/framework/contracts/locations/spot_ref.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace zlink::framework::detail
{

enum class actor_move_phase_t
{
    local,
    source_remote,
    target_pending,
    target_committing,
    reconcile
};

struct pending_actor_admission_t
{
    std::string actor_key;
    actor_ref_t source_actor;
    spot_rid_t source_spot_rid;
    spot_rid_t target_spot_rid;
    std::chrono::steady_clock::time_point deadline;
};

class actor_transfer_coordinator_t
{
  public:
    bool try_begin_local (const std::string &actor_key);
    bool try_begin_source_remote (const std::string &actor_key);
    void cancel_move (const std::string &actor_key);
    void mark_reconcile (const std::string &actor_key);
    void complete_move (const std::string &actor_key);
    bool blocks_dispatch (const std::string &actor_key) const;
    std::optional<actor_move_phase_t> phase (const std::string &actor_key) const;

    bool try_add_admission (std::string transfer_id, pending_actor_admission_t admission);
    std::optional<pending_actor_admission_t> begin_commit (const std::string &transfer_id,
                                                           const actor_ref_t &source_actor,
                                                           const spot_rid_t &target_spot_rid);
    void fail_commit (const std::string &transfer_id, bool reconcile);
    void complete_commit (const std::string &transfer_id);
    std::size_t cleanup_expired (std::chrono::steady_clock::time_point now);
    std::size_t pending_count () const;

    std::string next_transfer_id (const std::string &node_rid);

  private:
    struct move_state_t
    {
        actor_move_phase_t phase;
        std::string transfer_id;
    };

    mutable std::mutex _mutex;
    std::map<std::string, move_state_t> _moves;
    std::map<std::string, pending_actor_admission_t> _admissions;
    std::uint64_t _next_transfer_id = 1;
};

} // namespace zlink::framework::detail
