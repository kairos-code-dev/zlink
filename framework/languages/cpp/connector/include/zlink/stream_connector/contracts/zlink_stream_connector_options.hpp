/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/stream_connector/contracts/zlink_stream_enums.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

namespace zlink::stream_connector
{

struct heartbeat_options_t
{
    bool enabled = true;
    std::chrono::milliseconds interval{1000};
    std::chrono::milliseconds timeout{5000};
};

struct reconnect_options_t
{
    bool enabled = true;
    std::chrono::milliseconds initial_delay{250};
    std::chrono::milliseconds max_delay{5000};
    double backoff_factor = 2.0;
    std::optional<int> max_attempts = 3;
};

struct connector_options_t
{
    std::string endpoint;
    transport_t transport = transport_t::tcp;
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds request_timeout{30000};
    heartbeat_options_t heartbeat;
    reconnect_options_t reconnect;
    std::size_t max_send_payload_size = 64 * 1024;
    std::size_t max_metadata_size = 8 * 1024;
    bool skip_server_certificate_validation = false;
    dispatch_mode_t dispatch_mode = dispatch_mode_t::manual;
    compression_t compression = compression_t::none;
};

} // namespace zlink::stream_connector
