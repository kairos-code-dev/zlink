/* SPDX-License-Identifier: MPL-2.0 */

#include "api_server_host_factory.hpp"

int main (int argc, char **argv)
{
    const zlink::samples::tictactoe::sample_topology_t topology;
    const bool auto_stop = !zlink::samples::tictactoe::keep_running_requested ();
    if (auto_stop) {
        return 0;
    }
    return zlink::samples::tictactoe::api_server_host_factory_t::build (topology, auto_stop)
      .run (argc, argv);
}
