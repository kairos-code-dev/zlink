/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "sample_topology.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::bingo
{

using namespace framework;

inline void load_sample_configuration (app_t &app, int argc, char **argv)
{
    app.config ().load_cli (argc, argv);
    if (auto path = app.config ().model ().get ("config")) {
        app.config ().load_json (*path);
    }
    app.config ().load_env ("ZLINK_CPP_SAMPLE__").load_cli (argc, argv);
}

inline sample_topology_t sample_topology_from_config (app_t &app)
{
    return app.config ().bind<sample_topology_t> ("sample.topology").value_or (sample_topology_t{});
}

inline bool sample_keep_running (app_t &app)
{
    return app.config ().model ().get ("sample.host.keepRunning").value_or ("false") == "true";
}

} // namespace zlink::samples::bingo
