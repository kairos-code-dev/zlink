/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>
#include <zlink.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/channels/route_handler_registry.hpp"
#include "runtime/channels/route_packet_dispatcher.hpp"
#include "runtime/spots/spot_route_internal_dispatcher.hpp"
#include "runtime/spots/spot_route_packets.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

void add_string_serializer (zlink::framework::serializer_registry_t &serializers)
{
    serializers.add<std::string> (
      [] (const std::string &value) {
          return zlink::framework::encoded_payload_t::from_string (value);
      },
      [] (const zlink::framework::encoded_payload_t &payload) { return payload.to_string (); });
}

class test_spot_context_t : public zlink::framework::spot_context_t
{
  public:
    explicit test_spot_context_t (
      std::shared_ptr<zlink::framework::detail::spot_context_state_t> state) :
        zlink::framework::spot_context_t (std::move (state))
    {
    }
};

class controlled_worker_scheduler_t final : public zlink::framework::detail::worker_scheduler_t
{
  public:
    bool try_schedule (std::function<void ()> work) override
    {
        std::lock_guard lock (mutex);
        worker_jobs.push (std::move (work));
        changed.notify_all ();
        return true;
    }

    void post_owner (std::function<void ()> work) override
    {
        std::lock_guard lock (mutex);
        owner_jobs.push (std::move (work));
        changed.notify_all ();
    }

    bool wait_worker_job_count (std::size_t expected)
    {
        std::unique_lock lock (mutex);
        return changed.wait_for (lock, std::chrono::milliseconds (500),
                                 [&] { return worker_jobs.size () == expected; });
    }

    void run_worker_job ()
    {
        std::function<void ()> job;
        {
            std::lock_guard lock (mutex);
            job = std::move (worker_jobs.front ());
            worker_jobs.pop ();
        }
        job ();
    }

    void run_owner_job ()
    {
        std::function<void ()> job;
        {
            std::lock_guard lock (mutex);
            job = std::move (owner_jobs.front ());
            owner_jobs.pop ();
        }
        job ();
    }

  private:
    std::mutex mutex;
    std::condition_variable changed;
    std::queue<std::function<void ()>> worker_jobs;
    std::queue<std::function<void ()>> owner_jobs;
};

struct player_actor_factory_t
{
    int joined_value{};
    int moved_value{};
    int ref_updates{};
    std::uint64_t last_generation{};
    zlink::framework::actor_ref_t current_ref;

    void set_actor_ref (const zlink::framework::actor_ref_t &actor_ref)
    {
        ++ref_updates;
        current_ref = actor_ref;
        last_generation = actor_ref.generation ();
    }
};

struct entry_spot_t : public zlink::framework::entry_spot_t
{
    void onCreateActor (player_actor_factory_t &actor,
                        const zlink::framework::message_t &request)
    {
        ++created_count;
        created_payloads.push_back (request.decode<std::string> ());
        actor.joined_value += 1;
    }

    void on_actor_joined (player_actor_factory_t &actor)
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
    std::vector<std::string> created_payloads;
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
    zlink::framework::actor_context_t context;

    void set_actor_context (const zlink::framework::actor_context_t &actor_context)
    {
        context = actor_context;
    }
};

struct relay_actor_factory_t
{
    relay_actor_t create (std::string actor_id) const { return {std::move (actor_id)}; }
};

struct relay_entry_spot_t : public zlink::framework::entry_spot_t
{
    void configure (zlink::framework::entry_spot_context_t &context)
    {
        context.handlers ().add_actor_packet<&relay_entry_spot_t::on_relay> ("relay.request");
    }

    void configure (zlink::framework::spot_context_t &context)
    {
        zlink::framework::entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    void onCreateActor (relay_actor_t &, const zlink::framework::message_t &request)
    {
        ++created_count;
        created_payloads.push_back (request.decode<std::string> ());
    }

    void on_actor_joined (relay_actor_t &) { ++joined_count; }

    relay_reply_t on_relay (relay_actor_t &actor,
                            zlink::framework::spot_actor_request_context_t &,
                            const relay_request_t &request)
    {
        return {actor.actor_id + ":" + std::to_string (request.value)};
    }

    int created_count{};
    int joined_count{};
    std::vector<std::string> created_payloads;
};

struct relay_spot_t : public zlink::framework::spot_t
{
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_actor_packet<&relay_spot_t::on_relay> ("relay.request");
    }

    zlink::framework::spot_actor_join_response_t on_actor_join (
      relay_actor_t &, const zlink::framework::message_t &)
    {
        return zlink::framework::spot_actor_join_response_t::accept ();
    }

    void onLeaveActor (relay_actor_t &) { left_count++; }

    relay_reply_t on_relay (relay_actor_t &actor,
                            zlink::framework::spot_actor_request_context_t &,
                            const relay_request_t &request)
    {
        return {actor.actor_id + ":" + std::to_string (request.value)};
    }

    static inline int left_count{};
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

    zlink::framework::spot_create_response_t on_create (
      const zlink::framework::message_t &request)
    {
        ++create_count;
        try {
            last_create_request = request.decode<std::string> ();
        }
        catch (const zlink::framework::framework_exception_t &) {
            last_create_request = "";
        }
        if (reject_create) {
            return zlink::framework::spot_create_response_t::reject (
              zlink::framework::message_t::from (std::string ("create-rejected")));
        }
        return zlink::framework::spot_create_response_t::accept (
          zlink::framework::message_t::from (std::string ("create-accepted")));
    }

    void on_initialize () { ++initialize_count; }

