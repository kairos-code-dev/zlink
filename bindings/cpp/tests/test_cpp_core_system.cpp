#include "test_helpers.hpp"

#include <vector>

#if defined(ZLINK_HAVE_WINDOWS)
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

void test_localhost ()
{
    zlink::context_t ctx;
    zlink::dealer_socket_t dealer (ctx);
    dealer.bind ("tcp://127.0.0.1:*");
}

void test_max_native_sockets ()
{
    int target = 1000;
#if !defined(ZLINK_HAVE_WINDOWS)
    struct rlimit rl;
    if (getrlimit (RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY) {
        const int safe = static_cast<int> (rl.rlim_cur > 128 ? rl.rlim_cur - 64 : 64);
        if (safe < target)
            target = safe;
    }
#endif

    if (target < 64)
        target = 64;

#if defined(ZLINK_HAVE_WINDOWS)
    std::vector<SOCKET> handles;
#else
    std::vector<int> handles;
#endif

    for (int i = 0; i < target; ++i) {
        const int fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd < 0)
            break;
        handles.push_back (fd);
    }

    assert (static_cast<int> (handles.size ()) >= 64);

    for (size_t i = 0; i < handles.size (); ++i)
#if defined(ZLINK_HAVE_WINDOWS)
        closesocket (handles[i]);
#else
        close (handles[i]);
#endif
}

} // namespace

int main ()
{
    test_localhost ();
    test_max_native_sockets ();
    return 0;
}
