/* SPDX-License-Identifier: MPL-2.0 */

#include "../../Shared/E2E/client_e2e_stream_server.hpp"

#include <zlink/Contracts/Sockets/stream_socket.hpp>

int main ()
{
    zlink::context_t context;
    zlink::stream_socket_t server (context);
    server.options ().notify (false);
    const zlink::samples::bingo::sample_topology_t topology;
    server.bind (topology.stream_endpoint);
    zlink::samples::bingo::run_client_e2e_stream_server (server, topology.stream_endpoint);
    return 0;
}
