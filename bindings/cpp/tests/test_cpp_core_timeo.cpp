#include "test_helpers.hpp"

#include <cerrno>
#include <cstring>

int main ()
{
    zlink::context_t ctx;

    const std::string endpoint = unique_inproc ("inproc://cpp-timeout-", "dealer");

    zlink::dealer_socket_t frontend (ctx);
    frontend.bind (endpoint);

    char buffer[32];
    assert (frontend.recv (buffer, sizeof (buffer), zlink::recv_flag::dontwait)
            == -1);
    assert (zlink_errno () == EAGAIN);

    const int timeout_ms = 250;
    const int jitter_ms = 70;
    assert (frontend.set_option (zlink::socket_option::rcvtimeo, timeout_ms) == 0);

    const auto start = std::chrono::steady_clock::now ();
    assert (frontend.recv (buffer, sizeof (buffer), zlink::recv_flag::none) == -1);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds> (
                           std::chrono::steady_clock::now () - start)
                           .count ();
    assert (zlink_errno () == EAGAIN);
    assert (elapsed >= timeout_ms - jitter_ms);

    zlink::dealer_socket_t backend (ctx);
    backend.connect (endpoint);
    assert (backend.set_option (zlink::socket_option::sndtimeo, timeout_ms) == 0);

    assert (backend.send ("Hello", 5) == 5);
    std::memset (buffer, 0, sizeof (buffer));
    assert (recv_with_timeout (frontend, buffer, sizeof (buffer), 2000) == 5);
    assert (std::memcmp (buffer, "Hello", 5) == 0);

    return 0;
}
