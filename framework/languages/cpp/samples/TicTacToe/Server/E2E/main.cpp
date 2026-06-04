/* SPDX-License-Identifier: MPL-2.0 */

#include "../../Shared/E2E/client_e2e_server.hpp"

#include <zlink/Contracts/Sockets/stream_socket.hpp>

#include <boost/asio.hpp>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace
{

std::pair<std::string, std::uint16_t>
parse_http_host_port (const std::string &endpoint)
{
  constexpr auto scheme = std::string_view { "http://" };
  if (endpoint.rfind (scheme, 0) != 0) {
    throw std::runtime_error ("unsupported HTTP endpoint");
  }
  const auto authority = endpoint.substr (scheme.size ());
  const auto colon = authority.rfind (':');
  if (colon == std::string::npos) {
    throw std::runtime_error ("HTTP endpoint port is required");
  }
  const auto host = authority.substr (0, colon);
  const auto port_text = authority.substr (colon + 1);
  const auto port = static_cast<std::uint16_t> (std::stoul (port_text));
  return { host, port };
}

bool
can_connect_tcp (const std::string &host, std::uint16_t port)
{
  boost::asio::io_context io;
  boost::asio::ip::tcp::socket socket (io);
  boost::system::error_code ec;
  socket.connect ({ boost::asio::ip::make_address (host, ec), port }, ec);
  return !ec;
}

void
wait_for_http_listener (const std::string &endpoint, int &api_exit_code)
{
  const auto [host, port] = parse_http_host_port (endpoint);
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (api_exit_code != -1) {
      throw std::runtime_error ("HTTP API exited before readiness");
    }
    if (can_connect_tcp (host, port)) {
      return;
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (10));
  }
  throw std::runtime_error ("HTTP API did not become ready");
}

} // namespace

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

  wait_for_http_listener (topology.api_http_endpoint, api_exit_code);

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
