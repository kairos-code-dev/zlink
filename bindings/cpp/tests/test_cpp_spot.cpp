#include <zlink.hpp>

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

int main()
{
    zlink::context_t ctx;
    zlink::spot_node_t node(ctx);
    zlink::spot_t spot(node);

    assert(spot.subscribe("chat:room1:msg") == 0);

    std::vector<zlink::message_t> parts;
    parts.emplace_back(5);
    std::memcpy(parts[0].data(), "hello", 5);

    assert(spot.publish("chat:room1:msg", parts) == 0);

    zlink::msgv_t recv_parts;
    std::string topic;
    assert(spot.recv(recv_parts, topic, 0) == 0);
    assert(topic == "chat:room1:msg");
    assert(recv_parts.size() == 1);
    assert(zlink_msg_size(&recv_parts[0]) == 5);
    assert(std::memcmp(zlink_msg_data(&recv_parts[0]), "hello", 5) == 0);

    return 0;
}
