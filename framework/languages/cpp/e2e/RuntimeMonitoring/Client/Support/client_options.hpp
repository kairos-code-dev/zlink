/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::client
{

struct client_options_t
{
    std::string scenario;
    std::string service_url;
    std::string filtered_service_url;
    std::string throw_service_url;
    std::string trigger_url;
    std::string log_dir;
    std::string old_service_channel_endpoint;
    std::string new_service_channel_endpoint;
};

inline client_options_t read_client_options (int argc, char **argv)
{
    client_options_t options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto assign = [&] (const std::string &prefix, std::string &target) {
            if (argument.rfind (prefix, 0) != 0) {
                return false;
            }
            target = argument.substr (prefix.size ());
            return true;
        };
        if (!assign ("--scenario=", options.scenario)
            && !assign ("--service-url=", options.service_url)
            && !assign ("--filtered-service-url=", options.filtered_service_url)
            && !assign ("--throw-service-url=", options.throw_service_url)
            && !assign ("--trigger-url=", options.trigger_url)
            && !assign ("--log-dir=", options.log_dir)
            && !assign ("--old-service-channel-endpoint=",
                        options.old_service_channel_endpoint)
            && !assign ("--new-service-channel-endpoint=",
                        options.new_service_channel_endpoint)) {
            throw std::runtime_error ("unknown RuntimeMonitoring client option: " + argument);
        }
    }
    if (options.scenario.empty () || options.service_url.empty ()
        || options.filtered_service_url.empty () || options.trigger_url.empty ()
        || options.log_dir.empty ()) {
        throw std::runtime_error (
          "RuntimeMonitoring client requires scenario, service URLs, trigger URL, and log dir");
    }
    return options;
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
