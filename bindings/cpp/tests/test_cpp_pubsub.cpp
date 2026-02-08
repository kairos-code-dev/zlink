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

        zlink::socket_t pub(ctx, zlink::socket_type::pub);
        zlink::socket_t sub(ctx, zlink::socket_type::sub);

        std::string endpoint = endpoint_for(tc, "pubsub");
        assert(pub.bind(endpoint) == 0);
        if (tc.name != "inproc")
            endpoint = bound_endpoint(pub);
        assert(sub.connect(endpoint) == 0);

        const char *topic = "topic";
        assert(sub.set(zlink::socket_option::subscribe, topic, 5) == 0);
        sleep_ms(50);

        const char *payload = "topic payload";
        assert(pub.send(payload, std::strlen(payload)) ==
               static_cast<int>(std::strlen(payload)));

        char buf[64];
        std::memset(buf, 0, sizeof(buf));
        const int rc = recv_with_timeout(sub, buf, sizeof(buf), 2000);
        assert(rc > 0);
        assert(std::memcmp(buf, "topic", 5) == 0);
    }

    return 0;
}
