#include "test_helpers.hpp"

#include <cerrno>
#include <cstring>

template<typename SocketLike>
static bool wait_send_eagain (SocketLike &sock_,
                              const char *payload_,
                              int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        const int rc = raw_send (
          sock_, payload_, std::strlen (payload_), zlink::send_flag::dontwait);
        if (rc == -1 && zlink_errno () == EAGAIN)
            return true;
        sleep_ms (5);
    }

    return false;
}

template<typename SocketLike>
static bool wait_send_success (SocketLike &sock_,
                               const char *payload_,
                               int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        const int rc = raw_send (
          sock_, payload_, std::strlen (payload_), zlink::send_flag::dontwait);
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

    zlink::dealer_socket_t backend (ctx);
    zlink::dealer_socket_t frontend (ctx);

    const int zero = 0;
    assert (backend.set_option (zlink::socket_option::linger, zero) == 0);
    assert (frontend.set_option (zlink::socket_option::linger, zero) == 0);

    const int on = 1;
    assert (frontend.set_option (zlink::socket_option::immediate, on) == 0);

    std::string endpoint = endpoint_for (transport_case_t{"tcp", ""}, "immediate");
    backend.bind (endpoint);

    frontend.connect (endpoint);

    assert (raw_send (backend, "Hello", 5) == 5);
    char recv_buf[16];
    std::memset (recv_buf, 0, sizeof (recv_buf));
    assert (recv_with_timeout (frontend, recv_buf, sizeof (recv_buf), 2000) == 5);
    assert (std::memcmp (recv_buf, "Hello", 5) == 0);

    assert (raw_send (frontend, "Hello", 5, zlink::send_flag::dontwait) == 5);

    backend.close ();
    sleep_ms (200);

    assert (wait_send_eagain (frontend, "Hello", 2000));

    zlink::dealer_socket_t rebound_backend (ctx);
    assert (rebound_backend.set_option (zlink::socket_option::linger, zero) == 0);
    rebound_backend.bind (endpoint);

    assert (raw_send (rebound_backend, "Hello", 5) == 5);
    std::memset (recv_buf, 0, sizeof (recv_buf));
    assert (recv_with_timeout (frontend, recv_buf, sizeof (recv_buf), 2000) == 5);
    assert (std::memcmp (recv_buf, "Hello", 5) == 0);

    assert (wait_send_success (frontend, "Hello", 2000));

    return 0;
}
