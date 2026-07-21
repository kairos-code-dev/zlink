/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/locations/options.hpp>
#include <zlink/framework/contracts/locations/stores.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace zlink::framework::runtime
{

/* Joins raw location rows with owner leases for every framework read path.
 * Store implementations stay policy-free and write APIs do not pass through
 * this read-only boundary. */
class live_location_reader_t final
{
  public:
    live_location_reader_t (location_store_t &store, location_options_t options = {}) :
        _store (&store), _polling_interval (options.polling_interval)
    {
    }

    task_t<std::vector<peer_location_t>> list_peers (peer_location_filter_t filter)
    {
        auto rows = _store->list_peers (std::move (filter)).result ().value ();
        filter_live (rows);
        return completed (std::move (rows));
    }

    task_t<std::vector<peer_location_t>> list_raw_peers (peer_location_filter_t filter)
    {
        return _store->list_peers (std::move (filter));
    }

    task_t<std::optional<spot_location_t>> resolve_spot (spot_location_key_t key)
    {
        auto row = _store->resolve_spot (std::move (key)).result ().value ();
        return completed (live (std::move (row)));
    }

    task_t<location_page_t<spot_location_t>>
    list_spots (spot_location_filter_t filter, location_page_request_t page = {})
    {
        auto rows = _store->list_spots (std::move (filter), page).result ().value ();
        filter_live (rows.items);
        return completed (std::move (rows));
    }


    task_t<location_page_t<spot_location_t>>
    list_raw_spots (spot_location_filter_t filter, location_page_request_t page = {})
    {
        return _store->list_spots (std::move (filter), std::move (page));
    }

    task_t<std::optional<actor_location_t>> resolve_actor (actor_location_key_t key)
    {
        auto row = _store->resolve_actor (std::move (key)).result ().value ();
        return completed (live (std::move (row)));
    }

    task_t<location_page_t<actor_location_t>>
    list_actors (actor_location_filter_t filter, location_page_request_t page = {})
    {
        auto rows = _store->list_actors (std::move (filter), page).result ().value ();
        filter_live (rows.items);
        return completed (std::move (rows));
    }

    task_t<location_page_t<actor_location_t>>
    list_raw_actors (actor_location_filter_t filter, location_page_request_t page = {})
    {
        return _store->list_actors (std::move (filter), std::move (page));
    }

    task_t<std::optional<route_location_t>> resolve_route (route_location_key_t key)
    {
        auto row = _store->resolve_route (std::move (key)).result ().value ();
        return completed (live (std::move (row)));
    }

    task_t<location_page_t<route_location_t>>
    list_routes (route_location_filter_t filter, location_page_request_t page = {})
    {
        auto rows = _store->list_routes (std::move (filter), page).result ().value ();
        filter_live (rows.items);
        return completed (std::move (rows));
    }

    task_t<location_page_t<route_location_t>>
    list_raw_routes (route_location_filter_t filter, location_page_request_t page = {})
    {
        return _store->list_routes (std::move (filter), std::move (page));
    }

    std::set<std::string> live_owner_ids () { return live_owners (); }

    task_t<owner_lease_snapshot_t> list_owner_leases () { return _store->list_owner_leases (); }

  private:
    template <typename T> static task_t<T> completed (T value)
    {
        return task_t<T> (result_t<T>::success (std::move (value)));
    }

    std::set<std::string> live_owners ()
    {
        const auto now = std::chrono::steady_clock::now ();
        std::lock_guard lock (_lease_gate);
        if (!_lease_fetched_at || _polling_interval <= std::chrono::milliseconds::zero ()
            || now - *_lease_fetched_at >= _polling_interval) {
            const auto snapshot = _store->list_owner_leases ().result ().value ();
            _lease_expirations.clear ();
            const auto trace_enabled = [] {
                const char *value = std::getenv ("ZLINK_CPP_AUTO_CONNECT_TRACE");
                return value != nullptr && *value != '\0';
            } ();
            if (trace_enabled) {
                std::cerr << "zlink owner-lease snapshot"
                          << " monotonicMs="
                          << std::chrono::duration_cast<std::chrono::milliseconds> (
                               now.time_since_epoch ())
                               .count ()
                          << " owners=" << snapshot.leases.size ();
            }
            for (const auto &lease : snapshot.leases) {
                const auto remaining = lease.lease_expires_at - snapshot.store_now;
                if (remaining > std::chrono::system_clock::duration::zero ()) {
                    _lease_expirations[lease.owner_id] =
                      now + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
                              remaining);
                }
                if (trace_enabled) {
                    std::cerr << " owner=" << lease.owner_id
                              << ",remainingMs="
                              << std::chrono::duration_cast<std::chrono::milliseconds> (remaining)
                                   .count ();
                }
            }
            if (trace_enabled) {
                std::cerr << '\n';
            }
            _lease_fetched_at = now;
        }
        std::set<std::string> owners;
        for (const auto &[owner_id, expires_at] : _lease_expirations) {
            if (expires_at > now) {
                owners.insert (owner_id);
            }
        }
        return owners;
    }

    template <typename T> std::optional<T> live (std::optional<T> row)
    {
        if (!row) {
            return std::nullopt;
        }
        const auto owners = live_owners ();
        return owners.contains (row->owner_id) ? std::move (row) : std::nullopt;
    }

    template <typename T> void filter_live (std::vector<T> &rows)
    {
        const auto owners = live_owners ();
        std::erase_if (rows,
                       [&owners] (const T &row) { return !owners.contains (row.owner_id); });
    }

    location_store_t *_store;
    std::chrono::milliseconds _polling_interval;
    std::mutex _lease_gate;
    std::optional<std::chrono::steady_clock::time_point> _lease_fetched_at;
    std::map<std::string, std::chrono::steady_clock::time_point> _lease_expirations;
};

} // namespace zlink::framework::runtime
