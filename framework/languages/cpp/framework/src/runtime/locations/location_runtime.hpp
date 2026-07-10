/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/locations/location_key_codec.hpp"

#include <zlink/framework/contracts/locations/options.hpp>
#include <zlink/framework/contracts/locations/stores.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <random>
#include <thread>

namespace zlink::framework::runtime
{

class location_runtime_t
{
  public:
    explicit location_runtime_t (location_store_t &store,
                                 location_options_t options = {},
                                 std::string owner_id = make_owner_id ()) :
        _store (&store), _options (options), _owner_id (std::move (owner_id))
    {
    }

    ~location_runtime_t () { stop (); }

    location_runtime_t (const location_runtime_t &) = delete;
    location_runtime_t &operator= (const location_runtime_t &) = delete;

    const std::string &owner_id () const noexcept { return _owner_id; }

    const location_options_t &options () const noexcept { return _options; }

    bool owner_lease_healthy () const noexcept
    {
        std::lock_guard lock (_state_gate);
        return _owner_lease_healthy;
    }

    std::optional<std::chrono::system_clock::time_point> owner_lease_renewed_at () const
    {
        std::lock_guard lock (_state_gate);
        return _owner_lease_renewed_at;
    }

    std::optional<std::string> last_error () const
    {
        std::lock_guard lock (_state_gate);
        return _last_error;
    }

    void start (zlink::routing_id_t node_rid)
    {
        bool expected = false;
        if (!_started.compare_exchange_strong (expected, true)) {
            return;
        }
        _node_rid = std::move (node_rid);
        renew_owner_lease_once ();
        _heartbeat_stop.store (false, std::memory_order_release);
        _heartbeat = std::thread ([this] { heartbeat_loop (); });
    }

    void stop () noexcept
    {
        if (!_started.exchange (false)) {
            return;
        }
        _heartbeat_stop.store (true, std::memory_order_release);
        _heartbeat_wake.notify_all ();
        if (_heartbeat.joinable ()) {
            _heartbeat.join ();
        }
        try {
            _store->remove_owner_lease (_owner_id).result ().value ();
            _store->remove_all_by_owner (_owner_id).result ().value ();
        }
        catch (const std::exception &error) {
            record_failure (error.what ());
        }
    }

    owner_lease_renewal_t renew_owner_lease_once ()
    {
        try {
            auto result = _store->renew_owner_lease (_owner_id, _node_rid, _options.owner_lease_ttl)
                            .result ()
                            .value ();
            std::lock_guard lock (_state_gate);
            _owner_lease_healthy = true;
            _owner_lease_renewed_at = result.store_now;
            _last_error.reset ();
            return result;
        }
        catch (const std::exception &error) {
            record_failure (error.what ());
            return {};
        }
    }

    location_write_result_t write_peer (peer_location_t peer, location_write_intent_t intent)
    {
        peer.owner_id = _owner_id;
        auto canonical = location_key_codec_t::encode_peer_key (peer_location_key_t{
          peer.auto_connect_type, peer.mesh_name, peer.role, peer.node_rid, peer.endpoint});
        auto result = _store->update_peer (std::move (peer), intent).result ().value ();
        notify_if_stale (result, location_kind_t::peer, canonical);
        return result;
    }

    location_write_result_t remove_peer (peer_location_key_t key, std::int64_t generation)
    {
        auto canonical = location_key_codec_t::encode_peer_key (key);
        auto result =
          _store->remove_peer (std::move (key), location_owner_token_t{_owner_id, generation})
            .result ()
            .value ();
        notify_if_stale (result, location_kind_t::peer, canonical);
        return result;
    }

    location_write_result_t write_spot (spot_location_t spot, location_write_intent_t intent)
    {
        spot.owner_id = _owner_id;
        auto canonical = location_key_codec_t::encode_spot_key (
          spot_location_key_t{spot.mesh_name, spot.spot_rid});
        auto result = _store->update_spot (std::move (spot), intent).result ().value ();
        notify_if_stale (result, location_kind_t::spot, canonical);
        return result;
    }

