/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/routing_id.hpp"

#include <optional>
#include <string>

namespace zlink
{

struct subscription_event_t
{
    subscription_event_t ()
        : routing_id (std::nullopt), topic (), subscribed (false)
    {
    }

    std::optional<routing_id_t> routing_id;
    std::string topic;
    bool subscribed;
};

struct subscription_filter_t
{
    std::string filter;
    bool is_pattern = false;
};

using subscription_entry_t = subscription_filter_t;

} // namespace zlink
