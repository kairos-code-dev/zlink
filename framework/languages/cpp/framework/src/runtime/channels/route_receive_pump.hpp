/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/channels/route_packet_dispatcher.hpp"

#include <deque>
#include <vector>

namespace zlink::framework::detail
{

struct route_receive_result_t
{
  std::size_t dispatched = 0;
  std::vector<route_dispatch_reply_t> replies;
};

class route_receive_pump_t
{
public:
  explicit route_receive_pump_t (route_packet_dispatcher_t dispatcher);

  void enqueue (route_received_packet_t packet);
  result_t<route_receive_result_t> drain ();
  std::size_t pending_count () const noexcept;

private:
  route_packet_dispatcher_t _dispatcher;
  std::deque<route_received_packet_t> _packets;
};

} // namespace zlink::framework::detail
