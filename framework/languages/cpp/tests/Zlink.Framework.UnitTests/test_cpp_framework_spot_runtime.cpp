/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{

struct player_actor_factory_t
{
    int joined_value{};
    int moved_value{};
};

struct entry_spot_t : public zlink::framework::entry_spot_t
{
    void on_post_actor_joined (player_actor_factory_t &actor)
    {
        ++joined_count;
        actor.joined_value += 10;
    }

    void on_actor_left (player_actor_factory_t &actor)
    {
        ++left_count;
        actor.moved_value += 10;
    }

    int joined_count{};
    int left_count{};
};

struct state_update_t
{
    int value{};
};

struct move_request_t
{
    int value{};
};

struct move_reply_t
{
    int value{};
};

struct stage_spot_t : public zlink::framework::spot_t
{
    void on_state_update (const state_update_t &message)
    {
        last_value = message.value;
        packet_seen = message.value;
    }

    void on_throwing_state_update (const state_update_t &)
    {
        throw std::runtime_error ("spot failure");
    }

    zlink::framework::spot_create_response_t on_create (const zlink::message_t &request)
    {
        ++create_count;
        last_create_request = request.to_string ();
        if (reject_create) {
            return zlink::framework::spot_create_response_t::reject (
              zlink::message_t::from ("create-rejected"));
        }
        return zlink::framework::spot_create_response_t::accept (
          zlink::message_t::from ("create-accepted"));
    }

    void on_initialize () { ++initialize_count; }

    zlink::framework::spot_actor_join_response_t on_actor_join (player_actor_factory_t &actor,
                                                                const zlink::message_t &request)
    {
        join_seen = std::stoi (request.to_string ());
        actor.joined_value = join_seen;
        if (!accept_join) {
            return zlink::framework::spot_actor_join_response_t::reject (
              zlink::message_t::from ("rejected"));
        }
        return zlink::framework::spot_actor_join_response_t::accept (
          zlink::message_t::from (std::to_string (join_seen + 1)));
    }

    void on_move (player_actor_factory_t &actor,
                  const zlink::framework::spot_actor_send_context_t &context,
                  const move_request_t &request)
    {
        if (context.packet_name == "move") {
            packet_seen = request.value;
        }
        const auto trace = context.metadata.find ("trace-id");
        last_trace_id = trace ? std::string (*trace) : "";
        saw_tenant_id = context.metadata.contains ("tenant-id");
        actor.moved_value = request.value;
    }

    void on_post_actor_joined (player_actor_factory_t &actor)
    {
        ++joined_count;
        actor.joined_value += 100;
    }

    void on_actor_left (player_actor_factory_t &actor)
    {
        ++left_count;
        actor.moved_value += 100;
    }

    void on_closing ()
    {
        ++closing_count;
        ++global_closing_count;
    }

    void on_actor_disconnected (player_actor_factory_t &) { ++disconnected_count; }

    int join_seen{};
    int packet_seen{};
    int joined_count{};
    int left_count{};
    int closing_count{};
    int disconnected_count{};
    int last_value{};
    std::string last_trace_id;
    bool saw_tenant_id = false;
    bool accept_join = true;
    static inline int create_count = 0;
    static inline int initialize_count = 0;
    static inline int global_closing_count = 0;
    static inline bool reject_create = false;
    static inline std::string last_create_request;
};

struct stage_wrapper_t
{
    stage_wrapper_t (zlink::framework::node_rid_t node,
                     zlink::framework::spot_rid_t spot,
                     zlink::framework::publisher_t publisher,
                     std::size_t packets) :
        node_rid (std::move (node)),
        spot_rid (std::move (spot)),
        outbound (std::move (publisher)),
        packet_count (packets)
    {
    }

    void apply (int delta) { state += delta; }

    zlink::framework::node_rid_t node_rid;
    zlink::framework::spot_rid_t spot_rid;
    zlink::framework::publisher_t outbound;
    std::size_t packet_count{};
    int state{};
};

struct factory_spot_t : public zlink::framework::spot_t
{
    explicit factory_spot_t (std::string value) : value (std::move (value)) {}

    void configure (zlink::framework::spot_context_t &context)
    {
        configured_spot_rid = std::string (context.spot_rid ().value ());
    }

    zlink::framework::spot_create_response_t on_create (const zlink::message_t &request)
    {
        ++create_count;
        last_request = request.to_string ();
        return zlink::framework::spot_create_response_t::accept (zlink::message_t::from (value));
    }

    void on_initialize () { ++initialize_count; }

    void on_closing () { ++closing_count; }

    std::string value;
    static inline int create_count = 0;
    static inline int initialize_count = 0;
    static inline int closing_count = 0;
    static inline std::string configured_spot_rid;
    static inline std::string last_request;
};

} // namespace

