/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

class test_spot_context_t : public zlink::framework::spot_context_t
{
  public:
    explicit test_spot_context_t (
      std::shared_ptr<zlink::framework::detail::spot_context_state_t> state) :
        zlink::framework::spot_context_t (std::move (state))
    {
    }
};

struct player_actor_factory_t
{
    int joined_value{};
    int moved_value{};
    int ref_updates{};
    std::uint64_t last_generation{};

    void set_actor_ref (const zlink::framework::actor_ref_t &actor_ref)
    {
        ++ref_updates;
        last_generation = actor_ref.generation ();
    }
};

struct entry_spot_t : public zlink::framework::entry_spot_t
{
    void onCreateActor (player_actor_factory_t &actor)
    {
        ++created_count;
        actor.joined_value += 1;
    }

    void onJoinActor (player_actor_factory_t &actor)
    {
        ++joined_count;
        actor.joined_value += 10;
    }

    void onLeaveActor (player_actor_factory_t &actor)
    {
        ++left_count;
        actor.moved_value += 10;
        if (on_left) {
            on_left (actor);
        }
    }

    void onDisconnectActor (player_actor_factory_t &) { ++disconnected_count; }

    int created_count{};
    int joined_count{};
    int left_count{};
    int disconnected_count{};
    std::function<void (player_actor_factory_t &)> on_left;
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

struct relay_request_t
{
    int value{};
};

struct relay_reply_t
{
    std::string value;
};

struct relay_actor_t
{
    std::string actor_id;
};

struct relay_actor_factory_t
{
    relay_actor_t create (std::string actor_id) const { return {std::move (actor_id)}; }
};

struct relay_spot_t : public zlink::framework::spot_t
{
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_actor_packet<&relay_spot_t::on_relay> ("relay.request");
    }

    zlink::framework::spot_actor_join_response_t on_actor_join (relay_actor_t &,
                                                                const zlink::message_t &)
    {
        return zlink::framework::spot_actor_join_response_t::accept ();
    }

    relay_reply_t on_relay (relay_actor_t &actor,
                            zlink::framework::spot_actor_request_context_t &,
                            const relay_request_t &request)
    {
        return {actor.actor_id + ":" + std::to_string (request.value)};
    }
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

    void onJoinActor (player_actor_factory_t &actor)
    {
        ++joined_count;
        actor.joined_value += 100;
    }

    void onLeaveActor (player_actor_factory_t &actor)
    {
        ++left_count;
        actor.moved_value += 100;
    }

    void on_closing ()
    {
        ++closing_count;
        ++global_closing_count;
    }

    void onDisconnectActor (player_actor_factory_t &) { ++disconnected_count; }

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

struct serial_probe_spot_t : public zlink::framework::spot_t
{
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_actor_packet<&serial_probe_spot_t::on_move> ("serial.move");
    }

    void on_move (player_actor_factory_t &,
                  zlink::framework::spot_actor_send_context_t &,
                  const move_request_t &)
    {
        std::unique_lock lock (mutex);
        ++running;
        if (running > max_running) {
            max_running = running;
        }
        ++starts;
        if (starts == 1) {
            first_started = true;
            changed.notify_all ();
            changed.wait (lock, [this] { return release_first; });
        }
        --running;
        changed.notify_all ();
    }

    bool wait_first_started ()
    {
        std::unique_lock lock (mutex);
        return changed.wait_for (lock, std::chrono::milliseconds (500),
                                 [this] { return first_started; });
    }

    int starts_seen () const
    {
        std::lock_guard lock (mutex);
        return starts;
    }

    void release ()
    {
        std::lock_guard lock (mutex);
        release_first = true;
        changed.notify_all ();
    }

    mutable std::mutex mutex;
    std::condition_variable changed;
    bool first_started = false;
    bool release_first = false;
    int running = 0;
    int max_running = 0;
    int starts = 0;
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

struct lifecycle_thread_probe_entry_spot_t : public zlink::framework::entry_spot_t
{
    void onJoinActor (player_actor_factory_t &)
    {
        ++joined_count;
        last_join_thread = std::this_thread::get_id ();
    }

    int joined_count{};
    std::thread::id last_join_thread;
};

} // namespace

