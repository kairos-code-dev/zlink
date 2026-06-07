/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>

#include <cstdlib>
#include <cstdint>
#include <string>

namespace zlink::samples::bingo
{

namespace detail
{
inline std::uint16_t sample_port_offset () noexcept
{
    const char *value = std::getenv ("ZLINK_CPP_SAMPLE_PORT_OFFSET");
    if (value == nullptr || *value == '\0') {
        return 0;
    }
    return static_cast<std::uint16_t> (std::atoi (value));
}

inline void apply_port_offset (std::string &endpoint, std::uint16_t offset)
{
    if (offset == 0) {
        return;
    }
    const auto colon = endpoint.rfind (':');
    if (colon == std::string::npos || colon + 1 >= endpoint.size ()) {
        return;
    }
    const auto port = static_cast<std::uint16_t> (
      static_cast<std::uint16_t> (std::atoi (endpoint.c_str () + colon + 1)) + offset);
    endpoint.replace (colon + 1, std::string::npos, std::to_string (port));
}
} // namespace detail

struct sample_topology_t
{
    sample_topology_t ()
    {
        const auto offset = detail::sample_port_offset ();
        detail::apply_port_offset (registry_pub_endpoint, offset);
        detail::apply_port_offset (registry_router_endpoint, offset);
        detail::apply_port_offset (api_channel_endpoint, offset);
        detail::apply_port_offset (play_channel_endpoint, offset);
        detail::apply_port_offset (play_spot_endpoint, offset);
        detail::apply_port_offset (play_spot_router_endpoint, offset);
        detail::apply_port_offset (session_spot_endpoint, offset);
        detail::apply_port_offset (session_router_endpoint, offset);
        detail::apply_port_offset (stream_endpoint, offset);
    }

    std::string registry_pub_endpoint = "tcp://127.0.0.1:47101";
    std::string registry_router_endpoint = "tcp://127.0.0.1:47102";
    std::string api_channel_endpoint = "tcp://127.0.0.1:47103";
    std::string play_channel_endpoint = "tcp://127.0.0.1:47104";
    std::string play_spot_endpoint = "tcp://127.0.0.1:47110";
    std::string play_spot_router_endpoint = "tcp://127.0.0.1:47111";
    std::string session_spot_endpoint = "tcp://127.0.0.1:47112";
    std::string session_router_endpoint = "tcp://127.0.0.1:47113";
    std::string stream_endpoint = "tcp://127.0.0.1:47114";
    zlink::routing_id_t session_router_rid = zlink::routing_id_t::from ("1101");
    zlink::routing_id_t session_pub_rid = zlink::routing_id_t::from ("1102");
    zlink::routing_id_t play_rid = zlink::routing_id_t::from ("2202");
};

} // namespace zlink::samples::bingo
