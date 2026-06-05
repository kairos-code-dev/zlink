/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: SPOT 라우티드 RPC (Spot ↔ Spot 요청/응답).
 * 한 노드의 Spot이 다른 노드의 Spot으로 직접 요청을 보내고, 상대가 응답한다.
 */

#include <boost/asio.hpp>
#include <zlink.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static std::string unique_tcp ()
{
    int fd = ::socket (AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    addr.sin_port = 0;
    (void) ::bind (fd, reinterpret_cast<sockaddr *> (&addr), sizeof (addr));
    socklen_t len = sizeof (addr);
    (void) ::getsockname (fd, reinterpret_cast<sockaddr *> (&addr), &len);
    int port = ntohs (addr.sin_port);
    ::close (fd);
    return "tcp://127.0.0.1:" + std::to_string (port);
}

static void wait_peer (zlink::service::spot_node_t &node)
{
    auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (15);
    while (std::chrono::steady_clock::now () < deadline) {
        if (node.status ().connected_peer_count () > 0)
            return;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
}

int main ()
{
    // --8<-- [start:doc]
    zlink::context_t ctx;
    zlink::service::spot_node_t server_node (ctx);
    zlink::service::spot_node_t client_node (ctx);
    zlink::service::spot_t server = server_node.create_spot ();
    zlink::service::spot_t client = client_node.create_spot ();

    server_node.set_routing_id (zlink::routing_id_t::from (std::string ("rpc-server-node")));
    client_node.set_routing_id (zlink::routing_id_t::from (std::string ("rpc-client-node")));
    server.set_routing_id (zlink::routing_id_t::from (std::string ("rpc-server-spot")));
    client.set_routing_id (zlink::routing_id_t::from (std::string ("rpc-client-spot")));
    // 라우티드 평면은 ROUTER bind가 필요하다 (pub bind보다 먼저).
    server_node.set_router_bind (unique_tcp ());
    client_node.set_router_bind (unique_tcp ());
    const std::string server_endpoint = unique_tcp ();
    const std::string client_endpoint = unique_tcp ();
    server_node.set_pub_bind (server_endpoint);
    client_node.set_pub_bind (client_endpoint);
    server_node.connect_peer (client_endpoint);
    client_node.connect_peer (server_endpoint);

    // 서버 Spot은 라우티드 요청을 받아 같은 평면으로 응답한다.
    server.set_dispatch_handler ([&] (zlink::service::spot_t &s, const zlink::spot_dispatch_info_t &info) {
        if (info.event != zlink::spot_dispatch_event_t::routed_readable)
            return;
        zlink::received_t received;
        while (s.recv_routed (received, zlink::recv_flags_t::dontwait) == static_cast<int> (zlink::recv_result_t::ok)) {
            zlink::message_t reply = zlink::message_t::from ("pong");
            s.reply_to_spot (*received.routing_id (), *received.spot_rid (), *received.request_seq ())
              .message (reply)
              .submit ();
        }
    });

    wait_peer (server_node);
    wait_peer (client_node);
    // 양쪽 owner route가 mesh로 전파될 시간을 준다.
    std::this_thread::sleep_for (std::chrono::seconds (1));

    // 클라이언트 Spot이 서버 Spot으로 요청한다. 응답은 콜백에서 바로 처리한다.
    auto done = std::make_shared<std::atomic<bool>> (false);
    zlink::message_t request = zlink::message_t::from ("ping");
    client
      .request_to_spot (zlink::routing_id_t::from (std::string ("rpc-server-node")),
                        zlink::routing_id_t::from (std::string ("rpc-server-spot")))
      .message (request)
      .timeout (std::chrono::seconds (3))
      .submit ([done] (zlink::request_result_t result, std::vector<zlink::message_t> parts) {
          if (result == zlink::request_result_t::ok && !parts.empty ())
              std::printf ("[spot/rpc] request \"ping\" -> reply \"%s\"\n", parts[0].to_string ().c_str ());
          done->store (true);
      });

    auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (!done->load () && std::chrono::steady_clock::now () < deadline)
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    return 0;
    // --8<-- [end:doc]
}
