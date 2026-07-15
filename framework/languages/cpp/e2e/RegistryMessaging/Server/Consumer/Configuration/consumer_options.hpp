/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace zlink::framework::e2e::registry_messaging::consumer
{

inline std::vector<std::string> split_csv (const std::string &text)
{
    std::vector<std::string> result;
    std::stringstream input (text);
    std::string item;
    while (std::getline (input, item, ',')) {
        if (!item.empty ()) {
            result.push_back (item);
        }
    }
    return result;
}

inline std::optional<int> parse_optional_int (const configuration_section_t &section,
                                             const char *name)
{
    const auto value = section.get (name);
    if (!value || value->empty ()) {
        return std::nullopt;
    }
    return std::stoi (*value);
}

struct consumer_options_t
{
    std::string http_endpoint;
    std::string redis_endpoint;
    std::string redis_key_prefix;
    std::vector<std::string> provider_endpoints;
    std::string log_dir;
    std::string trace_label;
    std::optional<int> client_max_message_size;

    static consumer_options_t bind (const configuration_section_t &section)
    {
        return {.http_endpoint = section.require ("httpEndpoint"),
                .redis_endpoint = section.get ("redis.endpoint").value_or (""),
                .redis_key_prefix = section.get ("redis.keyPrefix").value_or (""),
                .provider_endpoints =
                  split_csv (section.get ("providerEndpoints").value_or ("")),
                .log_dir = section.require ("logDir"),
                .trace_label = section.get ("traceLabel").value_or ("consumer"),
                .client_max_message_size =
                  parse_optional_int (section, "clientMaxMessageSize")};
    }
};

} // namespace zlink::framework::e2e::registry_messaging::consumer
