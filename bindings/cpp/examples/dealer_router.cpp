#include <zlink.hpp>
#include <cstring>
#include <string>

int main()
{
    zlink::context_t ctx;
    zlink::socket_t router(ctx, ZLINK_ROUTER);
    zlink::socket_t dealer(ctx, ZLINK_DEALER);

    const char *endpoint = "inproc://cpp-dealer-router";
    if (router.bind(endpoint) != 0)
        return 1;
    if (dealer.connect(endpoint) != 0)
        return 1;

    // Send from dealer to router
    const std::string payload = "ping";
    if (dealer.send(payload) < 0)
        return 1;

    // Router receives routing id and payload
    zlink::message_t rid;
    if (router.recv(rid) < 0)
        return 1;

    zlink::message_t msg;
    if (router.recv(msg) < 0)
        return 1;

    // Echo back
    if (router.send(rid, ZLINK_SNDMORE) < 0)
        return 1;
    if (router.send(msg) < 0)
        return 1;

    // Dealer receives reply
    zlink::message_t reply;
    if (dealer.recv(reply) < 0)
        return 1;

    return 0;
}
