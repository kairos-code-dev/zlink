/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/receive_dispatcher.hpp"

#include <utility>

namespace zlink::stream_connector::detail
{

void
receive_dispatcher_t::enqueue (packet_t packet)
{
  _state.dispatch_queue.push_back (std::move (packet));
}

} // namespace zlink::stream_connector::detail