    zlink::framework::spot_actor_join_response_t on_actor_join (
      player_actor_factory_t &actor, const zlink::framework::message_t &request)
    {
        join_seen = std::stoi (request.decode<std::string> ());
        actor.joined_value = join_seen;
        if (!accept_join) {
            return zlink::framework::spot_actor_join_response_t::reject (
              zlink::framework::message_t::from (std::string ("rejected")));
        }
        return zlink::framework::spot_actor_join_response_t::accept (
          zlink::framework::message_t::from (std::to_string (join_seen + 1)));
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

    void on_actor_joined (player_actor_factory_t &actor)
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

struct erased_disconnect_spot_t : public zlink::framework::spot_t
{
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_actor_packet<&erased_disconnect_spot_t::on_move> ("move");
    }

    zlink::framework::spot_actor_join_response_t on_actor_join (player_actor_factory_t &,
                                                                const zlink::message_t &)
    {
        return zlink::framework::spot_actor_join_response_t::accept ();
    }

    void on_move (player_actor_factory_t &,
                  const zlink::framework::spot_actor_send_context_t &,
                  const move_request_t &)
    {
    }

    void onDisconnectActor (player_actor_factory_t &) { ++disconnected_count; }

    int disconnected_count{};
};

struct subscription_spot_t : public zlink::framework::spot_t
{
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_subscribe<&subscription_spot_t::on_state_update> (
          "stage.state.updated");
    }

    void on_state_update (const state_update_t &message) { last_value = message.value; }

    int last_value{};
};

struct alternate_stage_spot_t : public zlink::framework::spot_t
{
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

struct async_probe_spot_t : public zlink::framework::spot_t
{
    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ()
          .add_actor_packet<&async_probe_spot_t::slow> ("async.slow")
          .add_actor_packet<&async_probe_spot_t::quick> ("async.quick");
    }

    zlink::framework::task_t<move_reply_t>
    slow (player_actor_factory_t &,
          zlink::framework::spot_actor_request_context_t &context,
          const move_request_t &request)
    {
        {
            std::lock_guard lock (mutex);
            slow_started = true;
            changed.notify_all ();
        }
        const auto value = co_await _context.run_worker ([] { return 77; }).async ();
        {
            std::lock_guard lock (mutex);
            slow_completed = true;
            changed.notify_all ();
        }
        co_return move_reply_t{value + request.value
                               + (context.packet_name == "async.slow" ? 0 : 1000)};
    }

    move_reply_t quick (player_actor_factory_t &,
                        zlink::framework::spot_actor_request_context_t &,
                        const move_request_t &request)
    {
        std::lock_guard lock (mutex);
        ++quick_count;
        changed.notify_all ();
        return move_reply_t{request.value + 1};
    }

    bool wait_slow_started ()
    {
        std::unique_lock lock (mutex);
        return changed.wait_for (lock, std::chrono::milliseconds (500),
                                 [this] { return slow_started; });
    }

    int quick_seen () const
    {
        std::lock_guard lock (mutex);
        return quick_count;
    }

    mutable std::mutex mutex;
    std::condition_variable changed;
    zlink::framework::spot_context_t _context;
    bool slow_started = false;
    bool slow_completed = false;
    int quick_count = 0;
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

    zlink::framework::spot_create_response_t on_create (
      const zlink::framework::message_t &request)
    {
        ++create_count;
        last_request = request.decode<std::string> ();
        return zlink::framework::spot_create_response_t::accept (
          zlink::framework::message_t::from (value));
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
    void on_actor_joined (player_actor_factory_t &)
    {
        ++joined_count;
        last_join_thread = std::this_thread::get_id ();
    }

    int joined_count{};
    std::thread::id last_join_thread;
};

struct auto_destroy_entry_spot_t : public zlink::framework::entry_spot_t
{
    void configure (zlink::framework::entry_spot_context_t &context) { entry_context = context; }

    void on_actor_joined (player_actor_factory_t &actor)
    {
        ++joined_count;
        if (destroy_on_join && !actor.current_ref.empty ()) {
            const auto destroyed = entry_context.destroyActor (actor.current_ref, actor).result ();
            if (destroyed) {
                ++destroyed_count;
            }
        }
    }

    zlink::framework::entry_spot_context_t entry_context;
    int joined_count{};
    int destroyed_count{};
    bool destroy_on_join = false;
};

struct actor_packet_self_leave_spot_t : public zlink::framework::spot_t
{
    void configure (zlink::framework::spot_context_t &context)
    {
        spot_context = context;
        context.handlers ().add_actor_packet<&actor_packet_self_leave_spot_t::on_leave_request> (
          "self.leave");
    }

    zlink::framework::spot_actor_join_response_t on_actor_join (
      player_actor_factory_t &, const zlink::framework::message_t &)
    {
        return zlink::framework::spot_actor_join_response_t::accept ();
    }

    void onLeaveActor (player_actor_factory_t &) { ++left_count; }

    relay_reply_t on_leave_request (player_actor_factory_t &actor,
                                    zlink::framework::spot_actor_request_context_t &,
                                    const relay_request_t &request)
    {
        auto left = spot_context.leaveActor (actor.current_ref, actor).result ();
        if (!left) {
            throw std::runtime_error ("self leave failed");
        }
        return {std::to_string (request.value)};
    }

    zlink::framework::spot_context_t spot_context;
    int left_count{};
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
        || snapshots[0].spot_names.size () != 2 || snapshots[0].entry_spot_name != "entry"
        || snapshots[0].actor_types.size () != 1) {
        return 1;
    }

