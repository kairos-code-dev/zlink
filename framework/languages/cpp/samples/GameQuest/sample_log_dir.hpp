/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::samples::gamequest
{

inline std::string flow_log_path (const std::string &role)
{
    const char *dir = std::getenv ("GAMEQUEST_LOG_DIR");
    const std::string base = (dir != nullptr && *dir != '\0') ? dir : "logs";
    return base + "/gamequest-" + role + ".log";
}

} // namespace zlink::samples::gamequest
