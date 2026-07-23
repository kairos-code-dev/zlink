/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <cstdint>
#include <string>

namespace zlink::framework
{

enum class location_auto_connect_type_t
{
    invalid = 0,
    route_mesh = 1,
    client_server = 2,
    dealer_mesh = 3,
    fanout = 4,
    spot_mesh = 5
};

// Matches the core discovery uint16_t service_role values. Value 1 is the
// reserved slot for the removed gateway role and must not be reused.
enum class location_role_t : std::uint16_t
{
    invalid = 0,
    spot = 2,
    router = 3,
    dealer = 4,
    pub = 5,
    sub = 6
};

enum class route_kind_t
{
    invalid = 0,
    actor_session = 1,
    spot_name = 2,
    framework_route = 3
};

enum class location_kind_t
{
    invalid = 0,
    peer = 1,
    spot = 2,
    actor = 3,
    route = 4
};

enum class placement_object_kind_t
{
    actor = 1,
    user_spot = 2,
    instance_spot = 3
};

} // namespace zlink::framework
