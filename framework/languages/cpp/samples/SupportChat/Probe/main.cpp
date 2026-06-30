/* SPDX-License-Identifier: MPL-2.0 */

#include "../Server/Configuration/sample_names.hpp"
#include "../Server/Configuration/sample_topology.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace zlink::samples::supportchat
{

namespace
{

std::string read_option (int argc, char **argv, const std::string &name)
{
    const std::string prefix = name + "=";
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == name && index + 1 < argc) {
            return argv[index + 1];
        }
        if (arg.rfind (prefix, 0) == 0) {
            return arg.substr (prefix.size ());
        }
    }
    return {};
}

bool has_active_server_endpoint (const std::vector<framework::topology_entry_t> &topology,
                                 const std::string &channel,
                                 const std::string &endpoint)
{
    for (const auto &entry : topology) {
        if (entry.name == channel && entry.endpoint == endpoint &&
            entry.kind == framework::service_kind_t::channel &&
            entry.role == framework::service_role_t::server &&
            entry.state == framework::topology_state_t::active) {
            return true;
        }
    }
    return false;
}

void print_topology (const std::vector<framework::topology_entry_t> &topology)
{
    for (const auto &entry : topology) {
        std::cerr << "topology channel=" << entry.name << " kind="
                  << static_cast<int> (entry.kind) << " role=" << static_cast<int> (entry.role)
                  << " state=" << static_cast<int> (entry.state)
                  << " endpoint=" << entry.endpoint << "\n";
    }
}

} // namespace

} // namespace zlink::samples::supportchat

int main (int argc, char **argv)
{
    using namespace zlink::samples::supportchat;

    const auto registry_endpoint = read_option (argc, argv, "--registry-endpoint");
    if (registry_endpoint.empty ()) {
        std::cerr << "Missing --registry-endpoint.\n";
        return 2;
    }
    const auto timeout_text = read_option (argc, argv, "--timeout-seconds");
    const auto timeout = std::chrono::seconds (timeout_text.empty () ? 10 : std::stoi (timeout_text));
    const sample_topology_t topology;
    const auto api_channel_endpoint =
      read_option (argc, argv, "--api-channel-endpoint").empty () ?
        topology.api_channel_endpoint :
        read_option (argc, argv, "--api-channel-endpoint");
    const auto support_channel_endpoint =
      read_option (argc, argv, "--support-channel-endpoint").empty () ?
        topology.support_channel_endpoint :
        read_option (argc, argv, "--support-channel-endpoint");

    zlink::framework::registry_query_client_t client;
    auto connected = client.connect (registry_endpoint);
    if (!connected) {
        std::cerr << "registry query connect failed\n";
        return 2;
    }

    const auto deadline = std::chrono::steady_clock::now () + timeout;
    std::vector<zlink::framework::topology_entry_t> last_topology;
    while (std::chrono::steady_clock::now () < deadline) {
        auto result = client.topology ();
        if (result) {
            last_topology = result.value ();
            if (has_active_server_endpoint (last_topology, sample_names_t::api_channel,
                                            api_channel_endpoint) &&
                has_active_server_endpoint (last_topology, sample_names_t::support_channel,
                                            support_channel_endpoint)) {
                std::cout << "topology=ready\n";
                return 0;
            }
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }

    print_topology (last_topology);
    std::cerr << "Timed out waiting for SupportChat sample topology readiness.\n";
    return 1;
}
