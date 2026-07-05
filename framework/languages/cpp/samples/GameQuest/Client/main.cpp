/* SPDX-License-Identifier: MPL-2.0 */

#include "gamequest_client_scenario.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

std::string env_or (const char *name, std::string fallback)
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

} // namespace

int main ()
{
    const auto api_a_stream =
      env_or ("GAMEQUEST_API_A_STREAM_ENDPOINT", "tcp://127.0.0.1:7421");
    const auto api_b_stream =
      env_or ("GAMEQUEST_API_B_STREAM_ENDPOINT", "tcp://127.0.0.1:7422");
    const auto api_a_http =
      env_or ("GAMEQUEST_API_A_HTTP_URL", "http://127.0.0.1:7423");
    const auto api_b_http =
      env_or ("GAMEQUEST_API_B_HTTP_URL", "http://127.0.0.1:7424");
    if (!zlink::samples::gamequest::gamequest_client_scenario_t{}.run (
          api_a_stream, api_b_stream, api_a_http, api_b_http)) {
        std::cerr << "gamequest=failed\n";
        return 1;
    }
    return 0;
}
