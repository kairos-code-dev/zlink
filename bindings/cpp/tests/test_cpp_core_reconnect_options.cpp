#include "test_helpers.hpp"

namespace {

bool expect_monitor_event (zlink::monitor_handle_t &monitor_,
                           uint64_t expected_event_,
                           int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink::monitor_event_t ev;
        while (monitor_recv_nowait (monitor_, &ev) == 0) {
            if (static_cast<uint64_t> (ev.event) == expected_event_)
                return true;
        }
        sleep_ms (10);
    }
    return false;
}

bool has_no_monitor_event (zlink::monitor_handle_t &monitor_, int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink::monitor_event_t ev;
        if (monitor_recv_nowait (monitor_, &ev) == 0)
            return false;
        sleep_ms (10);
    }
    return true;
}

void reconnect_default ()
{
    zlink::context_t ctx;
    zlink::pub_socket_t pub (ctx);
    zlink::sub_socket_t sub (ctx);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "reconnect-default");
    assert (pub.bind (endpoint) == 0);

    zlink::monitor_handle_t sub_mon = sub.monitor_handle (zlink::monitor_event::all);
    const int interval = 60 * 1000;
    assert (sub.set_option (zlink::socket_option::reconnect_ivl, interval) == 0);
    assert (sub.connect (endpoint) == 0);

    assert (expect_monitor_event (
      sub_mon, static_cast<uint64_t> (ZLINK_EVENT_CONNECT_DELAYED), 3000));
    assert (expect_monitor_event (
      sub_mon, static_cast<uint64_t> (ZLINK_EVENT_CONNECTED), 3000));
    assert (expect_monitor_event (
      sub_mon, static_cast<uint64_t> (ZLINK_EVENT_CONNECTION_READY), 3000));

    const int zero = 0;
    assert (pub.set_option (zlink::socket_option::linger, zero) == 0);
    assert (pub.close () == 0);

    assert (expect_monitor_event (
      sub_mon, static_cast<uint64_t> (ZLINK_EVENT_DISCONNECTED), 3000));
    assert (expect_monitor_event (
      sub_mon, static_cast<uint64_t> (ZLINK_EVENT_CONNECT_RETRIED), 3000));
    assert (has_no_monitor_event (sub_mon, 2000));

    assert (sub.set_option (zlink::socket_option::linger, zero) == 0);
    assert (sub.close () == 0);
    assert (sub_mon.close () == 0);
}

void reconnect_success ()
{
    zlink::context_t ctx;
    zlink::pub_socket_t pub (ctx);
    zlink::sub_socket_t sub (ctx);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "reconnect-success");
    assert (pub.bind (endpoint) == 0);

    zlink::monitor_handle_t sub_mon = sub.monitor_handle (zlink::monitor_event::all);
    const int interval = 1000;
    assert (sub.set_option (zlink::socket_option::reconnect_ivl, interval) == 0);
    assert (sub.connect (endpoint) == 0);

    assert (expect_monitor_event (
      sub_mon, static_cast<uint64_t> (ZLINK_EVENT_CONNECT_DELAYED), 3000));
    assert (expect_monitor_event (
      sub_mon, static_cast<uint64_t> (ZLINK_EVENT_CONNECTED), 3000));
    assert (expect_monitor_event (
      sub_mon, static_cast<uint64_t> (ZLINK_EVENT_CONNECTION_READY), 3000));

    const int zero = 0;
    assert (pub.set_option (zlink::socket_option::linger, zero) == 0);
    assert (pub.close () == 0);

    assert (expect_monitor_event (
      sub_mon, static_cast<uint64_t> (ZLINK_EVENT_DISCONNECTED), 3000));
    assert (expect_monitor_event (
      sub_mon, static_cast<uint64_t> (ZLINK_EVENT_CONNECT_RETRIED), 3000));
    assert (has_no_monitor_event (sub_mon, 300));

    zlink::pub_socket_t pub2 (ctx);
    assert (pub2.bind (endpoint) == 0);
    sleep_ms (300);

    assert (expect_monitor_event (
      sub_mon, static_cast<uint64_t> (ZLINK_EVENT_CONNECT_DELAYED), 3000));
    assert (expect_monitor_event (
      sub_mon, static_cast<uint64_t> (ZLINK_EVENT_CONNECTED), 3000));
    assert (expect_monitor_event (
      sub_mon, static_cast<uint64_t> (ZLINK_EVENT_CONNECTION_READY), 3000));
    assert (has_no_monitor_event (sub_mon, 300));

    assert (sub.set_option (zlink::socket_option::linger, zero) == 0);
    assert (pub2.set_option (zlink::socket_option::linger, zero) == 0);
    assert (sub.close () == 0);
    assert (pub2.close () == 0);
    assert (sub_mon.close () == 0);
}

} // namespace

int main ()
{
    reconnect_default ();
    reconnect_success ();
    return 0;
}
