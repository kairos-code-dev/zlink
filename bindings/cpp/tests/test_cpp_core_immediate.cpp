#include "test_helpers.hpp"

#include <cerrno>
#include <cstring>

static bool wait_send_eagain (zlink::socket_t &sock_, const char *payload_, int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        const int rc =
          sock_.send (payload_, std::strlen (payload_), zlink::send_flag::dontwait);
        if (rc == -1 && zlink_errno () == EAGAIN)
            return true;
        sleep_ms (5);
    }

    return false;
}

static bool wait_send_success (zlink::socket_t &sock_, const char *payload_, int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        const int rc =
          sock_.send (payload_, std::strlen (payload_), zlink::send_flag::dontwait);
        if (rc == static_cast<int> (std::strlen (payload_)))
            return true;
        if (rc == -1 && zlink_errno () != EAGAIN)
            return false;
        sleep_ms (5);
    }

    return false;
}

int main ()
{
    zlink::context_t ctx;

    zlink::socket_t backend (ctx, zlink::socket_type::dealer);
    zlink::socket_t frontend (ctx, zlink::socket_type::dealer);

    const int zero = 0;
    assert (backend.set (zlink::socket_option::linger, zero) == 0);
    assert (frontend.set (zlink::socket_option::linger, zero) == 0);

    const int on = 1;
    assert (frontend.set (zlink::socket_option::immediate, on) == 0);

    std::string endpoint = endpoint_for (transport_case_t{"tcp", ""}, "immediate");
    assert (backend.bind (endpoint) == 0);

    assert (frontend.connect (endpoint) == 0);

    assert (backend.send ("Hello", 5) == 5);
    char recv_buf[16];
    std::memset (recv_buf, 0, sizeof (recv_buf));
    assert (recv_with_timeout (frontend, recv_buf, sizeof (recv_buf), 2000) == 5);
    assert (std::memcmp (recv_buf, "Hello", 5) == 0);

    assert (frontend.send ("Hello", 5, zlink::send_flag::dontwait) == 5);

    assert (backend.close () == 0);
    sleep_ms (200);

    assert (wait_send_eagain (frontend, "Hello", 2000));

    zlink::socket_t rebound_backend (ctx, zlink::socket_type::dealer);
    assert (rebound_backend.set (zlink::socket_option::linger, zero) == 0);
    assert (rebound_backend.bind (endpoint) == 0);

    assert (rebound_backend.send ("Hello", 5) == 5);
    std::memset (recv_buf, 0, sizeof (recv_buf));
    assert (recv_with_timeout (frontend, recv_buf, sizeof (recv_buf), 2000) == 5);
    assert (std::memcmp (recv_buf, "Hello", 5) == 0);

    assert (wait_send_success (frontend, "Hello", 2000));

    return 0;
}
