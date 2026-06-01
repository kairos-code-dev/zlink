/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

struct stage_spot_t
{
};

struct stage_packet_t
{
};

struct stage_state_t
{
  int value = 0;
};

class stage_wrapper_t
{
public:
  stage_wrapper_t (zlink::framework::node_rid_t node,
                   zlink::framework::spot_rid_t spot,
                   zlink::framework::publisher_t publisher,
                   std::size_t packet_count,
                   zlink::framework::timer_options_t timer_options)
    : node_rid (std::move (node)),
      spot_rid (std::move (spot)),
      outbound (std::move (publisher)),
      packet_count (packet_count),
      timer_options (timer_options)
  {
  }

  void apply (int delta) { state.value += delta; }

  stage_state_t state;
  zlink::framework::node_rid_t node_rid;
  zlink::framework::spot_rid_t spot_rid;
  zlink::framework::publisher_t outbound;
  std::size_t packet_count = 0;
  zlink::framework::timer_options_t timer_options;
};

class game_module_t final : public zlink::framework::module_t
{
public:
  void configure_services (
    zlink::framework::service_collection_t &services) override
  {
    services.add_singleton<stage_state_t> ();
    services_configured = true;
  }

  void configure_zlink (zlink::framework::zlink_builder_t &zlink) override
  {
    zlink.node ("module-node")
      .channel ("stage.events",
                [](zlink::framework::channel_builder_t &channel) {
        channel.enable_publisher (
          [](zlink::framework::capability_builder_t &publisher) {
            publisher.bind ("tcp://127.0.0.1:9101");
          });
      });

    zlink::framework::spot_node_builder_t spot_builder;
    zlink.spot_node (
      "stage-node",
      [&spot_builder](zlink::framework::spot_node_builder_t &spot_node) {
        spot_node.add_spot<stage_spot_t> ("stage");
        spot_builder = spot_node;
      });
    auto context = spot_builder.create_spot ("stage");
    context.register_packet<stage_packet_t> ("stage.packet");

    zlink::framework::timer_options_t timer_options;
    timer_options.overrun_policy =
      zlink::framework::timer_overrun_policy_t::catch_up_bounded;
    timer_options.max_catch_up_ticks = 2;
    wrapper.emplace (context.node_rid (),
                     context.spot_rid (),
                     zlink.publisher (),
                     context.packet_registry ().size (),
                     timer_options);
    wrapper->apply (3);
    zlink_configured = true;
  }

  void configure_handlers (
    zlink::framework::handler_registry_t &handlers) override
  {
    (void) handlers;
    handlers_configured = true;
  }

  void configure_monitoring (
    zlink::framework::monitoring_builder_t &monitoring) override
  {
    monitoring.add_spot_events ("stage-node", std::chrono::seconds (1));
    monitoring_configured = true;
  }

  bool services_configured = false;
  bool zlink_configured = false;
  bool handlers_configured = false;
  bool monitoring_configured = false;
  std::optional<stage_wrapper_t> wrapper;
};

class recording_hosted_service_t final
  : public zlink::framework::hosted_service_t
{
public:
  recording_hosted_service_t (std::string name, std::vector<std::string> &events)
    : _name (std::move (name)), _events (events)
  {
  }

  void start (zlink::framework::service_provider_t &services) override
  {
    auto &state = services.get_required<stage_state_t> ();
    state.value += 1;
    _events.push_back ("start:" + _name);
  }

  void stop () noexcept override
  {
    _events.push_back ("stop:" + _name);
  }

private:
  std::string _name;
  std::vector<std::string> &_events;
};

} // namespace

int
main ()
{
  zlink::framework::app_t app = zlink::framework::app_t::create ();
  game_module_t module;
  std::vector<std::string> lifecycle;

  app.add_module (module)
    .add_hosted_service (
      std::make_unique<recording_hosted_service_t> ("first", lifecycle))
    .add_hosted_service (
      std::make_unique<recording_hosted_service_t> ("second", lifecycle));

  if (!module.services_configured || !module.zlink_configured ||
      !module.handlers_configured || !module.monitoring_configured ||
      !module.wrapper) {
    return 1;
  }
  if (module.wrapper->packet_count != 1 || module.wrapper->state.value != 3 ||
      module.wrapper->node_rid.empty () || module.wrapper->spot_rid.empty () ||
      module.wrapper->timer_options.overrun_policy !=
        zlink::framework::timer_overrun_policy_t::catch_up_bounded) {
    return 2;
  }

  int argc = 1;
  char program[] = "module-test";
  char *argv[] = { program, nullptr };
  if (app.run (argc, argv) != 0) {
    return 3;
  }
  const std::vector<std::string> expected {
    "start:first",
    "start:second",
    "stop:second",
    "stop:first"
  };
  if (lifecycle != expected) {
    return 4;
  }

  bool null_hosted_service_failed = false;
  try {
    zlink::framework::app_t invalid = zlink::framework::app_t::create ();
    invalid.add_hosted_service (nullptr);
  } catch (const zlink::framework::framework_exception_t &error) {
    null_hosted_service_failed =
      error.kind () ==
      zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!null_hosted_service_failed) {
    return 5;
  }

  return 0;
}
