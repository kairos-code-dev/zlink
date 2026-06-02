/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: SPOT 토픽 pub/sub.
 * 한 노드가 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
 */

#include <boost/asio.hpp>
#include <zlink.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

// 빈 TCP 포트를 잡아 endpoint를 만든다.
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
    assert (false && "spot peer not connected");
}

int main ()
{
// --8<-- [start:doc]
    zlink::context_t ctx;
    // 토픽을 발행하는 노드와 구독하는 노드.
    zlink::service::spot_node_t publisher_node (ctx);
    zlink::service::spot_node_t subscriber_node (ctx);

    const std::string topic = "room:lobby";
    const std::string pub_endpoint = unique_tcp ();
    const std::string sub_endpoint = unique_tcp ();
    publisher_node.set_pub_bind (pub_endpoint);
    subscriber_node.set_pub_bind (sub_endpoint);
    publisher_node.connect_peer (sub_endpoint);
    subscriber_node.connect_peer (pub_endpoint);

    zlink::service::spot_t publisher = publisher_node.create_spot ();
    zlink::service::spot_t subscriber = subscriber_node.create_spot ();
    // 구독자는 받을 토픽을 등록한다.
    subscriber.set_subscription (topic);
    wait_peer (publisher_node);
    wait_peer (subscriber_node);

    // 연결 직후 첫 publish가 닿기 전일 수 있어, 도착할 때까지 반복 발행한다.
    zlink::topic_message_t received;
    bool delivered = false;
    auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink::message_t m = zlink::message_t::from ("hello-everyone");
        publisher.publish (topic).message (m).submit ();
        const int rc = subscriber.subscribe (received, zlink::recv_flags_t::dontwait);
        if (rc == static_cast<int> (zlink::recv_result_t::ok)) {
            delivered = true;
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    assert (delivered);

    std::printf ("[spot/pubsub] topic \"%s\" -> recv: \"%s\"\n",
                 received.topic ().c_str (), received.parts ()[0].to_string ().c_str ());
    received.close ();
    return 0;
// --8<-- [end:doc]
}
