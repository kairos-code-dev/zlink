/* SPDX-License-Identifier: MPL-2.0 */

#include "api_server_host_factory.hpp"

int
main (int argc, char **argv)
{
  using namespace zlink::samples::bingo;

  const sample_topology_t topology;
  return api_server_host_factory_t::build (topology).run (argc, argv);
}