    zlink::framework::spot_node_builder_t builder;
    zlink::framework::zlink_builder_t manual_host;
    zlink::framework::serializer_registry_t manual_serializers;
    add_string_serializer (manual_serializers);
    manual_serializers.add<state_update_t> (
      [] (const state_update_t &value) {
          return zlink::framework::encoded_payload_t::from_string (std::to_string (value.value));
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return state_update_t{std::stoi (payload.to_string ())};
      });
    manual_serializers.add<move_request_t> (
      [] (const move_request_t &value) {
          return zlink::framework::encoded_payload_t::from_string (std::to_string (value.value));
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return move_request_t{std::stoi (payload.to_string ())};
      });
    manual_serializers.add<move_reply_t> (
      [] (const move_reply_t &value) {
          return zlink::framework::encoded_payload_t::from_string (std::to_string (value.value));
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return move_reply_t{std::stoi (payload.to_string ())};
      });
    zlink::framework::detail::channel_runtime_t::from (manual_host.message_bus ())
      .bind_serializers (manual_serializers);
    manual_host.route_channel ("game.route").connect ("inproc://cpp-framework-spot-route");
    zlink::framework::detail::channel_runtime_manager_t::from (manual_host)
      .initialize_route_channels (manual_host);
    builder = manual_host.add_spot_node ("manual-stage");
    auto create_factory_spot = [] { return std::make_shared<factory_spot_t> ("factory-reply"); };
    builder.bind ("tcp://0.0.0.0:9001")
      .use_discovery ("game.stage")
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

    zlink::framework::zlink_builder_t registry_host;
    auto registry_owner = registry_host.add_spot_node ("registry-owner");
    registry_owner.add_spot<stage_spot_t> ("stage");
    auto registry_lookup = registry_host.add_spot_node ("registry-lookup");
    registry_lookup.use_registry_spot_resolver ().add_spot<stage_spot_t> ("stage");
    const auto registry_owned_spot = registry_owner.get_or_create_spot (
      "stage", zlink::framework::spot_rid_t::from_string ("registry-room-1"));
    const auto registry_route = registry_lookup.resolve_spot (registry_owned_spot.spot_rid);
    if (!registry_route || registry_route->node_rid.value () != "registry-owner"
        || registry_route->spot_name != "stage") {
        return 90;
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
                                                        zlink::framework::message_t::from (std::string ("create-request")));
    auto existing_once =
      builder.get_or_create_spot ("stage", requested_rid, zlink::framework::message_t::from (std::string ("ignored")));
    if (created_once.state != zlink::framework::spot_create_state_t::created
        || existing_once.state != zlink::framework::spot_create_state_t::existing
        || existing_once.spot_rid.value () != requested_rid.value ()
        || stage_spot_t::create_count != get_or_create_count_before + 1
        || stage_spot_t::last_create_request != "create-request") {
        return 52;
    }
    builder.add_spot<alternate_stage_spot_t> ("alternate-stage");
    bool spot_type_mismatch_failed = false;
    try {
        (void) builder.get_or_create_spot ("alternate-stage", requested_rid);
    }
    catch (const zlink::framework::framework_exception_t &error) {
        spot_type_mismatch_failed =
          error.kind () == zlink::framework::framework_error_kind_t::spot_type_mismatch;
    }
    if (!spot_type_mismatch_failed || builder.find_spot (requested_rid)->spot_name != "stage") {
        return 53;
    }
    stage_spot_t::reject_create = true;
    auto rejected_create =
      builder.create_spot ("stage", zlink::framework::message_t::from (std::string ("reject-request")));
    stage_spot_t::reject_create = false;
    if (rejected_create.state != zlink::framework::spot_create_state_t::rejected
        || !rejected_create.reply
        || rejected_create.reply->decode<std::string> (manual_serializers) != "create-rejected"
        || builder.find_spot (rejected_create.spot_rid)) {
        return 54;
    }

