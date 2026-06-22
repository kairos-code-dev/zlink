/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::samples::bingo
{

// Resolves where a server role writes its message-flow log file. The directory
// comes from $BINGO_LOG_DIR (set by run_sample.sh to <sample>/logs) and defaults
// to a relative "logs" folder; use_file() creates the directory if it is missing.
inline std::string flow_log_path (const std::string &role)
{
    const char *dir = std::getenv ("BINGO_LOG_DIR");
    const std::string base = (dir != nullptr && *dir != '\0') ? dir : "logs";
    return base + "/bingo-" + role + ".log";
}

} // namespace zlink::samples::bingo
