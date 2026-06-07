/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

#include <future>
#include <thread>

namespace detail
{

void run_request_round_trip (zlink::dealer_socket_t &dealer_)
{
    zlink::message_t request = detail::make_message (detail::k_dealer_router_request);
    std::vector<zlink::message_t> reply = dealer_.request ()
                                            .message (request)
                                            .timeout (std::chrono::milliseconds (2000))
                                            .submit_async ()
                                            .get ();
    assert (reply.size () == 1);
    assert (reply[0].to_string () == detail::k_dealer_router_reply);
}

} // namespace detail

int main ()
{
    // --8<-- [start:doc]
    zlink::context_t ctx;
    zlink::router_socket_t router_socket (ctx);
    zlink::dealer_socket_t dealer_socket (ctx);
    zlink::socket_monitor_t router_monitor = router_socket.monitor_open ();
    zlink::socket_monitor_t dealer_monitor = dealer_socket.monitor_open ();

    const std::string routing_id_text = "request-reply-client";
    const zlink::routing_id_t routing_id = zlink::routing_id_t::from (
      reinterpret_cast<const uint8_t *> (routing_id_text.data ()), routing_id_text.size ());
    dealer_socket.set_routing_id (routing_id);
    const std::string endpoint = "tcp://127.0.0.1:0";
    router_socket.bind (endpoint);
    const std::string bound_endpoint = router_socket.options ().last_endpoint ();
    assert (!bound_endpoint.empty ());
    dealer_socket.connect (bound_endpoint);
    assert (detail::wait_connected (router_monitor, dealer_monitor));

    zlink::message_t warmup = detail::make_message ("warmup");
    dealer_socket.send ().message (warmup).submit ();
    zlink::received_t warmup_received;
    assert (router_socket.recv (warmup_received) == 0);
    assert (warmup_received.parts ().size () == 1);
    assert (warmup_received.parts ()[0].to_string () == "warmup");
    warmup_received.close ();

    std::future<void> request_done =
      std::async (std::launch::async, [&router_socket, &routing_id] () {
          zlink::received_t received;
          assert (router_socket.recv (received) == 0);
          assert (received.routing_id ().has_value ());
          assert (received.routing_id ()->to_string () == routing_id.to_string ());
          assert (received.parts ().size () == 1);
          assert (received.parts ()[0].to_string () == detail::k_dealer_router_request);
          assert (received.request_seq ().has_value ());
          assert (*received.request_seq () != 0u);
          zlink::message_t reply = detail::make_message (detail::k_dealer_router_reply);
          received.reply ().message (reply).submit ();
          received.close ();
      });

    zlink::message_t request = detail::make_message (detail::k_dealer_router_request);
    std::vector<zlink::message_t> reply = dealer_socket.request ()
                                            .message (request)
                                            .timeout (std::chrono::milliseconds (5000))
                                            .submit_async ()
                                            .get ();
    assert (reply.size () == 1);
    assert (reply[0].to_string () == detail::k_dealer_router_reply);
    request_done.get ();

    std::printf ("[dealer-router/request-reply/async] send: \"%s\" -> recv: \"%s\"\n",
                 detail::k_dealer_router_request, detail::k_dealer_router_reply);
    std::fflush (stdout);
    return 0;
    // --8<-- [end:doc]
}
