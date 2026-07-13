/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"
#include "../../Shared/runtime_monitoring_contracts.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline void run_mon_c1_dispatch_failure_scenario (const client_options_t &options)
{
    auto failure_reply =
      post_profile_request (options.trigger_url, "/profile/request/throw",
                            profile_req_t{.value = "throw", .marker = "mon-c1-request"});
    ensure (failure_reply.provider_rid == "svc-throw",
            "MON-C1 direct trigger did not hit throwing-monitor service");

    const auto evidence = wait_evidence_contains (
      options.throw_service_url, "monitor-throw|",
      std::chrono::milliseconds (10000));
    ensure (any_contains (evidence, "monitor-socket|"), "MON-C1 socket evidence missing");
    ensure (any_contains (evidence, "monitor-throw|"), "MON-C1 throwing monitor evidence missing");
    const auto log_lines = wait_log_contains (
      options.trigger_url, "/logs/throw-stderr/wait", "monitoring-event-dispatch",
      std::chrono::milliseconds (10000));
    ensure (any_contains (log_lines, "monitoring dispatch failure for e2e"),
            "MON-C1 throwing stderr marker missing");

    auto reply = post_profile_request (options.trigger_url, "/profile/request/throw",
                                       profile_req_t{.value = "throw",
                                                     .marker = "mon-c1-recovery"});
    ensure (reply.value == "profile:throw", "MON-C1 messaging did not recover");
    std::cout << "scenario MON-C1 passed\n";
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
