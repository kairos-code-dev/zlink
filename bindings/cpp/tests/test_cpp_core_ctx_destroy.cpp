#include "test_helpers.hpp"

#include <atomic>
#include <cerrno>

namespace {

void test_ctx_destroy_basic ()
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);

    void *socket = zlink_socket (ctx, ZLINK_DEALER);
    assert (socket != NULL);

    assert (zlink_close (socket) == 0);
    assert (zlink_ctx_term (ctx) == 0);
}

void test_ctx_shutdown_disallows_new_socket ()
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);

    void *socket = zlink_socket (ctx, ZLINK_DEALER);
    assert (socket != NULL);
    assert (zlink_close (socket) == 0);

    assert (zlink_ctx_shutdown (ctx) == 0);

    void *after = zlink_socket (ctx, ZLINK_DEALER);
    assert (after == NULL);
    assert (zlink_errno () == ETERM);

    assert (zlink_ctx_term (ctx) == 0);
}

void test_ctx_shutdown_without_socket_disallows_new_socket ()
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);

    assert (zlink_ctx_shutdown (ctx) == 0);

    void *after = zlink_socket (ctx, ZLINK_DEALER);
    assert (after == NULL);
    assert (zlink_errno () == ETERM);

    assert (zlink_ctx_term (ctx) == 0);
}

void test_null_context_calls_fail ()
{
    assert (zlink_ctx_term (NULL) == -1);
    assert (zlink_errno () == EFAULT);

    assert (zlink_ctx_shutdown (NULL) == -1);
    assert (zlink_errno () == EFAULT);
}

#if defined(ZLINK_HAVE_POLLER)
struct poller_args_t
{
    void *ctx;
    std::atomic<int> ready;
    std::atomic<int> wait_errno;
};

void poller_thread (void *arg_)
{
    poller_args_t *args = static_cast<poller_args_t *> (arg_);

    void *socket = zlink_socket (args->ctx, ZLINK_DEALER);
    if (!socket)
        return;

    void *poller = zlink_poller_new ();
    if (!poller) {
        (void) zlink_close (socket);
        return;
    }

    if (zlink_poller_add (poller, socket, NULL, ZLINK_POLLIN) != 0) {
        (void) zlink_poller_destroy (&poller);
        (void) zlink_close (socket);
        return;
    }

    args->ready.store (1);

    zlink_poller_event_t event;
    const int rc = zlink_poller_wait (poller, &event, -1);
    if (rc == -1)
        args->wait_errno.store (zlink_errno ());

    (void) zlink_poller_destroy (&poller);
    (void) zlink_close (socket);
}

void test_poller_wait_exits_on_ctx_term ()
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);

    poller_args_t args;
    args.ctx = ctx;
    args.ready.store (0);
    args.wait_errno.store (0);

    void *thread = zlink_thread_start (&poller_thread, &args);
    assert (thread != NULL);

    while (args.ready.load () == 0)
        sleep_ms (10);

    assert (zlink_ctx_term (ctx) == 0);
    zlink_thread_join (thread);

    assert (args.wait_errno.load () == ETERM);
}
#endif

} // namespace

int main ()
{
    test_ctx_destroy_basic ();
    test_ctx_shutdown_disallows_new_socket ();
    test_ctx_shutdown_without_socket_disallows_new_socket ();
    test_null_context_calls_fail ();
#if defined(ZLINK_HAVE_POLLER)
    test_poller_wait_exits_on_ctx_term ();
#endif
    return 0;
}