    location_write_result_t remove_spot (spot_location_key_t key, std::int64_t generation)
    {
        auto canonical = location_key_codec_t::encode_spot_key (key);
        auto result =
          _store->remove_spot (std::move (key), location_owner_token_t{_owner_id, generation})
            .result ()
            .value ();
        notify_if_stale (result, location_kind_t::spot, canonical);
        return result;
    }

    location_write_result_t write_actor (actor_location_t actor, location_write_intent_t intent)
    {
        actor.owner_id = _owner_id;
        auto canonical =
          location_key_codec_t::encode_actor_key (actor_location_key_t{actor.actor_id});
        auto result = _store->update_actor (std::move (actor), intent).result ().value ();
        notify_if_stale (result, location_kind_t::actor, canonical);
        return result;
    }

    location_write_result_t write_route (route_location_t route, location_write_intent_t intent)
    {
        route.owner_id = _owner_id;
        auto canonical = location_key_codec_t::encode_route_key (
          route_location_key_t{route.route_kind, route.route_key});
        auto result = _store->update_route (std::move (route), intent).result ().value ();
        notify_if_stale (result, location_kind_t::route, canonical);
        return result;
    }

    location_write_result_t remove_actor (actor_location_key_t key, std::int64_t generation)
    {
        auto canonical = location_key_codec_t::encode_actor_key (key);
        auto result =
          _store->remove_actor (std::move (key), location_owner_token_t{_owner_id, generation})
            .result ()
            .value ();
        notify_if_stale (result, location_kind_t::actor, canonical);
        return result;
    }

    // A stale write only proves that this owner lost that one row. The callback
    // receives the row kind and canonical key so listeners deactivate exactly
    // that claim (the .NET runtime raises OwnershipLost per key the same way).
    void
    on_ownership_lost (std::function<void (location_kind_t, const std::string &)> callback)
    {
        std::lock_guard lock (_callbacks_gate);
        _ownership_lost_callbacks.push_back (std::move (callback));
    }

  private:
    static std::string make_owner_id ()
    {
        static std::atomic_uint64_t counter{1};
        const auto now = std::chrono::steady_clock::now ().time_since_epoch ().count ();
        const auto random = std::random_device{} ();
        return "cpp-location-owner-" + std::to_string (now) + "-" + std::to_string (random) + "-"
               + std::to_string (counter.fetch_add (1));
    }

    void heartbeat_loop ()
    {
        while (!_heartbeat_stop.load (std::memory_order_acquire)) {
            std::unique_lock lock (_heartbeat_gate);
            _heartbeat_wake.wait_for (lock, _options.heartbeat_interval, [this] {
                return _heartbeat_stop.load (std::memory_order_acquire);
            });
            if (_heartbeat_stop.load (std::memory_order_acquire)) {
                break;
            }
            lock.unlock ();
            renew_owner_lease_once ();
        }
    }

    void notify_if_stale (const location_write_result_t &result,
                          location_kind_t kind,
                          const std::string &canonical_key)
    {
        if (result.status != location_write_status_t::ignored_stale) {
            return;
        }
        std::vector<std::function<void (location_kind_t, const std::string &)>> callbacks;
        {
            std::lock_guard lock (_callbacks_gate);
            callbacks = _ownership_lost_callbacks;
        }
        for (const auto &callback : callbacks) {
            callback (kind, canonical_key);
        }
    }

    void record_failure (std::string message) const
    {
        std::lock_guard lock (_state_gate);
        _owner_lease_healthy = false;
        _last_error = std::move (message);
    }

    location_store_t *_store;
    location_options_t _options;
    std::string _owner_id;
    zlink::routing_id_t _node_rid = zlink::routing_id_t::from (std::uint32_t{0});
    std::atomic_bool _started = false;
    std::atomic_bool _heartbeat_stop = false;
    std::thread _heartbeat;
    std::mutex _heartbeat_gate;
    std::condition_variable _heartbeat_wake;
    mutable std::mutex _state_gate;
    mutable bool _owner_lease_healthy = false;
    mutable std::optional<std::chrono::system_clock::time_point> _owner_lease_renewed_at;
    mutable std::optional<std::string> _last_error;
    std::mutex _callbacks_gate;
    std::vector<std::function<void (location_kind_t, const std::string &)>>
      _ownership_lost_callbacks;
};

} // namespace zlink::framework::runtime
