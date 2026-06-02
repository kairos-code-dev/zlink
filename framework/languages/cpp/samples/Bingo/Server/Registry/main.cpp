/* SPDX-License-Identifier: MPL-2.0 */

#include "registry_host_factory.hpp"

int
main ()
{
  const zlink::samples::bingo::sample_topology_t topology;
  auto zlink =
    zlink::samples::bingo::registry_host_factory_t::build (topology);
  const auto registry = zlink.registry_options ();
  return registry.pub_endpoint == topology.registry_pub_endpoint &&
             registry.router_endpoint == topology.registry_router_endpoint
           ? 0
           : 1;
}
