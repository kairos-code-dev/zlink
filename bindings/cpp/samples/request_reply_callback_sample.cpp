/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

#include <cstdlib>
#include <thread>

int main ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router_socket (ctx);
    zlink::dealer_socket_t dealer_socket (ctx);
    zlink::monitor_handle_t router_monitor = router_socket.monitor_handle ();
    zlink::monitor_handle_t dealer_monitor = dealer_socket.monitor_handle ();

    const std::string routing_id_text = "request-reply-client";
    const zlink::routing_id_t routing_id = zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (routing_id_text.data ()),
      routing_id_text.size ());
    dealer_socket.set_routing_id (routing_id);
    const std::string endpoint =
      detail::unique_inproc ("request-reply-callback");
    assert (router_socket.bind (endpoint) == 0);
    assert (dealer_socket.connect (endpoint) == 0);
    assert (detail::wait_connected (router_monitor, dealer_monitor));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    zlink::message_t warmup = detail::make_message ("warmup");
    dealer_socket.send (warmup);
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    const zlink::received_t warmup_received = router_socket.recv ();
    assert (warmup_received.parts ().size () == 1);
    assert (warmup_received.parts ()[0].to_string () == "warmup");

    std::future<void> request_done = std::async (
      std::launch::async, [&router_socket, &routing_id] () {
          const zlink::received_t received = router_socket.recv ();
          assert (received.routing_id ().has_value ());
          assert (received.routing_id ()->to_string () == routing_id.to_string ());
          assert (received.parts ().size () == 1);
          assert (received.parts ()[0].to_string ()
                  == detail::k_dealer_router_request);
          assert (received.request_seq ().has_value ());
          assert (*received.request_seq () != 0u);
          zlink::message_t reply =
            detail::make_message (detail::k_dealer_router_reply);
          received.reply (reply);
      });

    std::promise<void> reply_handled;
    std::future<void> reply_done = reply_handled.get_future ();
    zlink::message_t request =
      detail::make_message (detail::k_dealer_router_request);
    dealer_socket.request (
      request,
      [&reply_handled] (zlink::request_result_t result,
                        std::vector<zlink::message_t> reply) {
          assert (result == zlink::request_result_t::ok);
          assert (reply.size () == 1);
          assert (reply[0].to_string () == detail::k_dealer_router_reply);
          reply_handled.set_value ();
      },
      zlink::send_flags_t::none, std::chrono::milliseconds (5000));

    request_done.get ();
    assert (reply_done.wait_for (std::chrono::milliseconds (5000))
            == std::future_status::ready);

    std::printf (
      "[dealer-router/request-reply/callback] send: \"%s\" -> recv: \"%s\"\n",
      detail::k_dealer_router_request, detail::k_dealer_router_reply);
    std::fflush (stdout);
    std::quick_exit (0);
}
