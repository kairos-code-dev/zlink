/* SPDX-License-Identifier: MPL-2.0 */

#include "session_server_host_factory.hpp"

int main (int argc, char **argv)
{
    const zlink::samples::tictactoe::sample_topology_t topology;
    const bool auto_stop = !zlink::samples::tictactoe::keep_running_requested ();
    return zlink::samples::tictactoe::session_server_host_factory_t::build (topology, auto_stop).run (argc, argv);
}
