#include "test_helpers.hpp"

#include <cstring>

int main()
{
    zlink::context_t ctx;

    const std::vector<transport_case_t> cases = transport_cases();
    for (size_t i = 0; i < cases.size(); ++i) {
        const transport_case_t &tc = cases[i];
        if (!transport_supported(tc))
            continue;

        zlink::socket_t router(ctx, zlink::socket_type::router);
        zlink::socket_t dealer(ctx, zlink::socket_type::dealer);

        std::string endpoint = endpoint_for(tc, "dealer-router");
        assert(router.bind(endpoint) == 0);
        if (tc.name != "inproc")
            endpoint = bound_endpoint(router);
        assert(dealer.connect(endpoint) == 0);
        sleep_ms(50);

        const char *payload = "hello";
        assert(dealer.send(payload, 5) == 5);

        zlink::message_t rid;
        zlink::message_t msg;
        assert(recv_msg_with_timeout(router, rid, 2000) >= 0);
        assert(recv_msg_with_timeout(router, msg, 2000) >= 0);
        assert(msg.size() == 5);
        assert(std::memcmp(msg.data(), "hello", 5) == 0);

        assert(router.send(rid, zlink::send_flag::sndmore) >= 0);
        const char *reply = "world";
        assert(router.send(reply, 5) == 5);

        char buf[16];
        const int rc = recv_with_timeout(dealer, buf, sizeof(buf), 2000);
        assert(rc == 5);
        assert(std::memcmp(buf, "world", 5) == 0);
    }

    return 0;
}
