#ifndef BINDINGS_CPP_PERF_MULTI_DISPATCH_HPP
#define BINDINGS_CPP_PERF_MULTI_DISPATCH_HPP

#include <string>

int perf_multi_dealer_dealer_server (const std::string &transport, size_t size);
int perf_multi_dealer_dealer_client (const std::string &transport,
                                     size_t size,
                                     const std::string &endpoint);

int perf_multi_dealer_router_server (const std::string &transport, size_t size);
int perf_multi_dealer_router_client (const std::string &transport,
                                     size_t size,
                                     const std::string &endpoint);

int perf_multi_router_router_server (const std::string &transport, size_t size);
int perf_multi_router_router_client (const std::string &transport,
                                     size_t size,
                                     const std::string &endpoint);

int perf_multi_pubsub_server (const std::string &transport, size_t size);
int perf_multi_pubsub_client (const std::string &transport,
                              size_t size,
                              const std::string &endpoint);

int perf_multi_gateway_server (const std::string &transport, size_t size);
int perf_multi_gateway_client (const std::string &transport,
                               size_t size,
                               const std::string &endpoint);

int perf_multi_spot_server (const std::string &transport, size_t size);
int perf_multi_spot_client (const std::string &transport,
                            size_t size,
                            const std::string &endpoint);

int perf_multi_stream_server (const std::string &transport, size_t size);
int perf_multi_stream_client (const std::string &transport,
                              size_t size,
                              const std::string &endpoint);

int perf_multi_stream_callback_server (const std::string &transport, size_t size);
int perf_multi_stream_callback_client (const std::string &transport,
                                       size_t size,
                                       const std::string &endpoint);

int perf_multi_stream_len32be_server (const std::string &transport, size_t size);
int perf_multi_stream_len32be_client (const std::string &transport,
                                      size_t size,
                                      const std::string &endpoint);

typedef int (*perf_multi_server_fn_t) (const std::string &, size_t);
typedef int (*perf_multi_client_fn_t) (const std::string &, size_t, const std::string &);

inline perf_multi_server_fn_t resolve_multi_server_fn (const std::string &pattern)
{
    if (pattern == "MULTI_DEALER_DEALER")
        return &perf_multi_dealer_dealer_server;
    if (pattern == "MULTI_DEALER_ROUTER")
        return &perf_multi_dealer_router_server;
    if (pattern == "MULTI_ROUTER_ROUTER")
        return &perf_multi_router_router_server;
    if (pattern == "MULTI_PUBSUB")
        return &perf_multi_pubsub_server;
    if (pattern == "MULTI_GATEWAY")
        return &perf_multi_gateway_server;
    if (pattern == "MULTI_SPOT")
        return &perf_multi_spot_server;
    if (pattern == "MULTI_STREAM")
        return &perf_multi_stream_server;
    if (pattern == "MULTI_STREAM_CALLBACK")
        return &perf_multi_stream_callback_server;
    if (pattern == "MULTI_STREAM_LEN32BE")
        return &perf_multi_stream_len32be_server;
    return NULL;
}

inline perf_multi_client_fn_t resolve_multi_client_fn (const std::string &pattern)
{
    if (pattern == "MULTI_DEALER_DEALER")
        return &perf_multi_dealer_dealer_client;
    if (pattern == "MULTI_DEALER_ROUTER")
        return &perf_multi_dealer_router_client;
    if (pattern == "MULTI_ROUTER_ROUTER")
        return &perf_multi_router_router_client;
    if (pattern == "MULTI_PUBSUB")
        return &perf_multi_pubsub_client;
    if (pattern == "MULTI_GATEWAY")
        return &perf_multi_gateway_client;
    if (pattern == "MULTI_SPOT")
        return &perf_multi_spot_client;
    if (pattern == "MULTI_STREAM")
        return &perf_multi_stream_client;
    if (pattern == "MULTI_STREAM_CALLBACK")
        return &perf_multi_stream_callback_client;
    if (pattern == "MULTI_STREAM_LEN32BE")
        return &perf_multi_stream_len32be_client;
    return NULL;
}

#endif
