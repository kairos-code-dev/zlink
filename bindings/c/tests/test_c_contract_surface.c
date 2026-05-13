#include <assert.h>
#include <stddef.h>

#include <zlink.h>

int main(void)
{
    assert(ZLINK_VERSION_MAJOR == 6);
    assert(ZLINK_VERSION_MINOR == 0);
    assert(ZLINK_VERSION_PATCH == 0);
    assert(ZLINK_VERSION == ZLINK_MAKE_VERSION(6, 0, 0));

    assert(ZLINK_SOCKET_PAIR == 0x1001);
    assert(ZLINK_SOCKET_STREAM == 0x1008);
    assert(ZLINK_DONTWAIT == ZLINK_SEND_FLAGS_DONTWAIT);

    zlink_msg_t msg;
    assert(sizeof msg >= 64);

    int major = 0;
    int minor = 0;
    int patch = 0;
    zlink_version(&major, &minor, &patch);
    assert(major == ZLINK_VERSION_MAJOR);
    assert(minor == ZLINK_VERSION_MINOR);
    assert(patch == ZLINK_VERSION_PATCH);

    assert(zlink_send_part != NULL);
    assert(zlink_send_part_rid != NULL);
    assert(zlink_recv_part != NULL);
    assert(zlink_publish_part != NULL);
    assert(zlink_subscribe_part != NULL);
    return 0;
}
