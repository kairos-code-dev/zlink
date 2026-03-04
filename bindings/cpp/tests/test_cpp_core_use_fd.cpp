#include "test_helpers.hpp"

#if !defined(ZLINK_HAVE_WINDOWS)

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(ZLINK_HAVE_IPC)
#include <sys/un.h>
#endif

namespace {

int prebind_tcp (char *endpoint_, size_t endpoint_len_)
{
    const int fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert (fd >= 0);

    const int reuse = 1;
    assert (setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof (reuse))
            == 0);

    struct sockaddr_in addr;
    std::memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    addr.sin_port = htons (0);

    assert (bind (fd, reinterpret_cast<struct sockaddr *> (&addr), sizeof (addr)) == 0);
    assert (listen (fd, SOMAXCONN) == 0);

    struct sockaddr_in bound;
    socklen_t len = sizeof (bound);
    assert (getsockname (fd, reinterpret_cast<struct sockaddr *> (&bound), &len) == 0);

    const unsigned port = ntohs (bound.sin_port);
    std::snprintf (endpoint_, endpoint_len_, "tcp://127.0.0.1:%u", port);
    return fd;
}

#if defined(ZLINK_HAVE_IPC)
int prebind_ipc (char *endpoint_, size_t endpoint_len_, char *path_, size_t path_len_)
{
    static int seq = 0;
    std::snprintf (path_, path_len_, "/tmp/zlink-cpp-usefd-%d-%d.sock",
                   static_cast<int> (getpid ()), ++seq);

    const int fd = socket (AF_UNIX, SOCK_STREAM, 0);
    assert (fd >= 0);

    struct sockaddr_un addr;
    std::memset (&addr, 0, sizeof (addr));
    addr.sun_family = AF_UNIX;
    std::snprintf (addr.sun_path, sizeof (addr.sun_path), "%s", path_);

    unlink (path_);
    assert (bind (fd, reinterpret_cast<struct sockaddr *> (&addr), sizeof (addr)) == 0);
    assert (listen (fd, SOMAXCONN) == 0);

    std::snprintf (endpoint_, endpoint_len_, "ipc://%s", path_);
    return fd;
}
#endif

void test_pair_tcp_use_fd ()
{
    zlink::context_t ctx;
    zlink::socket_t sb (ctx, zlink::socket_type::pair);
    zlink::socket_t sc (ctx, zlink::socket_type::pair);

    char endpoint[128];
    const int fd = prebind_tcp (endpoint, sizeof (endpoint));

    assert (sb.set (zlink::socket_option::use_fd, &fd, sizeof (fd)) == 0);
    if (sb.bind (endpoint) != 0) {
        close (fd);
        return;
    }
    assert (sc.connect (endpoint) == 0);

    bounce (sb, sc);
}

#if defined(ZLINK_HAVE_IPC)
void test_pair_ipc_use_fd ()
{
    zlink::context_t ctx;
    zlink::socket_t sb (ctx, zlink::socket_type::pair);
    zlink::socket_t sc (ctx, zlink::socket_type::pair);

    char endpoint[256];
    char path[220];
    const int fd = prebind_ipc (endpoint, sizeof (endpoint), path, sizeof (path));

    assert (sb.set (zlink::socket_option::use_fd, &fd, sizeof (fd)) == 0);
    if (sb.bind (endpoint) != 0) {
        close (fd);
        unlink (path);
        return;
    }
    assert (sc.connect (endpoint) == 0);

    bounce (sb, sc);

    unlink (path);
}
#endif

} // namespace

int main ()
{
    test_pair_tcp_use_fd ();
#if defined(ZLINK_HAVE_IPC)
    test_pair_ipc_use_fd ();
#endif
    return 0;
}

#else

int main ()
{
    return 0;
}

#endif