int main ()
{
    using zlink::framework::framework_error_kind_t;

    zlink::framework::zlink_builder_t zlink;
    zlink.add_node ("stage-node");
    auto channel = zlink.channel ("game.stage");
    channel.enable_publisher ().bind ("tcp://127.0.0.1:8101");
    channel.enable_subscriber ().use_discovery ();
    zlink.add_spot_node ("stage-spot-node")
      .bind ("tcp://0.0.0.0:9000")
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
    builder = manual_host.add_spot_node ("manual-stage");
    auto create_factory_spot = [] {
        return std::make_shared<factory_spot_t> ("factory-reply");
    };
    builder.bind ("tcp://0.0.0.0:9001")
      .use_discovery ("game.stage")
      .attach_publisher ("game.stage")
      .add_entry_spot<entry_spot_t> ()
      .add_actor_factory<player_actor_factory_t> ("player")
      .add_spot<stage_spot_t> ("stage")
      .add_spot<factory_spot_t> ("factory", create_factory_spot);

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
    lifecycle_builder = lifecycle_host.add_spot_node ("lifecycle-stage");
    lifecycle_builder
      .add_entry_spot<entry_spot_t> ([lifecycle_entry_spot] { return lifecycle_entry_spot; })
      .add_actor_factory<player_actor_factory_t> ("player")
      .add_spot<stage_spot_t> ("stage", [lifecycle_stage_spot] {
          return lifecycle_stage_spot;
      });
    auto lifecycle_entry = lifecycle_builder.create_spot ("entry");
    auto lifecycle_stage = lifecycle_builder.create_spot ("stage");
    zlink::framework::entry_spot_context_t lifecycle_entry_context (lifecycle_entry.context);
    auto lifecycle_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (lifecycle_builder);
    zlink::framework::detail::actor_gateway_runtime_t lifecycle_gateway;
    lifecycle_runtime.on_destroy_actor ([&] (const zlink::framework::actor_ref_t &actor_ref) {
        return lifecycle_gateway.destroy_actor (actor_ref);
    });
    lifecycle_runtime.on_actor_ref_updated ([&] (const zlink::framework::actor_ref_t &actor_ref) {
        return lifecycle_gateway.update_actor_ref (actor_ref);
    });
    auto lifecycle_actor = lifecycle_gateway.manager ().create ("player", "joined-player").value ();
    auto lifecycle_actor_context = lifecycle_actor.context ();
    player_actor_factory_t lifecycle_actor_state;
    player_actor_factory_t destroyActor_state;
    player_actor_factory_t rejected_actor_state;
    lifecycle_gateway.on_join_spot ([&] (const zlink::framework::actor_ref_t &actor_ref,
                                         zlink::framework::spot_rid_t spot_rid,
                                         const zlink::message_t &payload) {
        auto &actor_state = actor_ref.actor_id () == "rejected-player"  ? rejected_actor_state
                            : actor_ref.actor_id () == "destroy-player" ? destroyActor_state
                                                                        : lifecycle_actor_state;
        return lifecycle_runtime.join_actor_to_spot<stage_spot_t> (actor_ref, std::move (spot_rid),
                                                                   actor_state, payload);
    });
    auto lifecycle_join =
      lifecycle_actor_context
        .join_spot (lifecycle_stage.spot_rid, zlink::message_t::from (std::string ("41")))
        .async ()
        .result ();
    if (!lifecycle_join || lifecycle_join.value ().result_code != 0
        || lifecycle_join.value ().reply.to_string () != "42"
        || lifecycle_stage_spot->join_seen != 41 || lifecycle_stage_spot->joined_count != 1
        || lifecycle_actor_state.joined_value != 141) {
        return 59;
    }
    auto stale_disconnect =
      lifecycle_runtime.notify_onDisconnectActor (lifecycle_actor.ref (), lifecycle_actor_state);
    if (stale_disconnect
        || stale_disconnect.error_kind () != framework_error_kind_t::actor_stale_generation
        || lifecycle_stage_spot->disconnected_count != 0) {
        return 67;
    }
    auto current_disconnect = lifecycle_runtime.notify_onDisconnectActor (
      lifecycle_actor_context.actor_ref (), lifecycle_actor_state);
    if (!current_disconnect || lifecycle_stage_spot->disconnected_count != 1) {
        return 68;
    }
    if (!lifecycle_gateway.manager ().find ("joined-player")) {
        return 85;
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
        .async ()
        .result ();
    lifecycle_stage_spot->accept_join = true;
    if (!rejected_runtime_join || rejected_runtime_join.value ().result_code == 0
        || rejected_runtime_join.value ().reply.to_string () != "rejected"
        || lifecycle_stage_spot->joined_count != 1
        || rejected_context.actor_ref ().node_rid ().value () != "local") {
        return 61;
    }

    lifecycle_gateway.on_join_entry_spot (
      [&] (const zlink::framework::actor_ref_t &actor_ref, zlink::framework::node_rid_t node_rid) {
          auto &actor_state =
            actor_ref.actor_id () == "destroy-player" ? destroyActor_state : lifecycle_actor_state;
          return lifecycle_runtime.join_actor_to_entry_spot<entry_spot_t> (
            actor_ref, std::move (node_rid), actor_state);
      });
    auto lifecycle_entry_join =
      lifecycle_actor_context
        .join_entry_spot (zlink::framework::node_rid_t::from_string ("lifecycle-stage"))
        .async ()
        .result ();
    if (!lifecycle_entry_join || lifecycle_stage_spot->left_count != 1
        || lifecycle_entry_spot->joined_count != 1 || lifecycle_actor_state.moved_value != 100
        || lifecycle_entry_spot->created_count != 1 || lifecycle_actor_state.joined_value != 152) {
        return 62;
    }
    auto entry_disconnect = lifecycle_runtime.notify_onDisconnectActor (
      lifecycle_actor_context.actor_ref (), lifecycle_actor_state);
    if (!entry_disconnect || lifecycle_entry_spot->disconnected_count != 1) {
        return 69;
    }
    if (!lifecycle_gateway.manager ().find ("joined-player")) {
        return 86;
    }
    auto destroyActor = lifecycle_gateway.manager ().create ("player", "destroy-player").value ();
    auto destroy_context = destroyActor.context ();
    auto destroy_stage_join =
      destroy_context
        .join_spot (lifecycle_stage.spot_rid, zlink::message_t::from (std::string ("43")))
        .async ()
        .result ();
    if (!destroy_stage_join || lifecycle_stage_spot->joined_count != 2) {
        return 70;
    }
    auto direct_destroy =
      lifecycle_entry_context.destroyActor (destroy_context.actor_ref (), destroyActor_state)
        .result ();
    if (direct_destroy
        || direct_destroy.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 71;
    }
    auto destroy_entry_join =
      destroy_context
        .join_entry_spot (zlink::framework::node_rid_t::from_string ("lifecycle-stage"))
        .async ()
        .result ();
    if (!destroy_entry_join || lifecycle_stage_spot->left_count != 2
        || lifecycle_entry_spot->joined_count != 2) {
        return 72;
    }
    const auto entry_left_before_destroy = lifecycle_entry_spot->left_count;
    auto destroy_result =
      lifecycle_entry_context.destroyActor (destroy_context.actor_ref (), destroyActor_state)
        .result ();
    auto duplicate_destroy =
      lifecycle_entry_context.destroyActor (destroy_context.actor_ref (), destroyActor_state)
        .result ();
    if (!destroy_result || !duplicate_destroy
        || lifecycle_entry_spot->left_count != entry_left_before_destroy) {
        return 73;
    }
    if (lifecycle_gateway.manager ().find ("destroy-player")) {
        return 79;
    }
    auto recreated_destroy_actor = lifecycle_gateway.manager ().create ("player", "destroy-player");
    if (!recreated_destroy_actor) {
        return 80;
    }
    auto leaveActor =
      lifecycle_gateway.manager ().create ("player", "context-leave-player").value ();
    auto leave_context = leaveActor.context ();
    player_actor_factory_t leaveActor_state;
    auto leave_stage_join =
      leave_context
        .join_spot (lifecycle_stage.spot_rid, zlink::message_t::from (std::string ("44")))
        .async ()
        .result ();
    if (!leave_stage_join || lifecycle_stage_spot->joined_count != 3) {
        return 76;
    }
    const auto stage_left_before_context_leave = lifecycle_stage_spot->left_count;
    const auto entry_joined_before_context_leave = lifecycle_entry_spot->joined_count;
    auto context_leave =
      lifecycle_stage.context.leaveActor (leave_context.actor_ref (), leaveActor_state).result ();
    if (!context_leave || lifecycle_stage_spot->left_count != stage_left_before_context_leave + 1
        || lifecycle_entry_spot->joined_count != entry_joined_before_context_leave + 1
        || leaveActor_state.ref_updates != 1
        || leaveActor_state.last_generation != context_leave.value ().generation ()) {
        return 77;
    }
    auto context_leave_destroy =
      lifecycle_entry_context.destroyActor (context_leave.value (), leaveActor_state).result ();
    if (!context_leave_destroy) {
        return 78;
    }
    if (lifecycle_gateway.manager ().find ("context-leave-player")) {
        return 81;
    }

    auto thread_probe_entry = std::make_shared<lifecycle_thread_probe_entry_spot_t> ();
    auto thread_probe_stage = std::make_shared<stage_spot_t> ();
    zlink::framework::spot_node_builder_t thread_probe_builder;
    zlink::framework::zlink_builder_t thread_probe_host;
    thread_probe_builder = thread_probe_host.add_spot_node ("thread-probe-stage");
    thread_probe_builder
      .add_entry_spot<lifecycle_thread_probe_entry_spot_t> ([thread_probe_entry] {
          return thread_probe_entry;
      })
      .add_actor_factory<player_actor_factory_t> ("player")
      .add_spot<stage_spot_t> ("stage", [thread_probe_stage] {
          return thread_probe_stage;
      });
    auto thread_probe_entry_create = thread_probe_builder.create_spot ("entry");
    auto thread_probe_stage_create = thread_probe_builder.create_spot ("stage");
    auto thread_probe_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (thread_probe_builder);
    if (thread_probe_entry_create.state != zlink::framework::spot_create_state_t::created
        || thread_probe_stage_create.state != zlink::framework::spot_create_state_t::created) {
        return 90;
    }

    player_actor_factory_t direct_entry_actor;
    zlink::framework::actor_ref_t direct_entry_ref (
      zlink::framework::node_rid_t::from_string ("thread-probe-stage"), "player",
      "direct-entry-player", 1);
    const auto direct_entry_caller = std::this_thread::get_id ();
    auto direct_entry_join =
      thread_probe_runtime
        .join_actor_to_entry_spot<lifecycle_thread_probe_entry_spot_t> (
          direct_entry_ref, zlink::framework::node_rid_t::from_string ("thread-probe-stage"),
          direct_entry_actor);
    if (!direct_entry_join || thread_probe_entry->joined_count != 1
        || thread_probe_entry->last_join_thread == direct_entry_caller) {
        return 87;
    }

    player_actor_factory_t context_leave_actor;
    zlink::framework::actor_ref_t context_leave_ref (
      zlink::framework::node_rid_t::from_string ("thread-probe-stage"), "player",
      "context-leave-thread-player", 1);
    auto context_leave_stage_join =
      thread_probe_runtime.join_actor_to_spot<stage_spot_t> (
        context_leave_ref, thread_probe_stage_create.spot_rid, context_leave_actor,
        zlink::message_t::from (std::string ("45")));
    if (!context_leave_stage_join || context_leave_stage_join.value ().result_code != 0) {
        return 88;
    }
    thread_probe_entry->last_join_thread = std::thread::id ();
    const auto context_leave_caller = std::this_thread::get_id ();
    auto context_leave_thread_result =
      thread_probe_stage_create.context
        .leaveActor (context_leave_stage_join.value ().actor, context_leave_actor)
        .result ();
    if (!context_leave_thread_result || thread_probe_entry->joined_count != 2
        || thread_probe_entry->last_join_thread == context_leave_caller) {
        return 89;
    }

    zlink::framework::spot_node_builder_t relay_builder;
    zlink::framework::zlink_builder_t relay_host;
    relay_builder = relay_host.add_spot_node ("relay-stage");
    relay_builder.add_entry_spot<entry_spot_t> ()
      .add_actor_factory<relay_actor_factory_t> ("relay-player")
      .add_spot<relay_spot_t> ("relay-room");
    auto relay_spot = relay_builder.create_spot ("relay-room");
    auto relay_runtime = zlink::framework::detail::spot_node_runtime_t::from (relay_builder);
    relay_actor_t relay_actor{"relay-actor"};
    zlink::framework::actor_ref_t relay_actor_ref (
      zlink::framework::node_rid_t::from_string ("relay-stage"), "relay-player", "relay-actor", 1);
    auto relay_join = relay_runtime.join_actor_to_spot<relay_spot_t> (
      relay_actor_ref, relay_spot.spot_rid, relay_actor, zlink::message_t{});
    if (!relay_join || relay_join.value ().result_code != 0) {
        return 82;
    }
    zlink::framework::service_collection_t relay_services;
    auto relay_provider = relay_services.build_provider ();
    zlink::framework::serializer_registry_t relay_serializers;
    relay_serializers.add<relay_request_t> (
      [] (const relay_request_t &value) {
          return zlink::message_t::from (std::to_string (value.value));
      },
      [] (const zlink::message_t &message) {
          return relay_request_t{std::stoi (message.to_string ())};
      });
    relay_serializers.add<relay_reply_t> (
      [] (const relay_reply_t &value) { return zlink::message_t::from (value.value); },
      [] (const zlink::message_t &message) { return relay_reply_t{message.to_string ()}; });
    auto relay_dispatch = relay_runtime.relay_actor_packet (
      relay_join.value ().actor, zlink::framework::actor_context_t{}, "relay.request",
      zlink::message_t::from (std::string ("64")), relay_provider, relay_serializers);
    if (!relay_dispatch || !relay_dispatch.value ()
        || relay_dispatch.value ()->to_string () != "relay-actor:64") {
        return 83;
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
    const auto entry_left_before_manual_leave = lifecycle_entry_spot->left_count;
    auto entry_leave =
      lifecycle_runtime.leaveActor (lifecycle_actor_context.actor_ref (), lifecycle_actor_state);
    if (!entry_leave) {
        return 65;
    }
    if (lifecycle_entry_spot->left_count != entry_left_before_manual_leave + 1) {
        return 74;
    }
    if (lifecycle_actor_state.moved_value != 110) {
        return 75;
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
        const std::vector<std::string> invalid_endpoints{" "};
        invalid.attach_channel_client ("profile", invalid_endpoints);
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
        const std::vector<std::string> invalid_endpoints{" "};
        invalid.attach_publisher ("events", invalid_endpoints);
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
      .add_actor_packet<&stage_spot_t::on_move> ("move");
    const auto handler_descriptors = context.handlers ().descriptors ();
    if (handler_descriptors.size () != 3
        || handler_descriptors[0].kind != zlink::framework::spot_handler_kind_t::packet
        || handler_descriptors[0].packet_name != "state.update"
        || handler_descriptors[1].kind != zlink::framework::spot_handler_kind_t::packet
        || handler_descriptors[1].packet_name != "state.throw"
        || handler_descriptors[2].kind != zlink::framework::spot_handler_kind_t::actor_packet
        || handler_descriptors[2].packet_name != "move") {
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
    stage_spot.onJoinActor (actor);
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

    auto serial_state = std::make_shared<zlink::framework::detail::spot_context_state_t> ();
    serial_state->serial_executor =
      std::make_shared<zlink::framework::runtime::offload_executor_t> (1);
    serial_state->serial_queue =
      std::make_shared<zlink::framework::runtime::serial_execution_queue_t> (
        *serial_state->serial_executor);
    auto serial_context = test_spot_context_t (serial_state);
    serial_probe_spot_t serial_spot;
    serial_spot.configure (serial_context);
    player_actor_factory_t serial_actor;
    auto invoke_serial = [&] {
        return serial_context.handlers ().invoke_actor_packet (
          "serial.move", serial_spot, serial_actor, spot_provider, spot_serializers,
          zlink::message_t::from (std::string ("1")));
    };
    std::optional<zlink::framework::result_t<zlink::message_t>> first_result;
    std::optional<zlink::framework::result_t<zlink::message_t>> second_result;
    std::thread first ([&] { first_result = invoke_serial (); });
    if (!serial_spot.wait_first_started ()) {
        serial_spot.release ();
        first.join ();
        return 49;
    }
    std::thread second ([&] { second_result = invoke_serial (); });
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    if (serial_spot.starts_seen () != 1) {
        serial_spot.release ();
        first.join ();
        second.join ();
        return 50;
    }
    serial_spot.release ();
    first.join ();
    second.join ();
    if (!first_result || !*first_result || !second_result || !*second_result
        || serial_spot.max_running != 1 || serial_spot.starts_seen () != 2) {
        return 51;
    }

    stage_spot.onLeaveActor (actor);
    if (stage_spot.left_count != 1 || actor.moved_value != 156) {
        return 26;
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
      context.publish ("stage.state.updated", state_update_t{1}).async ().result ();
    if (!publish_result) {
        return 14;
    }

    auto send_result =
      context.send_to (remote_route->node_rid, remote_route->spot_rid, state_update_t{2})
        .async ()
        .result ();
    if (!send_result) {
        return 15;
    }

    auto request_result = context
                            .request_to<move_reply_t> (remote_route->node_rid,
                                                       remote_route->spot_rid, move_request_t{3})
                            .async ()
                            .result ();
    if (request_result || request_result.error_kind () != framework_error_kind_t::timeout) {
        return 16;
    }

    auto missing_route_result =
      context
        .request_to<move_reply_t> (zlink::framework::node_rid_t{}, zlink::framework::spot_rid_t{},
                                   move_request_t{4})
        .async ()
        .result ();
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
