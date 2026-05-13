#include <string.h>

#include <zlink.h>

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr))                                                          \
            return __LINE__;                                                  \
    } while (0)

int main(void)
{
    void *ctx = zlink_ctx_new();
    CHECK(ctx != NULL);

    void *pair = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
    CHECK(pair != NULL);

    zlink_socket_monitor_open_options_t monitor_options;
    memset(&monitor_options, 0, sizeof(monitor_options));
    monitor_options.events = ZLINK_SOCKET_MONITOR_EVENT_ALL;
    void *monitor = zlink_socket_monitor_open(pair, &monitor_options);
    CHECK(monitor != NULL);

    zlink_socket_monitor_event_t event;
    CHECK(zlink_socket_monitor_recv(monitor, &event, ZLINK_RECV_FLAGS_DONTWAIT)
          == ZLINK_RECV_NO_DATA);

    void *poller = zlink_poller_new();
    CHECK(poller != NULL);
    CHECK(zlink_poller_add(poller, pair, pair, ZLINK_POLLIN) == ZLINK_CONFIG_OK);
    CHECK(zlink_poller_size(poller, NULL) == 1);
    CHECK(zlink_poller_modify(poller, pair, ZLINK_POLLOUT) == ZLINK_CONFIG_OK);
    CHECK(zlink_poller_remove(poller, pair) == ZLINK_CONFIG_OK);
    CHECK(zlink_poller_size(poller, NULL) == 0);
    CHECK(zlink_poller_destroy(&poller) == ZLINK_CLOSE_OK);
    CHECK(poller == NULL);

    zlink_spot_node_options_t node_options;
    memset(&node_options, 0, sizeof(node_options));
    node_options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new(ctx, &node_options);
    CHECK(node != NULL);
    void *spot = zlink_spot_new(node);
    CHECK(spot != NULL);

    zlink_spot_node_status_t status;
    CHECK(zlink_spot_node_status_snapshot(node, &status) == ZLINK_CONFIG_OK);

    CHECK(zlink_spot_destroy(&spot) == ZLINK_CLOSE_OK);
    CHECK(spot == NULL);
    CHECK(zlink_spot_node_destroy(&node) == ZLINK_CLOSE_OK);
    CHECK(node == NULL);
    CHECK(zlink_monitor_close(&monitor) == ZLINK_CLOSE_OK);
    CHECK(monitor == NULL);
    CHECK(zlink_close(pair) == ZLINK_CLOSE_OK);
    CHECK(zlink_ctx_term(ctx) == ZLINK_CLOSE_OK);
    return 0;
}
