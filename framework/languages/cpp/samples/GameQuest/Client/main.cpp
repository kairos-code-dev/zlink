/* SPDX-License-Identifier: MPL-2.0 */

#include "game_quest_client_scenario.hpp"

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
        api_url = env_or ("GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL", "http://127.0.0.1:7414");
    }
    auto stream_endpoint = read_option (argc, argv, "--stream-endpoint");
    if (stream_endpoint.empty ()) {
        stream_endpoint = env_or ("GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT", "tcp://127.0.0.1:7418");
    }
    if (!zlink::samples::gamequest::game_quest_client_scenario_t{}.run (api_url,
                                                                         stream_endpoint)) {
        std::cerr << "gamequest=failed\n";
        return 1;
    }
    std::cout << "gamequest=completed\n";
    return 0;
}
