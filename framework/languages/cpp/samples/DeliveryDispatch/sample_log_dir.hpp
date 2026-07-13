/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::samples::deliverydispatch
{

inline std::string flow_log_path (const std::string &role)
{
    const char *dir = std::getenv ("DELIVERYDISPATCH_LOG_DIR");
    const std::string base = (dir != nullptr && *dir != '\0') ? dir : "logs";
    return base + "/deliverydispatch-" + role + ".log";
}

} // namespace zlink::samples::deliverydispatch
