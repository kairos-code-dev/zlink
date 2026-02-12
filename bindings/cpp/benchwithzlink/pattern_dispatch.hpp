#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <zlink.hpp>

int run_pattern_pair(const std::string &transport, size_t size);
int run_pattern_pubsub(const std::string &transport, size_t size);
int run_pattern_dealer_dealer(const std::string &transport, size_t size);
int run_pattern_dealer_router(const std::string &transport, size_t size);
int run_pattern_router_router(const std::string &transport, size_t size);
int run_pattern_router_router_poll(const std::string &transport, size_t size);
int run_pattern_stream(const std::string &transport, size_t size);
int run_pattern_gateway(const std::string &transport, size_t size);
int run_pattern_spot(const std::string &transport, size_t size);

int env_int(const char *name, int def);
int resolve_msg_count(size_t size);
std::string endpoint_for(const std::string &transport, const std::string &name);
void settle();
void print_result(const char *pattern,
                  const std::string &transport,
                  size_t size,
                  double throughput,
                  double latency);
bool wait_for_input(zlink::socket_t &socket, long timeout_ms);
std::vector<unsigned char> stream_expect_connect_event(zlink::socket_t &socket);
int stream_send(zlink::socket_t &socket,
                const std::vector<unsigned char> &rid,
                const void *data,
                size_t len);
int stream_recv(zlink::socket_t &socket,
                std::vector<unsigned char> *rid_out,
                void *buf,
                size_t cap);

int run_pair_like(const char *pattern,
                  int a_type,
                  int b_type,
                  const std::string &transport,
                  size_t size);
int run_pubsub(const std::string &transport, size_t size);
int run_dealer_router(const std::string &transport, size_t size);
int run_router_router(const std::string &transport, size_t size, bool use_poll);
int run_stream(const std::string &transport, size_t size);
int run_gateway(const std::string &transport, size_t size);
int run_spot(const std::string &transport, size_t size);
