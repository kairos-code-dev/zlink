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
        _store (&store)
    {
        static_cast<void> (options);
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

    std::set<std::string> live_owner_ids ()
    {
        std::set<std::string> owner_ids;
        for (const auto &row :
             _store->list_peers ({}).result ().value ())
            owner_ids.insert (row.owner_id);
        collect_page_owner_ids (
          owner_ids,
          [this] (location_page_request_t page) {
              return _store
                ->list_spots ({}, page)
                .result ()
                .value ();
          });
        collect_page_owner_ids (
          owner_ids,
          [this] (location_page_request_t page) {
              return _store
                ->list_actors ({}, page)
                .result ()
                .value ();
          });
        collect_page_owner_ids (
          owner_ids,
          [this] (location_page_request_t page) {
              return _store
                ->list_routes ({}, page)
                .result ()
                .value ();
          });
        return live_owners (owner_ids);
    }

  private:
    template <typename T> static task_t<T> completed (T value)
    {
        return task_t<T> (result_t<T>::success (std::move (value)));
    }

    template <typename PageLoader>
    static void collect_page_owner_ids (
      std::set<std::string> &owner_ids,
      PageLoader load)
    {
        location_page_request_t request;
        do {
            const auto page = load (request);
            for (const auto &row : page.items)
                owner_ids.insert (row.owner_id);
            request.continuation_token =
              page.continuation_token;
        } while (request.continuation_token);
    }

    std::set<std::string> live_owners (
      const std::set<std::string> &owner_ids)
    {
        std::set<std::string> owners;
        for (const auto &owner_id : owner_ids) {
            const auto lease =
              _store->read_owner_lease (owner_id)
                .result ()
                .value ();
            const auto *found =
              std::get_if<owner_lease_found_t> (&lease);
            if (found != nullptr
                && found->lease_expires_at
                     > found->store_now)
                owners.insert (owner_id);
        }
        return owners;
    }

    template <typename T> std::optional<T> live (std::optional<T> row)
    {
        if (!row) {
            return std::nullopt;
        }
        const auto owners =
          live_owners ({row->owner_id});
        return owners.contains (row->owner_id) ? std::move (row) : std::nullopt;
    }

    template <typename T> void filter_live (std::vector<T> &rows)
    {
        std::set<std::string> owner_ids;
        for (const auto &row : rows)
            owner_ids.insert (row.owner_id);
        const auto owners = live_owners (owner_ids);
        std::erase_if (rows,
                       [&owners] (const T &row) { return !owners.contains (row.owner_id); });
    }

    location_store_t *_store;
};

} // namespace zlink::framework::runtime
