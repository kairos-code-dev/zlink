/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>
#include <zlink.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/channels/route_handler_registry.hpp"
#include "runtime/channels/route_packet_dispatcher.hpp"
#include "runtime/locations/in_memory_location_store.hpp"
#include "runtime/locations/location_lifecycle.hpp"
#include "runtime/locations/location_runtime.hpp"
#include "runtime/spots/spot_route_internal_dispatcher.hpp"
#include "runtime/spots/spot_route_packets.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <algorithm>
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

class recording_spot_location_resolver_t final
    : public zlink::framework::spot_location_resolver_t
{
  public:
    zlink::framework::task_t<std::optional<zlink::framework::spot_address_t>>
    resolve_spot_address (std::string mesh_name, zlink::routing_id_t spot_rid) override
    {
        last_mesh_name = std::move (mesh_name);
        last_spot_rid = spot_rid.to_string ();
        ++calls;
        return zlink::framework::task_t<std::optional<zlink::framework::spot_address_t>> (
          zlink::framework::result_t<std::optional<zlink::framework::spot_address_t>>::success (
            address));
    }

    std::optional<zlink::framework::spot_address_t> address;
    int calls = 0;
    std::string last_mesh_name;
    std::string last_spot_rid;
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

    void onDisconnectActor (relay_actor_t &) { disconnected_count++; }

    relay_reply_t on_relay (relay_actor_t &actor,
                            zlink::framework::spot_actor_request_context_t &,
                            const relay_request_t &request)
    {
        return {actor.actor_id + ":" + std::to_string (request.value)};
    }

    static inline int left_count{};
    static inline int disconnected_count{};
};

struct entry_dispatch_probe_actor_t
{
    std::string actor_id;
    zlink::framework::actor_context_t context;
    int handled{};

    void set_actor_ref (const zlink::framework::actor_ref_t &actor_ref)
    {
        actor_id = std::string (actor_ref.actor_id ());
    }

    void set_actor_context (const zlink::framework::actor_context_t &actor_context)
    {
        context = actor_context;
    }
};

struct entry_dispatch_probe_actor_factory_t
{
    entry_dispatch_probe_actor_t create (std::string actor_id) const
    {
        return {std::move (actor_id)};
    }
};

struct entry_dispatch_probe_spot_t : public zlink::framework::entry_spot_t
{
    void configure (zlink::framework::entry_spot_context_t &context)
    {
        context.handlers ()
          .add_actor_packet<&entry_dispatch_probe_spot_t::on_block> ("entry.block")
          .add_actor_packet<&entry_dispatch_probe_spot_t::on_yield> ("entry.yield");
    }

    void onCreateActor (entry_dispatch_probe_actor_t &, const zlink::framework::message_t &)
    {
        ++created_count;
    }

    void on_actor_joined (entry_dispatch_probe_actor_t &) { ++joined_count; }

    relay_reply_t on_block (entry_dispatch_probe_actor_t &actor,
                            zlink::framework::spot_actor_request_context_t &,
                            const relay_request_t &request)
    {
        {
            std::unique_lock lock (mutex);
            ++running;
            max_running = std::max (max_running, running);
            starts.push_back (actor.actor_id + ":" + std::to_string (request.value));
            changed.notify_all ();
            if (request.value == 1) {
                changed.wait (lock, [this] { return release_first; });
            }
        }
        actor.handled += 1;
        {
            std::lock_guard lock (mutex);
            --running;
            changed.notify_all ();
        }
        return {actor.actor_id + ":" + std::to_string (actor.handled)};
    }

    zlink::framework::task_t<relay_reply_t>
    on_yield (entry_dispatch_probe_actor_t &,
              zlink::framework::spot_actor_request_context_t &,
              const relay_request_t &)
    {
        zlink::framework::request_call_t<int> call (
          "entry.yield",
          [] (const std::string &, std::chrono::milliseconds,
              const zlink::framework::request_call_t<int>::metadata_map_t &) {
              return zlink::framework::task_t<int> (
                zlink::framework::result_t<int>::failure (
                  zlink::framework::framework_error_kind_t::request_failed, "should not submit"));
          });
        const auto value = co_await call.yield ();
        co_return relay_reply_t{std::to_string (value)};
    }

    bool wait_starts (std::size_t expected)
    {
        std::unique_lock lock (mutex);
        return changed.wait_for (lock, std::chrono::milliseconds (500),
                                 [&] { return starts.size () >= expected; });
    }

    void release ()
    {
        std::lock_guard lock (mutex);
        release_first = true;
        changed.notify_all ();
    }

    mutable std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::string> starts;
    bool release_first = false;
    int running = 0;
    int max_running = 0;
    int created_count = 0;
    int joined_count = 0;
};

struct stage_spot_t : public zlink::framework::spot_t
{
    void on_state_update (const state_update_t &message)
    {
        last_value = message.value;
        packet_seen = message.value;
    }

    void on_state_update_context (const zlink::framework::spot_packet_context_t &context,
                                  const state_update_t &message)
    {
        last_value = message.value;
        packet_seen = message.value;
        last_packet_content_type = context.content_type;
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
        last_actor_content_type = context.content_type;
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
    std::string last_packet_content_type;
    std::string last_actor_content_type;
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
          .add_actor_packet<&async_probe_spot_t::slow_yield> ("async.slow-yield")
          .add_actor_packet<&async_probe_spot_t::request_yield> ("async.request-yield")
          .add_actor_packet<&async_probe_spot_t::join_yield> ("async.join-yield")
          .add_actor_packet<&async_probe_spot_t::quick> ("async.quick");
    }

