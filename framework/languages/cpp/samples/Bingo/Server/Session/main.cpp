/* SPDX-License-Identifier: MPL-2.0 */

#include "session_server_host_factory.hpp"

int
main (int argc, char **argv)
{
  const zlink::samples::bingo::sample_topology_t topology;
  return zlink::samples::bingo::session_server_host_factory_t::build (topology)
    .run (argc, argv);
}
