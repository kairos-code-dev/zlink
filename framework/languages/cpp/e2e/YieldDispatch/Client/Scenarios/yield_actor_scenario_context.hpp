/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>

namespace zlink::framework::e2e::yield_dispatch::client
{

struct yield_actor_scenario_context_t
{
    std::string spot_rid;
    std::string actor_a;
    std::string actor_b;
};

} // namespace zlink::framework::e2e::yield_dispatch::client
