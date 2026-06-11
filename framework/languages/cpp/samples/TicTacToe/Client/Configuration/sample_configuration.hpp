/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "sample_topology.hpp"

#include <cstdlib>
#include <string>

namespace zlink::samples::tictactoe
{

inline sample_topology_t load_sample_topology (int argc, char **argv)
{
    sample_topology_t topology;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index] == nullptr ? std::string{} : argv[index];
        const std::string prefix = "--api-http-endpoint=";
        if (arg.rfind (prefix, 0) == 0) {
            topology.api_http_endpoint = arg.substr (prefix.size ());
        }
        else if (arg == "--api-http-endpoint" && index + 1 < argc) {
            topology.api_http_endpoint = argv[++index];
        }
    }
    if (const char *endpoint = std::getenv ("ZLINK_CPP_CLIENT_API_HTTP_ENDPOINT")) {
        topology.api_http_endpoint = endpoint;
    }
    return topology;
}

} // namespace zlink::samples::tictactoe
