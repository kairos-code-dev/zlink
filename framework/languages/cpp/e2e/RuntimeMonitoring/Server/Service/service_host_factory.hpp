/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Support/service_host.hpp"

namespace zlink::framework::e2e::runtime_monitoring::service
{

inline int run_all_service_host (int argc, char **argv)
{
    return run_service_host (argc, argv);
}

} // namespace zlink::framework::e2e::runtime_monitoring::service
