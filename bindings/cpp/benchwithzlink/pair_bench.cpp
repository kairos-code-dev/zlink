#include <zlink.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <set>
#include <vector>

#include "pattern_dispatch.hpp"

static int get_port()
{
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        ::close(s);
        return 0;
    }
    socklen_t len = sizeof(addr);
    ::getsockname(s, reinterpret_cast<sockaddr *>(&addr), &len);
    int port = ntohs(addr.sin_port);
    ::close(s);
    return port;
}

static std::set<std::string> &ipc_paths()
{
    static std::set<std::string> paths;
    return paths;
}

static void cleanup_ipc_paths()
{
    for (std::set<std::string>::const_iterator it = ipc_paths().begin();
         it != ipc_paths().end();
         ++it) {
        ::unlink(it->c_str());
    }
}

static void register_ipc_endpoint(const std::string &endpoint)
{
    const std::string prefix = "ipc://";
    if (endpoint.compare(0, prefix.size(), prefix) != 0)
        return;
    const std::string path = endpoint.substr(prefix.size());
    if (path.empty() || path[0] != '/')
        return;

    static bool cleanup_registered = false;
    if (!cleanup_registered) {
        std::atexit(cleanup_ipc_paths);
        cleanup_registered = true;
    }

    ipc_paths().insert(path);
    ::unlink(path.c_str());
}

int env_int(const char *name, int def)
{
    const char *v = std::getenv(name);
    if (!v)
        return def;
    int x = std::atoi(v);
    return x > 0 ? x : def;
}

int resolve_msg_count(size_t size)
{
    int env = env_int("BENCH_MSG_COUNT", 0);
    if (env > 0)
        return env;
    return size <= 1024 ? 200000 : 20000;
}

std::string endpoint_for(const std::string &transport, const std::string &name)
{
    if (transport == "inproc") {
        return "inproc://bench-" + name + "-"
               + std::to_string(
                 std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count());
    }
    if (transport == "ipc") {
        const std::string endpoint = "ipc:///tmp/zlink-bench-" + name + "-"
                                     + std::to_string(get_port()) + ".sock";
        register_ipc_endpoint(endpoint);
        return endpoint;
    }
    int port = get_port();
    return transport + "://127.0.0.1:" + std::to_string(port);
}

void settle()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

void print_result(const char *pattern,
                  const std::string &transport,
                  size_t size,
                  double throughput,
                  double latency)
{
    std::cout << "RESULT,current," << pattern << "," << transport << "," << size
              << ",throughput," << throughput << "\n";
    std::cout << "RESULT,current," << pattern << "," << transport << "," << size
              << ",latency," << latency << "\n";
}

bool wait_for_input(zlink::socket_t &socket, long timeout_ms)
{
    zlink::poller_t poller;
    std::vector<zlink::poll_event_t> events;
    poller.add(socket, zlink::poll_event::pollin);
    return poller.wait(events, timeout_ms) > 0;
}

std::vector<unsigned char> stream_expect_connect_event(zlink::socket_t &socket)
{
    zlink::message_t id_frame;
    int id_len = socket.recv(id_frame);
    if (id_len <= 0) {
        return std::vector<unsigned char>();
    }

    const unsigned char *ptr = static_cast<const unsigned char *>(id_frame.data());
    std::vector<unsigned char> rid(ptr, ptr + id_len);

    unsigned char payload[16];
    int p_len = socket.recv(payload, sizeof(payload));
    if (p_len != 1 || payload[0] != 0x01) {
        return std::vector<unsigned char>();
    }
    return rid;
}

int stream_send(zlink::socket_t &socket,
                const std::vector<unsigned char> &rid,
                const void *data,
                size_t len)
{
    if (rid.empty())
        return -1;
    if (socket.send(rid.data(), rid.size(), zlink::send_flag::sndmore) < 0)
        return -1;
    return socket.send(data, len, zlink::send_flag::none);
}

int stream_recv(zlink::socket_t &socket,
                std::vector<unsigned char> *rid_out,
                void *buf,
                size_t cap)
{
    zlink::message_t id_frame;
    int id_len = socket.recv(id_frame);
    if (id_len <= 0) {
        return -1;
    }
    if (rid_out) {
        const unsigned char *ptr = static_cast<const unsigned char *>(id_frame.data());
        rid_out->assign(ptr, ptr + id_len);
    }
    return socket.recv(buf, cap, zlink::recv_flag::none);
}

int main(int argc, char **argv)
{
    if (argc < 4)
        return 1;

    std::string pattern = argv[1];
    std::string transport = argv[2];
    size_t size = static_cast<size_t>(std::strtoull(argv[3], NULL, 10));

    if (pattern == "PAIR")
        return run_pattern_pair(transport, size);
    if (pattern == "PUBSUB")
        return run_pattern_pubsub(transport, size);
    if (pattern == "DEALER_DEALER")
        return run_pattern_dealer_dealer(transport, size);
    if (pattern == "DEALER_ROUTER")
        return run_pattern_dealer_router(transport, size);
    if (pattern == "ROUTER_ROUTER")
        return run_pattern_router_router(transport, size);
    if (pattern == "ROUTER_ROUTER_POLL")
        return run_pattern_router_router_poll(transport, size);
    if (pattern == "STREAM")
        return run_pattern_stream(transport, size);
    if (pattern == "GATEWAY")
        return run_pattern_gateway(transport, size);
    if (pattern == "SPOT")
        return run_pattern_spot(transport, size);

    return 2;
}
