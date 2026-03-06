#include "test_helpers.hpp"

static void test_gateway_router_mode_rejects_facade_send()
{
    zlink::context_t ctx;
    void *discovery =
      zlink_discovery_new_typed(ctx.handle(), ZLINK_SERVICE_TYPE_GATEWAY);
    assert(discovery != NULL);

    void *gateway = zlink_gateway_new(ctx.handle(), discovery, NULL);
    assert(gateway != NULL);
    assert(zlink_gateway_router_socket(gateway) != NULL);

    zlink_msg_t part;
    assert(zlink_msg_init_size(&part, 4) == 0);
    std::memcpy(zlink_msg_data(&part), "test", 4);
    assert(zlink_gateway_send(gateway, "svc", &part, 1, 0) == -1);
    assert(errno == EFSM);
    assert(zlink_msg_close(&part) == 0);

    assert(zlink_gateway_destroy(&gateway) == 0);
    assert(zlink_discovery_destroy(&discovery) == 0);
}

static void test_spot_pub_socket_mode_rejects_facade_publish()
{
    zlink::context_t ctx;
    void *node = zlink_spot_node_new(ctx.handle());
    assert(node != NULL);

    const std::string endpoint = unique_inproc("inproc://cpp-", "spot-mode-pub");
    assert(zlink_spot_node_bind(node, endpoint.c_str()) == 0);
    assert(zlink_spot_node_pub_socket(node) != NULL);

    void *pub = zlink_spot_pub_new(node);
    assert(pub != NULL);

    zlink_msg_t part;
    assert(zlink_msg_init_size(&part, 4) == 0);
    std::memcpy(zlink_msg_data(&part), "test", 4);
    assert(zlink_spot_pub_publish(pub, "mode:pub", &part, 1, 0) == -1);
    assert(errno == EFSM);
    assert(zlink_msg_close(&part) == 0);

    assert(zlink_spot_pub_destroy(&pub) == 0);
    assert(zlink_spot_node_destroy(&node) == 0);
}

int main()
{
    test_gateway_router_mode_rejects_facade_send();
    test_spot_pub_socket_mode_rejects_facade_publish();
    return 0;
}
