/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/locations/location_runtime.hpp"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace zlink::framework::runtime
{

class location_lifecycle_t
{
  public:
    using actor_deactivate_callback_t = std::function<void (const actor_location_t &)>;

    struct actor_claim_result_t
    {
        location_write_status_t status = location_write_status_t::ignored_stale;
        actor_location_t actor;
        std::uint64_t store_generation = 0;
        std::chrono::system_clock::time_point updated_at{};
    };

    struct spot_claim_result_t
    {
        location_write_status_t status = location_write_status_t::ignored_stale;
        spot_location_t spot;
        std::chrono::system_clock::time_point updated_at{};
    };

    explicit location_lifecycle_t (location_runtime_t &runtime) :
        _runtime (&runtime), _state (std::make_shared<state_t> ())
    {
        std::weak_ptr<state_t> weak_state = _state;
        // Ownership loss arrives per row: only the claim for that canonical key is
        // deactivated. Other claims held by this owner stay live (.NET parity).
        runtime.on_ownership_lost (
          [weak_state] (location_kind_t kind, const std::string &canonical_key) {
              auto state = weak_state.lock ();
              if (!state) {
                  return;
              }
              if (kind == location_kind_t::actor) {
                  deactivate_actor (*state, canonical_key);
              } else if (kind == location_kind_t::spot) {
                  std::lock_guard lock (state->gate);
                  state->spots.erase (canonical_key);
              }
          });
    }

    ~location_lifecycle_t ()
    {
        std::lock_guard lock (_state->gate);
        _state->active = false;
        _state->actors.clear ();
        _state->spots.clear ();
    }

    location_lifecycle_t (const location_lifecycle_t &) = delete;
    location_lifecycle_t &operator= (const location_lifecycle_t &) = delete;

    actor_claim_result_t claim_actor (actor_location_t actor,
                                      actor_deactivate_callback_t deactivate = {},
                                      bool takeover = false)
    {
        auto result =
          _runtime->write_actor (actor, location_write_intent_t::new_claim);
        if (result.status == location_write_status_t::rejected_conflict && takeover) {
            result = _runtime->write_actor (actor, location_write_intent_t::takeover);
        }
        if (result.status != location_write_status_t::stored) {
            return {result.status, std::move (actor), 0, result.updated_at};
        }

        {
            std::lock_guard lock (_state->gate);
            if (_state->active) {
                _state->actors[actor_key (actor)] =
                  actor_claim_t{actor, static_cast<std::uint64_t> (result.generation),
                                std::move (deactivate)};
            }
        }
        return {result.status, std::move (actor),
                static_cast<std::uint64_t> (result.generation), result.updated_at};
    }

    location_write_result_t update_actor_location (actor_location_t actor)
    {
        actor_claim_t tracked;
        const auto key = actor_key (actor);
        {
            std::lock_guard lock (_state->gate);
            const auto found = _state->actors.find (key);
            if (found == _state->actors.end ()) {
                return {location_write_status_t::ignored_stale, 0, {}};
            }
            tracked = found->second;
        }

        actor.owner_id = tracked.actor.owner_id;
        const auto result = _runtime->write_actor (actor, location_write_intent_t::renew);
        if (result.status == location_write_status_t::stored) {
            std::lock_guard lock (_state->gate);
            const auto found = _state->actors.find (key);
            if (found != _state->actors.end ()) {
                found->second.actor = std::move (actor);
                found->second.store_generation = result.generation;
            }
            return result;
        }

        if (result.status == location_write_status_t::ignored_stale) {
            deactivate_actor (*_state, key);
        }
        return result;
    }

    bool owns_actor (actor_location_key_t key) const
    {
        std::lock_guard lock (_state->gate);
        return _state->actors.contains (actor_key (key));
    }

    spot_claim_result_t claim_spot (spot_location_t spot)
    {
        const auto result = _runtime->write_spot (spot, location_write_intent_t::new_claim);
        if (result.status != location_write_status_t::stored) {
            return {result.status, std::move (spot), result.updated_at};
        }

        spot.generation = result.generation;
        {
            std::lock_guard lock (_state->gate);
            if (_state->active) {
                _state->spots[spot_key (spot)] = spot;
            }
        }
        return {result.status, std::move (spot), result.updated_at};
    }

    location_write_result_t release_spot (spot_location_key_t key)
    {
        spot_location_t spot;
        {
            std::lock_guard lock (_state->gate);
            const auto found = _state->spots.find (spot_key (key));
            if (found == _state->spots.end ()) {
                return {location_write_status_t::ignored_stale, 0, {}};
            }
            spot = found->second;
        }

        const auto result =
          _runtime->remove_spot (spot_location_key_t{spot.spot_id},
                                 spot.generation);
        if (result.status == location_write_status_t::stored
            || result.status == location_write_status_t::ignored_stale) {
            std::lock_guard lock (_state->gate);
            _state->spots.erase (spot_key (spot));
        }
        return result;
    }

    location_write_result_t renew_actor (actor_location_key_t key)
    {
        actor_location_t actor;
        {
            std::lock_guard lock (_state->gate);
            const auto found = _state->actors.find (actor_key (key));
            if (found == _state->actors.end ()) {
                return {location_write_status_t::ignored_stale, 0, {}};
            }
            actor = found->second.actor;
        }

        const auto result = _runtime->write_actor (actor, location_write_intent_t::renew);
        if (result.status == location_write_status_t::stored) {
            std::lock_guard lock (_state->gate);
            const auto found = _state->actors.find (actor_key (key));
            if (found != _state->actors.end ()) {
                found->second.store_generation = result.generation;
            }
            return result;
        }

        if (result.status == location_write_status_t::ignored_stale) {
            deactivate_actor (*_state, actor_key (key));
        }
        return result;
    }

    location_write_result_t release_actor (actor_location_key_t key)
    {
        actor_claim_t claim;
        {
            std::lock_guard lock (_state->gate);
            const auto found = _state->actors.find (actor_key (key));
            if (found == _state->actors.end ()) {
                return {location_write_status_t::ignored_stale, 0, {}};
            }
            claim = found->second;
            // Untracked before the write: a stale remove after another owner's
            // takeover is ignored by the store and must not fire deactivation.
            _state->actors.erase (found);
        }

        return _runtime->remove_actor (
          actor_location_key_t{claim.actor.mesh_name, claim.actor.actor_id},
          claim.store_generation);
    }

    std::size_t tracked_actor_count () const
    {
        std::lock_guard lock (_state->gate);
        return _state->actors.size ();
    }

  private:
    struct actor_claim_t
    {
        actor_location_t actor;
        std::uint64_t store_generation = 0;
        actor_deactivate_callback_t deactivate;
    };

    struct state_t
    {
        mutable std::mutex gate;
        bool active = true;
        std::map<std::string, actor_claim_t> actors;
        std::map<std::string, spot_location_t> spots;
    };

    static std::string actor_key (const actor_location_t &actor)
    {
        return actor_key (actor_location_key_t{actor.mesh_name, actor.actor_id});
    }

    static std::string actor_key (const actor_location_key_t &key)
    {
        return location_key_codec_t::encode_actor_key (key);
    }

    static std::string spot_key (const spot_location_t &spot)
    {
        return spot_key (spot_location_key_t{spot.spot_id});
    }

    static std::string spot_key (const spot_location_key_t &key)
    {
        return location_key_codec_t::encode_spot_key (key);
    }

    static void deactivate_actor (state_t &state, const std::string &key)
    {
        actor_claim_t claim;
        {
            std::lock_guard lock (state.gate);
            if (!state.active) {
                return;
            }
            const auto found = state.actors.find (key);
            if (found == state.actors.end ()) {
                return;
            }
            claim = std::move (found->second);
            state.actors.erase (found);
        }
        if (claim.deactivate) {
            claim.deactivate (claim.actor);
        }
    }

    static void deactivate_all (state_t &state)
    {
        std::vector<actor_claim_t> claims;
        {
            std::lock_guard lock (state.gate);
            if (!state.active) {
                return;
            }
            for (auto &entry : state.actors) {
                claims.push_back (std::move (entry.second));
            }
            state.actors.clear ();
        }
        for (const auto &claim : claims) {
            if (claim.deactivate) {
                claim.deactivate (claim.actor);
            }
        }
    }

    location_runtime_t *_runtime;
    std::shared_ptr<state_t> _state;
};

} // namespace zlink::framework::runtime
