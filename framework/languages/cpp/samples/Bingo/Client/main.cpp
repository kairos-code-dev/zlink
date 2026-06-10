/* SPDX-License-Identifier: MPL-2.0 */

#include "bingo_client_scenario.hpp"
#include "../../Shared/client_connector_helpers.hpp"

int main ()
{
    using namespace zlink::samples::bingo;

    bingo_client_options_t options;
    auto client1 = zlink::stream_connector::connector_factory_t::create (
      zlink::samples::make_manual_connector_options (
        options.stream_endpoint, options.connect_timeout, options.request_timeout));
    auto client2 = zlink::stream_connector::connector_factory_t::create (
      zlink::samples::make_manual_connector_options (
        options.stream_endpoint, options.connect_timeout, options.request_timeout));
    register_bingo_client_codecs (client1);
    register_bingo_client_codecs (client2);

    return bingo_client_scenario_t{}.run (client1, client2) ? 0 : 1;
}
