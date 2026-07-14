/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "Support/client_support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <future>
#include <iostream>
#include <vector>

namespace sf_client = zlink::framework::e2e::store_failure::client;
namespace sf = zlink::framework::e2e::store_failure;

namespace
{

struct options_t
{
    std::string scenario = sf_client::env_or ("ZLINK_CPP_SF_SCENARIO", "SF-A1");
    std::string consumer_url = sf_client::env_or ("ZLINK_CPP_SF_CONSUMER_URL");
    std::string provider_a_url = sf_client::env_or ("ZLINK_CPP_SF_PROVIDER_A_URL");
    std::string provider_b_url = sf_client::env_or ("ZLINK_CPP_SF_PROVIDER_B_URL");
    std::string provider_c_url = sf_client::env_or ("ZLINK_CPP_SF_PROVIDER_C_URL");
    std::string provider_c_start_file =
      sf_client::env_or ("ZLINK_CPP_SF_PROVIDER_C_START_FILE");
    std::string provider_a_endpoint = sf_client::env_or ("ZLINK_CPP_SF_PROVIDER_A_ENDPOINT");
    std::string provider_b_endpoint = sf_client::env_or ("ZLINK_CPP_SF_PROVIDER_B_ENDPOINT");
    std::string replacement_provider_url =
      sf_client::env_or ("ZLINK_CPP_SF_PROVIDER_B_REPLACEMENT_URL");
    std::string replacement_provider_endpoint =
      sf_client::env_or ("ZLINK_CPP_SF_PROVIDER_B_REPLACEMENT_ENDPOINT");
    std::chrono::milliseconds heartbeat{
      sf_client::env_int ("ZLINK_CPP_SF_LOCATION_HEARTBEAT_MS", 1000)};
    std::chrono::milliseconds lease_ttl{
      sf_client::env_int ("ZLINK_CPP_SF_LOCATION_LEASE_TTL_MS", 3000)};
    std::chrono::milliseconds polling{sf_client::env_int ("ZLINK_CPP_SF_LOCATION_POLLING_MS", 500)};
    std::chrono::milliseconds grace{sf_client::env_int ("ZLINK_CPP_SF_LOCATION_GRACE_MS", 6000)};
};

std::chrono::milliseconds stale_peer_timeout (const options_t &options)
{
    return options.lease_ttl * 6 + options.polling * 12 + options.heartbeat * 4;
}

void baseline (const options_t &options)
{
    sf_client::wait_peers (
      options.consumer_url,
      [] (const auto &peers) {
          return sf_client::has_rid (peers, "api-a") && sf_client::has_rid (peers, "api-b");
      },
      options.lease_ttl + options.polling * 8, "SF-A1 providers did not appear");

    std::set<std::string> served;
    for (int i = 0; i < 8; ++i) {
        auto reply = sf_client::request_profile (options.consumer_url, "sf-a1-" + std::to_string (i));
        sf_client::ensure (reply.value == "profile:fast", "SF-A1 unexpected reply");
        served.insert (reply.provider_rid);
    }
    sf_client::ensure (!served.empty (), "SF-A1 no provider served traffic");

    for (const auto &url : {options.consumer_url, options.provider_a_url, options.provider_b_url}) {
        sf_client::wait_status (
          url,
          [] (const auto &status) {
              return status.store_healthy && status.owner_lease_healthy
                     && status.owner_lease_renewed_at_unix_ms > 0
                     && status.last_refresh_at_unix_ms > 0;
          },
          options.heartbeat * 8, "SF-A1 status was not healthy");
    }
    std::cout << "scenario SF-A1 passed\n";
}

void polling_fallback (const options_t &options)
{
    const auto status = sf_client::get_status (options.consumer_url);
    sf_client::ensure (!status.watch_enabled, "SF-A2 consumer unexpectedly reports watch enabled");
    sf_client::wait_peers (
      options.consumer_url,
      [] (const auto &peers) {
          return sf_client::has_rid (peers, "api-a") && sf_client::has_rid (peers, "api-b")
                 && !sf_client::has_rid (peers, "api-c");
      },
      options.polling * 8 + options.heartbeat, "SF-A2 initial providers were not ready");

    sf_client::ensure (!options.provider_c_start_file.empty (),
                       "SF-A2 provider start signal path is required");
    std::ofstream (options.provider_c_start_file) << "start\n";
    sf_client::wait_peers (
      options.consumer_url,
      [] (const auto &peers) { return sf_client::has_rid (peers, "api-c"); },
      options.polling * 8 + options.heartbeat,
      "SF-A2 added provider did not appear through polling");

    bool provider_c_served = false;
    const auto routing_deadline =
      std::chrono::steady_clock::now () + options.polling * 8 + options.heartbeat;
    for (int attempt = 0; std::chrono::steady_clock::now () < routing_deadline; ++attempt) {
        const auto reply = sf_client::request_profile (
          options.consumer_url, "sf-a2-added-" + std::to_string (attempt));
        sf_client::ensure (reply.value == "profile:fast", "SF-A2 added provider request failed");
        if (reply.provider_rid == "api-c") {
            provider_c_served = true;
            break;
        }
    }
    sf_client::ensure (provider_c_served, "SF-A2 added provider never served traffic");

    sf_client::post_empty (options.provider_c_url, "/shutdown");
    sf_client::wait_down (options.provider_c_url);
    sf_client::wait_peers (
      options.consumer_url,
      [] (const auto &peers) { return !sf_client::has_rid (peers, "api-c"); },
      options.polling * 8 + options.heartbeat,
      "SF-A2 removed provider did not disappear through polling");
    for (int attempt = 0; attempt < 8; ++attempt) {
        const auto reply = sf_client::request_profile (
          options.consumer_url, "sf-a2-removed-" + std::to_string (attempt));
        sf_client::ensure (reply.provider_rid != "api-c",
                           "SF-A2 removed provider still served traffic");
    }
    std::cout << "scenario SF-A2 passed\n";
}

void fail_static (const options_t &options)
{
    sf_client::stop_store ();
    try {
        sf_client::drive_requests (options.consumer_url, "sf-b1", options.lease_ttl * 7 / 10,
                                   "SF-B1");
        sf_client::wait_status (
          options.consumer_url,
          [] (const auto &status) { return !status.store_healthy && !status.last_error.empty (); },
          options.heartbeat * 8, "SF-B1 outage status was not visible");
    }
    catch (...) {
        sf_client::restart_store ();
        throw;
    }
    sf_client::restart_store ();
    sf_client::wait_status (
      options.consumer_url,
      [] (const auto &status) { return status.store_healthy && status.owner_lease_healthy; },
      options.heartbeat * 10, "SF-B1 status did not recover");
    std::cout << "scenario SF-B1 passed\n";
}

void grace_exceeded (const options_t &options)
{
    sf_client::stop_store ();
    try {
        sf_client::wait_ready (options.replacement_provider_url);
        const auto replies = sf_client::drive_requests (
          options.consumer_url, "sf-b2", options.grace + options.heartbeat * 2, "SF-B2");
        sf_client::ensure (
          std::none_of (replies.begin (), replies.end (), [] (const auto &reply) {
              return reply.provider_rid == "api-b";
          }),
          "SF-B2 replacement provider served before recovery");
        const auto status = sf_client::get_status (options.consumer_url);
        sf_client::ensure (!status.store_healthy, "SF-B2 outage was not visible after grace");
    }
    catch (...) {
        sf_client::restart_store ();
        throw;
    }
    sf_client::restart_store ();
    sf_client::wait_status (
      options.consumer_url,
      [] (const auto &status) { return status.store_healthy && status.owner_lease_healthy; },
      options.heartbeat * 10, "SF-B2 status did not recover");
    sf_client::wait_peers (
      options.consumer_url,
      [&options] (const auto &peers) {
          return std::any_of (peers.begin (), peers.end (), [&options] (const auto &peer) {
              return peer.rid == "api-b"
                     && peer.endpoint == options.replacement_provider_endpoint;
          });
      },
      options.lease_ttl + options.polling * 12,
      "SF-B2 replacement provider row did not appear after recovery");
    bool replacement_used = false;
    const auto deadline = std::chrono::steady_clock::now () + options.heartbeat * 8;
    for (int index = 0; std::chrono::steady_clock::now () < deadline; ++index) {
        const auto reply = sf_client::request_profile (
          options.consumer_url, sf_client::unique_marker ("sf-b2-recovered-" + std::to_string (index)));
        if (reply.provider_rid == "api-b") {
            replacement_used = true;
            break;
        }
    }
    sf_client::ensure (replacement_used, "SF-B2 replacement provider was not used after recovery");
    std::cout << "scenario SF-B2 passed\n";
}

void crash_lease_expiry (const options_t &options)
{
    sf_client::post_empty (options.provider_b_url, "/admin/crash");
    sf_client::wait_down (options.provider_b_url);
    sf_client::wait_peers (
      options.consumer_url,
      [] (const auto &peers) { return !sf_client::has_rid (peers, "api-b"); },
      stale_peer_timeout (options), "SF-C1 api-b row did not expire");
    std::this_thread::sleep_for (options.polling * 4);
    for (int i = 0; i < 8; ++i) {
        auto reply = sf_client::request_profile (options.consumer_url, "sf-c1-" + std::to_string (i));
        sf_client::ensure (reply.provider_rid == "api-a", "SF-C1 routed to dead provider");
    }
    std::cout << "scenario SF-C1 passed\n";
}

void graceful_removal (const options_t &options)
{
    const auto propagation_bound =
      options.polling + std::chrono::seconds (5) + std::chrono::milliseconds (100);
    auto drain = std::async (std::launch::async, [&] {
        return sf_client::post_json<sf::operation_status_t, sf::operation_status_t> (
          options.provider_b_url, "/drain", {}, std::chrono::seconds (35));
    });
    sf_client::wait_peers (
      options.consumer_url,
      [] (const auto &peers) {
          return std::any_of (peers.begin (), peers.end (), [] (const auto &peer) {
              return peer.rid == "api-b" && peer.draining;
          });
      },
      options.polling + options.heartbeat,
      "SF-C2 api-b did not publish draining=true");
    const auto draining_status = sf_client::get_status (options.provider_b_url);
    sf_client::ensure (draining_status.owner_lease_healthy,
                       "SF-C2 api-b owner lease was unhealthy during drain");

    int consecutive_survivor_replies = 0;
    const auto propagation_deadline = std::chrono::steady_clock::now () + propagation_bound;
    for (int probe = 0; std::chrono::steady_clock::now () < propagation_deadline
                        && consecutive_survivor_replies < 20;
         ++probe) {
        const auto reply = sf_client::request_profile (
          options.consumer_url, "sf-c2-propagation-" + std::to_string (probe));
        consecutive_survivor_replies =
          reply.provider_rid == "api-a" ? consecutive_survivor_replies + 1 : 0;
    }
    sf_client::ensure (consecutive_survivor_replies == 20,
                       "SF-C2 draining provider remained eligible for new requests");

    const auto drain_result = drain.get ();
    sf_client::ensure (drain_result.status == "drained",
                       "SF-C2 drain did not complete as drained");
    sf_client::wait_down (options.provider_b_url);
    const auto removal_started = std::chrono::steady_clock::now ();
    sf_client::wait_peers (
      options.consumer_url,
      [] (const auto &peers) { return !sf_client::has_rid (peers, "api-b"); }, options.lease_ttl,
      "SF-C2 api-b row did not disappear on shutdown");
    sf_client::ensure (std::chrono::steady_clock::now () - removal_started < options.lease_ttl,
                       "SF-C2 row removal did not beat the lease TTL");
    for (int i = 0; i < 6; ++i) {
        auto reply = sf_client::request_profile (options.consumer_url, "sf-c2-" + std::to_string (i));
        sf_client::ensure (reply.provider_rid == "api-a", "SF-C2 routed to stopped provider");
    }
    std::cout << "scenario SF-C2 passed\n";
}

struct tolerant_traffic_t
{
    std::vector<sf::profile_res_t> replies;
    std::chrono::milliseconds max_success_gap{0};
};

tolerant_traffic_t drive_tolerant_requests (const options_t &options,
                                             std::chrono::milliseconds window)
{
    tolerant_traffic_t result;
    const auto deadline = std::chrono::steady_clock::now () + window;
    auto last_success = std::chrono::steady_clock::now ();
    for (int index = 0; std::chrono::steady_clock::now () < deadline; ++index) {
        try {
            auto reply = sf_client::request_profile (
              options.consumer_url, sf_client::unique_marker ("sf-d2-" + std::to_string (index)));
            const auto now = std::chrono::steady_clock::now ();
            result.max_success_gap = std::max (
              result.max_success_gap,
              std::chrono::duration_cast<std::chrono::milliseconds> (now - last_success));
            last_success = now;
            result.replies.push_back (std::move (reply));
        }
        catch (...) {
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (150));
    }
    result.max_success_gap = std::max (
      result.max_success_gap, std::chrono::duration_cast<std::chrono::milliseconds> (
                                std::chrono::steady_clock::now () - last_success));
    sf_client::ensure (!result.replies.empty (), "SF-D2 produced no successful traffic");
    sf_client::ensure (result.max_success_gap < options.lease_ttl * 2,
                       "SF-D2 max_success_gap exceeded dead-transport tolerance");
    return result;
}

void warm_provider_connections (const options_t &options, const std::string &scenario)
{
    std::set<std::string> served;
    for (int index = 0; index < 20 && served.size () < 2; ++index) {
        const auto reply = sf_client::request_profile (
          options.consumer_url, sf_client::unique_marker (scenario + "-warm-" + std::to_string (index)));
        served.insert (reply.provider_rid);
    }
    sf_client::ensure (served.contains ("api-a") && served.contains ("api-b"),
                       scenario + " did not warm both provider connections");
}

void short_recovery (const options_t &options)
{
    warm_provider_connections (options, "SF-D1");
    sf_client::wait_connected (options.consumer_url, options.provider_a_endpoint);
    sf_client::wait_connected (options.consumer_url, options.provider_b_endpoint);
    const auto before = sf_client::connection_evidence (options.consumer_url);
    auto traffic = std::async (std::launch::async, [&options] {
        return sf_client::drive_requests (options.consumer_url, "sf-d1", options.lease_ttl * 2,
                                          "SF-D1");
    });
    sf_client::stop_store ();
    std::this_thread::sleep_for (options.lease_ttl / 2);
    sf_client::restart_store ();
    (void) traffic.get ();
    sf_client::wait_status (
      options.consumer_url,
      [] (const auto &status) { return status.store_healthy && status.owner_lease_healthy; },
      options.heartbeat * 10, "SF-D1 status did not recover");
    const auto after = sf_client::connection_evidence (options.consumer_url);
    for (const auto &endpoint : {options.provider_a_endpoint, options.provider_b_endpoint}) {
        std::cerr << "SF-D1 connection endpoint=" << endpoint
                  << " connected-before="
                  << sf_client::connection_event_count (before, "Connected", endpoint)
                  << " connected-after="
                  << sf_client::connection_event_count (after, "Connected", endpoint)
                  << " disconnected-before="
                  << sf_client::connection_event_count (before, "Disconnected", endpoint)
                  << " disconnected-after="
                  << sf_client::connection_event_count (after, "Disconnected", endpoint) << '\n';
        sf_client::ensure (
          sf_client::connection_event_count (after, "Disconnected", endpoint)
            == sf_client::connection_event_count (before, "Disconnected", endpoint)
            && sf_client::connection_event_count (after, "Connected", endpoint)
                 == sf_client::connection_event_count (before, "Connected", endpoint),
          "SF-D1 survivor connection changed");
    }
    std::cout << "scenario SF-D1 passed\n";
}

void long_recovery (const options_t &options)
{
    warm_provider_connections (options, "SF-D2");
    sf_client::wait_connected (options.consumer_url, options.provider_a_endpoint);
    sf_client::wait_connected (options.consumer_url, options.provider_b_endpoint);
    const auto before = sf_client::connection_evidence (options.consumer_url);
    auto traffic = std::async (std::launch::async, [&options] {
        return drive_tolerant_requests (
          options, options.lease_ttl * 2 + options.heartbeat * 4);
    });
    sf_client::stop_store ();
    sf_client::post_empty (options.provider_b_url, "/admin/crash");
    sf_client::wait_down (options.provider_b_url);
    std::this_thread::sleep_for (options.lease_ttl + options.heartbeat);
    sf_client::restart_store ();
    const auto traffic_result = traffic.get ();
    sf_client::ensure (
      std::any_of (traffic_result.replies.begin (), traffic_result.replies.end (),
                   [] (const auto &reply) { return reply.provider_rid == "api-a"; }),
      "SF-D2 survivor served no outage traffic");

    sf_client::wait_peers (
      options.consumer_url,
      [] (const auto &peers) { return sf_client::has_rid (peers, "api-a"); },
      options.heartbeat * 8, "SF-D2 surviving provider did not re-register");
    sf_client::wait_peers (
      options.consumer_url,
      [] (const auto &peers) { return !sf_client::has_rid (peers, "api-b"); },
      stale_peer_timeout (options), "SF-D2 dead provider did not disappear");
    for (int i = 0; i < 8; ++i) {
        auto reply =
          sf_client::request_profile (options.consumer_url, "sf-d2-after-" + std::to_string (i));
        sf_client::ensure (reply.provider_rid == "api-a", "SF-D2 routed to dead provider");
    }
    const auto after = sf_client::connection_evidence (options.consumer_url);
    std::cerr << "SF-D2 survivor endpoint=" << options.provider_a_endpoint
              << " connected-before="
              << sf_client::connection_event_count (before, "Connected",
                                                     options.provider_a_endpoint)
              << " connected-after="
              << sf_client::connection_event_count (after, "Connected",
                                                     options.provider_a_endpoint)
              << " disconnected-before="
              << sf_client::connection_event_count (before, "Disconnected",
                                                     options.provider_a_endpoint)
              << " disconnected-after="
              << sf_client::connection_event_count (after, "Disconnected",
                                                     options.provider_a_endpoint)
              << '\n';
    sf_client::ensure (
      sf_client::connection_event_count (after, "Disconnected", options.provider_a_endpoint)
          == sf_client::connection_event_count (before, "Disconnected",
                                                 options.provider_a_endpoint)
        && sf_client::connection_event_count (after, "Connected", options.provider_a_endpoint)
             == sf_client::connection_event_count (before, "Connected",
                                                    options.provider_a_endpoint),
      "SF-D2 survivor connection changed");
    sf_client::ensure (
      sf_client::connection_event_count (after, "Disconnected", options.provider_b_endpoint)
        > sf_client::connection_event_count (before, "Disconnected",
                                             options.provider_b_endpoint),
      "SF-D2 dead provider disconnect was not observed");
    std::cout << "scenario SF-D2 passed\n";
}

void status_transition (const options_t &options)
{
    const auto initial = sf_client::wait_status (
      options.consumer_url,
      [] (const auto &status) {
          return status.store_healthy && status.owner_lease_healthy
                 && status.owner_lease_renewed_at_unix_ms > 0
                 && status.last_refresh_at_unix_ms > 0;
      },
      options.heartbeat * 8, "SF-D3 initial status was not healthy");
    sf_client::stop_store ();
    sf::runtime_status_res_t outage;
    try {
        outage = sf_client::wait_status (
          options.consumer_url,
          [] (const auto &status) {
              return !status.store_healthy && !status.owner_lease_healthy
                     && !status.last_error.empty ();
          },
          options.heartbeat * 10, "SF-D3 outage status was not visible");
    }
    catch (...) {
        sf_client::restart_store ();
        throw;
    }
    sf_client::ensure (
      outage.owner_lease_renewed_at_unix_ms >= initial.owner_lease_renewed_at_unix_ms
        && outage.last_refresh_at_unix_ms >= initial.last_refresh_at_unix_ms,
      "SF-D3 outage discarded the last successful runtime timestamps");
    sf_client::restart_store ();
    const auto recovered = sf_client::wait_status (
      options.consumer_url,
      [&outage] (const auto &status) {
          return status.store_healthy && status.owner_lease_healthy && status.last_error.empty ()
                 && status.last_refresh_at_unix_ms > outage.last_refresh_at_unix_ms
                 && status.owner_lease_renewed_at_unix_ms
                      > outage.owner_lease_renewed_at_unix_ms;
      },
      options.heartbeat * 10, "SF-D3 status did not recover");
    sf_client::ensure (
      recovered.last_refresh_at_unix_ms > outage.last_refresh_at_unix_ms,
      "SF-D3 recovery did not advance last refresh time");
    sf_client::ensure (
      recovered.owner_lease_renewed_at_unix_ms > outage.owner_lease_renewed_at_unix_ms,
      "SF-D3 recovery did not advance owner lease renewal time");
    std::cout << "scenario SF-D3 passed\n";
}

std::vector<double> measure_requests (const std::string &consumer_url,
                                      const std::string &marker_prefix,
                                      int count)
{
    std::vector<double> timings;
    timings.reserve (static_cast<std::size_t> (count));
    for (int i = 0; i < count; ++i) {
        const auto started = std::chrono::steady_clock::now ();
        auto reply = sf_client::request_profile (
          consumer_url, marker_prefix + "-" + std::to_string (i), std::chrono::seconds (3));
        const auto elapsed = std::chrono::steady_clock::now () - started;
        sf_client::ensure (reply.value == "profile:fast",
                           "SF-E1 request returned unexpected value");
        timings.push_back (
          std::chrono::duration<double, std::milli> (elapsed).count ());
    }
    return timings;
}

double percentile_ms (std::vector<double> values, double percentile)
{
    std::sort (values.begin (), values.end ());
    const auto index = static_cast<std::size_t> (
      std::max (0.0, std::ceil (percentile * static_cast<double> (values.size ())) - 1.0));
    return values[std::min (index, values.size () - 1)];
}

double measure_peer_query_ms (const options_t &options)
{
    const auto started = std::chrono::steady_clock::now ();
    auto peers = sf_client::wait_peers (
      options.consumer_url,
      [] (const auto &rows) {
          return sf_client::has_rid (rows, "api-a") && sf_client::has_rid (rows, "api-b");
      },
      std::chrono::seconds (10), "SF-E1 delayed peer query did not return both providers");
    const auto elapsed = std::chrono::steady_clock::now () - started;
    sf_client::ensure (peers.size () >= 2, "SF-E1 delayed peer query returned too few rows");
    return std::chrono::duration<double, std::milli> (elapsed).count ();
}

void store_delay_non_blocking (const options_t &options)
{
    constexpr int delay_ms = 1200;
    sf_client::wait_peers (
      options.consumer_url,
      [] (const auto &peers) {
          return sf_client::has_rid (peers, "api-a") && sf_client::has_rid (peers, "api-b");
      },
      options.lease_ttl + options.heartbeat * 4, "SF-E1 baseline peers were not ready");

    const auto baseline = measure_requests (options.consumer_url, "SF-E1-baseline", 10);
    const auto baseline_p99 = percentile_ms (baseline, 0.99);

    sf_client::set_store_delay (options.consumer_url, delay_ms);
    try {
        auto delayed_store_read = std::async (std::launch::async, [&options] {
            return measure_peer_query_ms (options);
        });
        std::this_thread::sleep_for (std::chrono::milliseconds (150));
        const auto concurrent = measure_requests (options.consumer_url, "SF-E1-concurrent", 12);
        const auto delayed_store_read_ms = delayed_store_read.get ();
        const auto concurrent_p99 = percentile_ms (concurrent, 0.99);
        const auto budget = std::max (baseline_p99 * 8.0, 750.0);

        sf_client::ensure (delayed_store_read_ms >= static_cast<double> (delay_ms) * 0.75,
                           "SF-E1 delayed store read finished too quickly");
        sf_client::ensure (concurrent_p99 <= budget,
                           "SF-E1 unrelated request p99 grew too much during store delay");

        auto recovery = sf_client::request_profile (options.consumer_url, "SF-E1-recovery");
        sf_client::ensure (recovery.value == "profile:fast",
                           "SF-E1 request path did not recover after delayed store read");
        sf_client::set_store_delay (options.consumer_url, 0);
    }
    catch (...) {
        sf_client::set_store_delay (options.consumer_url, 0);
        throw;
    }

    std::cout << "scenario SF-E1 passed\n";
}

} // namespace

int main ()
{
    const options_t options;
    if (options.scenario == "SF-A1") {
        baseline (options);
    } else if (options.scenario == "SF-A2") {
        polling_fallback (options);
    } else if (options.scenario == "SF-B1") {
        fail_static (options);
    } else if (options.scenario == "SF-B2") {
        grace_exceeded (options);
    } else if (options.scenario == "SF-C1") {
        crash_lease_expiry (options);
    } else if (options.scenario == "SF-C2") {
        graceful_removal (options);
    } else if (options.scenario == "SF-D1") {
        short_recovery (options);
    } else if (options.scenario == "SF-D2") {
        long_recovery (options);
    } else if (options.scenario == "SF-D3") {
        status_transition (options);
    } else if (options.scenario == "SF-E1") {
        store_delay_non_blocking (options);
    } else {
        throw std::runtime_error ("Unsupported StoreFailure scenario: " + options.scenario);
    }
    std::cout << "store-failure client scenario=" << options.scenario << " result=passed\n";
    return 0;
}
