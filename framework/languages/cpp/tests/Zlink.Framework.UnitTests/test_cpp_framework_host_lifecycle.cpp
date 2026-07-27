/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <stop_token>
#include <string_view>
#include <thread>

namespace
{

class blocking_stop_service_t final :
    public zlink::framework::hosted_service_t
{
  public:
    void start (zlink::framework::service_provider_t &) override
    {
        {
            std::lock_guard lock (_mutex);
            _started = true;
        }
        _changed.notify_all ();
    }

    void stop () noexcept override
    {
        std::unique_lock lock (_mutex);
        _stop_entered = true;
        _changed.notify_all ();
        _changed.wait (lock, [&] { return _release; });
    }

    void wait_started ()
    {
        std::unique_lock lock (_mutex);
        _changed.wait (lock, [&] { return _started; });
    }

    void wait_stop_entered ()
    {
        std::unique_lock lock (_mutex);
        _changed.wait (lock, [&] { return _stop_entered; });
    }

    void release ()
    {
        {
            std::lock_guard lock (_mutex);
            _release = true;
        }
        _changed.notify_all ();
    }

  private:
    std::mutex _mutex;
    std::condition_variable _changed;
    bool _started = false;
    bool _stop_entered = false;
    bool _release = false;
};

bool verify_relocation_blocker (
  std::string_view label,
  std::function<void (zlink::framework::zlink_framework_options_t &)> configure,
  zlink::framework::relocation_reason_t expected)
{
    auto app = zlink::framework::app_t::create ();
    app.add_zlink_framework (std::move (configure));
    auto service = std::make_unique<blocking_stop_service_t> ();
    auto *service_view = service.get ();
    app.add_hosted_service (std::move (service));

    char program[] = "termination-topology-preflight";
    char *arguments[] = {program, nullptr};
    int exit_code = -1;
    std::thread run_thread ([&] { exit_code = app.run (1, arguments); });
    service_view->wait_started ();
    const auto serving_deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (2);
    while (!app.is_ready ()
           && std::chrono::steady_clock::now () < serving_deadline)
        std::this_thread::yield ();

    const auto result =
      app.relocate (
           {.mode =
              zlink::framework::relocation_mode_t::planned_maintenance,
            .deadline = std::chrono::seconds (1)})
        .result ()
        .value ();
    const bool matched =
      result.outcome == zlink::framework::relocation_outcome_t::blocked
      && result.reason == expected
      && app.is_ready ();
    if (!matched)
        std::cerr << label
                  << " must block Relocate without changing Serving\n";

    auto shutdown = app.shutdown (std::chrono::seconds (2));
    service_view->wait_stop_entered ();
    service_view->release ();
    const auto stopped = shutdown.result ().value ();
    run_thread.join ();
    return matched
           && stopped.outcome
                == zlink::framework::termination_outcome_t::stopped
           && exit_code == 0;
}

} // namespace

