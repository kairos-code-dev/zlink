/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include <zlink/framework.hpp>

#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::service
{

struct service_options_t
{
    std::string rid;
    std::string http_endpoint;
    std::string redis_endpoint;
    std::string redis_key_prefix;
    std::string channel_endpoint;
    std::string spot_router_endpoint;
    std::string spot_pub_endpoint;
    std::string evidence_file;
    std::string monitor_profile;
    std::string log_dir;

    static service_options_t bind (const configuration_section_t &section)
    {
        return {.rid = section.require ("rid"),
                .http_endpoint = section.require ("httpEndpoint"),
                .redis_endpoint = section.require ("redis.endpoint"),
                .redis_key_prefix = section.require ("redis.keyPrefix"),
                .channel_endpoint = section.require ("channelEndpoint"),
                .spot_router_endpoint = section.require ("spotRouterEndpoint"),
                .spot_pub_endpoint = section.require ("spotPubEndpoint"),
                .evidence_file = section.require ("evidenceFile"),
                .monitor_profile = section.require ("monitorProfile"),
                .log_dir = section.require ("logDir")};
    }
};

inline service_options_t read_service_options (app_t &app, int argc, char **argv)
{
    app.config ().load_cli (argc, argv);
    const auto path = app.config ().model ().get ("config");
    if (!path) {
        throw std::runtime_error ("RuntimeMonitoring service requires --config=<path>");
    }
    app.config ().load_json (*path);
    return app.config ().bind_required<service_options_t> ("e2e");
}

} // namespace zlink::framework::e2e::runtime_monitoring::service
