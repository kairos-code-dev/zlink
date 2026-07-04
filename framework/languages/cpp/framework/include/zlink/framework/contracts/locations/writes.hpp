/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

namespace zlink::framework
{

enum class location_write_intent_t
{
    new_claim = 1,
    renew = 2,
    takeover = 3
};

enum class location_write_status_t
{
    stored = 1,
    ignored_stale = 2,
    rejected_conflict = 3
};

struct location_write_result_t
{
    location_write_status_t status = location_write_status_t::ignored_stale;
    std::int64_t generation = 0;
    std::chrono::system_clock::time_point updated_at{};

    static location_write_result_t stored (std::int64_t generation,
                                           std::chrono::system_clock::time_point updated_at)
    {
        return location_write_result_t{location_write_status_t::stored, generation,
                                       std::move (updated_at)};
    }
};

struct location_owner_token_t
{
    std::string owner_id;
    std::int64_t generation = 0;
};

struct owner_lease_renewal_t
{
    std::chrono::system_clock::time_point lease_expires_at{};
    std::chrono::system_clock::time_point store_now{};
};

} // namespace zlink::framework
