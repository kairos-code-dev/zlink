#include <zlink.hpp>
#include <cstring>
#include <string>

int main()
{
    zlink::context_t ctx;
    zlink::socket_t server(ctx, ZLINK_PAIR);
    zlink::socket_t client(ctx, ZLINK_PAIR);

    const char *endpoint = "inproc://cpp-pair-basic";
    if (server.bind(endpoint) != 0)
        return 1;
    if (client.connect(endpoint) != 0)
        return 1;

    const std::string payload = "hello";
    if (client.send(payload) < 0)
        return 1;

    char buf[16] = {0};
    const int rc = server.recv(buf, sizeof(buf), 0);
    if (rc <= 0)
        return 1;

    return 0;
}