int main ()
{
    if (!verify_relocation_blocker (
          "manual ClientServer topology",
          [] (zlink::framework::zlink_framework_options_t &options) {
              options.add_client_server_channel ("manual-orders")
                .client ()
                .connect ("tcp://127.0.0.1:29999");
          },
          zlink::framework::relocation_reason_t::manual_topology_unsupported)) {
        return EXIT_FAILURE;
    }
    if (!verify_relocation_blocker (
          "manual RouteMesh topology",
          [] (zlink::framework::zlink_framework_options_t &options) {
              auto node = options.add_route_mesh ("retire-manual-mesh");
              node.channel_name ("retire-manual-channel");
              node.set_routing_id (
                    zlink::routing_id_t::from ("retire-manual-node"))
                .listen ("inproc://cpp-retire-manual-node");
              node.peer_connections ().connect (
                "tcp://127.0.0.1:29998");
          },
          zlink::framework::relocation_reason_t::manual_topology_unsupported)) {
        return EXIT_FAILURE;
    }
    if (!verify_relocation_blocker (
          "automatic RouteMesh without a replacement",
          [] (zlink::framework::zlink_framework_options_t &options) {
              auto node = options.add_route_mesh ("retire-single-mesh");
              node.channel_name ("retire-single-channel");
              node.set_routing_id (
                    zlink::routing_id_t::from ("retire-single-node"))
                .listen ("inproc://cpp-retire-single-node");
          },
          zlink::framework::relocation_reason_t::target_unavailable)) {
        return EXIT_FAILURE;
    }

    auto app = zlink::framework::app_t::create ();
    if (app.runtime_state ()
        != zlink::framework::framework_runtime_state_t::preparing) {
        std::cerr << "new app must begin in Preparing\n";
        return EXIT_FAILURE;
    }

    bool planned_target_rejected = false;
    try {
        (void) app.relocate (
          {.mode =
             zlink::framework::relocation_mode_t::planned_maintenance,
           .target_application_version = 2});
    }
    catch (const std::invalid_argument &) {
        planned_target_rejected = true;
    }
    bool rolling_target_required = false;
    try {
        (void) app.relocate (
          {.mode = zlink::framework::relocation_mode_t::rolling_update});
    }
    catch (const std::invalid_argument &) {
        rolling_target_required = true;
    }
    if (!planned_target_rejected || !rolling_target_required) {
        std::cerr << "Relocate mode must validate its target version option\n";
        return EXIT_FAILURE;
    }

    const auto relocation =
      app.relocate (
           {.mode =
              zlink::framework::relocation_mode_t::planned_maintenance})
        .result ()
        .value ();
    if (relocation.outcome
          != zlink::framework::relocation_outcome_t::blocked
        || relocation.reason
             != zlink::framework::relocation_reason_t::runtime_not_ready) {
        std::cerr
          << "Relocate before Serving must return Blocked/RuntimeNotReady\n";
        return EXIT_FAILURE;
    }

    const auto shutdown =
      app.shutdown (std::chrono::seconds (1)).result ().value ();
    if (shutdown.outcome
             != zlink::framework::termination_outcome_t::stopped
        || shutdown.reason
             != zlink::framework::termination_reason_t::none
        || app.runtime_state ()
             != zlink::framework::framework_runtime_state_t::stopped) {
        std::cerr << "Shutdown must complete the shared termination operation\n";
        return EXIT_FAILURE;
    }
    const auto after_shutdown =
      app.relocate (
           {.mode =
              zlink::framework::relocation_mode_t::planned_maintenance})
        .result ()
        .value ();
    if (after_shutdown.outcome
          != zlink::framework::relocation_outcome_t::blocked
        || after_shutdown.reason
             != zlink::framework::relocation_reason_t::runtime_not_ready) {
        std::cerr << "Relocate after Shutdown must report RuntimeNotReady\n";
        return EXIT_FAILURE;
    }

    auto running = zlink::framework::app_t::create ();
    auto service = std::make_unique<blocking_stop_service_t> ();
    auto *service_view = service.get ();
    running.add_hosted_service (std::move (service));
    char program[] = "termination-facade";
    char *arguments[] = {program, nullptr};
    int exit_code = -1;
    std::thread run_thread (
      [&] { exit_code = running.run (1, arguments); });
    service_view->wait_started ();
    const auto serving_deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (1);
    while (!running.is_ready ()
           && std::chrono::steady_clock::now () < serving_deadline)
        std::this_thread::yield ();

    const auto unavailable_relocation =
      running
        .relocate (
          {.mode =
             zlink::framework::relocation_mode_t::planned_maintenance,
           .deadline = std::chrono::seconds (2)})
        .result ()
        .value ();
    if (unavailable_relocation.outcome
          != zlink::framework::relocation_outcome_t::blocked
        || unavailable_relocation.reason
             != zlink::framework::relocation_reason_t::target_unavailable
        || !running.is_ready ()) {
        std::cerr << "Relocation preflight blocker must preserve Serving\n";
        running.stop ();
        service_view->release ();
        run_thread.join ();
        return EXIT_FAILURE;
    }

    auto shared_shutdown = running.shutdown (std::chrono::seconds (2));
    service_view->wait_stop_entered ();
    if (shared_shutdown.await_ready ()) {
        std::cerr << "Shutdown must wait for hosted-service teardown\n";
        service_view->release ();
        run_thread.join ();
        return EXIT_FAILURE;
    }

    std::stop_source cancelled_source;
    cancelled_source.request_stop ();
    auto cancelled_waiter =
      running.shutdown (
        std::chrono::seconds (2), cancelled_source.get_token ());
    const auto &cancelled = cancelled_waiter.result ();
    if (cancelled
        || !cancelled.error ()
        || cancelled.error ()->code ()
             != std::make_error_code (std::errc::operation_canceled)) {
        std::cerr << "wait cancellation must cancel only the joining waiter\n";
        service_view->release ();
        run_thread.join ();
        return EXIT_FAILURE;
    }

    service_view->release ();
    const auto shared_result = shared_shutdown.result ().value ();
    run_thread.join ();
    if (shared_result.outcome
          != zlink::framework::termination_outcome_t::stopped
        || exit_code != 0) {
        std::cerr << "shared Shutdown must survive waiter cancellation\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
