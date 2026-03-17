#include "test_helpers.hpp"

#include <cstdio>
#include <cstring>

int main ()
{
    void *ctx1 = zlink_ctx_new ();
    void *ctx2 = zlink_ctx_new ();
    assert (ctx1 != NULL);
    assert (ctx2 != NULL);

    void *router = zlink_socket (ctx1, ZLINK_ROUTER, NULL);
    assert (router != NULL);

    int on = 1;
    assert (zlink_setsockopt (router, ZLINK_ROUTER_MANDATORY, &on, sizeof (on)) == 0);

    const std::string endpoint =
      endpoint_for (transport_case_t{"tcp", ""}, "issue-566");
    assert (zlink_bind (router, endpoint.c_str ()) == 0);

    for (int cycle = 0; cycle < 100; ++cycle) {
        void *dealer = zlink_socket (ctx2, ZLINK_DEALER, NULL);
        assert (dealer != NULL);

        char routing_id[11];
        std::snprintf (routing_id, sizeof (routing_id), "%09d", cycle);
        assert (zlink_setsockopt (dealer, ZLINK_ROUTING_ID, routing_id, 10) == 0);
        const int rcvtimeo = 1000;
        assert (zlink_setsockopt (dealer, ZLINK_RCVTIMEO, &rcvtimeo, sizeof (rcvtimeo))
                == 0);
        assert (zlink_connect (dealer, endpoint.c_str ()) == 0);

        for (int attempt = 0; attempt < 500; ++attempt) {
            (void) zlink_poll (NULL, 0, 2);
            const int rc = zlink_send (router, routing_id, 10, ZLINK_SNDMORE);
            if (rc == -1 && zlink_errno () == EHOSTUNREACH)
                continue;
            assert (rc == 10);
            assert (zlink_send (router, "HELLO", 5, 0) == 5);
            break;
        }

        char buf[16];
        std::memset (buf, 0, sizeof (buf));
        assert (zlink_recv (dealer, buf, sizeof (buf), 0) == 5);
        assert (std::memcmp (buf, "HELLO", 5) == 0);

        const int zero = 0;
        (void) zlink_setsockopt (dealer, ZLINK_LINGER, &zero, sizeof (zero));
        assert (zlink_close (dealer) == 0);
    }

    assert (zlink_close (router) == 0);
    assert (zlink_ctx_term (ctx1) == 0);
    assert (zlink_ctx_term (ctx2) == 0);
    return 0;
}
