/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "api_server_framework.hpp"

namespace zlink::samples::bingo
{

class api_server_host_factory_t
{
  public:
    static zlink::framework::app_t build (const sample_topology_t &topology);
};

inline zlink::framework::app_t api_server_host_factory_t::build (const sample_topology_t &topology)
{
    auto app = zlink::framework::app_t::create ();
    app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
    add_bingo_api_server (app, topology);
    return app;
}

} // namespace zlink::samples::bingo
