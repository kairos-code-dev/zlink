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

        zlink::socket_t a(ctx, zlink::socket_type::pair);
        zlink::socket_t b(ctx, zlink::socket_type::pair);

        std::string endpoint = endpoint_for(tc, "multipart");
        assert(a.bind(endpoint) == 0);
        if (tc.name != "inproc")
            endpoint = bound_endpoint(a);
        assert(b.connect(endpoint) == 0);
        sleep_ms(50);

        assert(b.send("a", 1, zlink::send_flag::sndmore) == 1);
        assert(b.send("b", 1, zlink::send_flag::none) == 1);

        zlink::message_t part1;
        zlink::message_t part2;
        assert(recv_msg_with_timeout(a, part1, 2000) >= 0);
        assert(part1.more());
        assert(recv_msg_with_timeout(a, part2, 2000) >= 0);
        assert(!part2.more());
        assert(part1.size() == 1);
        assert(part2.size() == 1);
        assert(std::memcmp(part1.data(), "a", 1) == 0);
        assert(std::memcmp(part2.data(), "b", 1) == 0);
    }

    return 0;
}
