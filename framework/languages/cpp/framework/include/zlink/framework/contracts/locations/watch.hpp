/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/locations/keys.hpp>
#include <zlink/framework/contracts/locations/values.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace zlink::framework
{

struct location_watch_filter_t
{
    location_kind_t kind = location_kind_t::peer;
    std::optional<std::string> mesh_name;
    std::optional<route_kind_t> route_kind;
};

enum class location_change_type_t
{
    upserted = 1,
    removed = 2,
    expired = 3
};

struct location_changed_t
{
    location_kind_t kind = location_kind_t::peer;
    location_key_t key;
    location_change_type_t change_type = location_change_type_t::upserted;
    std::int64_t generation = 0;
    std::chrono::system_clock::time_point updated_at{};
};

struct location_change_stamp_scope_t
{
    location_kind_t kind = location_kind_t::peer;
    std::optional<std::string> mesh_name;
};

using location_watch_callback_t = std::function<void (location_changed_t)>;

} // namespace zlink::framework
