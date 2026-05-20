#include <stddef.h>

#include <zlink.h>

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr))                                                          \
            return __LINE__;                                                  \
    } while (0)

int main(void)
{
    CHECK(ZLINK_VERSION_MAJOR == 6);
    CHECK(ZLINK_VERSION_MINOR == 0);
    CHECK(ZLINK_VERSION_PATCH == 1);
    CHECK(ZLINK_VERSION == ZLINK_MAKE_VERSION(6, 0, 1));

    CHECK(ZLINK_SOCKET_PAIR == 0x1001);
    CHECK(ZLINK_SOCKET_STREAM == 0x1008);
    CHECK(ZLINK_DONTWAIT == ZLINK_SEND_FLAGS_DONTWAIT);

    zlink_msg_t msg;
    CHECK(sizeof msg >= 64);

    int major = 0;
    int minor = 0;
    int patch = 0;
    zlink_version(&major, &minor, &patch);
    CHECK(major == ZLINK_VERSION_MAJOR);
    CHECK(minor == ZLINK_VERSION_MINOR);
    CHECK(patch == ZLINK_VERSION_PATCH);

    CHECK(zlink_send_part != NULL);
    CHECK(zlink_send_part_rid != NULL);
    CHECK(zlink_recv_part != NULL);
    CHECK(zlink_publish_part != NULL);
    CHECK(zlink_subscribe_part != NULL);
    CHECK(zlink_spot_node_spot_get_or_new != NULL);
    CHECK(zlink_spot_node_connect_router_channel_peer != NULL);
    CHECK(zlink_spot_node_disconnect_router_channel_peer != NULL);
    CHECK(zlink_spot_node_disconnect_router_channel_peer_rid != NULL);
    CHECK(zlink_spot_node_attach_router_channel_discovery != NULL);
    CHECK(ZLINK_SPOT_PEER_KIND_SPOT_MESH == 1);
    CHECK(ZLINK_SPOT_PEER_KIND_ROUTER_CHANNEL == 2);
    CHECK(offsetof(zlink_spot_node_peer_entry_t, kind) >
          offsetof(zlink_spot_node_peer_entry_t, source));
    return 0;
}
