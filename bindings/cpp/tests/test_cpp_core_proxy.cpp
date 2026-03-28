#include "test_helpers.hpp"
#include <zlink/runtime.hpp>

#if !defined(ZLINK_HAVE_WINDOWS)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

#if !defined(ZLINK_HAVE_WINDOWS)
void connect_with_retry (zlink::socket_t &socket_, const std::string &endpoint_)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (3000);
    while (std::chrono::steady_clock::now () < deadline) {
        if (socket_.connect (endpoint_) == 0)
            return;
        sleep_ms (10);
    }
    assert (false && "connect timeout");
}

[[noreturn]] void run_proxy_child (const std::string &frontend_ep_,
                                   const std::string &backend_ep_)
{
    zlink::context_t ctx;
    if (!ctx.valid ())
        _exit (2);

    zlink::socket_t frontend (ctx, zlink::socket_type::router);
    zlink::socket_t backend (ctx, zlink::socket_type::dealer);
    if (!frontend.valid () || !backend.valid ())
        _exit (3);

    const int linger = 0;
    (void) frontend.set (zlink::socket_option::linger, linger);
    (void) backend.set (zlink::socket_option::linger, linger);

    if (frontend.bind (frontend_ep_) != 0)
        _exit (4);
    if (backend.bind (backend_ep_) != 0)
        _exit (5);

    (void) zlink::proxy (frontend, backend);
    _exit (0);
}

void test_proxy_router_dealer_roundtrip ()
{
    const std::string frontend_ep = endpoint_for (transport_case_t{"tcp", ""},
                                                  "proxy-fe");
    const std::string backend_ep = endpoint_for (transport_case_t{"tcp", ""},
                                                 "proxy-be");

    const pid_t pid = fork ();
    assert (pid >= 0);

    if (pid == 0)
        run_proxy_child (frontend_ep, backend_ep);

    zlink::context_t ctx;
    assert (ctx.valid ());

    zlink::socket_t client (ctx, zlink::socket_type::dealer);
    zlink::socket_t worker (ctx, zlink::socket_type::dealer);
    assert (client.valid ());
    assert (worker.valid ());

    const int linger = 0;
    assert (client.set (zlink::socket_option::linger, linger) == 0);
    assert (worker.set (zlink::socket_option::linger, linger) == 0);

    connect_with_retry (client, frontend_ep);
    connect_with_retry (worker, backend_ep);

    sleep_ms (200);

    assert (client.send ("REQ", 3) == 3);

    zlink::message_t identity;
    zlink::message_t payload;
    assert (recv_msg_with_timeout (worker, identity, 2000) == 0);
    assert (recv_msg_with_timeout (worker, payload, 2000) == 0);
    assert (payload.size () == 3);
    assert (std::memcmp (payload.data (), "REQ", 3) == 0);

    assert (worker.send (identity, zlink::send_flag::sndmore) == 0);
    assert (worker.send ("REP", 3) == 3);

    char reply[8];
    const int reply_rc = recv_with_timeout (client, reply, sizeof (reply), 2000);
    assert (reply_rc == 3);
    assert (std::memcmp (reply, "REP", 3) == 0);

    (void) kill (pid, SIGTERM);
    int status = 0;
    (void) waitpid (pid, &status, 0);
}
#endif

} // namespace

int main ()
{
#if !defined(ZLINK_HAVE_WINDOWS)
    test_proxy_router_dealer_roundtrip ();
#endif
    return 0;
}
