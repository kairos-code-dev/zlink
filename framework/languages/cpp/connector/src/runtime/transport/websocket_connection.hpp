/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>

namespace zlink::stream_connector::detail
{

class websocket_connection_t
{
public:
  explicit websocket_connection_t (std::string endpoint);
  const std::string &endpoint () const noexcept;

private:
  std::string _endpoint;
};

} // namespace zlink::stream_connector::detail
