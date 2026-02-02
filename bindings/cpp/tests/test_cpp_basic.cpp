#include <zlink.hpp>

#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

static int recv_with_timeout(zlink::socket_t &socket, void *buf, size_t len, int timeout_ms)
{
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        const int rc = socket.recv(buf, len, ZLINK_DONTWAIT);
        if (rc >= 0)
            return rc;
        if (zlink_errno() != EAGAIN)
            return -1;
        if (std::chrono::steady_clock::now() >= deadline)
            return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

static int recv_msg_with_timeout(zlink::socket_t &socket, zlink::message_t &msg, int timeout_ms)
{
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        const int rc = socket.recv(msg, ZLINK_DONTWAIT);
        if (rc >= 0)
            return rc;
        if (zlink_errno() != EAGAIN)
            return -1;
        if (std::chrono::steady_clock::now() >= deadline)
            return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int main()
{
    zlink::context_t ctx;

    zlink::socket_t server(ctx, ZLINK_PAIR);
    zlink::socket_t client(ctx, ZLINK_PAIR);

    const char *endpoint = "inproc://cpp-basic";
    assert(server.bind(endpoint) == 0);
    assert(client.connect(endpoint) == 0);

    assert(server.set(ZLINK_RCVTIMEO, 1000) == 0);
    assert(server.set(ZLINK_SNDTIMEO, 1000) == 0);
    assert(client.set(ZLINK_RCVTIMEO, 1000) == 0);
    assert(client.set(ZLINK_SNDTIMEO, 1000) == 0);

    const std::string payload = "hello";
    assert(client.send(payload) == static_cast<int>(payload.size()));

    char buf[16] = {0};
    const int rc = recv_with_timeout(server, buf, sizeof(buf), 1000);
    assert(rc == static_cast<int>(payload.size()));
    assert(std::string(buf, buf + rc) == payload);

    // message_t send/recv
    zlink::message_t msg(5);
    std::memcpy(msg.data(), "world", 5);
    assert(client.send(msg) == 5);

    zlink::message_t recv_msg;
    assert(recv_msg_with_timeout(server, recv_msg, 1000) >= 0);
    assert(recv_msg.size() == 5);
    assert(std::memcmp(recv_msg.data(), "world", 5) == 0);

    // poller
    zlink::poller_t poller;
    poller.add(server, ZLINK_POLLIN, NULL);

    const std::string payload2 = "ping";
    assert(client.send(payload2) == static_cast<int>(payload2.size()));

    std::vector<zlink::poll_event_t> events;
    const int n = poller.wait(events, 1000);
    assert(n > 0);

    // utilities
    zlink::atomic_counter_t counter;
    counter.set(1);
    assert(counter.value() == 1);
    assert(counter.inc() == 2);
    assert(counter.dec() == 1);

    zlink::stopwatch_t sw;
    (void)sw.intermediate();
    (void)sw.stop();

    (void)zlink::has("ipc");

    return 0;
}
