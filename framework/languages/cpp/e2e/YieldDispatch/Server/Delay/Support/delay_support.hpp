/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include <string>
#include <utility>

namespace zlink::framework::e2e::yield_dispatch::server::delay {

struct delay_state_t
{
    explicit delay_state_t (std::string rid) : node_rid (std::move (rid)) {}
    std::string node_rid;
};

} // namespace zlink::framework::e2e::yield_dispatch::server::delay
