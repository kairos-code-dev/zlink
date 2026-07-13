#include <stddef.h>

#include <zlink.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr))                                                                               \
            return __LINE__;                                                                       \
    } while (0)

static void entry_join_handler (const zlink_actor_join_entry_spot_result_t *result,
                                zlink_msg_t *parts,
                                size_t part_count,
                                void *userdata)
{
    (void) result;
    (void) userdata;
    if (parts && part_count > 0)
        zlink_multipart_close (parts, part_count);
}

int main (void)
{
    CHECK (ZLINK_VERSION_MAJOR == 9);
    CHECK (ZLINK_VERSION_MINOR == 0);
    CHECK (ZLINK_VERSION_PATCH == 2);
    CHECK (ZLINK_VERSION == ZLINK_MAKE_VERSION (9, 0, 2));

    CHECK (ZLINK_SOCKET_PAIR == 0x1001);
    CHECK (ZLINK_SOCKET_STREAM == 0x1008);
    CHECK (ZLINK_DONTWAIT == ZLINK_SEND_FLAGS_DONTWAIT);

    zlink_msg_t msg;
    CHECK (sizeof msg >= 64);

    int major = 0;
    int minor = 0;
    int patch = 0;
    zlink_version (&major, &minor, &patch);
    CHECK (major == ZLINK_VERSION_MAJOR);
    CHECK (minor == ZLINK_VERSION_MINOR);
    CHECK (patch == ZLINK_VERSION_PATCH);

    CHECK (zlink_send_part != NULL);
    CHECK (zlink_send_part_rid != NULL);
    CHECK (zlink_recv_part != NULL);
    CHECK (zlink_publish_part != NULL);
    CHECK (zlink_subscribe_part != NULL);
    CHECK (zlink_spot_node_set_router_bind != NULL);
    CHECK (zlink_spot_node_set_pub_bind != NULL);
    CHECK (zlink_spot_node_spot_get_or_new != NULL);
    CHECK (zlink_spot_node_actor_join_entry_spot != NULL);
    CHECK (zlink_spot_node_actor_reply_no_bind != NULL);
    CHECK (ZLINK_ACTOR_RECV_INFO_NO_BIND == 1u);
    CHECK (offsetof (zlink_actor_recv_info_t, request_id)
           > offsetof (zlink_actor_recv_info_t, source_session_rid));
    CHECK (offsetof (zlink_actor_join_entry_spot_result_t, join_result_code)
           > offsetof (zlink_actor_join_entry_spot_result_t, result));
    CHECK (offsetof (zlink_actor_join_entry_spot_result_t, joined_spot_rid)
           > offsetof (zlink_actor_join_entry_spot_result_t, target_node_rid));
    zlink_actor_join_entry_spot_handler_fn entry_handler = entry_join_handler;
    CHECK (entry_handler != NULL);
    CHECK (zlink_spot_node_actor_join_entry_spot (NULL, NULL, NULL, NULL, 0, entry_handler,
                                                  NULL, ZLINK_DONTWAIT, 0)
           == ZLINK_SUBMIT_INVALID_ARGUMENT);
    CHECK (ZLINK_SPOT_PEER_KIND_SPOT_MESH == 1);
    CHECK (ZLINK_SPOT_PEER_KIND_ROUTER_CHANNEL == 2);
    CHECK (offsetof (zlink_spot_node_peer_entry_t, kind)
           > offsetof (zlink_spot_node_peer_entry_t, source));
    return 0;
}
