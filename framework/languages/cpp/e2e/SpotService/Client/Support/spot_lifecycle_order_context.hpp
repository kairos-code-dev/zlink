/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <string>

namespace zlink::framework::e2e::spot_service::client
{

struct spot_lifecycle_order_context_t
{
    std::string key = "spot-owner-order-sm-a4";
    std::string spot_rid = user_spot_rid_for_key (key);
    int current_value = 0;
};

} // namespace zlink::framework::e2e::spot_service::client
