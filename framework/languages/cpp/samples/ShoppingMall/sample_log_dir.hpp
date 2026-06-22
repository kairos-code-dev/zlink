/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::samples::shoppingmall
{

inline std::string flow_log_path (const std::string &role)
{
    const char *dir = std::getenv ("SHOPPINGMALL_LOG_DIR");
    const std::string base = (dir != nullptr && *dir != '\0') ? dir : "logs";
    return base + "/shoppingmall-" + role + ".log";
}

} // namespace zlink::samples::shoppingmall
