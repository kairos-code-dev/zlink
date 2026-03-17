#include "test_helpers.hpp"

#include <vector>

#if !defined(ZLINK_HAVE_WINDOWS)
#include <sys/resource.h>
#endif

namespace {

void test_system_max ()
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);

    const int requested_sockets = 2 * 65536;
    int no_of_sockets = requested_sockets;

#if !defined(ZLINK_HAVE_WINDOWS)
    struct rlimit rl;
    if (getrlimit (RLIMIT_NOFILE, &rl) == 0) {
        const unsigned long reserve_fds = 256;
        const unsigned long fds_per_socket = 4;
        const unsigned long max_fds =
          (rl.rlim_cur == RLIM_INFINITY) ? 1048576UL
                                         : static_cast<unsigned long> (rl.rlim_cur);

        if (max_fds > reserve_fds) {
            const unsigned long max_sockets =
              (max_fds - reserve_fds) / fds_per_socket;
            if (max_sockets < static_cast<unsigned long> (no_of_sockets))
                no_of_sockets = static_cast<int> (max_sockets);
        }
    }
#endif

    if (no_of_sockets < 1)
        no_of_sockets = 1;

    assert (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, no_of_sockets) == 0);

    std::vector<void *> sockets;
    while (true) {
        void *socket = zlink_socket (ctx, ZLINK_PAIR, NULL);
        if (!socket)
            break;
        sockets.push_back (socket);
    }

    assert (no_of_sockets <= static_cast<int> (sockets.size ()));

    for (int i = 0; i < 10; ++i)
        assert (zlink_socket (ctx, ZLINK_PAIR, NULL) == NULL);

    for (size_t i = 0; i < sockets.size (); ++i)
        assert (zlink_close (sockets[i]) == 0);

    assert (zlink_ctx_term (ctx) == 0);
}

void test_default_max ()
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);

    std::vector<void *> sockets;
    while (true) {
        void *socket = zlink_socket (ctx, ZLINK_PAIR, NULL);
        if (!socket)
            break;
        sockets.push_back (socket);
    }

    assert (static_cast<size_t> (ZLINK_MAX_SOCKETS_DFLT) <= sockets.size ());

    for (int i = 0; i < 10; ++i)
        assert (zlink_socket (ctx, ZLINK_PAIR, NULL) == NULL);

    for (size_t i = 0; i < sockets.size (); ++i)
        assert (zlink_close (sockets[i]) == 0);

    assert (zlink_ctx_term (ctx) == 0);
}

} // namespace

int main ()
{
    test_system_max ();
    test_default_max ();
    return 0;
}
