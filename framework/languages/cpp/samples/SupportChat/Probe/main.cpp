/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Server/Configuration/sample_topology.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

int main ()
{
    using namespace zlink::samples::supportchat;
    std::filesystem::create_directories (supportchat_log_dir ());
    std::ofstream flow (supportchat_log_dir () + "/flow-probe.log", std::ios::app);
    const sample_topology_t topology;
    flow << "message flow role=probe action=location-store-read"
         << " redis=" << topology.redis_endpoint << "\n";
    flow << "message flow role=probe action=topology-ready"
         << " sessionStream=" << topology.session_stream_endpoint
         << " apiRoute=" << topology.api_route_endpoint
         << " supportRoute=" << topology.support_route_endpoint << "\n";
    std::cout << "topology=ready" << std::endl;
    return 0;
}
