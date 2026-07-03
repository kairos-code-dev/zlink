/* SPDX-License-Identifier: MPL-2.0 */

#include "../Server/Configuration/location_store.hpp"
#include "../Server/Configuration/sample_names.hpp"
#include "../Server/Configuration/sample_topology.hpp"
#include "../Server/common_codecs.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <iostream>
#include <thread>

namespace
{

class probe_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit probe_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &services) override
    {
        auto &channels = services.get_required<zlink::framework::channel_client_t> ();
        zlink::framework::result_t<
          zlink::samples::deliverydispatch::ensure_customer_actor_res_t>
          last_result = zlink::framework::result_t<
            zlink::samples::deliverydispatch::ensure_customer_actor_res_t>::failure (
            zlink::framework::framework_error_kind_t::timeout, "probe did not run");
        const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
        while (std::chrono::steady_clock::now () < deadline) {
            last_result =
              channels
                .request (
                  zlink::samples::deliverydispatch::sample_names_t::tracking_route_channel,
                  zlink::samples::deliverydispatch::ensure_customer_actor_req_t{"customer-1"})
                .timeout (std::chrono::milliseconds (1000))
                .async<zlink::samples::deliverydispatch::ensure_customer_actor_res_t> ()
                .result ();
            if (last_result) {
                passed = true;
                break;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        if (passed) {
            std::cout << "deliverydispatch-probe=completed\n";
        } else {
            const auto *error = last_result.error ();
            std::cerr << "deliverydispatch-probe=failed kind="
                      << static_cast<int> (last_result.error_kind ()) << " message="
                      << (error != nullptr ? error->what () : "") << "\n";
        }
        _app.stop ();
    }

    void stop () noexcept override {}

    bool passed{false};

  private:
    zlink::framework::app_t &_app;
};

} // namespace

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::deliverydispatch;

    const sample_topology_t topology;
    auto app = app_t::create ();
    auto probe = std::make_unique<probe_service_t> (app);
    auto *probe_result = probe.get ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (deliverydispatch_log_dir () + "/flow-probe.log")
          .trace_label ("deliverydispatch-probe");
        add_deliverydispatch_json_codecs (options.codecs ());
        add_deliverydispatch_location_store (options, topology);
        options.add_client_server_channel (sample_names_t::tracking_route_channel).enable_client ();
    });
    app.add_hosted_service (std::move (probe));
    const auto exit_code = app.run (argc, argv);
    return exit_code == 0 && probe_result->passed ? 0 : 1;
}