    auto factory_created =
      builder.create_spot ("factory", zlink::framework::message_t::from (std::string ("factory-request")));
    if (factory_created.state != zlink::framework::spot_create_state_t::created
        || !factory_created.reply
        || factory_created.reply->decode<std::string> (manual_serializers) != "factory-reply"
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
    zlink::framework::detail::channel_runtime_t::from (lifecycle_host.message_bus ())
      .bind_serializers (manual_serializers);
    lifecycle_builder = lifecycle_host.add_spot_node ("lifecycle-stage");
    lifecycle_builder
      .add_entry_spot<entry_spot_t> ([lifecycle_entry_spot] { return lifecycle_entry_spot; })
      .add_actor_factory<player_actor_factory_t> ("player")
      .add_spot<stage_spot_t> ("stage", [lifecycle_stage_spot] { return lifecycle_stage_spot; });
    auto lifecycle_entry = lifecycle_builder.create_spot ("entry");
    auto lifecycle_stage = lifecycle_builder.create_spot ("stage");
    zlink::framework::entry_spot_context_t lifecycle_entry_context (lifecycle_entry.context);
    auto lifecycle_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (lifecycle_builder);
    zlink::framework::detail::actor_gateway_runtime_t lifecycle_gateway;
    zlink::framework::detail::actor_gateway_runtime_t missing_serializer_gateway;
    auto missing_serializer_create =
      missing_serializer_gateway.manager ().create ("player", "missing-player", std::string ("payload"));
    if (missing_serializer_create
        || missing_serializer_create.error_kind () != framework_error_kind_t::request_protocol_error) {
        return 107;
    }
    lifecycle_gateway.bind_serializers (manual_serializers);
    lifecycle_runtime.on_destroy_actor ([&] (const zlink::framework::actor_ref_t &actor_ref) {
        return lifecycle_gateway.destroy_actor (actor_ref);
    });
    lifecycle_runtime.on_actor_ref_updated ([&] (const zlink::framework::actor_ref_t &actor_ref) {
        return lifecycle_gateway.update_actor_ref (actor_ref);
    });
    auto lifecycle_actor =
      lifecycle_gateway.manager ()
        .create ("player", "joined-player", std::string ("entry-create-payload"))
        .value ();
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
        .join_spot (lifecycle_stage.spot_rid, std::string ("41"))
        .async ()
        .result ();
    if (!lifecycle_join || lifecycle_join.value ().result_code != 0
        || lifecycle_join.value ().reply.decode<std::string> (manual_serializers) != "42"
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
    auto erased_disconnect_spot = std::make_shared<erased_disconnect_spot_t> ();
    zlink::framework::zlink_builder_t erased_disconnect_host;
    auto erased_disconnect_builder = erased_disconnect_host.add_spot_node ("erased-stage");
    erased_disconnect_builder.add_actor_factory<player_actor_factory_t> ("player")
      .add_spot<erased_disconnect_spot_t> (
        "stage", [erased_disconnect_spot] { return erased_disconnect_spot; });
    auto erased_disconnect_stage = erased_disconnect_builder.create_spot ("stage");
    auto erased_disconnect_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (erased_disconnect_builder);
    auto erased_actor_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("remote-session"), "player", "erased-player", 3);
    auto erased_join = erased_disconnect_runtime.join_remote_actor_to_spot_erased (
      erased_actor_ref, erased_disconnect_stage.spot_rid,
      zlink::message_t::from (std::string ("44")));
    if (!erased_join || erased_join.value ().result_code != 0
        || erased_disconnect_spot->disconnected_count != 0) {
        return 104;
    }
    auto erased_disconnect =
      erased_disconnect_runtime.notify_actor_disconnected_erased (erased_join.value ().actor);
    if (!erased_disconnect || erased_disconnect_spot->disconnected_count != 1) {
        return 105;
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
        .join_spot (lifecycle_stage.spot_rid, std::string ("50"))
        .async ()
        .result ();
    lifecycle_stage_spot->accept_join = true;
    if (!rejected_runtime_join || rejected_runtime_join.value ().result_code == 0
        || rejected_runtime_join.value ().reply.decode<std::string> (manual_serializers) != "rejected"
        || lifecycle_stage_spot->joined_count != 1
        || rejected_context.actor_ref ().node_rid ().value () != "local") {
        return 61;
    }

    std::vector<std::string> lifecycle_entry_dispatch_payloads;
    lifecycle_gateway.on_join_entry_spot ([&] (const zlink::framework::actor_ref_t &actor_ref,
                                               zlink::framework::node_rid_t node_rid,
                                               const zlink::message_t &request) {
        lifecycle_entry_dispatch_payloads.push_back (request.to_string ());
        auto &actor_state =
          actor_ref.actor_id () == "destroy-player" ? destroyActor_state : lifecycle_actor_state;
        return lifecycle_runtime.join_actor_to_entry_spot<entry_spot_t> (
          actor_ref, std::move (node_rid), actor_state, request);
    });
    auto lifecycle_entry_join =
      lifecycle_actor_context
        .join_entry_spot (zlink::framework::node_rid_t::from_string ("lifecycle-stage"),
                          zlink::framework::message_t{})
        .async ()
        .result ();
    if (!lifecycle_entry_join || lifecycle_stage_spot->left_count != 1
        || lifecycle_entry_spot->joined_count != 1 || lifecycle_actor_state.moved_value != 100
        || lifecycle_entry_spot->created_count != 1
        || lifecycle_entry_dispatch_payloads.size () != 1
        || lifecycle_entry_dispatch_payloads[0] != "entry-create-payload"
        || lifecycle_entry_spot->created_payloads.size () != 1
        || lifecycle_entry_spot->created_payloads[0] != "entry-create-payload"
        || lifecycle_actor_state.joined_value != 152) {
        return 62;
    }
    auto lifecycle_entry_rejoin =
      lifecycle_actor_context
        .join_entry_spot (
          zlink::framework::node_rid_t::from_string ("lifecycle-stage"),
          std::string ("ignored-rejoin-payload"))
        .async ()
        .result ();
    if (!lifecycle_entry_rejoin || lifecycle_entry_spot->joined_count != 2
        || lifecycle_entry_spot->created_count != 1
        || lifecycle_entry_spot->created_payloads.size () != 1) {
        return 106;
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
        .join_spot (lifecycle_stage.spot_rid, std::string ("43"))
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
        .join_entry_spot (zlink::framework::node_rid_t::from_string ("lifecycle-stage"),
                              zlink::framework::message_t{})
        .async ()
        .result ();
    if (!destroy_entry_join || lifecycle_stage_spot->left_count != 2
        || lifecycle_entry_spot->joined_count != 3) {
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
        .join_spot (lifecycle_stage.spot_rid, std::string ("44"))
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

    auto auto_destroy_entry = std::make_shared<auto_destroy_entry_spot_t> ();
    auto auto_destroy_stage = std::make_shared<stage_spot_t> ();
    zlink::framework::spot_node_builder_t auto_destroy_builder;
    zlink::framework::zlink_builder_t auto_destroy_host;
    zlink::framework::detail::channel_runtime_t::from (auto_destroy_host.message_bus ())
      .bind_serializers (manual_serializers);
    auto_destroy_builder = auto_destroy_host.add_spot_node ("auto-destroy-stage");
    auto_destroy_builder
      .add_entry_spot<auto_destroy_entry_spot_t> (
        [auto_destroy_entry] { return auto_destroy_entry; })
      .add_actor_factory<player_actor_factory_t> ("player")
      .add_spot<stage_spot_t> ("stage", [auto_destroy_stage] { return auto_destroy_stage; });
    auto auto_destroy_entry_created = auto_destroy_builder.create_spot ("entry");
    auto auto_destroy_stage_created = auto_destroy_builder.create_spot ("stage");
    auto auto_destroy_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (auto_destroy_builder);
    zlink::framework::detail::actor_gateway_runtime_t auto_destroy_gateway;
    auto_destroy_gateway.bind_serializers (manual_serializers);
    player_actor_factory_t auto_destroy_actor_state;
    auto_destroy_runtime.on_destroy_actor ([&] (const zlink::framework::actor_ref_t &actor_ref) {
        return auto_destroy_gateway.destroy_actor (actor_ref);
    });
    auto_destroy_runtime.on_actor_ref_updated (
      [&] (const zlink::framework::actor_ref_t &actor_ref) {
          return auto_destroy_gateway.update_actor_ref (actor_ref);
      });
    auto_destroy_gateway.on_join_spot ([&] (const zlink::framework::actor_ref_t &actor_ref,
                                            zlink::framework::spot_rid_t spot_rid,
                                            const zlink::message_t &request) {
        return auto_destroy_runtime.join_actor_to_spot<stage_spot_t> (
          actor_ref, std::move (spot_rid), auto_destroy_actor_state, request);
    });
    auto_destroy_gateway.on_join_entry_spot ([&] (const zlink::framework::actor_ref_t &actor_ref,
                                                  zlink::framework::node_rid_t node_rid,
                                                  const zlink::message_t &request) {
        return auto_destroy_runtime.join_actor_to_entry_spot<auto_destroy_entry_spot_t> (
          actor_ref, std::move (node_rid), auto_destroy_actor_state, request);
    });
    auto auto_destroy_actor =
      auto_destroy_gateway.manager ().create ("player", "auto-destroy-player").value ();
    auto auto_destroy_context = auto_destroy_actor.context ();
    auto auto_destroy_initial_entry_join =
      auto_destroy_context
        .join_entry_spot (zlink::framework::node_rid_t::from_string ("auto-destroy-stage"),
                              zlink::framework::message_t{})
        .async ()
        .result ();
    if (!auto_destroy_initial_entry_join
        || auto_destroy_initial_entry_join.value ().result_code != 0
        || auto_destroy_entry->joined_count != 1 || auto_destroy_entry->destroyed_count != 0) {
        return 95;
    }
    auto auto_destroy_stage_join = auto_destroy_context
                                     .join_spot (auto_destroy_stage_created.spot_rid,
                                                     std::string ("46"))
                                     .async ()
                                     .result ();
    if (!auto_destroy_stage_join || auto_destroy_stage_join.value ().result_code != 0
        || auto_destroy_stage->joined_count != 1) {
        return 96;
    }
    auto_destroy_entry->destroy_on_join = true;
    auto auto_destroy_leave =
      auto_destroy_stage_created.context
        .leaveActor (auto_destroy_stage_join.value ().actor, auto_destroy_actor_state)
        .result ();
    if (!auto_destroy_leave) {
        return 97;
    }
    if (auto_destroy_entry->joined_count != 2) {
        return 98;
    }
    if (auto_destroy_entry->destroyed_count != 1) {
        return 99;
    }
    if (auto_destroy_gateway.manager ().find ("auto-destroy-player")) {
        return 100;
    }

    auto packet_leave_entry = std::make_shared<auto_destroy_entry_spot_t> ();
    auto packet_leave_spot = std::make_shared<actor_packet_self_leave_spot_t> ();
    zlink::framework::spot_node_builder_t packet_leave_builder;
    zlink::framework::zlink_builder_t packet_leave_host;
    zlink::framework::detail::channel_runtime_t::from (packet_leave_host.message_bus ())
      .bind_serializers (manual_serializers);
    packet_leave_builder = packet_leave_host.add_spot_node ("packet-leave-stage");
    packet_leave_builder
      .add_entry_spot<auto_destroy_entry_spot_t> (
        [packet_leave_entry] { return packet_leave_entry; })
      .add_actor_factory<player_actor_factory_t> ("player")
      .add_spot<actor_packet_self_leave_spot_t> ("stage",
                                                 [packet_leave_spot] { return packet_leave_spot; });
    auto packet_leave_stage_created = packet_leave_builder.create_spot ("stage");
    auto packet_leave_entry_created = packet_leave_builder.create_spot ("entry");
    auto packet_leave_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (packet_leave_builder);
    zlink::framework::detail::actor_gateway_runtime_t packet_leave_gateway;
    packet_leave_gateway.bind_serializers (manual_serializers);
    player_actor_factory_t packet_leave_actor_state;
    packet_leave_runtime.on_destroy_actor ([&] (const zlink::framework::actor_ref_t &actor_ref) {
        return packet_leave_gateway.destroy_actor (actor_ref);
    });
    packet_leave_runtime.on_actor_ref_updated (
      [&] (const zlink::framework::actor_ref_t &actor_ref) {
          return packet_leave_gateway.update_actor_ref (actor_ref);
      });
    packet_leave_gateway.on_join_entry_spot ([&] (const zlink::framework::actor_ref_t &actor_ref,
                                                  zlink::framework::node_rid_t node_rid,
                                                  const zlink::message_t &request) {
        return packet_leave_runtime.join_actor_to_entry_spot<auto_destroy_entry_spot_t> (
          actor_ref, std::move (node_rid), packet_leave_actor_state, request);
    });
    auto packet_leave_actor =
      packet_leave_gateway.manager ().create ("player", "packet-leave-player").value ();
    auto packet_leave_initial_join =
      packet_leave_actor.context ()
        .join_entry_spot (zlink::framework::node_rid_t::from_string ("packet-leave-stage"),
                              zlink::framework::message_t{})
        .async ()
        .result ();
    auto packet_leave_stage_join =
      packet_leave_runtime.join_actor_to_spot<actor_packet_self_leave_spot_t> (
        packet_leave_initial_join.value ().actor, packet_leave_stage_created.spot_rid,
        packet_leave_actor_state, zlink::message_t{});
    if (!packet_leave_stage_join || packet_leave_stage_join.value ().result_code != 0) {
        return 101;
    }
    zlink::framework::service_collection_t packet_leave_services;
    auto packet_leave_provider = packet_leave_services.build_provider ();
    zlink::framework::serializer_registry_t packet_leave_serializers;
    add_string_serializer (packet_leave_serializers);
    packet_leave_serializers.add<relay_request_t> (
      [] (const relay_request_t &value) {
          return zlink::framework::encoded_payload_t::from_string (std::to_string (value.value));
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return relay_request_t{std::stoi (payload.to_string ())};
      });
    packet_leave_serializers.add<relay_reply_t> (
      [] (const relay_reply_t &value) {
          return zlink::framework::encoded_payload_t::from_string (value.value);
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return relay_reply_t{payload.to_string ()};
      });
    packet_leave_entry->destroy_on_join = true;
    auto packet_leave_reply = packet_leave_runtime.relay_actor_packet (
      packet_leave_stage_join.value ().actor, zlink::framework::actor_context_t{}, "self.leave",
      zlink::message_t::from (std::string ("7")), packet_leave_provider, packet_leave_serializers);
    if (!packet_leave_reply || !packet_leave_reply.value ()
        || packet_leave_reply.value ()->to_string () != "7" || packet_leave_spot->left_count != 1
        || packet_leave_gateway.manager ().find ("packet-leave-player")) {
        return 102;
    }

    auto thread_probe_entry = std::make_shared<lifecycle_thread_probe_entry_spot_t> ();
    auto thread_probe_stage = std::make_shared<stage_spot_t> ();
    zlink::framework::spot_node_builder_t thread_probe_builder;
    zlink::framework::zlink_builder_t thread_probe_host;
    zlink::framework::detail::channel_runtime_t::from (thread_probe_host.message_bus ())
      .bind_serializers (manual_serializers);
    thread_probe_builder = thread_probe_host.add_spot_node ("thread-probe-stage");
    thread_probe_builder
      .add_entry_spot<lifecycle_thread_probe_entry_spot_t> (
        [thread_probe_entry] { return thread_probe_entry; })
      .add_actor_factory<player_actor_factory_t> ("player")
      .add_spot<stage_spot_t> ("stage", [thread_probe_stage] { return thread_probe_stage; });
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
      thread_probe_runtime.join_actor_to_entry_spot<lifecycle_thread_probe_entry_spot_t> (
        direct_entry_ref, zlink::framework::node_rid_t::from_string ("thread-probe-stage"),
        direct_entry_actor, zlink::message_t{});
    if (!direct_entry_join || thread_probe_entry->joined_count != 1
        || thread_probe_entry->last_join_thread == direct_entry_caller) {
        return 87;
    }

    player_actor_factory_t context_leave_actor;
    zlink::framework::actor_ref_t context_leave_ref (
      zlink::framework::node_rid_t::from_string ("thread-probe-stage"), "player",
      "context-leave-thread-player", 1);
    auto context_leave_stage_join = thread_probe_runtime.join_actor_to_spot<stage_spot_t> (
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
    auto relay_entry_spot = std::make_shared<relay_entry_spot_t> ();
    relay_builder.add_entry_spot<relay_entry_spot_t> (
                   [relay_entry_spot] { return relay_entry_spot; })
      .add_actor_factory<relay_actor_factory_t> ("relay-player")
      .add_spot<relay_spot_t> ("relay-room");
    (void) relay_builder.create_spot ("entry");
    auto relay_spot = relay_builder.create_spot ("relay-room");
    auto relay_runtime = zlink::framework::detail::spot_node_runtime_t::from (relay_builder);
    const auto routed_actor_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("play-a"), "relay-player", "routed-actor", 9);
    relay_spot_t::left_count = 0;
    auto routed_join = relay_runtime.join_remote_actor_to_spot_erased (
      routed_actor_ref, relay_spot.spot_rid, zlink::message_t{});
    if (!routed_join || routed_join.value ().result_code != 0
        || routed_join.value ().actor.node_rid ().value () != "play-a"
        || routed_join.value ().actor.generation () != 9) {
        return 86;
    }
    const auto routed_instance = relay_runtime.actor_instance<relay_actor_t> (routed_actor_ref);
    if (!routed_instance || routed_instance->get ().actor_id != "routed-actor") {
        return 87;
    }
    auto remote_leave =
      relay_spot.context.leaveActor (routed_join.value ().actor, routed_instance->get ()).result ();
    if (!remote_leave || remote_leave.value ().node_rid ().value () != "play-a"
        || relay_spot.context.manager ().current_actor_ref (routed_actor_ref)
        || relay_runtime.actor_spot (routed_actor_ref) || relay_spot_t::left_count == 0) {
        return 82;
    }
    zlink::framework::serializer_registry_t route_join_serializers;
    zlink::framework::detail::register_spot_route_packet_serializers (route_join_serializers);
    zlink::framework::service_collection_t route_join_services;
    auto route_join_provider = route_join_services.build_provider ();
    zlink::framework::detail::route_handler_registry_t route_join_handlers;
    zlink::framework::detail::actor_gateway_runtime_t route_join_actor_gateway;
    zlink::framework::detail::spot_route_internal_dispatcher_t route_join_internal (
      relay_runtime, route_join_actor_gateway, relay_host.route_client (route_join_serializers),
      route_join_serializers);
    zlink::framework::detail::route_packet_dispatcher_t route_join_dispatcher (
      "relay.route", route_join_provider, route_join_serializers, route_join_handlers,
      route_join_internal);
    zlink::framework::runtime::messaging::envelope_codec_t route_join_envelope;
    zlink::framework::runtime::messaging::envelope_header_t route_join_header;
    route_join_header.kind = zlink::framework::runtime::messaging::message_kind_t::request;
    route_join_header.channel_name = "relay.route";
    route_join_header.message_name =
      zlink::framework::detail::spot_actor_join_route_request_t::packet_name;
    const auto route_join_request = zlink::framework::detail::make_spot_actor_join_route_request (
      zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("play-b"),
                                     "relay-player", "routed-through-channel", 3),
      relay_spot.spot_rid, zlink::message_t::from (std::string ("route-payload")));
    auto route_join_parts = route_join_envelope.encode_parts (
      route_join_header,
      std::type_index (typeid (zlink::framework::detail::spot_actor_join_route_request_t)),
      &route_join_request, route_join_serializers);
    auto route_join_dispatch =
      route_join_dispatcher.dispatch (zlink::framework::detail::route_received_packet_t{
        zlink::routing_id_t::from (std::string ("play-b")), 90, route_join_parts});
    if (!route_join_dispatch || !route_join_dispatch.value ()
        || !route_join_dispatch.value ()->request_seq
        || route_join_dispatch.value ()->request_seq.value () != 90) {
        return 90;
    }
    auto route_join_reply_body =
      route_join_envelope.decode_body (route_join_dispatch.value ()->parts);
    if (!route_join_reply_body) {
        return 91;
    }
    const auto route_join_reply =
      route_join_serializers.get<zlink::framework::detail::spot_actor_join_route_reply_t> ()
        .deserialize (zlink::framework::detail::encoded_payload_from_raw (route_join_reply_body.value ()));
    if (route_join_reply.result_code != 0 || route_join_reply.actor_node_rid != "play-b"
        || route_join_reply.actor_generation != 3) {
        return 92;
    }
    const auto routed_channel_instance = relay_runtime.actor_instance<relay_actor_t> (
      zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("play-b"),
                                     "relay-player", "routed-through-channel", 3));
    if (!routed_channel_instance
        || routed_channel_instance->get ().actor_id != "routed-through-channel") {
        return 93;
    }
    if (routed_channel_instance->get ().context.actor_ref ().actor_id ()
        != "routed-through-channel") {
        return 94;
    }
    if (!route_join_actor_gateway.actor_bound ("routed-through-channel")) {
        return 95;
    }
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
    add_string_serializer (relay_serializers);
    relay_serializers.add<relay_request_t> (
      [] (const relay_request_t &value) {
          return zlink::framework::encoded_payload_t::from_string (std::to_string (value.value));
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return relay_request_t{std::stoi (payload.to_string ())};
      });
    relay_serializers.add<relay_reply_t> (
      [] (const relay_reply_t &value) {
          return zlink::framework::encoded_payload_t::from_string (value.value);
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return relay_reply_t{payload.to_string ()};
      });
    auto relay_dispatch = relay_runtime.relay_actor_packet (
      relay_join.value ().actor, zlink::framework::actor_context_t{}, "relay.request",
      zlink::message_t::from (std::string ("64")), relay_provider, relay_serializers);
    if (!relay_dispatch || !relay_dispatch.value ()
        || relay_dispatch.value ()->to_string () != "relay-actor:64") {
        return 83;
    }
    zlink::framework::detail::actor_gateway_runtime_t lazy_relay_gateway;
    lazy_relay_gateway.bind_serializers (relay_serializers);
    auto lazy_relay_actor =
      lazy_relay_gateway.manager ()
        .create ("relay-player", "lazy-relay", std::string ("lazy-create"))
        .value ();
    auto lazy_relay_dispatch = relay_runtime.relay_actor_packet (
      lazy_relay_actor.ref (), lazy_relay_actor.context (), "relay.request",
      zlink::message_t::from (std::string ("65")), relay_provider, relay_serializers);
    if (!lazy_relay_dispatch) {
        return 108;
    }
    if (!lazy_relay_dispatch.value ()) {
        return 110;
    }
    if (lazy_relay_dispatch.value ()->to_string () != "lazy-relay:65") {
        return 111;
    }
    if (relay_entry_spot->created_count != 1) {
        return 112;
    }
    if (relay_entry_spot->created_payloads.size () != 1) {
        return 113;
    }
    if (relay_entry_spot->created_payloads[0] != "lazy-create") {
        return 114;
    }
    if (relay_entry_spot->joined_count != 1) {
        return 115;
    }
    auto lazy_relay_dispatch_again = relay_runtime.relay_actor_packet (
      lazy_relay_actor.ref (), lazy_relay_actor.context (), "relay.request",
      zlink::message_t::from (std::string ("66")), relay_provider, relay_serializers);
    if (!lazy_relay_dispatch_again || relay_entry_spot->created_count != 1
        || relay_entry_spot->joined_count != 1) {
        return 109;
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
    if (lifecycle_actor_state.moved_value != 120) {
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

    zlink::framework::spot_node_builder_t registry_alias;
    registry_alias.use_registry_spot_resolver ("alias.route");
    if (!registry_alias.snapshot ().registry_spot_route_channel
        || *registry_alias.snapshot ().registry_spot_route_channel != "alias.route") {
        return 90;
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

    zlink::framework::spot_node_builder_t peer_pub_alias;
    peer_pub_alias.connect_peer_pub ("tcp://127.0.0.1:9006");
    if (peer_pub_alias.snapshot ().pub_sub_manual_connections.size () != 1
        || peer_pub_alias.snapshot ().pub_sub_manual_connections[0] != "tcp://127.0.0.1:9006") {
        return 410;
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
    add_string_serializer (spot_serializers);
    spot_serializers.add<state_update_t> (
      [] (const state_update_t &value) {
          return zlink::framework::encoded_payload_t::from_string (std::to_string (value.value));
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return state_update_t{std::stoi (payload.to_string ())};
      });
    spot_serializers.add<move_request_t> (
      [] (const move_request_t &value) {
          return zlink::framework::encoded_payload_t::from_string (std::to_string (value.value));
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return move_request_t{std::stoi (payload.to_string ())};
      });
    spot_serializers.add<move_reply_t> (
      [] (const move_reply_t &value) {
          return zlink::framework::encoded_payload_t::from_string (std::to_string (value.value));
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return move_reply_t{std::stoi (payload.to_string ())};
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
      stage_spot.on_actor_join (actor, zlink::framework::message_t::from (std::string ("41")));
    if (!join_dispatch.accepted || !join_dispatch.reply
        || join_dispatch.reply->decode<std::string> (spot_serializers) != "42"
        || actor.joined_value != 41 || stage_spot.join_seen != 41) {
        return 23;
    }
    stage_spot.on_actor_joined (actor);
    if (stage_spot.joined_count != 1 || actor.joined_value != 141) {
        return 25;
    }
    stage_spot.accept_join = false;
    const auto rejected_join =
      stage_spot.on_actor_join (actor, zlink::framework::message_t::from (std::string ("50")));
    if (rejected_join.accepted || !rejected_join.reply
        || rejected_join.reply->decode<std::string> (spot_serializers) != "rejected") {
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

    auto async_scheduler = std::make_shared<controlled_worker_scheduler_t> ();
    auto async_state = std::make_shared<zlink::framework::detail::spot_context_state_t> ();
    async_state->serial_executor =
      std::make_shared<zlink::framework::runtime::offload_executor_t> (1);
    async_state->serial_queue =
      std::make_shared<zlink::framework::runtime::serial_execution_queue_t> (
        *async_state->serial_executor);
    async_state->worker_scheduler = async_scheduler;
    auto async_context = test_spot_context_t (async_state);
    async_probe_spot_t async_spot;
    async_spot.configure (async_context);
    player_actor_factory_t async_actor;
    auto invoke_async = [&] (std::string_view packet_name, int value) {
        return async_context.handlers ().invoke_actor_packet (
          packet_name, async_spot, async_actor, spot_provider, spot_serializers,
          zlink::message_t::from (std::to_string (value)));
    };
    auto slow_future =
      std::async (std::launch::async, [&] { return invoke_async ("async.slow", 1); });
    if (!async_spot.wait_slow_started () || !async_scheduler->wait_worker_job_count (1)) {
        return 52;
    }
    auto quick_future =
      std::async (std::launch::async, [&] { return invoke_async ("async.quick", 9); });
    if (quick_future.wait_for (std::chrono::milliseconds (500)) != std::future_status::ready) {
        return 53;
    }
    const auto quick_result = quick_future.get ();
    if (!quick_result
        || spot_serializers.get<move_reply_t> ().deserialize (zlink::framework::detail::encoded_payload_from_raw (quick_result.value ())).value != 10
        || async_spot.quick_seen () != 1) {
        return 54;
    }
    if (slow_future.wait_for (std::chrono::milliseconds (50)) == std::future_status::ready) {
        return 55;
    }
    async_scheduler->run_worker_job ();
    async_scheduler->run_owner_job ();
    if (slow_future.wait_for (std::chrono::milliseconds (500)) != std::future_status::ready) {
        return 56;
    }
    const auto slow_result = slow_future.get ();
    if (!slow_result
        || spot_serializers.get<move_reply_t> ().deserialize (zlink::framework::detail::encoded_payload_from_raw (slow_result.value ())).value != 78) {
        return 57;
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

    zlink::framework::zlink_builder_t pubsub_host;
    zlink::framework::detail::channel_runtime_t::from (pubsub_host.message_bus ())
      .bind_serializers (spot_serializers);
    auto subscription_spot = std::make_shared<subscription_spot_t> ();
    auto pubsub_builder = pubsub_host.add_spot_node ("pubsub-node");
    pubsub_builder.enable_pub_sub ("inproc://cpp-framework-spot-pubsub")
      .add_spot<subscription_spot_t> ("subscription",
                                      [subscription_spot] { return subscription_spot; });
    auto pubsub_created = pubsub_builder.create_spot ("subscription");
    auto pubsub_runtime = zlink::framework::detail::spot_node_runtime_t::from (pubsub_builder);
    zlink::context_t native_context;
    auto native_node = std::make_shared<zlink::service::spot_node_t> (native_context);
    native_node->set_pub_bind ("inproc://cpp-framework-spot-pubsub");
    pubsub_runtime.attach_native_node (native_node);
    auto native_publish =
      pubsub_created.context.publish ("stage.state.updated", state_update_t{9}).async ().result ();
    if (!native_publish) {
        return 91;
    }
    const auto subscription_deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (500);
    while (subscription_spot->last_value != 9
           && std::chrono::steady_clock::now () < subscription_deadline) {
        (void) pubsub_runtime.drain_subscriptions (spot_provider, spot_serializers);
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    pubsub_runtime.detach_native_node ();
    native_node->close ();
    native_context.shutdown ();
    native_context.term ();
    if (subscription_spot->last_value != 9) {
        return 92;
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
    const auto state_update_packet = zlink::framework::detail::message_name<state_update_t> ();
    if (ordering.size () != 3
        || ordering[0] != "publish:stage.state.updated:" + state_update_packet + ":1"
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
