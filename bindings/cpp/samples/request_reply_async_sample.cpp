/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

#include <coroutine>
namespace detail
{

class sample_task_t
{
  public:
    struct promise_type
    {
        sample_task_t get_return_object ()
        {
            return sample_task_t (_completion.get_future ());
        }

        std::suspend_never initial_suspend () noexcept { return {}; }
        std::suspend_never final_suspend () noexcept { return {}; }

        void return_void () { _completion.set_value (); }

        void unhandled_exception ()
        {
            _completion.set_exception (std::current_exception ());
        }

      private:
        std::promise<void> _completion;

        friend class sample_task_t;
    };

    explicit sample_task_t (std::future<void> completion_)
        : _completion (std::move (completion_))
    {
    }

    void wait () { _completion.get (); }

  private:
    std::future<void> _completion;
};

sample_task_t run_request_round_trip (zlink::request_dealer_t &dealer_)
{
    zlink::received_t reply = co_await dealer_.request (
      detail::make_message (detail::k_dealer_router_request),
      std::chrono::milliseconds (2000));
    assert (reply.parts.size () == 1);
    assert (reply.parts[0].to_string () == detail::k_dealer_router_reply);
}

} // namespace detail

int main ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router_socket (ctx);
    zlink::dealer_socket_t dealer_socket (ctx);
    zlink::monitor_handle_t router_monitor = router_socket.monitor_handle ();
    zlink::monitor_handle_t dealer_monitor = dealer_socket.monitor_handle ();

    std::string endpoint = detail::unique_tcp ("request-reply-async");
    zlink::routing_id_t routing_id ("request-reply-client");
    assert (dealer_socket.set_routing_id (routing_id) == 0);
    assert (router_socket.bind (endpoint) == 0);
    assert (dealer_socket.connect (endpoint) == 0);
    assert (detail::wait_connected (router_monitor, dealer_monitor));

    zlink::request_router_t router (router_socket);
    zlink::request_dealer_t dealer (dealer_socket);

    std::promise<void> request_handled;
    std::future<void> request_done = request_handled.get_future ();
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

    detail::sample_task_t round_trip = detail::run_request_round_trip (dealer);
    round_trip.wait ();
    assert (request_done.wait_for (std::chrono::milliseconds (2000))
            == std::future_status::ready);

    std::printf (
      "[dealer-router/request-reply/async] send: \"%s\" -> recv: \"%s\"\n",
      detail::k_dealer_router_request, detail::k_dealer_router_reply);
    return 0;
}
