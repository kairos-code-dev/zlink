/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <fstream>
#include <string>

namespace zlink::samples::tictactoe
{

inline constexpr const char *sample_log_file = "tictactoe-server.log";

inline void reset_sample_log ()
{
    std::ofstream (sample_log_file, std::ios::trunc).close ();
}

inline void append_sample_log_line (const std::string &line)
{
    std::ofstream log (sample_log_file, std::ios::app);
    log << line << '\n';
}

} // namespace zlink::samples::tictactoe
