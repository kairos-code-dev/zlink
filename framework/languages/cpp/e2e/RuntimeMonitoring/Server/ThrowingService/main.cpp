/* SPDX-License-Identifier: MPL-2.0 */

#include "throwing_service_host_factory.hpp"

int main (int argc, char **argv)
{
    return zlink::framework::e2e::runtime_monitoring::throwing_service::
      run_throwing_service_host (argc, argv);
}
