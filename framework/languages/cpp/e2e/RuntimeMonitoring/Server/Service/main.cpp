/* SPDX-License-Identifier: MPL-2.0 */

#include "Support/service_host.hpp"

namespace rm_service = zlink::framework::e2e::runtime_monitoring::service;

int main (int argc, char **argv)
{
    return rm_service::run_service_host (argc, argv);
}
