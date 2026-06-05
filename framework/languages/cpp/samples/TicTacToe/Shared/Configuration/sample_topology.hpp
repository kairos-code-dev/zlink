/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>

#include <cstdlib>
#include <cstdint>
#include <string>

namespace zlink::samples::tictactoe
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
    const auto port =
      static_cast<std::uint16_t> (static_cast<std::uint16_t> (std::atoi (endpoint.c_str () + colon + 1)) + offset);
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
        detail::apply_port_offset (api_endpoint, offset);
        detail::apply_port_offset (api_http_endpoint, offset);
        detail::apply_port_offset (play_endpoint, offset);
        detail::apply_port_offset (session_spot_endpoint, offset);
        detail::apply_port_offset (session_router_endpoint, offset);
        detail::apply_port_offset (play_router_endpoint, offset);
        detail::apply_port_offset (play_spot_endpoint, offset);
        detail::apply_port_offset (play_spot_router_endpoint, offset);
        detail::apply_port_offset (stream_endpoint, offset);
    }

    std::string registry_pub_endpoint = "tcp://127.0.0.1:48101";
    std::string registry_router_endpoint = "tcp://127.0.0.1:48102";
    std::string api_endpoint = "tcp://127.0.0.1:48103";
    std::string api_http_endpoint = "http://127.0.0.1:48113";
    std::string play_endpoint = "tcp://127.0.0.1:48104";
    std::string session_spot_endpoint = "tcp://127.0.0.1:48105";
    std::string session_router_endpoint = "tcp://127.0.0.1:48106";
    std::string play_router_endpoint = "tcp://127.0.0.1:48109";
    std::string play_spot_endpoint = "tcp://127.0.0.1:48110";
    std::string play_spot_router_endpoint = "tcp://127.0.0.1:48111";
    std::string stream_endpoint = "tcp://127.0.0.1:48112";
    zlink::routing_id_t session_rid = zlink::routing_id_t::from ("1101");
    zlink::routing_id_t play_rid = zlink::routing_id_t::from ("2202");
};

} // namespace zlink::samples::tictactoe
