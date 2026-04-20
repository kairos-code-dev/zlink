/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    zlink::dealer_socket_t dealer (ctx);
    zlink::monitor_handle_t router_monitor = router.monitor_handle ();
    zlink::monitor_handle_t dealer_monitor = dealer.monitor_handle ();

    assert (router.bind ("tcp://127.0.0.1:0") == 0);
    const std::string endpoint = router.options ().last_endpoint ();
    assert (!endpoint.empty ());
    assert (dealer.connect (endpoint) == 0);
    assert (detail::wait_connected (router_monitor, dealer_monitor));

    const std::string sent = detail::k_dealer_router_request;
    zlink::message_t outbound = detail::make_message (sent);
    dealer.send (outbound);

    zlink::received_t inbound = router.recv ();
    assert (inbound.routing_id ().has_value ());
    assert (inbound.parts ().size () == 1);
    assert (inbound.parts ()[0].to_string () == detail::k_dealer_router_request);

    const std::string reply_payload = detail::k_dealer_router_reply;
    zlink::message_t reply = detail::make_message (reply_payload);
    router.send (*inbound.routing_id (), reply);
    inbound.close ();

    zlink::received_t echoed = dealer.recv ();
    assert (echoed.parts ().size () == 1);
    const std::string received = echoed.parts ()[0].to_string ();
    assert (received == detail::k_dealer_router_reply);
    echoed.close ();
    std::printf ("[dealer-router/recv] send: \"%s\" → recv: \"%s\"\n",
                 sent.c_str (), received.c_str ());
    return 0;
}
