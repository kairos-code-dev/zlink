/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

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
  std::string stream_endpoint = "tcp://127.0.0.1:47112";
};

} // namespace zlink::samples::bingo
