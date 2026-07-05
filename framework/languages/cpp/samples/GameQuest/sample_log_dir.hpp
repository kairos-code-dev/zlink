/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Server/Configuration/sample_topology.hpp"

#include <string>

namespace zlink::samples::gamequest
{

inline std::string gamequest_flow_log_path (const std::string &role)
{
    return gamequest_log_dir () + "/flow-" + role + ".log";
}

} // namespace zlink::samples::gamequest
