#include "test_helpers.hpp"

#include <zlink_c.h>

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
            zlink_msg_t routing_part;
            assert (zlink_msg_init_size (&routing_part, 10) == 0);
            std::memcpy (zlink_msg_data (&routing_part), routing_id, 10);
            const zlink_submit_result_t rc = zlink_send_part (
              router, &routing_part, static_cast<zlink_send_flags_t> (0),
              ZLINK_PART_MORE);
            if (rc != ZLINK_SUBMIT_OK && zlink_errno () == EHOSTUNREACH) {
                assert (zlink_msg_close (&routing_part) == 0);
                continue;
            }
            assert (rc == ZLINK_SUBMIT_OK);

            zlink_msg_t hello_part;
            assert (zlink_msg_init_size (&hello_part, 5) == 0);
            std::memcpy (zlink_msg_data (&hello_part), "HELLO", 5);
            assert (
              zlink_send_part (router, &hello_part,
                               static_cast<zlink_send_flags_t> (0),
                               ZLINK_PART_FINAL)
              == ZLINK_SUBMIT_OK);
            break;
        }

        char buf[16];
        std::memset (buf, 0, sizeof (buf));
        const zlink_routing_id_t *source_rid = NULL;
        zlink_msg_t recv_part;
        assert (zlink_msg_init (&recv_part) == 0);
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        assert (zlink_recv_part (dealer, &source_rid, &recv_part, &has_more,
                                 static_cast<zlink_recv_flags_t> (0))
                == ZLINK_RECV_OK);
        assert (!has_more);
        assert (zlink_msg_size (&recv_part) == 5);
        std::memcpy (buf, zlink_msg_data (&recv_part), zlink_msg_size (&recv_part));
        assert (zlink_msg_close (&recv_part) == 0);
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