    void set_join_context (zlink::framework::actor_context_t context)
    {
        _join_context = std::move (context);
    }

    zlink::framework::task_t<move_reply_t>
    slow_yield (player_actor_factory_t &,
                zlink::framework::spot_actor_request_context_t &context,
                const move_request_t &request)
    {
        {
            std::lock_guard lock (mutex);
            slow_started = true;
            changed.notify_all ();
        }
        const auto value = co_await _context.run_worker ([] { return 77; }).yield ();
        {
            std::lock_guard lock (mutex);
            slow_completed = true;
            changed.notify_all ();
        }
        co_return move_reply_t{value + request.value
                               + (context.packet_name == "async.slow-yield" ? 0 : 1000)};
    }

    zlink::framework::task_t<move_reply_t>
    request_yield (player_actor_factory_t &,
                   zlink::framework::spot_actor_request_context_t &,
                   const move_request_t &request)
    {
        auto source =
          std::make_shared<zlink::framework::detail::task_completion_source_t<int>> ();
        {
            std::lock_guard lock (mutex);
            request_source = source;
            slow_started = true;
            changed.notify_all ();
        }
        zlink::framework::request_call_t<int> call (
          "async.request-yield",
          [source] (const std::string &, std::chrono::milliseconds,
                    const zlink::framework::request_call_t<int>::metadata_map_t &) {
              return source->task ();
          });
        const auto value = co_await call.yield ();
        {
            std::lock_guard lock (mutex);
            slow_completed = true;
            changed.notify_all ();
        }
        co_return move_reply_t{value + request.value};
    }

