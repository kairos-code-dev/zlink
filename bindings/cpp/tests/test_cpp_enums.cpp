#include <zlink.hpp>

#include <cassert>

int main()
{
    // socket_type values
    assert(static_cast<int>(zlink::socket_type::pair) == ZLINK_PAIR);
    assert(static_cast<int>(zlink::socket_type::pub) == ZLINK_PUB);
    assert(static_cast<int>(zlink::socket_type::sub) == ZLINK_SUB);
    assert(static_cast<int>(zlink::socket_type::dealer) == ZLINK_DEALER);
    assert(static_cast<int>(zlink::socket_type::router) == ZLINK_ROUTER);
    assert(static_cast<int>(zlink::socket_type::xpub) == ZLINK_XPUB);
    assert(static_cast<int>(zlink::socket_type::xsub) == ZLINK_XSUB);
    assert(static_cast<int>(zlink::socket_type::stream) == ZLINK_STREAM);

    // context_option values
    assert(static_cast<int>(zlink::context_option::io_threads) == ZLINK_IO_THREADS);
    assert(static_cast<int>(zlink::context_option::max_sockets) == ZLINK_MAX_SOCKETS);
    assert(static_cast<int>(zlink::context_option::thread_name_prefix) == ZLINK_THREAD_NAME_PREFIX);

    // socket_option values
    assert(static_cast<int>(zlink::socket_option::linger) == ZLINK_LINGER);
    assert(static_cast<int>(zlink::socket_option::sndhwm) == ZLINK_SNDHWM);
    assert(static_cast<int>(zlink::socket_option::rcvhwm) == ZLINK_RCVHWM);
    assert(static_cast<int>(zlink::socket_option::subscribe) == ZLINK_SUBSCRIBE);
    assert(static_cast<int>(zlink::socket_option::tls_cert) == ZLINK_TLS_CERT);
    assert(static_cast<int>(zlink::socket_option::tls_password) == ZLINK_TLS_PASSWORD);
    assert(static_cast<int>(zlink::socket_option::zmp_metadata) == ZLINK_ZMP_METADATA);

    // send_flag values
    assert(static_cast<int>(zlink::send_flag::none) == 0);
    assert(static_cast<int>(zlink::send_flag::dontwait) == ZLINK_DONTWAIT);
    assert(static_cast<int>(zlink::send_flag::sndmore) == ZLINK_SNDMORE);

    // recv_flag values
    assert(static_cast<int>(zlink::recv_flag::none) == 0);
    assert(static_cast<int>(zlink::recv_flag::dontwait) == ZLINK_DONTWAIT);

    // monitor_event values
    assert(static_cast<int>(zlink::monitor_event::connected) == ZLINK_EVENT_CONNECTED);
    assert(static_cast<int>(zlink::monitor_event::disconnected) == ZLINK_EVENT_DISCONNECTED);
    assert(static_cast<int>(zlink::monitor_event::all) == ZLINK_EVENT_ALL);

    // disconnect_reason values
    assert(static_cast<int>(zlink::disconnect_reason::unknown) == ZLINK_DISCONNECT_UNKNOWN);
    assert(static_cast<int>(zlink::disconnect_reason::ctx_term) == ZLINK_DISCONNECT_CTX_TERM);

    // poll_event values
    assert(static_cast<int>(zlink::poll_event::pollin) == ZLINK_POLLIN);
    assert(static_cast<int>(zlink::poll_event::pollout) == ZLINK_POLLOUT);
    assert(static_cast<int>(zlink::poll_event::pollerr) == ZLINK_POLLERR);
    assert(static_cast<int>(zlink::poll_event::pollpri) == ZLINK_POLLPRI);

    // service_type values
    assert(static_cast<int>(zlink::service_type::gateway) == ZLINK_SERVICE_TYPE_GATEWAY);
    assert(static_cast<int>(zlink::service_type::spot) == ZLINK_SERVICE_TYPE_SPOT);

    // gateway_lb_strategy values
    assert(static_cast<int>(zlink::gateway_lb_strategy::round_robin) == ZLINK_GATEWAY_LB_ROUND_ROBIN);
    assert(static_cast<int>(zlink::gateway_lb_strategy::weighted) == ZLINK_GATEWAY_LB_WEIGHTED);

    // spot_topic_mode values
    assert(static_cast<int>(zlink::spot_topic_mode::queue) == ZLINK_SPOT_TOPIC_QUEUE);
    assert(static_cast<int>(zlink::spot_topic_mode::ringbuffer) == ZLINK_SPOT_TOPIC_RINGBUFFER);

    // socket role values
    assert(static_cast<int>(zlink::registry_socket_role::pub) == ZLINK_REGISTRY_SOCKET_PUB);
    assert(static_cast<int>(zlink::registry_socket_role::router) == ZLINK_REGISTRY_SOCKET_ROUTER);
    assert(static_cast<int>(zlink::registry_socket_role::peer_sub) == ZLINK_REGISTRY_SOCKET_PEER_SUB);
    assert(static_cast<int>(zlink::discovery_socket_role::sub) == ZLINK_DISCOVERY_SOCKET_SUB);
    assert(static_cast<int>(zlink::gateway_socket_role::router) == ZLINK_GATEWAY_SOCKET_ROUTER);
    assert(static_cast<int>(zlink::receiver_socket_role::router) == ZLINK_RECEIVER_SOCKET_ROUTER);
    assert(static_cast<int>(zlink::receiver_socket_role::dealer) == ZLINK_RECEIVER_SOCKET_DEALER);
    assert(static_cast<int>(zlink::spot_node_socket_role::pub) == ZLINK_SPOT_NODE_SOCKET_PUB);
    assert(static_cast<int>(zlink::spot_node_socket_role::sub) == ZLINK_SPOT_NODE_SOCKET_SUB);
    assert(static_cast<int>(zlink::spot_node_socket_role::dealer) == ZLINK_SPOT_NODE_SOCKET_DEALER);
    assert(static_cast<int>(zlink::spot_socket_role::pub) == ZLINK_SPOT_SOCKET_PUB);
    assert(static_cast<int>(zlink::spot_socket_role::sub) == ZLINK_SPOT_SOCKET_SUB);

    // flag OR operators
    {
        zlink::send_flag combined = zlink::send_flag::dontwait | zlink::send_flag::sndmore;
        assert(static_cast<int>(combined) == (ZLINK_DONTWAIT | ZLINK_SNDMORE));
    }
    {
        zlink::monitor_event combined =
            zlink::monitor_event::connected | zlink::monitor_event::disconnected;
        assert(static_cast<int>(combined) ==
               (ZLINK_EVENT_CONNECTED | ZLINK_EVENT_DISCONNECTED));
    }
    {
        zlink::poll_event combined = zlink::poll_event::pollin | zlink::poll_event::pollout;
        assert(static_cast<int>(combined) == (ZLINK_POLLIN | ZLINK_POLLOUT));
    }

    // socket creation with enum
    {
        zlink::context_t ctx;
        zlink::socket_t sock(ctx, zlink::socket_type::pair);
        assert(sock.handle() != nullptr);
    }

    return 0;
}
