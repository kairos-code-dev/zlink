/* SPDX-License-Identifier: MPL-2.0 */

#include "registry_host_factory.hpp"

int
main (int argc, char **argv)
{
  return zlink::samples::tictactoe::registry_host_factory_t::build ()
    .run (argc, argv);
}
