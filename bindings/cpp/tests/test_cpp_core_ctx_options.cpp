#include "test_helpers.hpp"

#include <cerrno>

#if defined(ZLINK_HAVE_LINUX)
#include <sched.h>
#endif

namespace {

void test_ctx_option_defaults ()
{
    zlink::context_t ctx;

    assert (zlink_ctx_get (ctx.handle (), ZLINK_MAX_SOCKETS)
            == ZLINK_MAX_SOCKETS_DFLT);

#if defined(ZLINK_USE_SELECT)
    assert (zlink_ctx_get (ctx.handle (), ZLINK_SOCKET_LIMIT) == FD_SETSIZE - 1);
#elif defined(ZLINK_USE_POLL) || defined(ZLINK_USE_EPOLL) || defined(ZLINK_USE_DEVPOLL) || defined(ZLINK_USE_KQUEUE)
    assert (zlink_ctx_get (ctx.handle (), ZLINK_SOCKET_LIMIT) == 65535);
#endif

    assert (zlink_ctx_get (ctx.handle (), ZLINK_IO_THREADS)
            == ZLINK_IO_THREADS_DFLT);
    assert (zlink_ctx_get (ctx.handle (), ZLINK_IPV6) == 0);

#if defined(ZLINK_MSG_T_SIZE)
    assert (zlink_ctx_get (ctx.handle (), ZLINK_MSG_T_SIZE)
            == static_cast<int> (sizeof (zlink_msg_t)));
#endif
}

void test_ctx_option_ipv6_set ()
{
    zlink::context_t ctx;
    assert (zlink_ctx_set (ctx.handle (), ZLINK_IPV6, 1) == 0);
    assert (zlink_ctx_get (ctx.handle (), ZLINK_IPV6) == 1);
}

void test_ctx_thread_opts ()
{
    zlink::context_t ctx;

    assert (zlink_ctx_set (ctx.handle (), ZLINK_THREAD_SCHED_POLICY,
                           ZLINK_THREAD_SCHED_POLICY_DFLT)
            == -1);
    assert (zlink_errno () == EINVAL);

    assert (zlink_ctx_set (ctx.handle (), ZLINK_THREAD_PRIORITY,
                           ZLINK_THREAD_PRIORITY_DFLT)
            == -1);
    assert (zlink_errno () == EINVAL);

#if defined(ZLINK_HAVE_LINUX)
    assert (zlink_ctx_set (ctx.handle (), ZLINK_THREAD_SCHED_POLICY, SCHED_OTHER)
            == 0);
#else
    assert (zlink_ctx_set (ctx.handle (), ZLINK_THREAD_SCHED_POLICY, 0) == 0);
#endif

    assert (zlink_ctx_set (ctx.handle (), ZLINK_THREAD_AFFINITY_CPU_ADD, 0) == 0);
    assert (zlink_ctx_set (ctx.handle (), ZLINK_THREAD_AFFINITY_CPU_REMOVE, 0)
            == 0);

    assert (zlink_ctx_set (ctx.handle (), ZLINK_THREAD_NAME_PREFIX, 1234) == 0);
    assert (zlink_ctx_get (ctx.handle (), ZLINK_THREAD_NAME_PREFIX) == 1234);
}

void test_ctx_zero_copy ()
{
#if defined(ZLINK_ZERO_COPY_RECV)
    zlink::context_t ctx;

    assert (zlink_ctx_get (ctx.handle (), ZLINK_ZERO_COPY_RECV) == 1);
    assert (zlink_ctx_set (ctx.handle (), ZLINK_ZERO_COPY_RECV, 0) == 0);
    assert (zlink_ctx_get (ctx.handle (), ZLINK_ZERO_COPY_RECV) == 0);

    zlink::socket_t pull (ctx, zlink::socket_type::dealer);
    zlink::socket_t push (ctx, zlink::socket_type::dealer);

    const std::string endpoint = endpoint_for (transport_case_t{"tcp", ""},
                                               "ctx-zero-copy");
    assert (pull.bind (endpoint) == 0);
    assert (push.connect (endpoint) == 0);

    send_string_expect_success (push, "abcd");
    send_string_expect_success (push,
                                "01234567890123456789012345678901234567890123456789");

    recv_string_expect_success (pull, "abcd");
    recv_string_expect_success (pull,
                                "01234567890123456789012345678901234567890123456789");

    assert (zlink_ctx_set (ctx.handle (), ZLINK_ZERO_COPY_RECV, 1) == 0);
    assert (zlink_ctx_get (ctx.handle (), ZLINK_ZERO_COPY_RECV) == 1);
#endif
}

void test_ctx_option_blocky ()
{
    zlink::context_t ctx;

    assert (zlink_ctx_set (ctx.handle (), ZLINK_IPV6, 1) == 0);

    {
        zlink::socket_t router (ctx, zlink::socket_type::router);
        int ipv6 = 0;
        size_t optlen = sizeof (ipv6);
        assert (zlink_getsockopt (router.handle (), ZLINK_IPV6, &ipv6, &optlen) == 0);
        assert (ipv6 == 1);

        int linger = 0;
        optlen = sizeof (linger);
        assert (zlink_getsockopt (router.handle (), ZLINK_LINGER, &linger, &optlen)
                == 0);
        assert (linger == -1);
    }

    assert (zlink_ctx_set (ctx.handle (), ZLINK_BLOCKY, 0) == 0);
    assert (zlink_ctx_get (ctx.handle (), ZLINK_BLOCKY) == 0);

    {
        zlink::socket_t router (ctx, zlink::socket_type::router);
        int linger = -1;
        size_t optlen = sizeof (linger);
        assert (zlink_getsockopt (router.handle (), ZLINK_LINGER, &linger, &optlen)
                == 0);
        assert (linger == 0);
    }
}

void test_ctx_option_invalid ()
{
    zlink::context_t ctx;
    assert (zlink_ctx_set (ctx.handle (), -1, 0) == -1);
    assert (zlink_errno () == EINVAL);

    assert (zlink_ctx_get (ctx.handle (), -1) == -1);
    assert (zlink_errno () == EINVAL);
}

} // namespace

int main ()
{
    test_ctx_option_defaults ();
    test_ctx_option_ipv6_set ();
    test_ctx_thread_opts ();
    test_ctx_zero_copy ();
    test_ctx_option_blocky ();
    test_ctx_option_invalid ();
    return 0;
}
