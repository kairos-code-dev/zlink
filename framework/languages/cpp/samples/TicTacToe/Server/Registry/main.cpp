/* SPDX-License-Identifier: MPL-2.0 */

#include "registry_host_factory.hpp"

int
main ()
{
  auto zlink = zlink::samples::tictactoe::registry_host_factory_t::build ();
  const auto registry = zlink.registry_options ();
  return registry.pub_endpoint == "tcp://127.0.0.1:48101" &&
             registry.router_endpoint == "tcp://127.0.0.1:48102"
           ? 0
           : 1;
}
