/* SPDX-License-Identifier: MPL-2.0 */

#include "shopping_mall_client_scenario.hpp"

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
        api_url = env_or ("SHOPPINGMALL_API_A_HTTP_URL", "http://127.0.0.1:48203");
    }
    if (!zlink::samples::shoppingmall::shopping_mall_client_scenario_t{}.run (api_url)) {
        std::cerr << "shoppingmall=failed\n";
        return 1;
    }
    std::cout << "shoppingmall=completed\n";
    return 0;
}
