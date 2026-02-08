#include "test_helpers.hpp"

int main()
{
    zlink::context_t ctx;

    const std::vector<transport_case_t> cases = transport_cases();
    for (size_t i = 0; i < cases.size(); ++i) {
        const transport_case_t &tc = cases[i];
        if (!transport_supported(tc))
            continue;

        zlink::socket_t xpub(ctx, zlink::socket_type::xpub);
        zlink::socket_t xsub(ctx, zlink::socket_type::xsub);

        int verbose = 1;
        assert(xpub.set(zlink::socket_option::xpub_verbose, verbose) == 0);

        std::string endpoint = endpoint_for(tc, "xpub-xsub");
        assert(xpub.bind(endpoint) == 0);
        if (tc.name != "inproc")
            endpoint = bound_endpoint(xpub);
        assert(xsub.connect(endpoint) == 0);
        sleep_ms(50);

        const unsigned char sub[] = {1, 't', 'o', 'p', 'i', 'c'};
        assert(xsub.send(sub, sizeof(sub)) == static_cast<int>(sizeof(sub)));

        unsigned char buf[16];
        const int rc = recv_with_timeout(xpub, buf, sizeof(buf), 2000);
        assert(rc > 0);
        assert(buf[0] == 1);
    }

    return 0;
}
