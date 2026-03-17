#include "test_helpers.hpp"

#if !defined(ZLINK_HAVE_WINDOWS)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

#if !defined(ZLINK_HAVE_WINDOWS)
void connect_with_retry (void *socket_, const std::string &endpoint_)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (3000);
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_connect (socket_, endpoint_.c_str ()) == 0)
            return;
        sleep_ms (10);
    }
    assert (false && "connect timeout");
}

[[noreturn]] void run_proxy_child (const std::string &frontend_ep_,
                                   const std::string &backend_ep_)
{
    void *ctx = zlink_ctx_new ();
    if (!ctx)
        _exit (2);

    void *frontend = zlink_socket (ctx, ZLINK_ROUTER, NULL);
    void *backend = zlink_socket (ctx, ZLINK_DEALER, NULL);
    if (!frontend || !backend)
        _exit (3);

    const int linger = 0;
    (void) zlink_setsockopt (frontend, ZLINK_LINGER, &linger, sizeof (linger));
    (void) zlink_setsockopt (backend, ZLINK_LINGER, &linger, sizeof (linger));

    if (zlink_bind (frontend, frontend_ep_.c_str ()) != 0)
        _exit (4);
    if (zlink_bind (backend, backend_ep_.c_str ()) != 0)
        _exit (5);

    (void) zlink_proxy (frontend, backend, NULL);
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

    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);

    void *client = zlink_socket (ctx, ZLINK_DEALER, NULL);
    void *worker = zlink_socket (ctx, ZLINK_DEALER, NULL);
    assert (client != NULL);
    assert (worker != NULL);

    const int linger = 0;
    assert (zlink_setsockopt (client, ZLINK_LINGER, &linger, sizeof (linger)) == 0);
    assert (zlink_setsockopt (worker, ZLINK_LINGER, &linger, sizeof (linger)) == 0);

    connect_with_retry (client, frontend_ep);
    connect_with_retry (worker, backend_ep);

    sleep_ms (200);

    assert (zlink_send (client, "REQ", 3, 0) == 3);

    zlink_msg_t identity;
    zlink_msg_t payload;
    assert (zlink_msg_init (&identity) == 0);
    assert (zlink_msg_init (&payload) == 0);

    assert (zlink_msg_recv (&identity, worker, 0) >= 0);
    assert (zlink_msg_recv (&payload, worker, 0) >= 0);
    assert (zlink_msg_size (&payload) == 3);
    assert (std::memcmp (zlink_msg_data (&payload), "REQ", 3) == 0);

    assert (zlink_msg_send (&identity, worker, ZLINK_SNDMORE) >= 0);
    assert (zlink_send (worker, "REP", 3, 0) == 3);

    char reply[8];
    int reply_rc = -1;
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (2000);
    while (std::chrono::steady_clock::now () < deadline) {
        reply_rc = zlink_recv (client, reply, sizeof (reply), ZLINK_DONTWAIT);
        if (reply_rc >= 0)
            break;
        if (zlink_errno () != EAGAIN)
            break;
        sleep_ms (1);
    }

    assert (reply_rc == 3);
    assert (std::memcmp (reply, "REP", 3) == 0);

    assert (zlink_msg_close (&payload) == 0);

    assert (zlink_close (client) == 0);
    assert (zlink_close (worker) == 0);
    assert (zlink_ctx_term (ctx) == 0);

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
