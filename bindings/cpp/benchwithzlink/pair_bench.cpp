#include <zlink.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static int get_port() {
  int s = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(s);
    return 0;
  }
  socklen_t len = sizeof(addr);
  ::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
  int port = ntohs(addr.sin_port);
  ::close(s);
  return port;
}

static int env_int(const char* name, int def) {
  const char* v = std::getenv(name);
  if (!v) return def;
  int x = std::atoi(v);
  return x > 0 ? x : def;
}

static int resolve_msg_count(size_t size) {
  int env = env_int("BENCH_MSG_COUNT", 0);
  if (env > 0) return env;
  return size <= 1024 ? 200000 : 20000;
}

int main(int argc, char** argv) {
  if (argc < 4) return 1;
  std::string pattern = argv[1];
  std::string transport = argv[2];
  size_t size = static_cast<size_t>(std::strtoull(argv[3], nullptr, 10));
  if (pattern != "PAIR") return 0;

  int warmup = env_int("BENCH_WARMUP_COUNT", 1000);
  int lat_count = env_int("BENCH_LAT_COUNT", 500);
  int msg_count = resolve_msg_count(size);

  void* ctx = zlink_ctx_new();
  void* a = zlink_socket(ctx, ZLINK_PAIR);
  void* b = zlink_socket(ctx, ZLINK_PAIR);

  std::string endpoint;
  if (transport == "inproc") {
    endpoint = "inproc://bench-pair-" + std::to_string(
      std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
  } else {
    int port = get_port();
    endpoint = transport + "://127.0.0.1:" + std::to_string(port);
  }

  if (zlink_bind(a, endpoint.c_str()) != 0 || zlink_connect(b, endpoint.c_str()) != 0) {
    zlink_close(a); zlink_close(b); zlink_ctx_term(ctx); return 2;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<char> buf(size, 'a');
  std::vector<char> rbuf(size);

  for (int i = 0; i < warmup; ++i) {
    if (zlink_send(b, buf.data(), size, 0) < 0) { zlink_close(a); zlink_close(b); zlink_ctx_term(ctx); return 2; }
    if (zlink_recv(a, rbuf.data(), size, 0) < 0) { zlink_close(a); zlink_close(b); zlink_ctx_term(ctx); return 2; }
  }

  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < lat_count; ++i) {
    if (zlink_send(b, buf.data(), size, 0) < 0) { zlink_close(a); zlink_close(b); zlink_ctx_term(ctx); return 2; }
    int n = zlink_recv(a, rbuf.data(), size, 0);
    if (n < 0) { zlink_close(a); zlink_close(b); zlink_ctx_term(ctx); return 2; }
    if (zlink_send(a, rbuf.data(), static_cast<size_t>(n), 0) < 0) { zlink_close(a); zlink_close(b); zlink_ctx_term(ctx); return 2; }
    if (zlink_recv(b, rbuf.data(), size, 0) < 0) { zlink_close(a); zlink_close(b); zlink_ctx_term(ctx); return 2; }
  }
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
  double latency = static_cast<double>(us) / (lat_count * 2);

  t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < msg_count; ++i) {
    if (zlink_send(b, buf.data(), size, 0) < 0) { zlink_close(a); zlink_close(b); zlink_ctx_term(ctx); return 2; }
  }
  for (int i = 0; i < msg_count; ++i) {
    if (zlink_recv(a, rbuf.data(), size, 0) < 0) { zlink_close(a); zlink_close(b); zlink_ctx_term(ctx); return 2; }
  }
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
  double throughput = static_cast<double>(msg_count) / (static_cast<double>(ns) / 1e9);

  std::cout << "RESULT,current,PAIR," << transport << "," << size << ",throughput," << throughput << "\n";
  std::cout << "RESULT,current,PAIR," << transport << "," << size << ",latency," << latency << "\n";

  zlink_close(a);
  zlink_close(b);
  zlink_ctx_term(ctx);
  return 0;
}
