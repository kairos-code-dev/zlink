/* SPDX-License-Identifier: MPL-2.0 */

#include "../../Shared/E2E/client_e2e_server.hpp"

#include <zlink/Contracts/Sockets/stream_socket.hpp>

#include <thread>

int
main ()
{
  using namespace zlink::samples::tictactoe;

  reset_sample_log ();
  const sample_topology_t topology;
  auto api_app = build_client_e2e_api_server (topology);
  int api_exit_code = -1;
  std::thread api_thread ([&api_app, &api_exit_code] {
    const char *argv_raw[] = { "tictactoe-api" };
    auto **argv = const_cast<char **> (argv_raw);
    api_exit_code = api_app.run (1, argv);
  });

  zlink::context_t context;
  zlink::stream_socket_t server (context);
  server.options ().notify (false);
  server.bind (topology.stream_endpoint);
  run_client_e2e_stream_server (
    server,
    topology.stream_endpoint,
    sample_names_t::x_actor_id);

  api_app.stop ();
  api_thread.join ();
  return api_exit_code == 0 ? 0 : 1;
}
