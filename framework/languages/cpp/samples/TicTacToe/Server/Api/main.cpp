/* SPDX-License-Identifier: MPL-2.0 */

#include "api_server_host_factory.hpp"

int
main (int argc, char **argv)
{
  const zlink::samples::tictactoe::sample_topology_t topology;
  return zlink::samples::tictactoe::api_server_host_factory_t::build (topology)
    .run (argc, argv);
}
