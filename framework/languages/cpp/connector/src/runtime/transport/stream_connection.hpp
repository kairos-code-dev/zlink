/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/connector_runtime.hpp"

#include <optional>
#include <cstdint>
#include <string>
#include <vector>

namespace zlink::stream_connector::detail
{

struct endpoint_parts_t
{
  std::string host;
  std::string port;
};

std::optional<endpoint_parts_t> parse_tcp_endpoint (const std::string &endpoint);
bool is_transport_connected (const connector_state_t &state);
std::string read_line (connector_state_t &state);
void write_line (connector_state_t &state, const std::string &line);
std::vector<std::uint8_t> read_exact (connector_state_t &state,
                                      std::size_t size);
void write_bytes (connector_state_t &state,
                  const std::vector<std::uint8_t> &bytes);

} // namespace zlink::stream_connector::detail
