/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "api_server_framework.hpp"

namespace zlink::samples::tictactoe
{

class api_server_host_factory_t
{
  public:
    static zlink::framework::app_t build (const sample_topology_t &topology)
    {
        auto app = zlink::framework::app_t::create ();
        add_sample_auto_stop (app);
        add_tictactoe_api_server (app, topology);
        return app;
    }
};

} // namespace zlink::samples::tictactoe