    zlink::framework::task_t<move_reply_t>
    join_yield (player_actor_factory_t &,
                zlink::framework::spot_actor_request_context_t &,
                const move_request_t &request)
    {
        {
            std::lock_guard lock (mutex);
            slow_started = true;
            changed.notify_all ();
        }
        const auto joined = co_await _join_context
                              .join_spot (
                                zlink::framework::spot_rid_t::from_string ("async-join-target"),
                                zlink::framework::message_t::from (std::string ("join")))
                              .yield ();
        {
            std::lock_guard lock (mutex);
            slow_completed = true;
            changed.notify_all ();
        }
        co_return move_reply_t{static_cast<int> (joined.actor.generation ()) + request.value};
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

    void reset_probe ()
    {
        std::lock_guard lock (mutex);
        slow_started = false;
        slow_completed = false;
        quick_count = 0;
        request_source.reset ();
    }

    std::shared_ptr<zlink::framework::detail::task_completion_source_t<int>>
    wait_request_source ()
    {
        std::unique_lock lock (mutex);
        const auto ready = changed.wait_for (lock, std::chrono::milliseconds (500),
                                             [this] { return request_source != nullptr; });
        return ready ? request_source : nullptr;
    }

    mutable std::mutex mutex;
    std::condition_variable changed;
    zlink::framework::spot_context_t _context;
    std::shared_ptr<zlink::framework::detail::task_completion_source_t<int>> request_source;
    zlink::framework::actor_context_t _join_context;
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
    channel.enable_subscriber ();
    zlink.add_spot_node ("stage-spot-node")
      .bind ("tcp://0.0.0.0:9000")
      .enable_router ("tcp://0.0.0.0:9002")
      .connect_router ("tcp://127.0.0.1:9003")
      .enable_pub_sub ("tcp://0.0.0.0:9004")
      .connect_pub_sub ("tcp://127.0.0.1:9005")
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
        || !snapshots[0].discovery_channel_name
        || *snapshots[0].discovery_channel_name != "stage-spot-node"
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
    auto context_manager = context.manager ();
    const auto manager_created = context_manager.create_spot ("stage");
    if (manager_created.state != zlink::framework::spot_create_state_t::created
        || manager_created.context.spot_name () != "stage"
        || !context_manager.find_spot (manager_created.spot_rid)
        || context_manager.spot_name_for (manager_created.spot_rid) != "stage"
        || !context_manager.resolve_spot (manager_created.spot_rid)
        || context_manager.list_spots ().empty ()) {
        return 133;
    }
    const auto manager_close = context_manager.close_spot (manager_created.spot_rid).result ();
    if (!manager_close || !manager_close.value ()
        || context_manager.find_spot (manager_created.spot_rid)) {
        return 135;
    }
    const auto manager_typed_create_count = stage_spot_t::create_count;
    auto manager_typed_created =
      context_manager.create_spot ("stage", std::string ("manager-create-request"));
    if (manager_typed_created.state != zlink::framework::spot_create_state_t::created
        || stage_spot_t::create_count != manager_typed_create_count + 1
        || stage_spot_t::last_create_request != "manager-create-request") {
        return 137;
    }
    const auto manager_requested_rid =
      zlink::framework::spot_rid_t::from_string ("manager-requested-stage");
    auto manager_get_or_create =
      context_manager.get_or_create_spot ("stage", manager_requested_rid,
                                          std::string ("manager-get-request"));
    auto manager_existing = context_manager.get_or_create_spot (
      "stage", manager_requested_rid, std::string ("manager-ignored-request"));
    if (manager_get_or_create.state != zlink::framework::spot_create_state_t::created
        || manager_existing.state != zlink::framework::spot_create_state_t::existing
        || manager_get_or_create.spot_rid.value () != "manager-requested-stage"
        || manager_existing.spot_rid.value () != "manager-requested-stage"
        || !context_manager.find_spot (manager_requested_rid)) {
        return 138;
    }
    if (!context_manager.close_spot (manager_typed_created.spot_rid).result ().value ()
        || !context_manager.close_spot (manager_requested_rid).result ().value ()) {
        return 139;
    }
    zlink::framework::spot_node_manager_t empty_manager;
    bool empty_manager_create_failed = false;
    try {
        (void) empty_manager.create_spot ("missing");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_manager_create_failed =
          error.kind () == zlink::framework::framework_error_kind_t::spot_create_failed;
    }
    if (!empty_manager_create_failed || empty_manager.find_spot (context.spot_rid ())
                                          || !empty_manager.list_spots ().empty ()) {
        return 136;
    }
    context
      .send_to (zlink::framework::node_rid_t{}, zlink::framework::spot_rid_t{},
                move_request_t{1})
      .packet_name ("move")
      .submit ();
    const auto empty_spot_route_request =
      context
        .request_to<move_reply_t> (zlink::framework::node_rid_t{},
                                   zlink::framework::spot_rid_t{}, move_request_t{1})
        .packet_name ("move")
        .async ()
        .result ();
    if (empty_spot_route_request
        || empty_spot_route_request.error_kind ()
             != zlink::framework::framework_error_kind_t::spot_route_not_found) {
        return 112;
    }
    zlink::framework::zlink_builder_t no_route_host;
    auto no_route_builder = no_route_host.add_spot_node ("no-route-stage");
    zlink::framework::detail::channel_runtime_t::from (no_route_host.message_bus ())
      .bind_serializers (manual_serializers);
    no_route_builder.add_spot<stage_spot_t> ("stage");
    auto no_route_spot = no_route_builder.create_spot ("stage");
    auto no_route_context = no_route_spot.context;
    const auto no_route_request =
      no_route_context
        .request_to<move_reply_t> (zlink::framework::node_rid_t::from_string ("remote-node"),
                                   zlink::framework::spot_rid_t::from_string ("remote-spot"),
                                   move_request_t{2})
        .packet_name ("move")
        .async ()
        .result ();
    if (no_route_request
        || no_route_request.error_kind ()
             != zlink::framework::framework_error_kind_t::spot_route_not_found) {
        return 113;
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
    const auto location_resolved_rid =
      zlink::framework::spot_rid_t::from_string ("location-stage");
    recording_spot_location_resolver_t location_resolver;
    location_resolver.address = zlink::framework::spot_address_t{
      "manual-stage", zlink::routing_id_t::from ("location-node"),
      zlink::routing_id_t::from ("location-stage")};
    auto builder_runtime = zlink::framework::detail::spot_node_runtime_t::from (builder);
    builder_runtime.bind_spot_location_resolver (location_resolver);
    const auto location_resolved_route = builder_runtime.resolve_spot (location_resolved_rid);
    if (!location_resolved_route
        || location_resolved_route->node_rid.value () != "location-node"
        || location_resolved_route->spot_rid.value () != "location-stage"
        || location_resolver.calls != 1 || location_resolver.last_mesh_name != "manual-stage"
        || location_resolver.last_spot_rid != "location-stage") {
        return 114;
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

    zlink::framework::runtime::in_memory_location_store_t spot_location_store;
    zlink::framework::runtime::location_runtime_t spot_location_runtime (
      spot_location_store, {}, "spot-runtime-owner");
    spot_location_runtime.start (zlink::routing_id_t::from ("spot-runtime-node"));
    zlink::framework::runtime::location_lifecycle_t spot_location_lifecycle (
      spot_location_runtime);
    zlink::framework::zlink_builder_t location_bound_host;
    auto location_bound_builder = location_bound_host.add_spot_node ("location-bound-node");
    location_bound_builder.set_routing_id (zlink::routing_id_t::from ("location-bound-rid"))
      .add_spot<stage_spot_t> ("stage");
    auto location_bound_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (location_bound_builder);
    location_bound_runtime.bind_location_lifecycle (spot_location_lifecycle);
    auto location_bound_stage = location_bound_runtime.create_spot ("stage");
    if (location_bound_stage.state != zlink::framework::spot_create_state_t::created) {
        spot_location_runtime.stop ();
        return 108;
    }
    auto stored_spot =
      spot_location_store
        .resolve_spot (zlink::framework::spot_location_key_t{
          .mesh_name = "location-bound-node",
          .spot_rid = zlink::routing_id_t::from (
            std::string (location_bound_stage.spot_rid.value ()))})
        .result ()
        .value ();
    if (!stored_spot || stored_spot->owner_id != "spot-runtime-owner"
        || stored_spot->spot_type != "stage"
        || stored_spot->node_rid.to_string () != "location-bound-rid") {
        spot_location_runtime.stop ();
        return 109;
    }
    if (!location_bound_runtime.close_spot (location_bound_stage.spot_rid).result ().value ()) {
        spot_location_runtime.stop ();
        return 110;
    }
    stored_spot =
      spot_location_store
        .resolve_spot (zlink::framework::spot_location_key_t{
          .mesh_name = "location-bound-node",
          .spot_rid = zlink::routing_id_t::from (
            std::string (location_bound_stage.spot_rid.value ()))})
        .result ()
        .value ();
    if (stored_spot.has_value ()) {
        spot_location_runtime.stop ();
        return 111;
    }
    spot_location_runtime.stop ();

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
    const auto erased_empty_disconnect =
      erased_disconnect_runtime.notify_actor_disconnected_erased (zlink::framework::actor_ref_t (
        zlink::framework::node_rid_t{}, "", "", 0));
    if (erased_empty_disconnect
        || erased_empty_disconnect.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 130;
    }
    const auto erased_stale_disconnect =
      erased_disconnect_runtime.notify_actor_disconnected_erased (zlink::framework::actor_ref_t (
        zlink::framework::node_rid_t::from_string ("remote-session"), "player", "erased-player", 2));
    if (erased_stale_disconnect
        || erased_stale_disconnect.error_kind () != framework_error_kind_t::actor_stale_generation) {
        return 131;
    }
    const auto erased_missing_disconnect =
      erased_disconnect_runtime.notify_actor_disconnected_erased (zlink::framework::actor_ref_t (
        zlink::framework::node_rid_t::from_string ("remote-session"), "player", "missing-player", 1));
    if (!erased_missing_disconnect) {
        return 132;
    }
    erased_disconnect_runtime.record_actor_spot (
      zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("remote-session"),
                                     "player", "missing-context-player", 1),
      zlink::framework::spot_rid_t::from_string ("missing-spot"));
    const auto erased_missing_context_disconnect =
      erased_disconnect_runtime.notify_actor_disconnected_erased (zlink::framework::actor_ref_t (
        zlink::framework::node_rid_t::from_string ("remote-session"), "player",
        "missing-context-player", 1));
    if (!erased_missing_context_disconnect) {
        return 133;
    }
    zlink::framework::zlink_builder_t missing_factory_host;
    auto missing_factory_builder = missing_factory_host.add_spot_node ("missing-factory-stage");
    missing_factory_builder.add_spot<erased_disconnect_spot_t> ("stage");
    auto missing_factory_stage = missing_factory_builder.create_spot ("stage");
    auto missing_factory_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (missing_factory_builder);
    const auto missing_factory_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("remote-session"), "unknown", "missing-factory",
      1);
    missing_factory_runtime.record_actor_spot (missing_factory_ref, missing_factory_stage.spot_rid);
    const auto erased_missing_factory_disconnect =
      missing_factory_runtime.notify_actor_disconnected_erased (missing_factory_ref);
    if (erased_missing_factory_disconnect
        || erased_missing_factory_disconnect.error_kind ()
             != framework_error_kind_t::actor_route_not_found) {
        return 134;
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
    auto empty_destroy =
      lifecycle_entry_context.destroyActor (zlink::framework::actor_ref_t{}, destroyActor_state)
        .result ();
    if (empty_destroy
        || empty_destroy.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 107;
    }
    auto wrong_owner_destroy = lifecycle_entry_context
                                 .destroyActor (
                                   zlink::framework::actor_ref_t (
                                     zlink::framework::node_rid_t::from_string ("other-node"),
                                     "player", "destroy-player", 1),
                                   destroyActor_state)
                                 .result ();
    if (wrong_owner_destroy
        || wrong_owner_destroy.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 108;
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
    auto stale_destroy = lifecycle_entry_context
                           .destroyActor (
                             zlink::framework::actor_ref_t (
                               destroy_context.actor_ref ().node_rid (),
                               std::string (destroy_context.actor_ref ().actor_type ()),
                               std::string (destroy_context.actor_ref ().actor_id ()),
                               destroy_context.actor_ref ().generation () + 1),
                             destroyActor_state)
                           .result ();
    if (!stale_destroy || !lifecycle_gateway.manager ().find ("destroy-player")) {
        return 109;
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
    zlink::framework::service_collection_t post_destroy_services;
    auto post_destroy_provider = post_destroy_services.build_provider ();
    zlink::framework::serializer_registry_t post_destroy_serializers;
    add_string_serializer (post_destroy_serializers);
    const auto created_before_post_destroy_relay = lifecycle_entry_spot->created_count;
    const auto joined_before_post_destroy_relay = lifecycle_entry_spot->joined_count;
    auto post_destroy_relay = lifecycle_runtime.relay_actor_packet (
      destroy_context.actor_ref (), destroy_context, "missing.after.destroy",
      zlink::message_t::from (std::string ("after-destroy")), post_destroy_provider,
      post_destroy_serializers);
    if (post_destroy_relay
        || post_destroy_relay.error_kind () != framework_error_kind_t::actor_route_not_found
        || lifecycle_entry_spot->created_count != created_before_post_destroy_relay
        || lifecycle_entry_spot->joined_count != joined_before_post_destroy_relay) {
        return 120;
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
    auto empty_leave =
      lifecycle_stage.context.leaveActor (zlink::framework::actor_ref_t{}, leaveActor_state).result ();
    if (empty_leave
        || empty_leave.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 110;
    }
    auto wrong_spot_leave =
      lifecycle_stage.context.leaveActor (lifecycle_actor_context.actor_ref (), leaveActor_state)
        .result ();
    if (wrong_spot_leave
        || wrong_spot_leave.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 111;
    }
    auto stale_leave = lifecycle_stage.context
                         .leaveActor (
                           zlink::framework::actor_ref_t (
                             leave_context.actor_ref ().node_rid (),
                             std::string (leave_context.actor_ref ().actor_type ()),
                             std::string (leave_context.actor_ref ().actor_id ()),
                             leave_context.actor_ref ().generation () + 1),
                           leaveActor_state)
                         .result ();
    if (stale_leave
        || stale_leave.error_kind () != framework_error_kind_t::actor_stale_generation) {
        return 112;
    }
    auto unjoined_actor =
      lifecycle_gateway.manager ().create ("player", "context-unjoined-player").value ();
    auto unjoined_leave =
      lifecycle_stage.context.leaveActor (unjoined_actor.context ().actor_ref (), leaveActor_state)
        .result ();
    if (!unjoined_leave) {
        return 113;
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
    relay_spot_t::disconnected_count = 0;
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
    route_join_serializers.add<relay_request_t> (
      [] (const relay_request_t &value) {
          return zlink::framework::encoded_payload_t::from_string (std::to_string (value.value));
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return relay_request_t{std::stoi (payload.to_string ())};
      });
    route_join_serializers.add<relay_reply_t> (
      [] (const relay_reply_t &value) {
          return zlink::framework::encoded_payload_t::from_string (value.value);
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return relay_reply_t{payload.to_string ()};
      });
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
    zlink::framework::spot_actor_message_metadata_t route_packet_metadata;
    route_packet_metadata.content_type = "application/x-msgpack";
    route_packet_metadata.values.emplace ("trace", "route-packet");
    const auto route_packet_request =
      zlink::framework::detail::make_spot_actor_packet_route_request (
        zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("play-b"),
                                       "relay-player", "routed-through-channel", 3),
        relay_spot.spot_rid, "relay.request", zlink::message_t::from (std::string ("71")),
        route_packet_metadata);
    zlink::framework::runtime::messaging::envelope_header_t route_packet_header;
    route_packet_header.kind = zlink::framework::runtime::messaging::message_kind_t::request;
    route_packet_header.channel_name = "relay.route";
    route_packet_header.message_name =
      zlink::framework::detail::spot_actor_packet_route_request_t::packet_name;
    auto route_packet_parts = route_join_envelope.encode_parts (
      route_packet_header,
      std::type_index (typeid (zlink::framework::detail::spot_actor_packet_route_request_t)),
      &route_packet_request, route_join_serializers);
    auto route_packet_dispatch =
      route_join_dispatcher.dispatch (zlink::framework::detail::route_received_packet_t{
        zlink::routing_id_t::from (std::string ("play-b")), 96, route_packet_parts});
    if (!route_packet_dispatch || !route_packet_dispatch.value ()
        || route_packet_dispatch.value ()->request_seq.value_or (0) != 96) {
        return 96;
    }
    auto route_packet_reply_body =
      route_join_envelope.decode_body (route_packet_dispatch.value ()->parts);
    if (!route_packet_reply_body) {
        return 97;
    }
    const auto route_packet_reply =
      route_join_serializers.get<zlink::framework::detail::spot_actor_packet_route_reply_t> ()
        .deserialize (
          zlink::framework::detail::encoded_payload_from_raw (route_packet_reply_body.value ()));
    if (!route_packet_reply.actor_ref_present) {
        return 98;
    }
    if (!route_packet_reply.has_reply) {
        return 100;
    }
    if (route_packet_reply.actor_node_rid != "relay-stage") {
        return 101;
    }
    if (route_packet_reply.actor_id != "routed-through-channel") {
        return 102;
    }
    if (zlink::message_t::from (route_packet_reply.payload).to_string ()
        != "routed-through-channel:71") {
        return 103;
    }
    const auto route_disconnect_request =
      zlink::framework::detail::make_spot_actor_disconnect_route_request (
        zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("play-b"),
                                       "relay-player", "routed-through-channel", 3));
    zlink::framework::runtime::messaging::envelope_header_t route_disconnect_header;
    route_disconnect_header.kind = zlink::framework::runtime::messaging::message_kind_t::request;
    route_disconnect_header.channel_name = "relay.route";
    route_disconnect_header.message_name =
      zlink::framework::detail::spot_actor_disconnect_route_request_t::packet_name;
    auto route_disconnect_parts = route_join_envelope.encode_parts (
      route_disconnect_header,
      std::type_index (typeid (zlink::framework::detail::spot_actor_disconnect_route_request_t)),
      &route_disconnect_request, route_join_serializers);
    auto route_disconnect_dispatch =
      route_join_dispatcher.dispatch (zlink::framework::detail::route_received_packet_t{
        zlink::routing_id_t::from (std::string ("play-b")), 99, route_disconnect_parts});
    if (!route_disconnect_dispatch || !route_disconnect_dispatch.value ()
        || route_disconnect_dispatch.value ()->request_seq.value_or (0) != 99
        || relay_spot_t::disconnected_count != 1) {
        return 99;
    }
    auto route_disconnect_reply_body =
      route_join_envelope.decode_body (route_disconnect_dispatch.value ()->parts);
    if (!route_disconnect_reply_body) {
        return 104;
    }
    const auto route_disconnect_reply =
      route_join_serializers.get<zlink::framework::detail::spot_actor_disconnect_route_reply_t> ()
        .deserialize (zlink::framework::detail::encoded_payload_from_raw (
          route_disconnect_reply_body.value ()));
    if (!route_disconnect_reply.accepted) {
        return 105;
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

    auto entry_dispatch_spot = std::make_shared<entry_dispatch_probe_spot_t> ();
    zlink::framework::zlink_builder_t entry_dispatch_host;
    zlink::framework::detail::channel_runtime_t::from (entry_dispatch_host.message_bus ())
      .bind_serializers (relay_serializers);
    auto entry_dispatch_builder = entry_dispatch_host.add_spot_node ("entry-dispatch-stage");
    entry_dispatch_builder
      .add_entry_spot<entry_dispatch_probe_spot_t> (
        [entry_dispatch_spot] { return entry_dispatch_spot; })
      .add_actor_factory<entry_dispatch_probe_actor_factory_t> ("entry-dispatch-player");
    (void) entry_dispatch_builder.create_spot ("entry");
    auto entry_dispatch_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (entry_dispatch_builder);
    zlink::framework::detail::actor_gateway_runtime_t entry_dispatch_gateway;
    entry_dispatch_gateway.bind_serializers (relay_serializers);
    auto dispatch_actor_a =
      entry_dispatch_gateway.manager ()
        .create ("entry-dispatch-player", "actor-a", std::string ("create-a"))
        .value ();
    auto dispatch_actor_b =
      entry_dispatch_gateway.manager ()
        .create ("entry-dispatch-player", "actor-b", std::string ("create-b"))
        .value ();
    auto relay_entry_dispatch = [&] (const zlink::framework::session_actor_t &actor,
                                     int value) {
        return entry_dispatch_runtime.relay_actor_packet (
          actor.ref (), actor.context (), "entry.block",
          zlink::message_t::from (std::to_string (value)), relay_provider, relay_serializers);
    };
    std::optional<zlink::framework::result_t<std::optional<zlink::message_t>>>
      dispatch_a_first;
    std::optional<zlink::framework::result_t<std::optional<zlink::message_t>>>
      dispatch_b_first;
    std::optional<zlink::framework::result_t<std::optional<zlink::message_t>>>
      dispatch_a_second;
    std::thread dispatch_a_thread (
      [&] { dispatch_a_first = relay_entry_dispatch (dispatch_actor_a, 1); });
    if (!entry_dispatch_spot->wait_starts (1)) {
        entry_dispatch_spot->release ();
        dispatch_a_thread.join ();
        return 120;
    }
    std::thread dispatch_b_thread (
      [&] { dispatch_b_first = relay_entry_dispatch (dispatch_actor_b, 10); });
    if (!entry_dispatch_spot->wait_starts (2)) {
        entry_dispatch_spot->release ();
        dispatch_a_thread.join ();
        dispatch_b_thread.join ();
        return 121;
    }
    std::thread dispatch_a_second_thread (
      [&] { dispatch_a_second = relay_entry_dispatch (dispatch_actor_a, 2); });
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    {
        std::lock_guard lock (entry_dispatch_spot->mutex);
        if (std::find (entry_dispatch_spot->starts.begin (),
                       entry_dispatch_spot->starts.end (),
                       std::string ("actor-a:2")) != entry_dispatch_spot->starts.end ()) {
            entry_dispatch_spot->release ();
            dispatch_a_thread.join ();
            dispatch_b_thread.join ();
            dispatch_a_second_thread.join ();
            return 122;
        }
    }
    entry_dispatch_spot->release ();
    dispatch_a_thread.join ();
    dispatch_b_thread.join ();
    dispatch_a_second_thread.join ();
    if (!dispatch_a_first || !*dispatch_a_first || !dispatch_a_first->value ()
        || dispatch_a_first->value ()->to_string () != "actor-a:1" || !dispatch_b_first
        || !*dispatch_b_first || !dispatch_b_first->value ()
        || dispatch_b_first->value ()->to_string () != "actor-b:1" || !dispatch_a_second
        || !*dispatch_a_second || !dispatch_a_second->value ()
        || dispatch_a_second->value ()->to_string () != "actor-a:2") {
        return 123;
    }
    if (entry_dispatch_spot->max_running < 2 || entry_dispatch_spot->created_count != 2
        || entry_dispatch_spot->joined_count != 2) {
        return 124;
    }
    auto yield_actor =
      entry_dispatch_gateway.manager ()
        .create ("entry-dispatch-player", "actor-yield", std::string ("create-yield"))
        .value ();
    auto yield_dispatch = entry_dispatch_runtime.relay_actor_packet (
      yield_actor.ref (), yield_actor.context (), "entry.yield",
      zlink::message_t::from (std::string ("3")), relay_provider, relay_serializers);
    if (yield_dispatch) {
        return 125;
    }
    if (!yield_dispatch.error ()
        || std::string (yield_dispatch.error ()->what ()).find (
             "yield requires a framework Spot handler turn")
             == std::string::npos) {
        return 126;
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

    bool duplicate_actor_factory_failed = false;
    try {
        builder.add_actor_factory<player_actor_factory_t> ("player");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        duplicate_actor_factory_failed =
          error.kind () == framework_error_kind_t::actor_already_exists;
    }
    if (!duplicate_actor_factory_failed) {
        return 128;
    }

    bool duplicate_entry_spot_failed = false;
    try {
        builder.add_entry_spot<entry_spot_t> ();
    }
    catch (const zlink::framework::framework_exception_t &error) {
        duplicate_entry_spot_failed =
          error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!duplicate_entry_spot_failed) {
        return 129;
    }

    bool empty_entry_factory_failed = false;
    try {
        zlink::framework::spot_node_builder_t invalid;
        invalid.add_entry_spot<entry_spot_t> ({});
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_entry_factory_failed =
          error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!empty_entry_factory_failed) {
        return 130;
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

    bool empty_resolver_name_failed = false;
    try {
        builder.add_spot_resolver ("", [] (zlink::framework::spot_rid_t) {
            return std::optional<zlink::framework::spot_route_t>{};
        });
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_resolver_name_failed =
          error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!empty_resolver_name_failed) {
        return 131;
    }

    bool empty_resolver_callback_failed = false;
    try {
        builder.add_spot_resolver ("empty-callback", {});
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_resolver_callback_failed =
          error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!empty_resolver_callback_failed) {
        return 132;
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

    zlink::framework::spot_context_t empty_context;
    if (empty_context.close ().result ().value ()) {
        return 127;
    }
    bool empty_context_outbound_failed = false;
    try {
        (void) empty_context.outbound ();
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_context_outbound_failed =
          error.kind () == zlink::framework::framework_error_kind_t::request_failed;
    }
    if (!empty_context_outbound_failed) {
        return 128;
    }
    auto empty_context_request =
      empty_context
        .request_to<move_reply_t> (zlink::framework::node_rid_t::from_string ("remote-node"),
                                   zlink::framework::spot_rid_t::from_string ("remote-spot"),
                                   move_request_t{1})
        .async ()
        .result ();
    if (empty_context_request
        || empty_context_request.error_kind () != framework_error_kind_t::request_protocol_error) {
        return 129;
    }

    context.handlers ()
      .add_handler<&stage_spot_t::on_state_update> ("state.update")
      .add_handler<&stage_spot_t::on_state_update_context> ("state.context")
      .add_handler<&stage_spot_t::on_throwing_state_update> ("state.throw")
      .add_actor_packet<&stage_spot_t::on_move> ("move");
    const auto handler_descriptors = context.handlers ().descriptors ();
    if (handler_descriptors.size () != 4
        || handler_descriptors[0].kind != zlink::framework::spot_handler_kind_t::packet
        || handler_descriptors[0].packet_name != "state.update"
        || handler_descriptors[1].kind != zlink::framework::spot_handler_kind_t::packet
        || handler_descriptors[1].packet_name != "state.context"
        || handler_descriptors[2].kind != zlink::framework::spot_handler_kind_t::packet
        || handler_descriptors[2].packet_name != "state.throw"
        || handler_descriptors[3].kind != zlink::framework::spot_handler_kind_t::actor_packet
        || handler_descriptors[3].packet_name != "move") {
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
    const auto packet_context_dispatch = context.handlers ().invoke_packet (
      "state.context", stage_spot, spot_provider, spot_serializers,
      zlink::message_t::from (std::string ("32")));
    if (!packet_context_dispatch || stage_spot.last_packet_content_type != "application/json") {
        return 108;
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
    auto typed_metadata = projected;
    typed_metadata.content_type = "application/x-msgpack";
    player_actor_factory_t typed_actor;
    const auto typed_actor_dispatch = context.handlers ().invoke_actor_packet (
      "move", stage_spot, typed_actor, spot_provider, spot_serializers,
      zlink::message_t::from (std::string ("57")), typed_metadata);
    if (!typed_actor_dispatch || typed_actor.moved_value != 57
        || stage_spot.last_actor_content_type != "application/x-msgpack") {
        return 109;
    }
    const auto route_actor_ref = zlink::framework::actor_ref_t{
      zlink::framework::node_rid_t::from_string ("actor-node"), "player", "actor-1", 1};
    const auto route_packet = zlink::framework::detail::make_spot_actor_packet_route_request (
      route_actor_ref, context.spot_rid (), "move",
      zlink::message_t::from (std::string ("58")), typed_metadata);
    if (route_packet.content_type != "application/x-msgpack") {
        return 110;
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
    zlink::framework::detail::actor_gateway_runtime_t async_actor_gateway;
    async_actor_gateway.bind_serializers (spot_serializers);
    std::mutex async_join_mutex;
    std::condition_variable async_join_changed;
    bool async_join_started = false;
    bool async_join_release = false;
    async_actor_gateway.on_join_spot (
      [&] (const zlink::framework::actor_ref_t &actor_ref,
           zlink::framework::spot_rid_t spot_rid,
           const zlink::message_t &) {
          {
              std::unique_lock lock (async_join_mutex);
              async_join_started = actor_ref.actor_id () == "async-join-actor"
                                   && spot_rid.value () == "async-join-target";
              async_join_changed.notify_all ();
              async_join_changed.wait (lock, [&] { return async_join_release; });
          }
          return zlink::framework::result_t<zlink::framework::detail::actor_join_reply_t>::success (
            zlink::framework::detail::actor_join_reply_t{
              0,
              zlink::framework::actor_ref_t (
                zlink::framework::node_rid_t::from_string ("async-spot-node"),
                "player", "async-join-actor", 31),
              zlink::message_t{}});
      });
    async_spot.set_join_context (async_actor_gateway.actor_context (
      zlink::framework::actor_ref_t (
        zlink::framework::node_rid_t::from_string ("async-node"), "player",
        "async-join-actor", 30)));
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
    if (quick_future.wait_for (std::chrono::milliseconds (50)) == std::future_status::ready) {
        return 53;
    }
    if (slow_future.wait_for (std::chrono::milliseconds (50)) == std::future_status::ready) {
        return 54;
    }
    async_scheduler->run_worker_job ();
    async_scheduler->run_owner_job ();
    if (slow_future.wait_for (std::chrono::milliseconds (500)) != std::future_status::ready) {
        return 55;
    }
    const auto slow_result = slow_future.get ();
    if (!slow_result
        || spot_serializers.get<move_reply_t> ().deserialize (zlink::framework::detail::encoded_payload_from_raw (slow_result.value ())).value != 78) {
        return 56;
    }
    if (quick_future.wait_for (std::chrono::milliseconds (500)) != std::future_status::ready) {
        return 57;
    }
    const auto quick_result = quick_future.get ();
    if (!quick_result
        || spot_serializers.get<move_reply_t> ().deserialize (zlink::framework::detail::encoded_payload_from_raw (quick_result.value ())).value != 10
        || async_spot.quick_seen () != 1) {
        return 58;
    }

    async_spot.reset_probe ();
    auto yield_future =
      std::async (std::launch::async, [&] { return invoke_async ("async.slow-yield", 2); });
    if (!async_spot.wait_slow_started () || !async_scheduler->wait_worker_job_count (1)) {
        return 59;
    }
    auto yield_quick_future =
      std::async (std::launch::async, [&] { return invoke_async ("async.quick", 11); });
    if (yield_quick_future.wait_for (std::chrono::milliseconds (500))
        != std::future_status::ready) {
        return 60;
    }
    const auto yield_quick_result = yield_quick_future.get ();
    if (!yield_quick_result
        || spot_serializers.get<move_reply_t> ().deserialize (zlink::framework::detail::encoded_payload_from_raw (yield_quick_result.value ())).value != 12
        || async_spot.quick_seen () != 1) {
        return 61;
    }
    async_scheduler->run_worker_job ();
    async_scheduler->run_owner_job ();
    if (yield_future.wait_for (std::chrono::milliseconds (500)) != std::future_status::ready) {
        return 62;
    }
    const auto yield_result = yield_future.get ();
    if (!yield_result
        || spot_serializers.get<move_reply_t> ().deserialize (zlink::framework::detail::encoded_payload_from_raw (yield_result.value ())).value != 79) {
        return 63;
    }

    async_spot.reset_probe ();
    auto request_yield_future =
      std::async (std::launch::async, [&] { return invoke_async ("async.request-yield", 4); });
    auto request_source = async_spot.wait_request_source ();
    if (!request_source) {
        return 64;
    }
    auto request_quick_future =
      std::async (std::launch::async, [&] { return invoke_async ("async.quick", 13); });
    if (request_quick_future.wait_for (std::chrono::milliseconds (500))
        != std::future_status::ready) {
        return 65;
    }
    const auto request_quick_result = request_quick_future.get ();
    if (!request_quick_result
        || spot_serializers.get<move_reply_t> ().deserialize (zlink::framework::detail::encoded_payload_from_raw (request_quick_result.value ())).value != 14
        || async_spot.quick_seen () != 1) {
        return 66;
    }
    request_source->complete (zlink::framework::result_t<int>::success (90));
    if (request_yield_future.wait_for (std::chrono::milliseconds (500))
        != std::future_status::ready) {
        return 67;
    }
    const auto request_yield_result = request_yield_future.get ();
    if (!request_yield_result
        || spot_serializers.get<move_reply_t> ().deserialize (zlink::framework::detail::encoded_payload_from_raw (request_yield_result.value ())).value != 94) {
        return 68;
    }

    async_spot.reset_probe ();
    auto join_yield_future =
      std::async (std::launch::async, [&] { return invoke_async ("async.join-yield", 5); });
    {
        std::unique_lock lock (async_join_mutex);
        if (!async_join_changed.wait_for (
              lock, std::chrono::milliseconds (500), [&] { return async_join_started; })) {
            return 69;
        }
    }
    auto join_quick_future =
      std::async (std::launch::async, [&] { return invoke_async ("async.quick", 15); });
    if (join_quick_future.wait_for (std::chrono::milliseconds (500))
        != std::future_status::ready) {
        return 70;
    }
    const auto join_quick_result = join_quick_future.get ();
    if (!join_quick_result
        || spot_serializers.get<move_reply_t> ().deserialize (zlink::framework::detail::encoded_payload_from_raw (join_quick_result.value ())).value != 16
        || async_spot.quick_seen () != 1) {
        return 71;
    }
    {
        std::lock_guard lock (async_join_mutex);
        async_join_release = true;
        async_join_changed.notify_all ();
    }
    if (join_yield_future.wait_for (std::chrono::milliseconds (500))
        != std::future_status::ready) {
        return 72;
    }
    const auto join_yield_result = join_yield_future.get ();
    if (!join_yield_result
        || spot_serializers.get<move_reply_t> ().deserialize (zlink::framework::detail::encoded_payload_from_raw (join_yield_result.value ())).value != 36) {
        return 73;
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

    context.publish ("stage.state.updated", state_update_t{1}).submit ();

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
    pubsub_created.context.publish ("stage.state.updated", state_update_t{9}).submit ();
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

    context.send_to (remote_route->node_rid, remote_route->spot_rid, state_update_t{2}).submit ();

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
