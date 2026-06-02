/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Core/routing_id.hpp>

#include <string>

namespace zlink::samples::tictactoe
{

struct sample_topology_t
{
  std::string registry_pub_endpoint = "tcp://127.0.0.1:48101";
  std::string registry_router_endpoint = "tcp://127.0.0.1:48102";
  std::string api_endpoint = "tcp://127.0.0.1:48103";
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
