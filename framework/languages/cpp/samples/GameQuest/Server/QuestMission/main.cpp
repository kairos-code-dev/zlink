/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/sample_names.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <iostream>

namespace zlink::samples::gamequest
{

using namespace framework;

class gameplay_event_handler_t
{
  public:
    using event_type = quest_event_msg_t;
    static constexpr const char *topic_name = sample_names_t::gameplay_topic;

    void handle (const quest_event_msg_t &event, const publish_context_t &)
    {
        std::cerr << "gamequest mission observed event=" << event.event_id << "\n";
    }
};

} // namespace zlink::samples::gamequest

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::gamequest;

    const sample_topology_t topology;
    const auto mission = gamequest_env_or ("GAMEQUEST_MISSION_NAME", "mission-a");
    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (gamequest_log_dir () + "/flow-" + mission + ".log")
          .trace_label ("gamequest-" + mission);
        add_gamequest_json_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
        options.add_fanout_channel (sample_names_t::fanout_channel)
          .enable_subscriber ()
          .use_handler_group ("gamequest-gameplay");
        options.handlers ().group ("gamequest-gameplay").add_publish<gameplay_event_handler_t> ();
        options.http ()
          .listen (mission == "mission-b" ? topology.mission_b_http : topology.mission_a_http)
          .map_health ("/health");
    });
    return app.run (argc, argv);
}
