/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Configuration/sample_topology.hpp"

#include <chrono>
#include <string>

namespace zlink::samples::bingo
{

struct bingo_client_options_t
{
  bingo_client_options_t ()
  {
    sample_topology_t topology;
    stream_endpoint = topology.stream_endpoint;
  }

  std::string stream_endpoint;
  std::chrono::milliseconds connect_timeout { 5000 };
  std::chrono::milliseconds request_timeout { 5000 };
  bool use_embedded_server = true;
};

} // namespace zlink::samples::bingo
