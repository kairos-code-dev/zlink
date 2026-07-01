/* SPDX-License-Identifier: MPL-2.0 */

#include "delivery_dispatch_client_scenario.hpp"

using namespace zlink;

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

std::string read_option (int argc, char **argv, const std::string &name)
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (argv[index] == name) {
            return argv[index + 1];
        }
    }
    return {};
}

std::string env_or (const char *name, std::string fallback)
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

} // namespace

int main (int argc, char **argv)
{
    auto api_url = read_option (argc, argv, "--api-url");
    if (api_url.empty ()) {
        api_url = env_or ("DELIVERYDISPATCH_API_HTTP", "http://127.0.0.1:7392");
    }
    auto stream_endpoint = read_option (argc, argv, "--stream-endpoint");
    if (stream_endpoint.empty ()) {
        stream_endpoint = env_or (
          "DELIVERYDISPATCH_CUSTOMER_STREAM",
          env_or ("DELIVERYDISPATCH_SESSION_STREAM", "tcp://127.0.0.1:7400"));
    }
    auto courier_stream_endpoint = read_option (argc, argv, "--courier-stream-endpoint");
    if (courier_stream_endpoint.empty ()) {
        courier_stream_endpoint =
          env_or ("DELIVERYDISPATCH_COURIER_STREAM", env_or ("DELIVERYDISPATCH_SESSION_STREAM",
                                                             stream_endpoint));
    }
    if (!zlink::samples::deliverydispatch::delivery_dispatch_client_scenario_t{}.run (
          api_url, stream_endpoint, courier_stream_endpoint)) {
        std::cerr << "deliverydispatch=failed\n";
        return 1;
    }
    std::cout << "deliverydispatch=completed\n";
    return 0;
}
