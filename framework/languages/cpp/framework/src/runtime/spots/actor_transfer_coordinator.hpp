/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/actors/actor.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

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
    spot_id_t source_spot_id;
    spot_id_t target_spot_id;
    std::chrono::steady_clock::time_point deadline;
};

struct expired_actor_admission_t
{
    std::string transfer_id;
    pending_actor_admission_t admission;
};

struct evicted_actor_forwarding_t
{
    std::string actor_key;
    std::uint64_t old_generation = 0;
    std::string transfer_id;
};

struct actor_forwarding_target_t
{
    actor_ref_t actor;
    spot_route_t route;
};

// One in-flight actor packet preserved while its actor is moving (spot-actor
// spec §10). Sends are preserved and replayed transparently; a request is also
// preserved (§10.2-1) so it still reaches the committed target's handler even
// after the caller's reply channel has re-resolved or timed out (§10.5 late
// reply) — the replayed request's reply is best-effort.
struct handoff_packet_t
{
    std::string packet_name;
    std::vector<std::uint8_t> payload;
    std::string content_type;
    std::map<std::string, std::string> metadata;
    bool is_request = false;
};

class actor_transfer_coordinator_t
{
  public:
    bool try_begin_local (const std::string &actor_key);
    bool try_begin_source_remote (const std::string &actor_key, std::string transfer_id = {});
    void cancel_move (const std::string &actor_key);
    void mark_reconcile (const std::string &actor_key);
    // Returns the out→commit-ack elapsed time when the completed move was a
    // source-remote transfer (runtime-metrics §4.3 duration window); local
    // moves complete with nullopt.
    std::optional<std::chrono::steady_clock::duration>
    complete_move (const std::string &actor_key);
    bool blocks_dispatch (const std::string &actor_key) const;
    std::optional<actor_move_phase_t> phase (const std::string &actor_key) const;
    std::optional<std::string> transfer_id (const std::string &actor_key) const;

    // In-flight handoff (spot-actor spec §10). One-way packets that arrive while
    // the actor is moving are preserved here in arrival order; the commit path
    // drains them into the commit request, and packets that race the commit ack
    // drain into the forwarding path afterwards.
    bool try_append_backlog (const std::string &actor_key, handoff_packet_t packet);
    std::vector<handoff_packet_t> take_backlog (const std::string &actor_key);

    // Forwarding mapping lifetime (§10.4): activated when the source confirms
    // the target commit, refreshed on re-transfer (at most one entry per actor),
    // and evicted after the forward window so retained state cannot pile up.
    void activate_forwarding (const std::string &actor_key,
                              std::uint64_t old_generation,
                              actor_ref_t target_actor,
                              spot_route_t target_route,
                              std::chrono::steady_clock::time_point evict_at,
                              std::string transfer_id = {});
    bool forwards_stale_generation (const std::string &actor_key,
                                    std::uint64_t generation) const;
    std::optional<actor_forwarding_target_t>
    forwarding_target (const std::string &actor_key, std::uint64_t generation) const;
    std::vector<evicted_actor_forwarding_t>
    evict_expired_forwarding (std::chrono::steady_clock::time_point now);

    bool try_add_admission (std::string transfer_id, pending_actor_admission_t admission);
    std::optional<pending_actor_admission_t> begin_commit (const std::string &transfer_id,
                                                           const actor_ref_t &source_actor,
                                                           const spot_id_t &target_spot_id);
    std::optional<pending_actor_admission_t> pending_commit (
      const std::string &transfer_id,
      const actor_ref_t &source_actor,
      const spot_id_t &target_spot_id) const;
    void fail_commit (const std::string &transfer_id, bool reconcile);
    void complete_commit (const std::string &transfer_id);
    std::vector<expired_actor_admission_t>
    cleanup_expired (std::chrono::steady_clock::time_point now);
    std::size_t pending_count () const;

    std::string next_transfer_id (const std::string &node_rid);

  private:
    struct move_state_t
    {
        actor_move_phase_t phase;
        std::string transfer_id;
        std::optional<std::chrono::steady_clock::time_point> transfer_started_at;
    };

    struct forwarding_entry_t
    {
        std::uint64_t old_generation = 0;
        actor_ref_t target_actor;
        spot_route_t target_route;
        std::chrono::steady_clock::time_point evict_at;
        std::string transfer_id;
    };

    mutable std::mutex _mutex;
    std::map<std::string, move_state_t> _moves;
    std::map<std::string, pending_actor_admission_t> _admissions;
    std::map<std::string, std::vector<handoff_packet_t>> _backlogs;
    std::map<std::string, forwarding_entry_t> _forwardings;
    std::uint64_t _next_transfer_id = 1;
};

} // namespace zlink::framework::detail
