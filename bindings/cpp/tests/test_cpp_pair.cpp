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

        zlink::socket_t server(ctx, zlink::socket_type::pair);
        zlink::socket_t client(ctx, zlink::socket_type::pair);

        std::string endpoint = endpoint_for(tc, "pair");
        assert(server.bind(endpoint) == 0);
        if (tc.name != "inproc")
            endpoint = bound_endpoint(server);
        assert(client.connect(endpoint) == 0);
        sleep_ms(50);

        const char *payload = "ping";
        assert(client.send(payload, 4) == 4);

        char buf[16];
        std::memset(buf, 0, sizeof(buf));
        const int rc = recv_with_timeout(server, buf, sizeof(buf), 2000);
        assert(rc == 4);
        assert(std::memcmp(buf, "ping", 4) == 0);
    }

    return 0;
}
