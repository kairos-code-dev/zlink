/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/locations/location_key_codec.hpp"
#include "runtime/diagnostics/runtime_metrics.hpp"

#include <zlink/framework/contracts/locations/options.hpp>
#include <zlink/framework/contracts/locations/stores.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
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

    std::optional<location_owner_token_t> current_owner_token () const
    {
        std::lock_guard lock (_state_gate);
        return _owner_token;
    }

    /* Draining marker (graceful-drain-handoff §3.1): peer rows written while
     * draining carry the typed flag; a started drain generation never flips
     * the flag back to false. */
    /* Metric surface binding (runtime-metrics §4.5): the host wires the
     * monitoring state so lease renew failures/lateness and write conflicts
     * emit catalog instruments; unset keeps the zero-cost path. */
    void bind_monitoring (
      std::shared_ptr<framework::detail::monitoring_runtime_state_t> monitoring) noexcept
    {
        _monitoring = std::move (monitoring);
    }

    void set_draining (bool value) noexcept
    {
        if (value) {
            _draining.store (true, std::memory_order_release);
        }
    }

    bool draining () const noexcept { return _draining.load (std::memory_order_acquire); }

    /* Re-publishes every peer row owned by this runtime with draining=true.
     * Returns false when the store rejects the writes (retry until deadline
     * belongs to the drain worker). */
    bool republish_peer_rows_draining ()
    {
        try {
            auto peers = store_call (
              [&] { return _store->list_peers (peer_location_filter_t{}).result ().value (); });
            bool all_written = true;
            for (auto &peer : peers) {
                if (peer.owner_id != _owner_id || peer.draining) {
                    continue;
                }
                peer.draining = true;
                const auto written = write_peer (std::move (peer), location_write_intent_t::renew);
                if (written.status != location_write_status_t::stored) {
                    all_written = false;
                }
            }
            return all_written;
        }
        catch (...) {
            return false;
        }
    }

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
        static_cast<void> (node_rid);
        try {
            const auto claim =
              _store
                ->claim_owner_lease (
                  _owner_id, _options.owner_lease_ttl)
                .result ()
                .value ();
            const auto *claimed =
              std::get_if<owner_lease_claimed_t> (&claim);
            if (claimed == nullptr)
                throw std::runtime_error (
                  "owner lease claim was rejected");
            std::lock_guard lock (_state_gate);
            _owner_token = claimed->token;
            _owner_lease_healthy = true;
            _owner_lease_renewed_at = claimed->store_now;
            _last_error.reset ();
        }
        catch (...) {
            _started.store (false, std::memory_order_release);
            throw;
        }
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
            const auto token = current_owner_token ();
            if (token) {
                _store->remove_all_by_owner (*token)
                  .result ()
                  .value ();
                _store->release_owner_lease (*token)
                  .result ()
                  .value ();
                std::lock_guard lock (_state_gate);
                _owner_token.reset ();
            }
        }
        catch (const std::exception &error) {
            record_store_error ();
            record_failure (error.what ());
        }
    }

    /* Drain owner cleanup (graceful-drain-handoff §4-5): stops the lease
     * heartbeat, then removes this owner's lease and rows while the store
     * stays usable for the rest of teardown. Returns false when the store
     * rejects the cleanup (the drain worker maps it to OwnerCleanupFailed). */
    bool cleanup_owner () noexcept
    {
        if (_started.exchange (false)) {
            _heartbeat_stop.store (true, std::memory_order_release);
            _heartbeat_wake.notify_all ();
            if (_heartbeat.joinable ()) {
                _heartbeat.join ();
            }
        }
        try {
            const auto token = current_owner_token ();
            if (token) {
                _store->remove_all_by_owner (*token)
                  .result ()
                  .value ();
                _store->release_owner_lease (*token)
                  .result ()
                  .value ();
                std::lock_guard lock (_state_gate);
                _owner_token.reset ();
            }
            return true;
        }
        catch (const std::exception &error) {
            record_store_error ();
            record_failure (error.what ());
            return false;
        }
        catch (...) {
            return false;
        }
    }

    owner_lease_renew_result_t renew_owner_lease_once ()
    {
        runtime_metrics_t metrics (_monitoring);
        const auto metrics_enabled = metrics.enabled ();
        std::optional<std::chrono::steady_clock::time_point> due_at;
        if (metrics_enabled) {
            std::lock_guard lock (_state_gate);
            if (_last_renew_started_at) {
                due_at = *_last_renew_started_at + _options.heartbeat_interval;
            }
        }
        const auto started_at = std::chrono::steady_clock::now ();
        if (metrics_enabled) {
            std::lock_guard lock (_state_gate);
            _last_renew_started_at = started_at;
            if (due_at && started_at > *due_at) {
                metrics.histogram (
                  "zlink.location.owner_lease.renew.lateness", "s",
                  std::chrono::duration<double> (started_at - *due_at).count ());
            }
        }
        try {
            const auto token = current_owner_token ();
            if (!token)
                throw std::runtime_error (
                  "owner lease token is unavailable");
            auto result =
              _store
                ->renew_owner_lease (
                  *token, _options.owner_lease_ttl)
                .result ()
                .value ();
            const auto *renewed =
              std::get_if<owner_lease_renewed_t> (&result);
            if (renewed == nullptr)
                throw std::runtime_error (
                  "owner lease token is stale");
            if (const char *trace = std::getenv ("ZLINK_CPP_AUTO_CONNECT_TRACE");
                trace != nullptr && *trace != '\0') {
                const auto completed_at = std::chrono::steady_clock::now ();
                std::cerr << "zlink owner-lease renew"
                          << " monotonicMs="
                          << std::chrono::duration_cast<std::chrono::milliseconds> (
                               completed_at.time_since_epoch ())
                               .count ()
                          << " durationMs="
                          << std::chrono::duration_cast<std::chrono::milliseconds> (
                               completed_at - started_at)
                               .count ()
                          << " heartbeatMs=" << _options.heartbeat_interval.count ()
                          << " ttlMs=" << _options.owner_lease_ttl.count () << '\n';
            }
            std::lock_guard lock (_state_gate);
            _owner_lease_healthy = true;
            _owner_lease_renewed_at = renewed->store_now;
            _last_error.reset ();
            return result;
        }
        catch (const std::exception &error) {
            if (metrics_enabled) {
                metrics.counter ("zlink.location.owner_lease.renew.failures", "{failure}", 1);
            }
            record_failure (error.what ());
            return owner_lease_renew_result_t{
              owner_lease_stale_t{}};
        }
    }

    /* Store access failed (read or register): one error count per failure
     * (runtime-metrics §4.5 store.errors). The polling and write surfaces all
     * report here so the counter aggregates store health in one series. */
    void record_store_error () const
    {
        runtime_metrics_t metrics (_monitoring);
        if (metrics.enabled ()) {
            metrics.counter ("zlink.location.store.errors", "{error}", 1);
        }
    }

    /* Discovered peer total observed on the auto-connect polling tick
     * (runtime-metrics §7.2: the polling diff doubles as the gauge source, so
     * observable freshness follows the tick cadence). */
    void observe_discovered_peers (std::size_t count) const
    {
        runtime_metrics_t metrics (_monitoring);
        if (metrics.enabled ()) {
            metrics.observable ("zlink.location.peers", "{peer}",
                                static_cast<double> (count));
        }
    }

    location_write_result_t write_peer (peer_location_t peer, location_write_intent_t intent)
    {
        peer.owner_id = _owner_id;
        if (_draining.load (std::memory_order_acquire)) {
            peer.draining = true;
        }
        auto canonical = location_key_codec_t::encode_peer_key (peer_location_key_t{
          peer.auto_connect_type, peer.mesh_name, peer.role, peer.node_rid, peer.endpoint});
        auto result = store_call (
          [&] { return _store->update_peer (std::move (peer), intent).result ().value (); });
        if (result.status == location_write_status_t::rejected_conflict
            || result.status == location_write_status_t::ignored_stale) {
            runtime_metrics_t metrics (_monitoring);
            if (metrics.enabled ()) {
                metrics.counter ("zlink.location.write.conflicts", "{write}", 1);
            }
        }
        notify_if_stale (result, location_kind_t::peer, canonical);
        return result;
    }

    location_write_result_t remove_peer (peer_location_key_t key, std::int64_t generation)
    {
        auto canonical = location_key_codec_t::encode_peer_key (key);
        auto result = store_call ([&] {
            return _store->remove_peer (std::move (key), location_owner_token_t{_owner_id, generation})
              .result ()
              .value ();
        });
        notify_if_stale (result, location_kind_t::peer, canonical);
        return result;
    }

    location_write_result_t write_spot (spot_location_t spot, location_write_intent_t intent)
    {
        spot.owner_id = _owner_id;
        auto canonical = location_key_codec_t::encode_spot_key (
          spot_location_key_t{spot.spot_id});
        auto result = store_call (
          [&] { return _store->update_spot (std::move (spot), intent).result ().value (); });
        notify_if_stale (result, location_kind_t::spot, canonical);
        return result;
    }

    location_write_result_t remove_spot (spot_location_key_t key, std::int64_t generation)
    {
        auto canonical = location_key_codec_t::encode_spot_key (key);
        auto result = store_call ([&] {
            return _store->remove_spot (std::move (key), location_owner_token_t{_owner_id, generation})
              .result ()
              .value ();
        });
        notify_if_stale (result, location_kind_t::spot, canonical);
        return result;
    }

    location_write_result_t write_actor (actor_location_t actor, location_write_intent_t intent)
    {
        actor.owner_id = _owner_id;
        auto canonical =
          location_key_codec_t::encode_actor_key (
            actor_location_key_t{actor.mesh_name, actor.actor_id});
        auto result = store_call (
          [&] { return _store->update_actor (std::move (actor), intent).result ().value (); });
        notify_if_stale (result, location_kind_t::actor, canonical);
        return result;
    }

    location_write_result_t write_route (route_location_t route, location_write_intent_t intent)
    {
        route.owner_id = _owner_id;
        auto canonical = location_key_codec_t::encode_route_key (
          route_location_key_t{route.route_kind, route.route_key});
        auto result = store_call (
          [&] { return _store->update_route (std::move (route), intent).result ().value (); });
        notify_if_stale (result, location_kind_t::route, canonical);
        return result;
    }

    location_write_result_t remove_actor (actor_location_key_t key, std::int64_t generation)
    {
        auto canonical = location_key_codec_t::encode_actor_key (key);
        auto result = store_call ([&] {
            return _store->remove_actor (std::move (key), location_owner_token_t{_owner_id, generation})
              .result ()
              .value ();
        });
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

    /* Every store lookup/register goes through here so a thrown store failure
     * lands in the store.errors counter exactly once before it propagates. */
    template <typename Call> auto store_call (Call &&call) -> decltype (call ())
    {
        try {
            return call ();
        }
        catch (...) {
            record_store_error ();
            throw;
        }
    }

    location_store_t *_store;
    location_options_t _options;
    std::string _owner_id;
    std::atomic_bool _draining = false;
    std::shared_ptr<framework::detail::monitoring_runtime_state_t> _monitoring;
    std::optional<std::chrono::steady_clock::time_point> _last_renew_started_at;
    std::atomic_bool _started = false;
    std::atomic_bool _heartbeat_stop = false;
    std::thread _heartbeat;
    std::mutex _heartbeat_gate;
    std::condition_variable _heartbeat_wake;
    mutable std::mutex _state_gate;
    mutable bool _owner_lease_healthy = false;
    mutable std::optional<std::chrono::system_clock::time_point> _owner_lease_renewed_at;
    mutable std::optional<location_owner_token_t> _owner_token;
    mutable std::optional<std::string> _last_error;
    std::mutex _callbacks_gate;
    std::vector<std::function<void (location_kind_t, const std::string &)>>
      _ownership_lost_callbacks;
};

} // namespace zlink::framework::runtime
