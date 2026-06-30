/* SPDX-License-Identifier: MPL-2.0 */

#include "../Server/Configuration/sample_names.hpp"
#include "../Server/Configuration/sample_topology.hpp"
#include "../Server/common_codecs.hpp"

#include <zlink/framework.hpp>

#include <iostream>

namespace
{

class probe_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit probe_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &services) override
    {
        auto &channels = services.get_required<zlink::framework::channel_client_t> ();
        auto result =
          channels
            .request (zlink::samples::deliverydispatch::sample_names_t::tracking_route_channel,
                      zlink::samples::deliverydispatch::ensure_customer_actor_t{"customer-1"})
            .async<zlink::samples::deliverydispatch::customer_actor_ensured_t> ()
            .result ();
        passed = static_cast<bool> (result);
        if (passed) {
            std::cout << "deliverydispatch-probe=completed\n";
        } else {
            std::cerr << "deliverydispatch-probe=failed\n";
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
        options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
        options.add_client_server_channel (sample_names_t::tracking_route_channel).enable_client ();
    });
    app.add_hosted_service (std::move (probe));
    const auto exit_code = app.run (argc, argv);
    return exit_code == 0 && probe_result->passed ? 0 : 1;
}
