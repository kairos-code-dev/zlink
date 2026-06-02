/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Configuration/sample_names.hpp"
#include "../Shared/Configuration/sample_topology.hpp"

#include <chrono>
#include <string>

namespace zlink::samples::tictactoe
{

struct tictactoe_client_options_t
{
  tictactoe_client_options_t ()
  {
    sample_topology_t topology;
    api_endpoint = topology.api_endpoint;
    play_endpoint = topology.stream_endpoint;
  }

  std::string api_endpoint;
  std::string play_endpoint;
  std::string game_name = "tictactoe-game";
  std::string x_actor_id = sample_names_t::x_actor_id;
  std::string o_actor_id = sample_names_t::o_actor_id;
  std::chrono::milliseconds stream_timeout { 5000 };
};

} // namespace zlink::samples::tictactoe
