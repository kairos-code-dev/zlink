/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/locations/spot_address_resolvers.hpp"

#include <zlink/framework/contracts/locations/spot_handle.hpp>

#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace zlink::framework
{

/* Address snapshot the public handle keeps opaque. mesh_name doubles as the
 * SPOT route channel id, matching the names auto-connect advertises under. */
struct spot_handle_t::state_t
{
    using refresh_fn_t = std::function<std::optional<runtime::spot_address_t> ()>;

    state_t (runtime::spot_address_t initial_address, refresh_fn_t refresh_address) :
        address (std::move (initial_address)),
        refresh (std::move (refresh_address))
    {
    }

    runtime::spot_address_t snapshot () const
    {
        std::lock_guard<std::mutex> lock (gate);
        return address;
    }

    /* Refreshes the snapshot from the location source. Returns false when the
     * target no longer has a live row, keeping the last known address. */
    bool refresh_snapshot ()
    {
        if (!refresh) {
            return false;
        }
        auto refreshed = refresh ();
        if (!refreshed) {
            return false;
        }
        std::lock_guard<std::mutex> lock (gate);
        address = std::move (*refreshed);
        return true;
    }

    mutable std::mutex gate;
    runtime::spot_address_t address;
    refresh_fn_t refresh;
};

namespace runtime
{

/* Internal fixed-address handle for coordination paths that already know the
 * (route channel, node rid, spot id) triple; no refresh source exists. */
inline spot_handle_t make_fixed_spot_handle (spot_address_t address)
{
    return framework::detail::spot_handle_access_t::make (std::move (address), nullptr);
}

} // namespace runtime

} // namespace zlink::framework
