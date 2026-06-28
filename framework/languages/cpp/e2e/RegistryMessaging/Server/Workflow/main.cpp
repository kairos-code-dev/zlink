/* SPDX-License-Identifier: MPL-2.0 */

#include "Configuration/workflow_options.hpp"
#include "Handlers/workflow_handlers.hpp"
#include "Infrastructure/scenario_state.hpp"

#include "../../Shared/registry_messaging_contracts.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace e2e = zlink::framework::e2e::registry_messaging;
namespace rm_workflow = zlink::framework::e2e::registry_messaging::workflow;

namespace
{

void configure_common_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ();
    codecs.add_json<e2e::profile_request_t,
                    e2e::profile_reply_t,
                    e2e::evidence_entry_t,
                    e2e::evidence_snapshot_t> ();
}

} // namespace

int main (int argc, char **argv)
{
    const auto options = rm_workflow::read_workflow_options ();
    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (options.log_dir + "/" + options.rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (options.log_dir + "/" + options.rid + "-flow.log")
          .trace_label ("cpp-rm-" + options.rid);
        framework.services ().add_singleton<rm_workflow::scenario_state_t> (
          std::make_unique<rm_workflow::scenario_state_t> (options.rid, options.instance_id));
        configure_common_codecs (framework.codecs ());
        framework.handlers ().add<rm_workflow::workflow_request_handler_t> (e2e::handler_group);
        framework.use_discovery ().add_registry_endpoint (options.registry_router);
        framework.add_client_server_channel (e2e::workflow_channel)
          .enable_server (options.workflow_endpoint)
          .set_routing_id (zlink::routing_id_t::from (options.rid))
          .use_handler_group (e2e::handler_group);
    });
    return app.run (argc, argv);
}
