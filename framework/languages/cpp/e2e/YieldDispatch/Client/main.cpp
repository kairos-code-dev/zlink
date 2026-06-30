/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/yield_dispatch_contracts.hpp"
#include "Scenarios/yd_a1_basic_terminator_scenario.hpp"
#include "Scenarios/yd_a2_yield_terminator_scenario.hpp"
#include "Scenarios/yd_a3_continuation_context_scenario.hpp"
#include "Scenarios/yd_a4_worker_yield_scenario.hpp"
#include "Scenarios/yd_b1_other_actor_progress_scenario.hpp"
#include "Scenarios/yd_b2_same_actor_reentry_scenario.hpp"
#include "Scenarios/yd_b3_actor_join_yield_scenario.hpp"
#include "Scenarios/yd_c1_timer_isolation_scenario.hpp"
#include "Scenarios/yd_c2_timer_reentry_scenario.hpp"
#include "Scenarios/yd_c3_actor_timer_isolation_scenario.hpp"
#include "Scenarios/yd_d2_remote_spot_yield_scenario.hpp"
#include "Scenarios/yield_actor_scenario_context.hpp"
#include "Support/client_options.hpp"
#include "Support/scenario_assert.hpp"

#include <zlink/stream_connector.hpp>

#include <chrono>
#include <iostream>
#include <string>

namespace yd = zlink::framework::e2e::yield_dispatch;
namespace yd_client = zlink::framework::e2e::yield_dispatch::client;

namespace
{

using yd_client::ensure;
using yd_client::ensure_result;
using yd_client::unique_id;

} // namespace

int main ()
{
    try {
        const auto client_options = yd_client::parse_client_options ();
        ensure (!client_options.session_a_stream_endpoint.empty (),
                "ZLINK_CPP_E2E_STREAM_ENDPOINT is required");

        auto options = yd_client::make_connector_options (client_options);
        auto client = zlink::stream_connector::connector_factory_t::create (options);
        auto connected = client.connect ();
        ensure (static_cast<bool> (connected), "YieldDispatch stream connect failed");
        auto observer = zlink::stream_connector::connector_factory_t::create (options);
        auto observer_connected = observer.connect ();
        ensure (static_cast<bool> (observer_connected),
                "YieldDispatch observer stream connect failed");

        const auto spot_rid = unique_id ("yield-track-a");
        auto spot =
          client.request (yd::ensure_spot_req_t{.spot_rid = spot_rid})
            .packet_name (yd::ensure_spot_req_t::packet_name)
            .timeout (std::chrono::milliseconds (15000))
            .submit<yd::ensure_spot_reply_t> ();
        ensure_result (spot, "YD-A ensure spot request failed");
        ensure (spot.value ().spot_rid == spot_rid, "YD-A ensure spot reply mismatch");
        const yd_client::yield_actor_scenario_context_t actors{
          .spot_rid = spot_rid, .actor_a = unique_id ("actor-a"), .actor_b = unique_id ("actor-b")};
        auto bound_actors =
          client.request (yd::bind_yield_actors_req_t{.spot_rid = actors.spot_rid,
                                                      .actor_ids = {actors.actor_a, actors.actor_b}})
            .packet_name (yd::bind_yield_actors_req_t::packet_name)
            .timeout (std::chrono::milliseconds (15000))
            .submit<yd::bind_yield_actors_reply_t> ();
        ensure (static_cast<bool> (bound_actors), "YD-B bind actors failed");
        ensure (bound_actors.value ().actors.size () == 2, "YD-B bind actor count mismatch");
        auto observer_bound_actors =
          observer.request (
                    yd::bind_yield_actors_req_t{.spot_rid = actors.spot_rid,
                                                .actor_ids = {actors.actor_a, actors.actor_b}})
            .packet_name (yd::bind_yield_actors_req_t::packet_name)
            .timeout (std::chrono::milliseconds (15000))
            .submit<yd::bind_yield_actors_reply_t> ();
        ensure (static_cast<bool> (observer_bound_actors),
                "YD-B observer bind actors failed");

        yd_client::run_yd_a1_basic_terminator_scenario (client, spot_rid);
        yd_client::run_yd_a2_yield_terminator_scenario (client, observer, spot_rid);
        yd_client::run_yd_a3_continuation_context_scenario (client, observer, spot_rid);
        yd_client::run_yd_a4_worker_yield_scenario (client, observer, spot_rid);

        yd_client::run_yd_b1_other_actor_progress_scenario (client, observer, actors);
        yd_client::run_yd_b2_same_actor_reentry_scenario (client, observer, actors);
        yd_client::run_yd_b3_actor_join_yield_scenario (client, observer, actors);

        const auto timer_spot_rid = unique_id ("yield-timer");
        auto timer_spot =
          client.request (yd::ensure_spot_req_t{.spot_rid = timer_spot_rid})
            .packet_name (yd::ensure_spot_req_t::packet_name)
            .timeout (std::chrono::milliseconds (15000))
            .submit<yd::ensure_spot_reply_t> ();
        ensure (static_cast<bool> (timer_spot), "YD-C ensure timer spot request failed");
        ensure (timer_spot.value ().spot_rid == timer_spot_rid,
                "YD-C ensure timer spot reply mismatch");

        yd_client::run_yd_c1_timer_isolation_scenario (client, observer, timer_spot_rid);
        yd_client::run_yd_c2_timer_reentry_scenario (client, observer, timer_spot_rid);
        yd_client::run_yd_c3_actor_timer_isolation_scenario (client, observer, actors);

        yd_client::run_yd_d2_remote_spot_yield_scenario (client);

        (void) observer.close ();
        (void) client.close ();
        std::cout << "yield-dispatch track-a-d result=passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << "yield-dispatch client failed: " << error.what () << "\n";
        return 1;
    }
}
