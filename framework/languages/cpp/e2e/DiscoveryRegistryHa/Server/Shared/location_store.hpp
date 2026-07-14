/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework.hpp>
#include <zlink/locations/redis.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::store_failure::server
{

inline int env_int (const char *name, int fallback)
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return std::stoi (value);
    }
    return fallback;
}

class location_store_delay_state_t
{
  public:
    void set_delay (std::chrono::milliseconds delay)
    {
        const auto clamped = std::clamp<int> (static_cast<int> (delay.count ()), 0, 5000);
        _delay_milliseconds.store (clamped, std::memory_order_relaxed);
    }

    std::chrono::milliseconds delay () const
    {
        return std::chrono::milliseconds (_delay_milliseconds.load (std::memory_order_relaxed));
    }

  private:
    std::atomic<int> _delay_milliseconds{0};
};

class delayable_location_store_t final : public zlink::framework::location_store_t,
                                         public zlink::framework::location_change_stamp_store_t
{
  public:
    delayable_location_store_t (std::shared_ptr<zlink::framework::location_store_t> inner,
                                std::shared_ptr<location_store_delay_state_t> delay_state) :
        _inner (std::move (inner)), _delay_state (std::move (delay_state))
    {
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    update_peer (zlink::framework::peer_location_t peer,
                 zlink::framework::location_write_intent_t intent) override
    {
        return delayed<zlink::framework::location_write_result_t> (
          [inner = _inner, peer = std::move (peer), intent] () mutable {
              return inner->update_peer (std::move (peer), intent).result ();
          });
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    remove_peer (zlink::framework::peer_location_key_t key,
                 zlink::framework::location_owner_token_t owner) override
    {
        return delayed<zlink::framework::location_write_result_t> (
          [inner = _inner, key = std::move (key), owner = std::move (owner)] () mutable {
              return inner->remove_peer (std::move (key), std::move (owner)).result ();
          });
    }

    zlink::framework::task_t<std::vector<zlink::framework::peer_location_t>>
    list_peers (zlink::framework::peer_location_filter_t filter) override
    {
        return delayed<std::vector<zlink::framework::peer_location_t>> (
          [inner = _inner, filter = std::move (filter)] () mutable {
              return inner->list_peers (std::move (filter)).result ();
          });
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    update_spot (zlink::framework::spot_location_t spot,
                 zlink::framework::location_write_intent_t intent) override
    {
        return delayed<zlink::framework::location_write_result_t> (
          [inner = _inner, spot = std::move (spot), intent] () mutable {
              return inner->update_spot (std::move (spot), intent).result ();
          });
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    remove_spot (zlink::framework::spot_location_key_t key,
                 zlink::framework::location_owner_token_t owner) override
    {
        return delayed<zlink::framework::location_write_result_t> (
          [inner = _inner, key = std::move (key), owner = std::move (owner)] () mutable {
              return inner->remove_spot (std::move (key), std::move (owner)).result ();
          });
    }

    zlink::framework::task_t<std::optional<zlink::framework::spot_location_t>>
    resolve_spot (zlink::framework::spot_location_key_t key) override
    {
        return delayed<std::optional<zlink::framework::spot_location_t>> (
          [inner = _inner, key = std::move (key)] () mutable {
              return inner->resolve_spot (std::move (key)).result ();
          });
    }

    zlink::framework::task_t<zlink::framework::location_page_t<zlink::framework::spot_location_t>>
    list_spots (zlink::framework::spot_location_filter_t filter,
                zlink::framework::location_page_request_t page = {}) override
    {
        return delayed<zlink::framework::location_page_t<zlink::framework::spot_location_t>> (
          [inner = _inner, filter = std::move (filter), page = std::move (page)] () mutable {
              return inner->list_spots (std::move (filter), std::move (page)).result ();
          });
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    update_actor (zlink::framework::actor_location_t actor,
                  zlink::framework::location_write_intent_t intent) override
    {
        return delayed<zlink::framework::location_write_result_t> (
          [inner = _inner, actor = std::move (actor), intent] () mutable {
              return inner->update_actor (std::move (actor), intent).result ();
          });
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    remove_actor (zlink::framework::actor_location_key_t key,
                  zlink::framework::location_owner_token_t owner) override
    {
        return delayed<zlink::framework::location_write_result_t> (
          [inner = _inner, key = std::move (key), owner = std::move (owner)] () mutable {
              return inner->remove_actor (std::move (key), std::move (owner)).result ();
          });
    }

    zlink::framework::task_t<std::optional<zlink::framework::actor_location_t>>
    resolve_actor (zlink::framework::actor_location_key_t key) override
    {
        return delayed<std::optional<zlink::framework::actor_location_t>> (
          [inner = _inner, key = std::move (key)] () mutable {
              return inner->resolve_actor (std::move (key)).result ();
          });
    }

    zlink::framework::task_t<zlink::framework::location_page_t<zlink::framework::actor_location_t>>
    list_actors (zlink::framework::actor_location_filter_t filter,
                 zlink::framework::location_page_request_t page = {}) override
    {
        return delayed<zlink::framework::location_page_t<zlink::framework::actor_location_t>> (
          [inner = _inner, filter = std::move (filter), page = std::move (page)] () mutable {
              return inner->list_actors (std::move (filter), std::move (page)).result ();
          });
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    update_route (zlink::framework::route_location_t route,
                  zlink::framework::location_write_intent_t intent) override
    {
        return delayed<zlink::framework::location_write_result_t> (
          [inner = _inner, route = std::move (route), intent] () mutable {
              return inner->update_route (std::move (route), intent).result ();
          });
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    remove_route (zlink::framework::route_location_key_t key,
                  zlink::framework::location_owner_token_t owner) override
    {
        return delayed<zlink::framework::location_write_result_t> (
          [inner = _inner, key = std::move (key), owner = std::move (owner)] () mutable {
              return inner->remove_route (std::move (key), std::move (owner)).result ();
          });
    }

    zlink::framework::task_t<std::optional<zlink::framework::route_location_t>>
    resolve_route (zlink::framework::route_location_key_t key) override
    {
        return delayed<std::optional<zlink::framework::route_location_t>> (
          [inner = _inner, key = std::move (key)] () mutable {
              return inner->resolve_route (std::move (key)).result ();
          });
    }

    zlink::framework::task_t<zlink::framework::location_page_t<zlink::framework::route_location_t>>
    list_routes (zlink::framework::route_location_filter_t filter,
                 zlink::framework::location_page_request_t page = {}) override
    {
        return delayed<zlink::framework::location_page_t<zlink::framework::route_location_t>> (
          [inner = _inner, filter = std::move (filter), page = std::move (page)] () mutable {
              return inner->list_routes (std::move (filter), std::move (page)).result ();
          });
    }

    zlink::framework::task_t<zlink::framework::owner_lease_renewal_t>
    renew_owner_lease (std::string owner_id,
                       zlink::routing_id_t node_rid,
                       std::chrono::milliseconds lease_ttl) override
    {
        return delayed<zlink::framework::owner_lease_renewal_t> (
          [inner = _inner, owner_id = std::move (owner_id), node_rid = std::move (node_rid),
           lease_ttl] () mutable {
              return inner->renew_owner_lease (std::move (owner_id), std::move (node_rid), lease_ttl)
                .result ();
          });
    }

    zlink::framework::task_t<bool> remove_owner_lease (std::string owner_id) override
    {
        return delayed<bool> ([inner = _inner, owner_id = std::move (owner_id)] () mutable {
            return inner->remove_owner_lease (std::move (owner_id)).result ();
        });
    }

    zlink::framework::task_t<zlink::framework::owner_lease_snapshot_t>
    list_owner_leases () override
    {
        return delayed<zlink::framework::owner_lease_snapshot_t> ([inner = _inner] () mutable {
            return inner->list_owner_leases ().result ();
        });
    }

    zlink::framework::task_t<std::int64_t> remove_all_by_owner (std::string owner_id) override
    {
        return delayed<std::int64_t> (
          [inner = _inner, owner_id = std::move (owner_id)] () mutable {
              return inner->remove_all_by_owner (std::move (owner_id)).result ();
          });
    }

    zlink::framework::task_t<std::int64_t>
    get_change_stamp (zlink::framework::location_change_stamp_scope_t scope) override
    {
        return delayed<std::int64_t> ([inner = _inner, scope = std::move (scope)] () mutable {
            auto *stamp_store =
              dynamic_cast<zlink::framework::location_change_stamp_store_t *> (inner.get ());
            if (stamp_store == nullptr) {
                return zlink::framework::result_t<std::int64_t>::success (0);
            }
            return stamp_store->get_change_stamp (std::move (scope)).result ();
        });
    }

  private:
    template <typename TResult, typename TOperation>
    zlink::framework::task_t<TResult> delayed (TOperation operation)
    {
        auto delay = _delay_state ? _delay_state->delay () : std::chrono::milliseconds (0);
        if (delay.count () > 0) {
            std::this_thread::sleep_for (delay);
        }
        return zlink::framework::task_t<TResult> (operation ());
    }

    std::shared_ptr<zlink::framework::location_store_t> _inner;
    std::shared_ptr<location_store_delay_state_t> _delay_state;
};

inline void add_redis_location_store (zlink::framework::zlink_framework_options_t &framework,
                                      const std::string &redis_endpoint,
                                      const std::string &redis_key_prefix,
                                      std::shared_ptr<location_store_delay_state_t> delay_state = {})
{
    if (redis_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_REDIS_ENDPOINT is required");
    }
    if (redis_key_prefix.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX is required");
    }
    auto redis_store = std::make_shared<zlink::framework::locations::redis::redis_location_store_t> (
      zlink::framework::locations::redis::redis_location_options_t{
        .connection_string = redis_endpoint, .key_prefix = redis_key_prefix});
    if (delay_state) {
        framework.add_location_store (
          std::make_shared<delayable_location_store_t> (std::move (redis_store),
                                                        std::move (delay_state)));
    } else {
        framework.add_location_store (std::move (redis_store));
    }
    auto &locations = framework.configure_locations ();
    locations.heartbeat_interval =
      std::chrono::milliseconds (env_int ("ZLINK_CPP_SF_LOCATION_HEARTBEAT_MS", 1000));
    locations.owner_lease_ttl =
      std::chrono::milliseconds (env_int ("ZLINK_CPP_SF_LOCATION_LEASE_TTL_MS", 3000));
    locations.polling_interval =
      std::chrono::milliseconds (env_int ("ZLINK_CPP_SF_LOCATION_POLLING_MS", 500));
    locations.store_failure_grace =
      std::chrono::milliseconds (env_int ("ZLINK_CPP_SF_LOCATION_GRACE_MS", 6000));
}

} // namespace zlink::framework::e2e::store_failure::server
