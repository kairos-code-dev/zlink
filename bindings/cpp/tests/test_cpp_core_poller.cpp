#include "test_helpers.hpp"

#include <vector>

namespace {

void test_poller_modify_switches_event_mask ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t receiver (ctx);
    zlink::pair_socket_t sender (ctx);

    const std::string endpoint =
      unique_inproc ("inproc://cpp-poller-", "modify");
    receiver.bind (endpoint);
    sender.connect (endpoint);
    sleep_ms (50);

    zlink::poller_t poller;
    poller.add (receiver, zlink::poll_event::pollin);
    assert (poller.size () == 1);

    send_string_expect_success (sender, "ping");

    poller.modify (receiver, zlink::poll_event::pollout);

    std::vector<zlink::poll_event_t> events;
    assert (poller.wait (events, 1000) == 1);
    assert (!events.empty ());
    assert (events[0].socket == &receiver);
    assert ((events[0].revents & ZLINK_POLLOUT) != 0);
    assert ((events[0].revents & ZLINK_POLLIN) == 0);

    poller.modify (receiver, zlink::poll_event::pollin);

    events.clear ();
    assert (poller.wait (events, 1000) == 1);
    assert (!events.empty ());
    assert (events[0].socket == &receiver);
    assert ((events[0].revents & ZLINK_POLLIN) != 0);
    assert ((events[0].revents & ZLINK_POLLOUT) == 0);

    recv_string_expect_success (receiver, "ping");
}

} // namespace

int main ()
{
    test_poller_modify_switches_event_mask ();
    return 0;
}
