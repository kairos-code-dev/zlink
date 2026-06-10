/* SPDX-License-Identifier: MPL-2.0 */

#include "bingo_client_scenario.hpp"

#include <zlink/stream_connector.hpp>

int main ()
{
    using namespace zlink::samples::bingo;

    bingo_client_options_t options;
    zlink::stream_connector::connector_options_t connector_options;
    connector_options.endpoint = options.stream_endpoint;
    connector_options.connect_timeout = options.connect_timeout;
    connector_options.request_timeout = options.request_timeout;
    connector_options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::manual;

    auto client1 = zlink::stream_connector::connector_factory_t::create (
      connector_options);
    auto client2 = zlink::stream_connector::connector_factory_t::create (
      connector_options);
    register_bingo_client_codecs (client1);
    register_bingo_client_codecs (client2);

    return bingo_client_scenario_t{}.run (client1, client2) ? 0 : 1;
}
