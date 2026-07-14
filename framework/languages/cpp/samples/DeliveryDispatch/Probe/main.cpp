/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Server/Configuration/location_store.hpp"
#include "../Server/Configuration/sample_names.hpp"
#include "../Server/Configuration/sample_configuration.hpp"
#include "../Server/common_codecs.hpp"

#include <zlink/framework.hpp>

#include <iostream>

namespace
{

class probe_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit probe_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &) override
    {
        passed = true;
        std::cout << "topology=ready\n";
        std::cout << "deliverydispatch-probe=completed\n";
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

    auto app = app_t::create ();
    const auto configuration = load_sample_configuration (app, argc, argv);
    const auto &topology = configuration.topology;
    auto probe = std::make_unique<probe_service_t> (app);
    auto *probe_result = probe.get ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (configuration.flow_log_path ())
          .trace_label ("deliverydispatch-probe");
        add_deliverydispatch_json_codecs (options.codecs ());
        add_deliverydispatch_location_store (options, topology);
        options.add_client_server_channel (sample_names_t::tracking_route_channel).enable_client ();
    });
    app.add_hosted_service (std::move (probe));
    const auto exit_code = app.run (argc, argv);
    return exit_code == 0 && probe_result->passed ? 0 : 1;
}
