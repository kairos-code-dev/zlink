/* SPDX-License-Identifier: MPL-2.0 */

#include "supportchat_client_scenario.hpp"

#include <exception>
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
    try {
        zlink::samples::supportchat::supportchat_client_scenario_t scenario;
        scenario.run (env_or ("SUPPORTCHAT_SUPPORT_HTTP_URL", "http://127.0.0.1:7508"),
                      env_or ("SUPPORTCHAT_SESSION_STREAM", "tcp://127.0.0.1:7505"));
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "supportchat client failed: " << error.what () << std::endl;
        return 1;
    }
}
