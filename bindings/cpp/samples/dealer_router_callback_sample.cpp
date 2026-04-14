/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

#include <cstdlib>
#include <thread>

int main ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    zlink::dealer_socket_t dealer (ctx);
    zlink::monitor_handle_t router_monitor = router.monitor_handle ();
    zlink::monitor_handle_t dealer_monitor = dealer.monitor_handle ();

    const std::string routing_id_text = "dealer-router-callback-client";
    const zlink::routing_id_t routing_id = zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (routing_id_text.data ()),
      routing_id_text.size ());
    dealer.set_routing_id (routing_id);

    const std::string endpoint =
      detail::unique_inproc ("dealer-router-callback");
    assert (router.bind (endpoint) == 0);
    assert (dealer.connect (endpoint) == 0);
    assert (detail::wait_connected (router_monitor, dealer_monitor));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    zlink::message_t warmup = detail::make_message ("warmup");
    dealer.send (warmup);
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    const zlink::received_t warmup_received = router.recv ();
    assert (warmup_received.parts ().size () == 1);
    assert (warmup_received.parts ()[0].to_string () == "warmup");

    std::future<void> request_done = std::async (
      std::launch::async, [&router, &routing_id] () {
          const zlink::received_t request = router.recv ();
          assert (request.routing_id ().has_value ());
          assert (request.routing_id ()->to_string () == routing_id.to_string ());
          assert (request.parts ().size () == 1);
          assert (request.parts ()[0].to_string ()
                  == detail::k_dealer_router_request);
          assert (request.request_seq ().has_value ());

          zlink::message_t reply =
            detail::make_message (detail::k_dealer_router_reply);
          request.reply (reply);
      });

    std::promise<void> reply_handled;
    std::future<void> reply_done = reply_handled.get_future ();
    zlink::message_t request =
      detail::make_message (detail::k_dealer_router_request);
    dealer.request (
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

    std::printf ("[dealer-router/callback] send: \"%s\" -> recv: \"%s\"\n",
                 detail::k_dealer_router_request,
                 detail::k_dealer_router_reply);
    std::fflush (stdout);
    std::quick_exit (0);
}
