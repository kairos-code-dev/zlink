#include <zlink.hpp>
#include <cstring>
#include <string>
#include <vector>

int main()
{
    zlink::context_t ctx;
    zlink::spot_node_t node(ctx);
    zlink::spot_t spot(node);

    if (spot.subscribe("chat:room1:msg") != 0)
        return 1;

    std::vector<zlink::message_t> parts;
    parts.emplace_back(5);
    std::memcpy(parts[0].data(), "hello", 5);

    if (spot.publish("chat:room1:msg", parts) != 0)
        return 1;

    zlink::msgv_t recv_parts;
    std::string topic;
    if (spot.recv(recv_parts, topic, 0) != 0)
        return 1;

    return 0;
}
