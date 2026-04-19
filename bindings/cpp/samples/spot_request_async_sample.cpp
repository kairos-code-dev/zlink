/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t requester_node (ctx);
    zlink::service::spot_t requester = requester_node.create_spot ();
    zlink::router_socket_t responder_router (ctx);
    zlink::dealer_socket_t requester_dealer (ctx);
    assert (requester_node.valid ());
    assert (requester.valid ());
    assert (responder_router.valid ());
    assert (requester_dealer.valid ());

    const std::string channel_name = "orders";
    const std::string endpoint = detail::unique_tcp ("spot-channel-request");
    assert (responder_router.bind (endpoint) == 0);
    assert (requester_dealer.connect (endpoint) == 0);
    requester_node.attach_channel_dealer_manual (channel_name, requester_dealer);

    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> done (false);
    zlink::request_result_t result = zlink::request_result_t::terminated;
    std::vector<zlink::message_t> reply_parts;

    std::thread responder ([&responder_router] {
        const zlink::received_t received = responder_router.recv ();
        assert (received.parts ().size () == 1);
        assert (received.request_seq ().has_value ());
        assert (received.parts ()[0].to_string () == "spot-ping");
        zlink::message_t reply = detail::make_message ("spot-pong");
        received.reply (reply);
    });

    std::vector<zlink::message_t> request_parts;
    request_parts.push_back (detail::make_message ("spot-ping"));
    requester.request_channel (
      channel_name, request_parts,
      [&mutex, &cv, &done, &result, &reply_parts] (
        zlink::request_result_t request_result_,
        std::vector<zlink::message_t> reply_parts_) {
          {
              std::lock_guard<std::mutex> lock (mutex);
              result = request_result_;
              reply_parts = std::move (reply_parts_);
          }
          done.store (true, std::memory_order_release);
          cv.notify_one ();
      },
      zlink::send_flags_t::none, std::chrono::milliseconds (5000));

    {
        std::unique_lock<std::mutex> lock (mutex);
        cv.wait_for (lock, std::chrono::seconds (5),
                     [&done] { return done.load (std::memory_order_acquire); });
    }
    assert (done.load (std::memory_order_acquire));
    assert (result == zlink::request_result_t::ok);
    assert (reply_parts.size () == 1);
    const std::string reply = reply_parts[0].to_string ();
    assert (reply == "spot-pong");
    responder.join ();
    std::printf (
      "[spot/request/async] request: \"spot-ping\" -> reply: \"%s\"\n",
      reply.c_str ());
    return 0;
}
