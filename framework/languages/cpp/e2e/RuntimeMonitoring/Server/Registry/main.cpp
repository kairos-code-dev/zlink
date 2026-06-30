/* SPDX-License-Identifier: MPL-2.0 */

#include "registry_host_factory.hpp"

int main (int argc, char **argv)
{
    return zlink::framework::e2e::runtime_monitoring::registry::run_registry_host (argc, argv);
}
