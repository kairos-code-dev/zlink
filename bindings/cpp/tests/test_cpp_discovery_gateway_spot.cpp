#include "test_helpers.hpp"

#include <cstring>

int main()
{
    zlink::context_t ctx;

    const char *service = "svc";

    const std::vector<transport_case_t> cases = transport_cases();
    for (size_t i = 0; i < cases.size(); ++i) {
        const transport_case_t &tc = cases[i];
        if (!transport_supported(tc))
            continue;

        zlink::registry_t registry(ctx);
        std::string reg_pub = unique_inproc("inproc://reg-pub-", "cpp");
        std::string reg_router = unique_inproc("inproc://reg-router-", "cpp");
        assert(registry.set_endpoints(reg_pub.c_str(), reg_router.c_str()) == 0);
        assert(registry.start() == 0);
        sleep_ms(50);

        zlink::discovery_t discovery(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
        assert(discovery.connect_registry(reg_pub.c_str()) == 0);
        assert(discovery.subscribe(service) == 0);

        zlink::receiver_t receiver(ctx);
        std::string bind_ep = endpoint_for(tc, "receiver");
        if (receiver.bind(bind_ep.c_str()) != 0)
            continue;
        std::string adv_ep = bind_ep;
        if (tc.name != "inproc") {
            zlink::socket_t recv_router = zlink::socket_t::wrap(receiver.router_handle());
            adv_ep = bound_endpoint(recv_router);
        }
        assert(receiver.connect_registry(reg_router.c_str()) == 0);
        assert(receiver.register_service(service, adv_ep.c_str(), 1) == 0);
        sleep_ms(100);

        zlink::gateway_t gateway(ctx, discovery);

        std::vector<zlink::message_t> parts;
        parts.push_back(zlink::message_t(5));
        std::memcpy(parts[0].data(), "hello", 5);
        assert(gateway.send(service, parts) == 0);

        zlink::socket_t recv_router = zlink::socket_t::wrap(receiver.router_handle());
        zlink::message_t rid;
        zlink::message_t payload;
        assert(recv_msg_with_timeout(recv_router, rid, 2000) >= 0);
        bool got = false;
        for (int i = 0; i < 3; ++i) {
            assert(recv_msg_with_timeout(recv_router, payload, 2000) >= 0);
            if (payload.size() == 5
                && std::memcmp(payload.data(), "hello", 5) == 0) {
                got = true;
                break;
            }
        }
        assert(got);

        assert(recv_router.send(rid, ZLINK_SNDMORE) >= 0);
        const char *reply = "world";
        assert(recv_router.send(reply, 5) == 5);

        zlink::msgv_t out;
        std::string svc_out;
        const int recv_rc = gateway.recv(out, svc_out, ZLINK_DONTWAIT);
        if (recv_rc != 0) {
            // retry with simple loop
            const auto deadline =
              std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
            while (recv_rc != 0 && std::chrono::steady_clock::now() < deadline) {
                sleep_ms(5);
                if (gateway.recv(out, svc_out, ZLINK_DONTWAIT) == 0)
                    break;
            }
        }
        assert(!out.empty());
        assert(svc_out == service);
        assert(out.size() == 1);
        assert(out.at(0).size() == 5);
        assert(std::memcmp(out.at(0).data(), "world", 5) == 0);

        // spot
        zlink::spot_node_t node(ctx);
        std::string spot_ep = endpoint_for(tc, "spot");
        if (node.bind(spot_ep.c_str()) != 0)
            continue;
        std::string spot_adv = spot_ep;
        if (tc.name != "inproc") {
            zlink::socket_t pub = zlink::socket_t::wrap(node.pub_handle());
            spot_adv = bound_endpoint(pub);
        }
        assert(node.connect_registry(reg_router.c_str()) == 0);
        assert(node.register_service("spot", spot_adv.c_str()) == 0);

        zlink::spot_node_t peer(ctx);
        assert(peer.connect_registry(reg_router.c_str()) == 0);
        assert(peer.connect_peer_pub(spot_adv.c_str()) == 0);
        zlink::spot_t spot(peer);

        const char *topic = "topic";
        assert(spot.subscribe(topic) == 0);

        std::vector<zlink::message_t> spot_parts;
        spot_parts.push_back(zlink::message_t(8));
        std::memcpy(spot_parts[0].data(), "spot-msg", 8);
        assert(spot.publish(topic, spot_parts) == 0);

        zlink::msgv_t spot_out;
        std::string topic_out;
        const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
        while (spot.recv(spot_out, topic_out, ZLINK_DONTWAIT) != 0) {
            if (std::chrono::steady_clock::now() >= deadline)
                break;
            sleep_ms(5);
        }
        assert(!spot_out.empty());
        assert(topic_out == topic);
        assert(spot_out.size() == 1);
        assert(spot_out.at(0).size() == 8);
        assert(std::memcmp(spot_out.at(0).data(), "spot-msg", 8) == 0);
    }

    return 0;
}
