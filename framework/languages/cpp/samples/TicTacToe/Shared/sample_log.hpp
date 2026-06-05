/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <fstream>
namespace zlink::samples::tictactoe
{

inline constexpr const char *sample_log_file = "tictactoe-server.log";

inline void reset_sample_log ()
{
    std::ofstream (sample_log_file, std::ios::trunc).close ();
}

} // namespace zlink::samples::tictactoe
