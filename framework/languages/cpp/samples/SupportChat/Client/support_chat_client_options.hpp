/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Configuration/sample_topology.hpp"

#include <chrono>
#include <string>

namespace zlink::samples::supportchat
{

struct support_chat_client_options_t
{
    explicit support_chat_client_options_t (const sample_topology_t &topology = sample_topology_t{})
    {
        stream_endpoint = topology.stream_endpoint;
    }

    std::string stream_endpoint;
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds request_timeout{5000};
    std::chrono::milliseconds idle_timeout{3000};
    std::chrono::milliseconds close_grace_timeout{2000};
};

} // namespace zlink::samples::supportchat
