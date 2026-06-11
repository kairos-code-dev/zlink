/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "api_server_framework.hpp"

#include "../Configuration/sample_configuration.hpp"
#include "../host_support.hpp"

namespace zlink::samples::bingo
{

class api_server_host_factory_t
{
  public:
    static zlink::framework::app_t build (const sample_topology_t &topology, bool auto_stop = true);
    static zlink::framework::app_t &configure (zlink::framework::app_t &app,
                                               const sample_topology_t &topology,
                                               bool auto_stop = true);
};

inline zlink::framework::app_t api_server_host_factory_t::build (const sample_topology_t &topology,
                                                                 bool auto_stop)
{
    auto app = zlink::framework::app_t::create ();
    configure (app, topology, auto_stop);
    return app;
}

inline zlink::framework::app_t &api_server_host_factory_t::configure (
  zlink::framework::app_t &app,
  const sample_topology_t &topology,
  bool auto_stop)
{
    app.logging ().use_console ().set_level ("info");
    if (auto_stop) {
        app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
    }
    add_bingo_api_server (app, topology);
    return app;
}

} // namespace zlink::samples::bingo
