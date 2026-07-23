/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stop_token>
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

} // namespace

int main ()
{
    auto app = zlink::framework::app_t::create ();
    if (app.runtime_state ()
        != zlink::framework::framework_runtime_state_t::preparing) {
        std::cerr << "new app must begin in Preparing\n";
        return EXIT_FAILURE;
    }

    const auto retire = app.retire ().result ().value ();
    if (retire
        != zlink::framework::termination_result_t{
          zlink::framework::termination_intent_t::retire,
          zlink::framework::termination_outcome_t::blocked,
          zlink::framework::termination_reason_t::runtime_not_ready}) {
        std::cerr << "Retire before Serving must return Blocked/RuntimeNotReady\n";
        return EXIT_FAILURE;
    }

    const auto shutdown =
      app.shutdown (std::chrono::seconds (1)).result ().value ();
    if (shutdown.effective_intent
          != zlink::framework::termination_intent_t::shutdown
        || shutdown.outcome
             != zlink::framework::termination_outcome_t::stopped
        || shutdown.reason
             != zlink::framework::termination_reason_t::none
        || app.runtime_state ()
             != zlink::framework::framework_runtime_state_t::stopped) {
        std::cerr << "Shutdown must complete the shared termination operation\n";
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

    const auto unavailable_retire =
      running.retire (std::chrono::seconds (2)).result ().value ();
    if (unavailable_retire.outcome
          != zlink::framework::termination_outcome_t::blocked
        || unavailable_retire.reason
             != zlink::framework::termination_reason_t::store_unavailable
        || !running.is_ready ()) {
        std::cerr << "Retire preflight blocker must preserve Serving\n";
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
