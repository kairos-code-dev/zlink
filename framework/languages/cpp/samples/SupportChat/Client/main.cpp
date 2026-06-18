/* SPDX-License-Identifier: MPL-2.0 */

#include "support_chat_client_scenario.hpp"

#include "Configuration/sample_configuration.hpp"

#include <zlink/stream_connector.hpp>
#include <zlink/stream_e2e_client.hpp>

#include <iostream>
#include <string>

namespace
{

zlink::stream_connector::connector_t make_connector (const std::string &endpoint,
                                                     std::chrono::milliseconds connect_timeout,
                                                     std::chrono::milliseconds request_timeout)
{
    zlink::stream_connector::connector_options_t connector_options;
    connector_options.endpoint = endpoint;
    connector_options.connect_timeout = connect_timeout;
    connector_options.request_timeout = request_timeout;
    connector_options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;
    return zlink::stream_connector::connector_factory_t::create (connector_options);
}

} // namespace

int main (int argc, char **argv)
{
    using namespace zlink::samples::supportchat;

    support_chat_client_options_t options{load_sample_topology (argc, argv)};

    auto core_customer =
      make_connector (options.stream_endpoint, options.connect_timeout, options.request_timeout);
    auto core_agent =
      make_connector (options.stream_endpoint, options.connect_timeout, options.request_timeout);
    auto core_reconnecting_customer =
      make_connector (options.stream_endpoint, options.connect_timeout, options.request_timeout);
    auto core_waiting_customer =
      make_connector (options.stream_endpoint, options.connect_timeout, options.request_timeout);
    auto core_closing_customer =
      make_connector (options.stream_endpoint, options.connect_timeout, options.request_timeout);
    auto core_closing_agent =
      make_connector (options.stream_endpoint, options.connect_timeout, options.request_timeout);

    auto customer = zlink::stream_e2e_client::use (core_customer);
    auto agent = zlink::stream_e2e_client::use (core_agent);
    auto reconnecting_customer = zlink::stream_e2e_client::use (core_reconnecting_customer);
    auto waiting_customer = zlink::stream_e2e_client::use (core_waiting_customer);
    auto closing_customer = zlink::stream_e2e_client::use (core_closing_customer);
    auto closing_agent = zlink::stream_e2e_client::use (core_closing_agent);

    const bool ok = support_chat_client_scenario_t{}.run (customer, agent, reconnecting_customer,
                                                          waiting_customer, closing_customer,
                                                          closing_agent);
    if (!ok) {
        std::cerr << "supportchat=failed\n";
        return 1;
    }
    std::cout << "supportchat=completed\n";
    return 0;
}
