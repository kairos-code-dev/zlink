/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>

#include <string>

namespace zlink::samples::bingo
{

struct sample_topology_t
{
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
