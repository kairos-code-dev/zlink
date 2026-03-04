#include "test_helpers.hpp"

#include <thread>
#include <vector>

namespace {

const int kThreadCount = 100;

void worker_connect_and_close (void *ctx_, const std::string endpoint_)
{
    void *socket = zlink_socket (ctx_, ZLINK_SUB);
    if (!socket)
        return;
    (void) zlink_connect (socket, endpoint_.c_str ());
    (void) zlink_close (socket);
}

void test_shutdown_stress ()
{
    for (int round = 0; round < 10; ++round) {
        void *ctx = zlink_ctx_new ();
        assert (ctx != NULL);
        assert (zlink_ctx_set (ctx, ZLINK_IO_THREADS, 7) == 0);

        void *pub = zlink_socket (ctx, ZLINK_PUB);
        assert (pub != NULL);

        assert (zlink_bind (pub, "tcp://127.0.0.1:*") == 0);
        char endpoint_buf[256] = {0};
        size_t endpoint_len = sizeof (endpoint_buf);
        assert (zlink_getsockopt (pub, ZLINK_LAST_ENDPOINT, endpoint_buf,
                                  &endpoint_len)
                == 0);
        const std::string endpoint (endpoint_buf);

        std::vector<std::thread> threads;
        threads.reserve (kThreadCount);
        for (int i = 0; i < kThreadCount; ++i)
            threads.push_back (std::thread (worker_connect_and_close, ctx, endpoint));

        for (size_t i = 0; i < threads.size (); ++i)
            threads[i].join ();

        assert (zlink_close (pub) == 0);
        assert (zlink_ctx_term (ctx) == 0);
    }
}

} // namespace

int main ()
{
    test_shutdown_stress ();
    return 0;
}
