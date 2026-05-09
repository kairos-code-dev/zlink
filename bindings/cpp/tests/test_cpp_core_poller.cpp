#include "test_helpers.hpp"

#include <any>
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
    poller.add (receiver, zlink::poll_event_flag_t::pollin, &receiver);
    assert (poller.size () == 1);

    send_string_expect_success (sender, "ping");

    poller.modify (receiver, zlink::poll_event_flag_t::pollout);

    std::optional<zlink::poll_event_t> event =
      poller.wait (std::chrono::milliseconds (1000));
    assert (event.has_value ());
    zlink::pair_socket_t **first_tag =
      std::any_cast<zlink::pair_socket_t *> (&event->tag);
    assert (first_tag && *first_tag == &receiver);
    assert ((static_cast<short> (event->revents)
             & static_cast<short> (zlink::poll_event_flag_t::pollout))
            != 0);
    assert ((static_cast<short> (event->revents)
             & static_cast<short> (zlink::poll_event_flag_t::pollin))
            == 0);

    poller.modify (receiver, zlink::poll_event_flag_t::pollin);

    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    poller.wait_all (events, 0, std::chrono::milliseconds (1000));
    assert (events.size () == 1);
    zlink::pair_socket_t **second_tag =
      std::any_cast<zlink::pair_socket_t *> (&events[0].tag);
    assert (second_tag && *second_tag == &receiver);
    assert ((static_cast<short> (events[0].revents)
             & static_cast<short> (zlink::poll_event_flag_t::pollin))
            != 0);
    assert ((static_cast<short> (events[0].revents)
             & static_cast<short> (zlink::poll_event_flag_t::pollout))
            == 0);

    recv_string_expect_success (receiver, "ping");
}

} // namespace

int main ()
{
    test_poller_modify_switches_event_mask ();
    return 0;
}
