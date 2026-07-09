/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/messages.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace e2e = zlink::e2e::to_actor_messaging;

namespace
{

std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

void require (bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error (message);
    }
}

zlink::http_client::client_t make_http (const std::string &base_url)
{
    return zlink::http_client::client_t::create ()
      .base_url (base_url)
      .timeout (std::chrono::seconds (30))
      .build ();
}

e2e::actor_call_response_t call (zlink::http_client::client_t &caller,
                                 const std::string &endpoint,
                                 const std::string &scenario,
                                 const std::string &actor_id,
                                 const std::string &value)
{
    return caller.post (endpoint)
      .body (e2e::actor_call_request_t{scenario, actor_id, value})
      .fetch<e2e::actor_call_response_t> ();
}

void ensure_actor (zlink::http_client::client_t &actor,
                   const std::string &scenario,
                   const std::string &actor_id)
{
    const auto response = actor.post ("/ensure")
                            .body (e2e::actor_call_request_t{scenario, actor_id, "ensure"})
                            .fetch<e2e::actor_call_response_t> ();
    require (response.error_kind.empty (),
             scenario + " ensure failed: " + response.error_kind);
    require (response.result == "ensured", scenario + " ensure returned " + response.result);
}

void wait_until_ready (zlink::http_client::client_t &caller,
                       const std::string &scenario,
                       const std::string &actor_id)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (20);
    e2e::actor_call_response_t response;
    while (std::chrono::steady_clock::now () < deadline) {
        response = call (caller, "/request", scenario, actor_id, "ready");
        if (response.error_kind.empty () && response.result == "reply:ready") {
            return;
        }
        if (response.error_kind != "request_failed"
            && response.error_kind != "actor_route_not_found") {
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error (scenario + " readiness failed: " + response.error_kind + " "
                              + response.result);
}

void ensure_ready (zlink::http_client::client_t &actor,
                   zlink::http_client::client_t &caller,
                   const std::string &scenario,
                   const std::string &actor_id)
{
    ensure_actor (actor, scenario, actor_id);
    wait_until_ready (caller, scenario + "-ready", actor_id);
}

void assert_call (zlink::http_client::client_t &caller,
                  const std::string &scenario,
                  const std::string &actor_id,
                  const std::string &value,
                  const std::string &expected,
                  bool send)
{
    const auto response = call (caller, send ? "/send" : "/request", scenario, actor_id, value);
    require (response.error_kind.empty (),
             scenario + " unexpected error: " + response.error_kind);
    require (response.result == expected,
             scenario + " expected " + expected + " got " + response.result);
}

void assert_failure (zlink::http_client::client_t &caller,
                     const std::string &scenario,
                     const std::string &actor_id,
                     const std::string &expected_kind,
                     bool send)
{
    const auto response =
      call (caller, send ? "/send" : "/request", scenario, actor_id, "missing");
    require (response.error_kind == expected_kind,
             scenario + " expected " + expected_kind + " got " + response.error_kind
               + " result=" + response.result);
}

void prepare_failure (zlink::http_client::client_t &caller,
                      const std::string &scenario,
                      const std::string &actor_id)
{
    const auto response = call (caller, "/prepare-failure", scenario, actor_id, "prepare");
    require (response.error_kind.empty (),
             scenario + " prepare failed: " + response.error_kind + " " + response.result);
    require (response.result == "prepared", scenario + " prepare returned " + response.result);
}

void require_evidence (const std::vector<e2e::actor_evidence_t> &evidence,
                       const std::string &scenario,
                       const std::string &kind)
{
    for (const auto &entry : evidence) {
        if (entry.scenario == scenario && entry.kind == kind) {
            return;
        }
    }
    throw std::runtime_error (scenario + " " + kind + " evidence missing");
}

} // namespace

int main ()
{
    auto actor = make_http (env_or ("ZLINK_CPP_E2E_ACTOR_HTTP"));
    auto caller = make_http (env_or ("ZLINK_CPP_E2E_CALLER_HTTP"));

    ensure_ready (actor, caller, "TA-A1", "ta-a1");
    assert_call (caller, "TA-A1-send", "ta-a1", "a1-send", "sent", true);
    assert_call (caller, "TA-A1-request", "ta-a1", "a1-request", "reply:a1-request", false);

    ensure_ready (actor, caller, "TA-A2", "ta-a2");
    assert_call (caller, "TA-A2-unbound-send", "ta-a2", "a2-send", "sent", true);
    assert_call (caller, "TA-A2-unbound-request", "ta-a2", "a2-request", "reply:a2-request",
                 false);

    assert_failure (caller, "TA-A3-before-bind", "ta-a3-missing", "actor_route_not_found", true);
    ensure_ready (actor, caller, "TA-A3", "ta-a3");
    assert_call (caller, "TA-A3-after-bind-send", "ta-a3", "a3-send", "sent", true);
    assert_call (caller, "TA-A3-after-bind-request", "ta-a3", "a3-request", "reply:a3-request",
                 false);

    ensure_ready (actor, caller, "TA-A4", "ta-a4");
    assert_call (caller, "TA-A4-disconnected-send", "ta-a4", "a4-send", "sent", true);
    assert_call (caller, "TA-A4-disconnected-request", "ta-a4", "a4-request", "reply:a4-request",
                 false);
    assert_failure (caller, "TA-A4-destroyed", "ta-a4-destroyed", "actor_route_not_found",
                    false);

    assert_failure (caller, "TA-B1-missing-send", "missing-actor", "actor_route_not_found", true);
    assert_failure (caller, "TA-B1-missing-request", "missing-actor", "actor_route_not_found",
                    false);

    ensure_ready (actor, caller, "TA-B2", "ta-b2-stale");
    prepare_failure (caller, "TA-B2-prepare", "ta-b2-stale");
    assert_failure (caller, "TA-B2-stale-location", "ta-b2-stale", "actor_location_stale",
                    false);

    prepare_failure (caller, "TA-B3-prepare", "ta-b3-disconnected");
    assert_failure (caller, "TA-B3-route-not-connected", "ta-b3-disconnected",
                    "route_not_connected", false);
    ensure_ready (actor, caller, "TA-B3", "ta-b3");
    assert_call (caller, "TA-B3-route-restored", "ta-b3", "b3-request", "reply:b3-request",
                 false);

    const auto evidence = actor.get ("/evidence").fetch<std::vector<e2e::actor_evidence_t>> ();
    require_evidence (evidence, "TA-A1-send", "send");
    require_evidence (evidence, "TA-A1-request", "request");
    require_evidence (evidence, "TA-A2-unbound-send", "send");
    require_evidence (evidence, "TA-A3-after-bind-request", "request");
    require_evidence (evidence, "TA-A4-disconnected-send", "send");

    std::cout << "to-actor-messaging e2e result=passed\n";
    return 0;
}
