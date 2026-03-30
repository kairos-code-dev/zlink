/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    zlink::dealer_socket_t dealer (ctx);
    zlink::monitor_handle_t router_monitor = router.monitor_handle ();
    zlink::monitor_handle_t dealer_monitor = dealer.monitor_handle ();

    const std::string endpoint =
      detail::unique_tcp ("dealer-router-recv");
    assert (router.bind (endpoint) == 0);
    assert (dealer.connect (endpoint) == 0);
    assert (detail::wait_for_socket_monitor_event (
      router_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));
    assert (detail::wait_for_socket_monitor_event (
      dealer_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));

    zlink::message_t outbound =
      detail::make_message ("dealer-router-recv");
    dealer.send (outbound);

    const zlink::received_t inbound = router.receive ();
    assert (inbound.routing_id.size > 0);
    assert (inbound.parts.size () == 1);
    assert (inbound.parts[0].to_string () == "dealer-router-recv");

    zlink::message_t reply = detail::make_message ("dealer-reply");
    router.send (inbound.routing_id, reply);

    const zlink::received_t echoed = dealer.receive ();
    assert (echoed.parts.size () == 1);
    assert (echoed.parts[0].to_string () == "dealer-reply");
    return 0;
}
