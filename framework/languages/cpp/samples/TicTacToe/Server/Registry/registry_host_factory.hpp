/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"

namespace zlink::samples::tictactoe
{

class registry_host_factory_t
{
  public:
    static zlink::framework::app_t build (const sample_topology_t &topology)
    {
        auto app = zlink::framework::app_t::create ();
        app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.enable_registry (topology.registry_pub_endpoint, topology.registry_router_endpoint);
        });
        return app;
    }
};

} // namespace zlink::samples::tictactoe