int main ()
{
    using zlink::framework::framework_error_kind_t;

    zlink::framework::zlink_builder_t zlink;
    zlink.add_node ("stage-node")
      .channel (
        "game.stage",
        [] (zlink::framework::channel_builder_t &channel) {
            channel.enable_publisher ([] (zlink::framework::capability_builder_t &publisher) {
                publisher.bind ("tcp://127.0.0.1:8101");
            });
            channel.enable_subscriber ([] (zlink::framework::capability_builder_t &subscriber) {
                subscriber.use_discovery ();
            });
        })
      .add_spot_node ("stage-spot-node", [] (zlink::framework::spot_node_builder_t &spot_node) {
          spot_node.bind ("tcp://0.0.0.0:9000")
            .enable_router ("tcp://0.0.0.0:9002")
            .connect_router ("tcp://127.0.0.1:9003")
            .enable_pub_sub ("tcp://0.0.0.0:9004")
            .connect_pub_sub ("tcp://127.0.0.1:9005")
            .enable_actor_gateway ()
            .use_discovery ("game.stage")
            .attach_channel_client ("profile")
            .attach_publisher ("game.stage")
            .add_entry_spot<entry_spot_t> ()
            .add_actor_factory<player_actor_factory_t> ("player")
            .add_spot<stage_spot_t> ("stage");
      });

    const auto snapshots = zlink.spot_nodes ();
    if (snapshots.size () != 1 || snapshots[0].name != "stage-spot-node"
        || snapshots[0].bind_endpoint != "tcp://0.0.0.0:9000" || !snapshots[0].router_bind_endpoint
        || *snapshots[0].router_bind_endpoint != "tcp://0.0.0.0:9002"
        || snapshots[0].router_manual_connections.size () != 1
        || snapshots[0].router_manual_connections[0] != "tcp://127.0.0.1:9003"
        || !snapshots[0].pub_bind_endpoint
        || *snapshots[0].pub_bind_endpoint != "tcp://0.0.0.0:9004"
        || snapshots[0].pub_sub_manual_connections.size () != 1
        || snapshots[0].pub_sub_manual_connections[0] != "tcp://127.0.0.1:9005"
        || !snapshots[0].actor_gateway_enabled || !snapshots[0].discovery_channel_name
        || *snapshots[0].discovery_channel_name != "game.stage"
        || snapshots[0].attached_channel_clients.size () != 1
        || snapshots[0].attached_publishers.size () != 1
        || snapshots[0].attached_channel_client_details.size () != 1
        || snapshots[0].attached_channel_client_details[0].channel_name != "profile"
        || !snapshots[0].attached_channel_client_details[0].manual_connections.empty ()
        || snapshots[0].attached_publisher_details.size () != 1
        || snapshots[0].attached_publisher_details[0].channel_name != "game.stage"
        || !snapshots[0].attached_publisher_details[0].manual_connections.empty ()
        || snapshots[0].spot_names.size () != 2 || snapshots[0].entry_spot_name != "entry"
        || snapshots[0].actor_types.size () != 1) {
        return 1;
    }

    zlink::framework::spot_node_builder_t builder;
    zlink::framework::zlink_builder_t manual_host;
    manual_host.add_spot_node ("manual-stage",
                               [&builder] (zlink::framework::spot_node_builder_t &spot_node) {
                                   spot_node.bind ("tcp://0.0.0.0:9001")
                                     .use_discovery ("game.stage")
                                     .attach_publisher ("game.stage")
                                     .add_entry_spot<entry_spot_t> ()
                                     .add_actor_factory<player_actor_factory_t> ("player")
                                     .add_spot<stage_spot_t> ("stage")
                                     .add_spot<factory_spot_t> ("factory", [] {
                                         return std::make_shared<factory_spot_t> ("factory-reply");
                                     });
                                   builder = spot_node;
                               });

    const auto create_count_before = stage_spot_t::create_count;
    auto create_result = builder.create_spot ("stage");
    if (create_result.state != zlink::framework::spot_create_state_t::created
        || create_result.context.spot_rid ().empty ()) {
        return 46;
    }
    if (stage_spot_t::create_count != create_count_before + 1 || stage_spot_t::initialize_count < 1
        || stage_spot_t::last_create_request != "") {
        return 53;
    }
    auto context = create_result.context;
    if (context.node_rid ().empty () || context.spot_rid ().empty ()
        || context.spot_name () != "stage") {
        return 2;
    }

    const auto local_name = builder.spot_name_for (context.spot_rid ());
    if (!local_name || *local_name != "stage") {
        return 3;
    }

    const auto local_route = builder.resolve_spot (context.spot_rid ());
    if (!local_route || local_route->spot_name != "stage" || local_route->node_rid.empty ()) {
        return 4;
    }

    const auto remote_rid = zlink::framework::spot_rid_t::from_string ("remote-stage");
    builder.add_spot_resolver (
      "remote",
      [remote_rid] (
        zlink::framework::spot_rid_t rid) -> std::optional<zlink::framework::spot_route_t> {
          if (std::string (rid.value ()) != std::string (remote_rid.value ())) {
              return std::nullopt;
          }
          return zlink::framework::spot_route_t{
            zlink::framework::node_rid_t::from_string ("remote-node"), remote_rid, "remote-stage"};
      });
    const auto remote_route = builder.resolve_spot (remote_rid);
    if (!remote_route || remote_route->spot_name != "remote-stage") {
        return 5;
    }

    auto close_create = builder.create_spot ("stage");
    if (!builder.find_spot (close_create.spot_rid) || builder.list_spots ().empty ()) {
        return 49;
    }
    const auto closing_count_before = stage_spot_t::global_closing_count;
    const auto close_result = close_create.context.close ().result ();
    if (!close_result || !close_result.value () || builder.find_spot (close_create.spot_rid)
        || stage_spot_t::global_closing_count != closing_count_before + 1) {
        return 50;
    }
    const auto close_again = builder.close_spot (close_create.spot_rid).result ();
    if (!close_again || close_again.value ()) {
        return 51;
    }

    const auto requested_rid =
      zlink::framework::spot_rid_t::from_string ("manual-stage:stage:requested");
    const auto get_or_create_count_before = stage_spot_t::create_count;
    auto created_once = builder.get_or_create_spot ("stage", requested_rid,
                                                    zlink::message_t::from ("create-request"));
    auto existing_once =
      builder.get_or_create_spot ("stage", requested_rid, zlink::message_t::from ("ignored"));
    if (created_once.state != zlink::framework::spot_create_state_t::created
        || existing_once.state != zlink::framework::spot_create_state_t::existing
        || existing_once.spot_rid.value () != requested_rid.value ()
        || stage_spot_t::create_count != get_or_create_count_before + 1
        || stage_spot_t::last_create_request != "create-request") {
        return 52;
    }
    stage_spot_t::reject_create = true;
    auto rejected_create = builder.create_spot ("stage", zlink::message_t::from ("reject-request"));
    stage_spot_t::reject_create = false;
    if (rejected_create.state != zlink::framework::spot_create_state_t::rejected
        || !rejected_create.reply || rejected_create.reply->to_string () != "create-rejected"
        || builder.find_spot (rejected_create.spot_rid)) {
        return 54;
    }

    auto factory_created =
      builder.create_spot ("factory", zlink::message_t::from ("factory-request"));
    if (factory_created.state != zlink::framework::spot_create_state_t::created
        || !factory_created.reply || factory_created.reply->to_string () != "factory-reply"
        || factory_spot_t::create_count != 1 || factory_spot_t::initialize_count != 1
        || factory_spot_t::last_request != "factory-request"
        || factory_spot_t::configured_spot_rid != std::string (factory_created.spot_rid.value ())) {
        return 55;
    }
    if (!factory_created.context.close ().result ().value ()
        || factory_spot_t::closing_count != 1) {
        return 56;
    }
    bool empty_factory_failed = false;
    try {
        zlink::framework::spot_node_builder_t invalid;
        invalid.add_spot<factory_spot_t> ("factory", {});
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_factory_failed = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!empty_factory_failed) {
        return 57;
    }
    bool null_factory_failed = false;
    try {
        zlink::framework::spot_node_builder_t invalid;
        invalid.add_spot<factory_spot_t> ("factory",
                                          [] { return std::shared_ptr<factory_spot_t>{}; });
        (void) invalid.create_spot ("factory");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        null_factory_failed = error.kind () == framework_error_kind_t::spot_create_failed;
    }
    if (!null_factory_failed) {
        return 58;
    }

    auto lifecycle_entry_spot = std::make_shared<entry_spot_t> ();
    auto lifecycle_stage_spot = std::make_shared<stage_spot_t> ();
    zlink::framework::spot_node_builder_t lifecycle_builder;
    zlink::framework::zlink_builder_t lifecycle_host;
    lifecycle_host.add_spot_node (
      "lifecycle-stage", [&] (zlink::framework::spot_node_builder_t &spot_node) {
          spot_node
            .add_entry_spot<entry_spot_t> ([lifecycle_entry_spot] { return lifecycle_entry_spot; })
            .add_actor_factory<player_actor_factory_t> ("player")
            .add_spot<stage_spot_t> ("stage",
                                     [lifecycle_stage_spot] { return lifecycle_stage_spot; });
          lifecycle_builder = spot_node;
      });
    auto lifecycle_entry = lifecycle_builder.create_spot ("entry");
    auto lifecycle_stage = lifecycle_builder.create_spot ("stage");
    auto lifecycle_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (lifecycle_builder);
    zlink::framework::detail::actor_gateway_runtime_t lifecycle_gateway;
    auto lifecycle_actor = lifecycle_gateway.manager ().create ("player", "joined-player").value ();
    auto lifecycle_actor_context = lifecycle_actor.context ();
    player_actor_factory_t lifecycle_actor_state;
    player_actor_factory_t rejected_actor_state;
    lifecycle_gateway.on_join_spot ([&] (const zlink::framework::actor_ref_t &actor_ref,
                                         zlink::framework::spot_rid_t spot_rid,
                                         const zlink::message_t &payload) {
        auto &actor_state =
          actor_ref.actor_id () == "rejected-player" ? rejected_actor_state : lifecycle_actor_state;
        return lifecycle_runtime.join_actor_to_spot<stage_spot_t> (actor_ref, std::move (spot_rid),
                                                                   actor_state, payload);
    });
    auto lifecycle_join =
      lifecycle_actor_context
        .join_spot (lifecycle_stage.spot_rid, zlink::message_t::from (std::string ("41")))
        .submit ();
    if (!lifecycle_join || lifecycle_join.value ().result_code != 0
        || lifecycle_join.value ().reply.to_string () != "42"
        || lifecycle_stage_spot->join_seen != 41 || lifecycle_stage_spot->joined_count != 1
        || lifecycle_actor_state.joined_value != 141) {
        return 59;
    }
    auto close_joined_spot = lifecycle_stage.context.close ().result ();
    if (!close_joined_spot || close_joined_spot.value ()
        || !lifecycle_builder.find_spot (lifecycle_stage.spot_rid)) {
        return 60;
    }

    lifecycle_stage_spot->accept_join = false;
    auto rejected_actor =
      lifecycle_gateway.manager ().create ("player", "rejected-player").value ();
    auto rejected_context = rejected_actor.context ();
    auto rejected_runtime_join =
      rejected_context
        .join_spot (lifecycle_stage.spot_rid, zlink::message_t::from (std::string ("50")))
        .submit ();
    lifecycle_stage_spot->accept_join = true;
    if (!rejected_runtime_join || rejected_runtime_join.value ().result_code == 0
        || rejected_runtime_join.value ().reply.to_string () != "rejected"
        || lifecycle_stage_spot->joined_count != 1
        || rejected_context.actor_ref ().node_rid ().value () != "local") {
        return 61;
    }

    lifecycle_gateway.on_join_entry_spot (
      [&] (const zlink::framework::actor_ref_t &actor_ref, zlink::framework::node_rid_t node_rid) {
          return lifecycle_runtime.join_actor_to_entry_spot<entry_spot_t> (
            actor_ref, std::move (node_rid), lifecycle_actor_state);
      });
    auto lifecycle_entry_join =
      lifecycle_actor_context
        .join_entry_spot (zlink::framework::node_rid_t::from_string ("lifecycle-stage"))
        .submit ();
    if (!lifecycle_entry_join || lifecycle_stage_spot->left_count != 1
        || lifecycle_entry_spot->joined_count != 1 || lifecycle_actor_state.moved_value != 100
        || lifecycle_actor_state.joined_value != 151) {
        return 62;
    }
    auto close_after_actor_left = lifecycle_stage.context.close ().result ();
    if (!close_after_actor_left || !close_after_actor_left.value ()
        || lifecycle_builder.find_spot (lifecycle_stage.spot_rid)) {
        return 63;
    }
    auto close_entry_with_actor = lifecycle_entry.context.close ().result ();
    if (!close_entry_with_actor || close_entry_with_actor.value ()
        || !lifecycle_builder.find_spot (lifecycle_entry.spot_rid)) {
        return 64;
    }
    auto entry_leave =
      lifecycle_runtime.leave_actor (lifecycle_actor_context.actor_ref (), lifecycle_actor_state);
    if (!entry_leave || lifecycle_entry_spot->left_count != 1
        || lifecycle_actor_state.moved_value != 110) {
        return 65;
    }
    auto close_empty_entry = lifecycle_entry.context.close ().result ();
    if (!close_empty_entry || !close_empty_entry.value ()
        || lifecycle_builder.find_spot (lifecycle_entry.spot_rid)) {
        return 66;
    }

    bool duplicate_spot_failed = false;
    try {
        builder.add_spot<stage_spot_t> ("stage");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        duplicate_spot_failed = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!duplicate_spot_failed) {
        return 6;
    }

    bool duplicate_resolver_failed = false;
    try {
        builder.add_spot_resolver ("remote", [] (zlink::framework::spot_rid_t) {
            return std::optional<zlink::framework::spot_route_t>{};
        });
    }
    catch (const zlink::framework::framework_exception_t &error) {
        duplicate_resolver_failed = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!duplicate_resolver_failed) {
        return 7;
    }

    bool empty_discovery_failed = false;
    try {
        zlink::framework::spot_node_builder_t invalid;
        invalid.use_discovery ("");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_discovery_failed = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!empty_discovery_failed) {
        return 8;
    }

    zlink::framework::spot_node_builder_t registry;
    registry.use_registry_spot_remote_addresses ("game.route");
    if (!registry.snapshot ().registry_spot_route_channel
        || *registry.snapshot ().registry_spot_route_channel != "game.route") {
        return 9;
    }

    bool registry_conflict_failed = false;
    try {
        registry.add_spot_resolver ("custom", [] (zlink::framework::spot_rid_t) {
            return std::optional<zlink::framework::spot_route_t>{};
        });
    }
    catch (const zlink::framework::framework_exception_t &error) {
        registry_conflict_failed = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!registry_conflict_failed) {
        return 10;
    }

    bool empty_registry_route_failed = false;
    try {
        zlink::framework::spot_node_builder_t invalid;
        invalid.use_registry_spot_remote_addresses ("");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_registry_route_failed =
          error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!empty_registry_route_failed) {
        return 11;
    }

    bool empty_router_manual_endpoint_failed = false;
    try {
        zlink::framework::spot_node_builder_t invalid;
        invalid.connect_router (" ");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_router_manual_endpoint_failed =
          error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!empty_router_manual_endpoint_failed) {
        return 40;
    }

    bool empty_pub_sub_manual_endpoint_failed = false;
    try {
        zlink::framework::spot_node_builder_t invalid;
        invalid.connect_pub_sub (" ");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_pub_sub_manual_endpoint_failed =
          error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!empty_pub_sub_manual_endpoint_failed) {
        return 41;
    }

    bool empty_attach_failed = false;
    try {
        zlink::framework::spot_node_builder_t invalid;
        invalid.attach_channel_client (" ");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_attach_failed = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!empty_attach_failed) {
        return 30;
    }

    bool empty_attach_endpoint_failed = false;
    try {
        zlink::framework::spot_node_builder_t invalid;
        invalid.attach_channel_client ("profile", {" "});
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_attach_endpoint_failed =
          error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!empty_attach_endpoint_failed) {
        return 32;
    }

    bool empty_publisher_attach_failed = false;
    try {
        zlink::framework::spot_node_builder_t invalid;
        invalid.attach_publisher (" ");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_publisher_attach_failed =
          error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!empty_publisher_attach_failed) {
        return 31;
    }

    bool empty_publisher_attach_endpoint_failed = false;
    try {
        zlink::framework::spot_node_builder_t invalid;
        invalid.attach_publisher ("events", {" "});
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_publisher_attach_endpoint_failed =
          error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!empty_publisher_attach_endpoint_failed) {
        return 33;
    }

    context.register_packet<state_update_t> ("state.update");
    if (context.packet_registry ().size () != 1
        || context.packet_registry ()[0].packet_name != "state.update") {
        return 12;
    }

    context.handlers ()
      .add_handler<&stage_spot_t::on_state_update> ("state.update")
      .add_handler<&stage_spot_t::on_throwing_state_update> ("state.throw")
      .add_actor_packet<&stage_spot_t::on_move> ("move")
      .add_actor_disconnected<&stage_spot_t::on_actor_disconnected> ();
    const auto handler_descriptors = context.handlers ().descriptors ();
    if (handler_descriptors.size () != 4
        || handler_descriptors[0].kind != zlink::framework::spot_handler_kind_t::packet
        || handler_descriptors[0].packet_name != "state.update"
        || handler_descriptors[1].kind != zlink::framework::spot_handler_kind_t::packet
        || handler_descriptors[1].packet_name != "state.throw"
        || handler_descriptors[2].kind != zlink::framework::spot_handler_kind_t::actor_packet
        || handler_descriptors[2].packet_name != "move"
        || handler_descriptors[3].kind
             != zlink::framework::spot_handler_kind_t::actor_disconnected) {
        return 20;
    }

    zlink::framework::service_collection_t spot_services;
    auto spot_provider = spot_services.build_provider ();

    zlink::framework::serializer_registry_t spot_serializers;
    spot_serializers.add<state_update_t> (
      [] (const state_update_t &value) {
          return zlink::message_t::from (std::to_string (value.value));
      },
      [] (const zlink::message_t &message) {
          return state_update_t{std::stoi (message.to_string ())};
      });
    spot_serializers.add<move_request_t> (
      [] (const move_request_t &value) {
          return zlink::message_t::from (std::to_string (value.value));
      },
      [] (const zlink::message_t &message) {
          return move_request_t{std::stoi (message.to_string ())};
      });
    spot_serializers.add<move_reply_t> (
      [] (const move_reply_t &value) {
          return zlink::message_t::from (std::to_string (value.value));
      },
      [] (const zlink::message_t &message) {
          return move_reply_t{std::stoi (message.to_string ())};
      });

    stage_spot_t stage_spot;
    player_actor_factory_t actor;
    const auto packet_dispatch = context.handlers ().invoke_packet (
      "state.update", stage_spot, spot_provider, spot_serializers,
      zlink::message_t::from (std::string ("30")));
    if (!packet_dispatch || stage_spot.last_value != 30 || stage_spot.packet_seen != 30) {
        return 22;
    }

    const auto throwing_packet_dispatch =
      context.handlers ().invoke_packet ("state.throw", stage_spot, spot_provider, spot_serializers,
                                         zlink::message_t::from (std::string ("31")));
    if (throwing_packet_dispatch
        || throwing_packet_dispatch.error_kind () != framework_error_kind_t::request_failed) {
        return 45;
    }

    const auto join_dispatch =
      stage_spot.on_actor_join (actor, zlink::message_t::from (std::string ("41")));
    if (!join_dispatch.accepted || !join_dispatch.reply || join_dispatch.reply->to_string () != "42"
        || actor.joined_value != 41 || stage_spot.join_seen != 41) {
        return 23;
    }
    stage_spot.on_post_actor_joined (actor);
    if (stage_spot.joined_count != 1 || actor.joined_value != 141) {
        return 25;
    }
    stage_spot.accept_join = false;
    const auto rejected_join =
      stage_spot.on_actor_join (actor, zlink::message_t::from (std::string ("50")));
    if (rejected_join.accepted || !rejected_join.reply
        || rejected_join.reply->to_string () != "rejected") {
        return 47;
    }
    if (stage_spot.joined_count != 1) {
        return 48;
    }

    const auto move_dispatch = context.handlers ().invoke_actor_packet (
      "move", stage_spot, actor, spot_provider, spot_serializers,
      zlink::message_t::from (std::string ("55")));
    if (!move_dispatch || actor.moved_value != 55 || stage_spot.packet_seen != 55) {
        return 24;
    }
    if (!stage_spot.last_trace_id.empty () || stage_spot.saw_tenant_id) {
        return 36;
    }

    zlink::framework::message_metadata_policy_t metadata_policy;
    metadata_policy.add_forwarded_metadata_key ("trace-id");
    try {
        metadata_policy.add_forwarded_metadata_key ("");
        return 37;
    }
    catch (const zlink::framework::framework_exception_t &ex) {
        if (ex.kind () != framework_error_kind_t::request_protocol_error) {
            return 38;
        }
    }
    try {
        metadata_policy.add_forwarded_metadata_key (" ");
        return 42;
    }
    catch (const zlink::framework::framework_exception_t &ex) {
        if (ex.kind () != framework_error_kind_t::request_protocol_error) {
            return 43;
        }
    }
    std::map<std::string, std::string> stream_metadata{{"trace-id", "trace-1"},
                                                       {"tenant-id", "tenant-1"}};
    const auto projected = metadata_policy.project (stream_metadata);
    const auto projected_trace = projected.find ("trace-id");
    if (!projected_trace || *projected_trace != "trace-1" || projected.contains ("tenant-id")
        || projected.empty ()) {
        return 39;
    }
    const auto metadata_dispatch = context.handlers ().invoke_actor_packet (
      "move", stage_spot, actor, spot_provider, spot_serializers,
      zlink::message_t::from (std::string ("56")), projected);
    if (!metadata_dispatch || actor.moved_value != 56 || stage_spot.last_trace_id != "trace-1"
        || stage_spot.saw_tenant_id) {
        return 40;
    }

    stage_spot.on_actor_left (actor);
    if (stage_spot.left_count != 1 || actor.moved_value != 156) {
        return 26;
    }

    const auto disconnected_dispatch = context.handlers ().invoke_actor_disconnected (
      stage_spot, actor, spot_provider, spot_serializers);
    if (!disconnected_dispatch || stage_spot.disconnected_count != 1) {
        return 27;
    }

    bool duplicate_packet_failed = false;
    try {
        context.register_packet<state_update_t> ("state.update");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        duplicate_packet_failed = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!duplicate_packet_failed) {
        return 13;
    }

    bool duplicate_handler_failed = false;
    try {
        context.handlers ().add_handler<&stage_spot_t::on_state_update> ("state.update");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        duplicate_handler_failed = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!duplicate_handler_failed) {
        return 21;
    }

    auto publish_result =
      context.publish ("stage.state.updated", state_update_t{1}).submit ();
    if (!publish_result) {
        return 14;
    }

    auto send_result =
      context.send_to (remote_route->node_rid, remote_route->spot_rid, state_update_t{2})
        .submit ();
    if (!send_result) {
        return 15;
    }

    auto request_result = context
                            .request_to<move_reply_t> (remote_route->node_rid,
                                                       remote_route->spot_rid, move_request_t{3})
                            .submit ();
    if (request_result || request_result.error_kind () != framework_error_kind_t::timeout) {
        return 16;
    }

    auto missing_route_result =
      context
        .request_to<move_reply_t> (zlink::framework::node_rid_t{}, zlink::framework::spot_rid_t{},
                                   move_request_t{4})
        .submit ();
    if (missing_route_result
        || missing_route_result.error_kind () != framework_error_kind_t::spot_route_not_found) {
        return 17;
    }

    const auto runtime = zlink::framework::detail::spot_node_runtime_t::from (builder);
    const auto &ordering = runtime.ordering_log (context);
    if (ordering.size () != 3 || ordering[0] != "publish:stage.state.updated"
        || ordering[1] != "send_to:remote-stage" || ordering[2] != "request_to:remote-stage") {
        return 18;
    }

    stage_wrapper_t wrapper (context.node_rid (), context.spot_rid (), zlink.publisher (),
                             context.packet_registry ().size ());
    wrapper.apply (7);
    if (wrapper.node_rid.empty () || wrapper.spot_rid.empty () || wrapper.packet_count != 1
        || wrapper.state != 7) {
        return 19;
    }

    return 0;
}
