/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "Support/client_support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <vector>

namespace sf_client = zlink::framework::e2e::store_failure::client;

namespace
{

struct options_t
{
    std::string scenario = sf_client::env_or ("ZLINK_CPP_SF_SCENARIO", "SF-A1");
    std::string consumer_url = sf_client::env_or ("ZLINK_CPP_SF_CONSUMER_URL");
    std::string provider_a_url = sf_client::env_or ("ZLINK_CPP_SF_PROVIDER_A_URL");
    std::string provider_b_url = sf_client::env_or ("ZLINK_CPP_SF_PROVIDER_B_URL");
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
                     && status.has_last_refresh_at;
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
          return sf_client::has_rid (peers, "api-a") && sf_client::has_rid (peers, "api-b");
      },
      options.lease_ttl + options.polling * 8, "SF-A2 providers did not appear through polling");
    sf_client::post_empty (options.provider_b_url, "/shutdown");
    sf_client::wait_down (options.provider_b_url);
    sf_client::wait_peers (
      options.consumer_url,
      [] (const auto &peers) { return !sf_client::has_rid (peers, "api-b"); },
      options.lease_ttl, "SF-A2 removed provider did not disappear through polling");
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
        sf_client::drive_requests (options.consumer_url, "sf-b2", options.grace + options.heartbeat * 2,
                                   "SF-B2");
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
    sf_client::post_empty (options.provider_b_url, "/shutdown");
    sf_client::wait_down (options.provider_b_url);
    sf_client::wait_peers (
      options.consumer_url,
      [] (const auto &peers) { return !sf_client::has_rid (peers, "api-b"); }, options.lease_ttl,
      "SF-C2 api-b row did not disappear on shutdown");
    for (int i = 0; i < 6; ++i) {
        auto reply = sf_client::request_profile (options.consumer_url, "sf-c2-" + std::to_string (i));
        sf_client::ensure (reply.provider_rid == "api-a", "SF-C2 routed to stopped provider");
    }
    std::cout << "scenario SF-C2 passed\n";
}

void short_recovery (const options_t &options)
{
    sf_client::stop_store ();
    std::this_thread::sleep_for (options.lease_ttl / 2);
    sf_client::restart_store ();
    sf_client::wait_status (
      options.consumer_url,
      [] (const auto &status) { return status.store_healthy && status.owner_lease_healthy; },
      options.heartbeat * 10, "SF-D1 status did not recover");
    sf_client::drive_requests (options.consumer_url, "sf-d1", options.lease_ttl, "SF-D1");
    std::cout << "scenario SF-D1 passed\n";
}

void long_recovery (const options_t &options)
{
    sf_client::stop_store ();
    sf_client::post_empty (options.provider_b_url, "/admin/crash");
    sf_client::wait_down (options.provider_b_url);
    std::this_thread::sleep_for (options.lease_ttl + options.heartbeat);
    sf_client::restart_store ();

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
    std::cout << "scenario SF-D2 passed\n";
}

void status_transition (const options_t &options)
{
    sf_client::wait_status (
      options.consumer_url,
      [] (const auto &status) { return status.store_healthy && status.owner_lease_healthy; },
      options.heartbeat * 8, "SF-D3 initial status was not healthy");
    sf_client::stop_store ();
    try {
        sf_client::wait_status (
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
    sf_client::restart_store ();
    sf_client::wait_status (
      options.consumer_url,
      [] (const auto &status) {
          return status.store_healthy && status.owner_lease_healthy && status.has_last_refresh_at;
      },
      options.heartbeat * 10, "SF-D3 status did not recover");
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
