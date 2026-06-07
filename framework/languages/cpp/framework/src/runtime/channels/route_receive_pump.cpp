/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/route_receive_pump.hpp"

#include <utility>

namespace zlink::framework::detail
{

route_receive_pump_t::route_receive_pump_t (route_packet_dispatcher_t dispatcher) :
    _dispatcher (std::move (dispatcher))
{
}

void route_receive_pump_t::enqueue (route_received_packet_t packet)
{
    _packets.push_back (std::move (packet));
}

result_t<route_receive_result_t> route_receive_pump_t::drain ()
{
    route_receive_result_t result;
    while (!_packets.empty ()) {
        auto packet = std::move (_packets.front ());
        _packets.pop_front ();
        auto dispatch = _dispatcher.dispatch (packet);
        if (!dispatch) {
            return result_t<route_receive_result_t>::failure (
              dispatch.error_kind (),
              dispatch.error () ? dispatch.error ()->what () : "route packet dispatch failed");
        }
        ++result.dispatched;
        if (dispatch.value ().has_value ()) {
            result.replies.push_back (std::move (*dispatch.value ()));
        }
    }
    return result_t<route_receive_result_t>::success (std::move (result));
}

std::size_t route_receive_pump_t::pending_count () const noexcept
{
    return _packets.size ();
}

} // namespace zlink::framework::detail
