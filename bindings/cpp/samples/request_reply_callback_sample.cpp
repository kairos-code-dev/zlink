/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router_socket (ctx);
    zlink::dealer_socket_t dealer_socket (ctx);
    zlink::monitor_handle_t router_monitor = router_socket.monitor_handle ();
    zlink::monitor_handle_t dealer_monitor = dealer_socket.monitor_handle ();

    std::string endpoint = detail::unique_tcp ("request-reply-callback");
    zlink::routing_id_t routing_id ("request-reply-client");
    assert (dealer_socket.set_routing_id (routing_id) == 0);
    assert (router_socket.bind (endpoint) == 0);
    assert (dealer_socket.connect (endpoint) == 0);
    assert (detail::wait_connected (router_monitor, dealer_monitor));

    zlink::request_router_t router (router_socket);
    zlink::request_dealer_t dealer (dealer_socket);

    std::promise<void> request_handled;
    std::future<void> request_done = request_handled.get_future ();
    std::promise<void> reply_handled;
    std::future<void> reply_done = reply_handled.get_future ();

    router.on_receive (
      [&router, &request_handled, &routing_id] (zlink::received_t received) {
          assert (received.routing_id.to_string () == routing_id.to_string ());
          assert (received.parts.size () == 1);
          assert (received.parts[0].to_string ()
                  == detail::k_dealer_router_request);
          uint8_t msg_type = 0;
          uint64_t correlation_id = 0;
          assert (received.parts[0].get_request_info (&msg_type, &correlation_id)
                  == 0);
          assert (msg_type == static_cast<uint8_t> (
                                zlink::detail::request_reply_message_type_t::request));
          router.reply (received.routing_id, correlation_id,
                        detail::make_message (detail::k_dealer_router_reply));
          request_handled.set_value ();
      });

    dealer.request (
      detail::make_message (detail::k_dealer_router_request),
      [&reply_handled] (zlink::received_t reply) {
          assert (reply.parts.size () == 1);
          assert (reply.parts[0].to_string () == detail::k_dealer_router_reply);
          reply_handled.set_value ();
      },
      [] (zlink::error_t error) { throw error; }, std::chrono::milliseconds (2000));

    assert (request_done.wait_for (std::chrono::milliseconds (2000))
            == std::future_status::ready);
    assert (reply_done.wait_for (std::chrono::milliseconds (2000))
            == std::future_status::ready);

    std::printf (
      "[dealer-router/request-reply/callback] send: \"%s\" -> recv: \"%s\"\n",
      detail::k_dealer_router_request, detail::k_dealer_router_reply);
    return 0;
}
