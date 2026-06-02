/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>

namespace zlink::samples::tictactoe
{

struct sample_topology_t
{
  std::string api_endpoint = "tcp://127.0.0.1:48103";
  std::string play_endpoint = "tcp://127.0.0.1:48104";
  std::string spot_endpoint = "tcp://127.0.0.1:48110";
  std::string stream_endpoint = "tcp://127.0.0.1:48112";
};

} // namespace zlink::samples::tictactoe
