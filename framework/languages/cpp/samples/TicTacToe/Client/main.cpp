/* SPDX-License-Identifier: MPL-2.0 */

#include "tictactoe_client_scenario.hpp"

#include <zlink/http_client.hpp>
#include <zlink/stream_connector.hpp>

#include <iostream>
#include <stdexcept>

int main ()
{
    using namespace zlink::samples::tictactoe;

    try {
        tictactoe_client_options_t options;
        auto http_client = zlink::http_client::client_t::create ()
                             .base_url (options.api_http_endpoint)
                             .json ()
                             .build ();
        auto created = http_client.post ("/games")
                         .body (create_game_http_req_t{options.game_name})
                         .submit<create_game_http_res_t> ()
                         .result ();
        if (!created) { throw std::runtime_error ("HTTP POST /games failed."); }

        const auto room = created.value ().body;
        if (room.play_endpoint.empty ()) {
            throw std::runtime_error ("API returned an empty play endpoint.");
        }

        zlink::stream_connector::connector_options_t connector_options;
        connector_options.endpoint = room.play_endpoint;
        connector_options.connect_timeout = options.stream_timeout;
        connector_options.request_timeout = options.stream_timeout;
        connector_options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::manual;

        auto client1 = zlink::stream_connector::connector_factory_t::create (
          connector_options);
        auto client2 = zlink::stream_connector::connector_factory_t::create (
          connector_options);
        register_tictactoe_client_codecs (client1);
        register_tictactoe_client_codecs (client2);

        if (!tictactoe_client_scenario_t{}.run (client1, client2, room, options)) { return 1; }
        std::cout << "tictactoe=completed\n";
        return 0;
    }
    catch (const std::exception &ex) {
        std::cerr << "tictactoe=failed " << ex.what () << '\n';
        return 1;
    }
}
