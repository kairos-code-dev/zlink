/* SPDX-License-Identifier: FSL-1.1-ALv2 */

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
#include "runtime/locations/spot_address_resolvers.hpp"
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

zlink::routing_id_t make_target_rid (std::string value)
{
    return zlink::routing_id_t::from (std::move (value));
}

zlink::routing_id_t default_target_rid ()
{
    return zlink::routing_id_t::from (std::uint32_t{0});
}

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
    : public zlink::framework::runtime::spot_address_resolver_t
{
  public:
    zlink::framework::task_t<std::optional<zlink::framework::runtime::spot_address_t>>
    resolve_spot_address (std::string mesh_name, zlink::routing_id_t spot_rid) override
    {
        last_mesh_name = std::move (mesh_name);
        last_spot_rid = spot_rid.to_string ();
        ++calls;
        return zlink::framework::task_t<
          std::optional<zlink::framework::runtime::spot_address_t>> (
          zlink::framework::result_t<
            std::optional<zlink::framework::runtime::spot_address_t>>::success (address));
    }

    std::optional<zlink::framework::runtime::spot_address_t> address;
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

inline void to_json (nlohmann::json &json, const player_actor_factory_t &value)
{
    json = nlohmann::json{{"joinedValue", value.joined_value},
                          {"movedValue", value.moved_value},
                          {"refUpdates", value.ref_updates},
                          {"lastGeneration", value.last_generation},
                          {"nodeRid", std::string (value.current_ref.node_rid ().value ())},
                          {"actorType", std::string (value.current_ref.actor_type ())},
                          {"actorId", std::string (value.current_ref.actor_id ())},
                          {"actorGeneration", value.current_ref.generation ()}};
}

inline void from_json (const nlohmann::json &json, player_actor_factory_t &value)
{
    value.joined_value = json.value ("joinedValue", 0);
    value.moved_value = json.value ("movedValue", 0);
    value.ref_updates = json.value ("refUpdates", 0);
    value.last_generation = json.value ("lastGeneration", std::uint64_t{});
    value.current_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string (json.value ("nodeRid", std::string{})),
      json.value ("actorType", std::string{}), json.value ("actorId", std::string{}),
      json.value ("actorGeneration", std::uint64_t{}));
}

struct entry_spot_t : public zlink::framework::entry_spot_t
{
    void on_create_actor (player_actor_factory_t &actor, const zlink::framework::message_t &request)
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

    void on_leave_actor (player_actor_factory_t &actor)
    {
        ++left_count;
        actor.moved_value += 10;
        if (on_left) {
            on_left (actor);
        }
    }

    void on_disconnect_actor (player_actor_factory_t &) { ++disconnected_count; }

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

struct stage_closed_t
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

struct empty_relay_transfer_t : zlink::framework::actor_transfer_adapter_t<relay_actor_t>
{
    zlink::framework::task_t<zlink::framework::message_t>
    transfer_out (const relay_actor_t &) override
    {
        ++transfer_out_count;
        return zlink::framework::task_t<zlink::framework::message_t> (
          zlink::framework::result_t<zlink::framework::message_t>::success ({}));
    }

    zlink::framework::task_t<relay_actor_t>
    transfer_in (std::string actor_id, zlink::framework::message_t state) override
    {
        ++transfer_in_count;
        transfer_in_received_empty = state.empty ();
        return zlink::framework::task_t<relay_actor_t> (
          zlink::framework::result_t<relay_actor_t>::success ({std::move (actor_id)}));
    }

    static inline int transfer_out_count = 0;
    static inline int transfer_in_count = 0;
    static inline bool transfer_in_received_empty = false;
};

struct stateful_relay_actor_t
{
    std::string actor_id;
    std::string state;
    zlink::framework::actor_context_t context;

    void set_actor_context (zlink::framework::actor_context_t value)
    {
        context = std::move (value);
    }
};

struct stateful_relay_actor_factory_t
{
    stateful_relay_actor_t create (std::string actor_id) const
    {
        return {std::move (actor_id), {}};
    }
};

struct stateful_relay_transfer_t
    : zlink::framework::actor_transfer_adapter_t<stateful_relay_actor_t>
{
    zlink::framework::task_t<zlink::framework::message_t>
    transfer_out (const stateful_relay_actor_t &actor) override
    {
        return zlink::framework::task_t<zlink::framework::message_t> (
          zlink::framework::result_t<zlink::framework::message_t>::success (
            zlink::framework::message_t::from (actor.state)));
    }

    zlink::framework::task_t<stateful_relay_actor_t>
    transfer_in (std::string actor_id, zlink::framework::message_t state) override
    {
        return zlink::framework::task_t<stateful_relay_actor_t> (
          zlink::framework::result_t<stateful_relay_actor_t>::success (
            stateful_relay_actor_t{std::move (actor_id), state.decode<std::string> ()}));
    }
};

struct controllable_stateful_transfer_t
    : zlink::framework::actor_transfer_adapter_t<stateful_relay_actor_t>
{
    zlink::framework::task_t<zlink::framework::message_t>
    transfer_out (const stateful_relay_actor_t &actor) override
    {
        if (fail_out) {
            throw std::runtime_error ("transfer-out-failed");
        }
        return zlink::framework::task_t<zlink::framework::message_t> (
          zlink::framework::result_t<zlink::framework::message_t>::success (
            zlink::framework::message_t::from (actor.state)));
    }

    zlink::framework::task_t<stateful_relay_actor_t>
    transfer_in (std::string actor_id, zlink::framework::message_t state) override
    {
        if (fail_in) {
            throw std::runtime_error ("transfer-in-failed");
        }
        return zlink::framework::task_t<stateful_relay_actor_t> (
          zlink::framework::result_t<stateful_relay_actor_t>::success (
            stateful_relay_actor_t{std::move (actor_id), state.decode<std::string> ()}));
    }

    static inline bool fail_out = false;
    static inline bool fail_in = false;
};

struct stateful_lifecycle_probe_t
{
    void record (std::string event)
    {
        std::lock_guard lock (mutex);
        events.push_back (std::move (event));
    }

    std::vector<std::string> snapshot () const
    {
        std::lock_guard lock (mutex);
        return events;
    }

    void clear ()
    {
        std::lock_guard lock (mutex);
        events.clear ();
    }

    mutable std::mutex mutex;
    std::vector<std::string> events;
};

struct stateful_relay_spot_t : zlink::framework::spot_t
{
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_actor_request<&stateful_relay_spot_t::read> ("state.read");
        context.handlers ().add_actor_send<&stateful_relay_spot_t::on_note> ("state.note");
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (std::string_view,
                   const zlink::framework::message_t &)
    {
        if (lifecycle_probe) {
            lifecycle_probe->record ("admission");
        }
        return zlink::framework::spot_actor_join_response_t::accept ();
    }

    void on_leave_actor (const stateful_relay_actor_t &)
    {
        if (fail_leave) {
            throw std::runtime_error ("leave-failed");
        }
        if (lifecycle_probe) {
            lifecycle_probe->record ("leave");
        }
    }

    void on_actor_joined (const stateful_relay_actor_t &actor)
    {
        std::unique_lock lock (joined_gate);
        if (block_joined) {
            joined_entered = true;
            joined_changed.notify_all ();
            joined_changed.wait (lock, [this] { return !block_joined; });
        }
        if (fail_joined) {
            throw std::runtime_error ("joined-failed");
        }
        joined_state = actor.state;
        if (push_on_joined) {
            actor.context.bound_session ()
              .send (zlink::framework::message_t::from (std::string ("joined")))
              .submit ();
        }
        if (lifecycle_probe) {
            lifecycle_probe->record ("joined");
        }
        if (joined_reentry_probe) {
            // Run the probe on a separate thread: it calls back into the runtime,
            // which must not deadlock against the thread driving the commit.
            auto done = std::make_shared<std::promise<bool>> ();
            auto result = done->get_future ();
            std::thread worker ([probe = joined_reentry_probe, done] {
                done->set_value (probe ());
            });
            if (result.wait_for (std::chrono::seconds (2)) == std::future_status::ready) {
                joined_reentry_ok = result.get ();
                worker.join ();
            } else {
                joined_reentry_ok = false;
                worker.detach ();
            }
        }
    }

    void block_next_joined ()
    {
        std::lock_guard lock (joined_gate);
        block_joined = true;
        joined_entered = false;
    }

    bool wait_until_joined (std::chrono::milliseconds timeout)
    {
        std::unique_lock lock (joined_gate);
        return joined_changed.wait_for (lock, timeout, [this] { return joined_entered; });
    }

    void release_joined ()
    {
        std::lock_guard lock (joined_gate);
        block_joined = false;
        joined_changed.notify_all ();
    }

    relay_reply_t read (const stateful_relay_actor_t &actor,
                        zlink::framework::spot_actor_request_context_t &,
                        const relay_request_t &)
    {
        return {actor.state};
    }

    void on_note (stateful_relay_actor_t &,
                  const zlink::framework::spot_actor_send_context_t &,
                  const relay_reply_t &note)
    {
        std::lock_guard lock (notes_gate);
        notes.push_back (note.value);
        notes_changed.notify_all ();
    }

    bool wait_for_notes (std::size_t count, std::chrono::milliseconds timeout)
    {
        std::unique_lock lock (notes_gate);
        return notes_changed.wait_for (lock, timeout,
                                       [this, count] { return notes.size () >= count; });
    }

    std::vector<std::string> notes_snapshot ()
    {
        std::lock_guard lock (notes_gate);
        return notes;
    }

    std::mutex notes_gate;
    std::condition_variable notes_changed;
    std::vector<std::string> notes;
    std::string joined_state;
    std::mutex joined_gate;
    std::condition_variable joined_changed;
    bool block_joined = false;
    bool joined_entered = false;
    bool fail_leave = false;
    bool fail_joined = false;
    bool push_on_joined = false;
    std::shared_ptr<stateful_lifecycle_probe_t> lifecycle_probe;
    std::function<bool ()> joined_reentry_probe;
    bool joined_reentry_ok = false;
};

struct relay_entry_spot_t : public zlink::framework::entry_spot_t
{
    void configure (zlink::framework::entry_spot_context_t &context)
    {
        context.handlers ().add_actor_request<&relay_entry_spot_t::on_relay> ("relay.request");
    }

    void configure (zlink::framework::spot_context_t &context)
    {
        zlink::framework::entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    void on_create_actor (relay_actor_t &, const zlink::framework::message_t &request)
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
        context.handlers ().add_actor_request<&relay_spot_t::on_relay> ("relay.request");
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (std::string_view,
                   const zlink::framework::message_t &)
    {
        return zlink::framework::spot_actor_join_response_t::accept ();
    }

    void on_leave_actor (relay_actor_t &) { left_count++; }

    void on_actor_joined (relay_actor_t &) { joined_count++; }

    void on_disconnect_actor (relay_actor_t &) { disconnected_count++; }

    relay_reply_t on_relay (relay_actor_t &actor,
                            zlink::framework::spot_actor_request_context_t &,
                            const relay_request_t &request)
    {
        return {actor.actor_id + ":" + std::to_string (request.value)};
    }

    static inline int left_count{};
    static inline int disconnected_count{};
    static inline int joined_count{};
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
          .add_actor_request<&entry_dispatch_probe_spot_t::on_block> ("entry.block")
          .add_actor_request<&entry_dispatch_probe_spot_t::on_awaited> ("entry.await");
    }

    void on_create_actor (entry_dispatch_probe_actor_t &, const zlink::framework::message_t &)
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
    on_awaited (entry_dispatch_probe_actor_t &,
              zlink::framework::spot_actor_request_context_t &,
              const relay_request_t &)
    {
        zlink::framework::request_call_t<int> call (
          "entry.await", [] (const std::string &, std::chrono::milliseconds,
                             const zlink::framework::request_call_t<int>::metadata_map_t &) {
              return zlink::framework::task_t<int> (zlink::framework::result_t<int>::failure (
                zlink::framework::framework_error_kind_t::request_failed, "should not submit"));
          });
        const auto value = co_await call.async ();
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

    zlink::framework::spot_create_response_t on_create (const zlink::framework::message_t &request)
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

    zlink::framework::spot_actor_join_response_t
    on_actor_join (std::string_view,
                   const zlink::framework::message_t &request)
    {
        join_seen = std::stoi (request.decode<std::string> ());
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

    void on_leave_actor (player_actor_factory_t &actor)
    {
        ++left_count;
        actor.moved_value += 100;
    }

    void on_closing ()
    {
        ++closing_count;
        ++global_closing_count;
    }

    void on_disconnect_actor (player_actor_factory_t &) { ++disconnected_count; }

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
        context.handlers ().add_actor_send<&erased_disconnect_spot_t::on_move> ("move");
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (std::string_view, const zlink::message_t &)
    {
        return zlink::framework::spot_actor_join_response_t::accept ();
    }

    void on_move (player_actor_factory_t &,
                  const zlink::framework::spot_actor_send_context_t &,
                  const move_request_t &)
    {
    }

    void on_disconnect_actor (player_actor_factory_t &) { ++disconnected_count; }

    int disconnected_count{};
};

struct subscription_spot_t : public zlink::framework::spot_t
{
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_subscribe<&subscription_spot_t::on_state_update> (
          "stage.state.updated");
        context.handlers ().add_subscribe<&subscription_spot_t::on_stage_closed> (
          "stage.state.updated");
    }

    void on_state_update (const state_update_t &message) { last_value = message.value; }
    void on_stage_closed (const stage_closed_t &message) { closed_value = message.value; }

    int last_value{};
    int closed_value{};
};

struct alternate_stage_spot_t : public zlink::framework::spot_t
{
};

struct serial_probe_spot_t : public zlink::framework::spot_t
{
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_actor_send<&serial_probe_spot_t::on_move> ("serial.move");
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
          .add_actor_request<&async_probe_spot_t::slow> ("async.slow")
          .add_actor_request<&async_probe_spot_t::slow_await> ("async.slow-await")
          .add_actor_request<&async_probe_spot_t::request_await> ("async.request-await")
          .add_actor_request<&async_probe_spot_t::join_await> ("async.join-await")
          .add_actor_request<&async_probe_spot_t::quick> ("async.quick");
    }

    void set_join_context (zlink::framework::actor_context_t context)
    {
        _join_context = std::move (context);
    }

    zlink::framework::task_t<move_reply_t>
    slow_await (player_actor_factory_t &,
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
                               + (context.packet_name == "async.slow-await" ? 0 : 1000)};
    }

    zlink::framework::task_t<move_reply_t>
    request_await (player_actor_factory_t &,
                   zlink::framework::spot_actor_request_context_t &,
                   const move_request_t &request)
    {
        auto source = std::make_shared<zlink::framework::detail::task_completion_source_t<int>> ();
        {
            std::lock_guard lock (mutex);
            request_source = source;
            slow_started = true;
            changed.notify_all ();
        }
        zlink::framework::request_call_t<int> call (
          "async.request-await",
          [source] (const std::string &, std::chrono::milliseconds,
                    const zlink::framework::request_call_t<int>::metadata_map_t &) {
              return source->task ();
          });
        const auto value = co_await call.async ();
        {
            std::lock_guard lock (mutex);
            slow_completed = true;
            changed.notify_all ();
        }
        co_return move_reply_t{value + request.value};
    }

    zlink::framework::task_t<move_reply_t>
    join_await (player_actor_factory_t &,
                zlink::framework::spot_actor_request_context_t &,
                const move_request_t &request)
    {
        {
            std::lock_guard lock (mutex);
            slow_started = true;
            changed.notify_all ();
        }
        const auto joined =
          co_await _join_context
            .join_spot (zlink::framework::spot_rid_t::from_string ("async-join-target"),
                        zlink::framework::message_t::from (std::string ("join")))
            .async ();
        {
            std::lock_guard lock (mutex);
            slow_completed = true;
            changed.notify_all ();
        }
        co_return move_reply_t{
          static_cast<int> (std::get<zlink::framework::actor_join_accepted_t<zlink::framework::message_t>> (joined).actor.generation ()) + request.value};
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

    std::shared_ptr<zlink::framework::detail::task_completion_source_t<int>> wait_request_source ()
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

    zlink::framework::spot_create_response_t on_create (const zlink::framework::message_t &request)
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
    void configure (zlink::framework::entry_spot_context_t &context)
    {
        entry_context = context;
        context.handlers ().add_actor_request<&auto_destroy_entry_spot_t::on_probe> ("probe");
    }

    relay_reply_t on_probe (player_actor_factory_t &,
                            zlink::framework::spot_actor_request_context_t &,
                            const relay_request_t &request)
    {
        return {std::to_string (request.value)};
    }

    void on_actor_joined (player_actor_factory_t &actor)
    {
        ++joined_count;
        last_joined_moved_value = actor.moved_value;
        if (destroy_on_join && !actor.current_ref.empty ()) {
            const auto destroyed = entry_context.destroy_actor (actor).result ();
            if (destroyed) {
                ++destroyed_count;
            }
        }
    }

    zlink::framework::entry_spot_context_t entry_context;
    int joined_count{};
    int destroyed_count{};
    int last_joined_moved_value{};
    bool destroy_on_join = false;
};

struct actor_packet_self_leave_spot_t : public zlink::framework::spot_t
{
    void configure (zlink::framework::spot_context_t &context)
    {
        spot_context = context;
        context.handlers ().add_actor_request<&actor_packet_self_leave_spot_t::on_leave_request> (
          "self.leave");
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (std::string_view,
                   const zlink::framework::message_t &)
    {
        return zlink::framework::spot_actor_join_response_t::accept ();
    }

    void on_leave_actor (player_actor_factory_t &) { ++left_count; }

    relay_reply_t on_leave_request (player_actor_factory_t &actor,
                                    zlink::framework::spot_actor_request_context_t &,
                                    const relay_request_t &request)
    {
        actor.moved_value = request.value;
        auto left = spot_context.leave_actor (actor.current_ref, actor).result ();
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
      .connect_router (zlink::routing_id_t::from ("stage-peer"), "tcp://127.0.0.1:9006")
      .enable_pub_sub ("tcp://0.0.0.0:9004")
      .connect_pub_sub ("tcp://127.0.0.1:9005")
      .add_entry_spot<entry_spot_t> ()
      .add_actor_factory<player_actor_factory_t> ("player")
      .add_spot<stage_spot_t> ("stage");

    const auto snapshots = zlink::framework::detail::spot_node_runtime_t::snapshots (zlink);
    if (snapshots.size () != 1 || snapshots[0].name != "stage-spot-node"
        || snapshots[0].bind_endpoint != "tcp://0.0.0.0:9000" || !snapshots[0].router_bind_endpoint
        || *snapshots[0].router_bind_endpoint != "tcp://0.0.0.0:9002"
        || snapshots[0].router_manual_connections.size () != 1
        || snapshots[0].router_manual_connections[0] != "tcp://127.0.0.1:9003"
        || snapshots[0].router_manual_rid_connections.size () != 1
        || snapshots[0].router_manual_rid_connections[0].first.to_string () != "stage-peer"
        || snapshots[0].router_manual_rid_connections[0].second != "tcp://127.0.0.1:9006"
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
    manual_serializers.add<relay_request_t> (
      [] (const relay_request_t &value) {
          return zlink::framework::encoded_payload_t::from_string (std::to_string (value.value));
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return relay_request_t{std::stoi (payload.to_string ())};
      });
    manual_serializers.add<relay_reply_t> (
      [] (const relay_reply_t &value) {
          return zlink::framework::encoded_payload_t::from_string (value.value);
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return relay_reply_t{payload.to_string ()};
      });
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
        || !context_manager.find_spot (manager_created.spot_rid).result ().value ()
        || context_manager.spot_name_for (manager_created.spot_rid) != "stage"
        || !context_manager.resolve_spot (manager_created.spot_rid)
        || context_manager.list_spots ().result ().value ().empty ()) {
        return 133;
    }
    const auto manager_close = context_manager.close_spot (manager_created.spot_rid).result ();
    if (!manager_close || !manager_close.value ()
        || context_manager.find_spot (manager_created.spot_rid).result ().value ()) {
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
    auto manager_get_or_create = context_manager.get_or_create_spot (
      "stage", manager_requested_rid, std::string ("manager-get-request"));
    auto manager_existing = context_manager.get_or_create_spot (
      "stage", manager_requested_rid, std::string ("manager-ignored-request"));
    if (manager_get_or_create.state != zlink::framework::spot_create_state_t::created
        || manager_existing.state != zlink::framework::spot_create_state_t::existing
        || manager_get_or_create.spot_rid.value () != "manager-requested-stage"
        || manager_existing.spot_rid.value () != "manager-requested-stage"
        || !context_manager.find_spot (manager_requested_rid).result ().value ()) {
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
    if (!empty_manager_create_failed || empty_manager.find_spot (context.spot_rid ()).result ().value ()
        || !empty_manager.list_spots ().result ().value ().empty ()) {
        return 136;
    }
    bool empty_spot_route_send_rejected = false;
    try {
        context.send_to (default_target_rid (), default_target_rid (), move_request_t{1})
          .submit ();
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_spot_route_send_rejected =
          error.kind () == zlink::framework::framework_error_kind_t::spot_route_not_found;
    }
    if (!empty_spot_route_send_rejected) {
        return 140;
    }
    const auto empty_spot_route_request =
      context.request_to<move_reply_t> (default_target_rid (), default_target_rid (),
                                        move_request_t{1})
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
        .request_to<move_reply_t> (make_target_rid ("remote-node"),
                                   make_target_rid ("remote-spot"), move_request_t{2})
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
    const auto location_resolved_rid = zlink::framework::spot_rid_t::from_string ("location-stage");
    recording_spot_location_resolver_t location_resolver;
    location_resolver.address = zlink::framework::runtime::spot_address_t{
      "manual-stage", zlink::routing_id_t::from ("location-node"),
      zlink::routing_id_t::from ("location-stage")};
    auto builder_runtime = zlink::framework::detail::spot_node_runtime_t::from (builder);
    builder_runtime.bind_spot_location_resolver (location_resolver);
    const auto location_resolved_route = builder_runtime.resolve_spot (location_resolved_rid);
    if (!location_resolved_route || location_resolved_route->node_rid.value () != "location-node"
        || location_resolved_route->spot_rid.value () != "location-stage"
        || location_resolver.calls != 1 || location_resolver.last_mesh_name != "manual-stage"
        || location_resolver.last_spot_rid != "location-stage") {
        return 114;
    }

    /* graceful-drain-handoff §4-2: a draining node rejects new spot
     * creation while existing spots keep serving. */
    {
        auto drain_flag = std::make_shared<std::atomic_bool> (true);
        auto draining_runtime = zlink::framework::detail::spot_node_runtime_t::from (builder);
        draining_runtime.bind_drain_flag (drain_flag);
        bool draining_create_rejected = false;
        try {
            (void) builder.create_spot ("stage");
        }
        catch (const zlink::framework::framework_exception_t &error) {
            draining_create_rejected =
              error.kind () == zlink::framework::framework_error_kind_t::request_rejected;
        }
        if (!draining_create_rejected) {
            return 150;
        }
        drain_flag->store (false);
    }

    auto close_create = builder.create_spot ("stage");
    if (!builder.find_spot (close_create.spot_rid).result ().value ()
        || builder.list_spots ().result ().value ().empty ()) {
        return 49;
    }
    const auto closing_count_before = stage_spot_t::global_closing_count;
    const auto close_result = close_create.context.close ().result ();
    if (!close_result || !close_result.value ()
        || builder.find_spot (close_create.spot_rid).result ().value ()
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
    auto created_once = builder.get_or_create_spot (
      "stage", requested_rid, zlink::framework::message_t::from (std::string ("create-request")));
    auto existing_once = builder.get_or_create_spot (
      "stage", requested_rid, zlink::framework::message_t::from (std::string ("ignored")));
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
    if (!spot_type_mismatch_failed || builder.find_spot (requested_rid).result ().value ()->spot_name != "stage") {
        return 53;
    }
    stage_spot_t::reject_create = true;
    auto rejected_create = builder.create_spot (
      "stage", zlink::framework::message_t::from (std::string ("reject-request")));
    stage_spot_t::reject_create = false;
    if (rejected_create.state != zlink::framework::spot_create_state_t::rejected
        || !rejected_create.reply
        || rejected_create.reply->decode<std::string> (manual_serializers) != "create-rejected"
        || builder.find_spot (rejected_create.spot_rid).result ().value ()) {
        return 54;
    }

    auto factory_created = builder.create_spot (
      "factory", zlink::framework::message_t::from (std::string ("factory-request")));
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
    zlink::framework::runtime::location_runtime_t spot_location_runtime (spot_location_store, {},
                                                                         "spot-runtime-owner");
    spot_location_runtime.start (zlink::routing_id_t::from ("spot-runtime-node"));
    zlink::framework::runtime::location_lifecycle_t spot_location_lifecycle (spot_location_runtime);
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
    auto stored_spot = spot_location_store
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
    stored_spot = spot_location_store
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
    auto missing_serializer_create = missing_serializer_gateway.manager ().create (
      "player", "missing-player", std::string ("payload"));
    if (missing_serializer_create
        || missing_serializer_create.error_kind ()
             != framework_error_kind_t::request_protocol_error) {
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
    player_actor_factory_t destroy_actor_state;
    player_actor_factory_t rejected_actor_state;
    player_actor_factory_t leave_actor_state;
    lifecycle_gateway.on_join_spot ([&] (const zlink::framework::actor_ref_t &actor_ref,
                                         zlink::framework::spot_rid_t spot_rid,
                                         const zlink::message_t &payload) {
        auto &actor_state = actor_ref.actor_id () == "rejected-player" ? rejected_actor_state
                            : actor_ref.actor_id () == "destroy-player"
                              ? destroy_actor_state
                            : actor_ref.actor_id () == "context-leave-player"
                              ? leave_actor_state
                              : lifecycle_actor_state;
        return lifecycle_runtime.join_actor_to_spot<stage_spot_t> (actor_ref, std::move (spot_rid),
                                                                   actor_state, payload);
    });
    auto lifecycle_join =
      lifecycle_actor_context.join_spot (lifecycle_stage.spot_rid, std::string ("41"))
        .async ()
        .result ();
    const auto *lifecycle_join_accepted =
      lifecycle_join ? std::get_if<zlink::framework::actor_join_accepted_t<zlink::framework::message_t>> (&lifecycle_join.value ()) : nullptr;
    if (lifecycle_join_accepted == nullptr
        || lifecycle_join_accepted->reply.decode<std::string> (manual_serializers) != "42"
        || lifecycle_stage_spot->join_seen != 41 || lifecycle_stage_spot->joined_count != 1
        || lifecycle_actor_state.joined_value != 100) {
        return 59;
    }
    auto stale_disconnect =
      lifecycle_runtime.notify_on_disconnect_actor (lifecycle_actor.ref (), lifecycle_actor_state);
    if (stale_disconnect
        || (stale_disconnect.error () != nullptr
         && zlink::framework::detail::boundary_state (*stale_disconnect.error ()) != zlink::framework::detail::boundary_error_t::stale_generation)
        || lifecycle_stage_spot->disconnected_count != 0) {
        return 67;
    }
    auto current_disconnect = lifecycle_runtime.notify_on_disconnect_actor (
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
      erased_disconnect_runtime.notify_actor_disconnected_erased (
        zlink::framework::actor_ref_t (zlink::framework::node_rid_t{}, "", "", 0));
    if (erased_empty_disconnect
        || erased_empty_disconnect.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 130;
    }
    const auto erased_stale_disconnect =
      erased_disconnect_runtime.notify_actor_disconnected_erased (
        zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("remote-session"),
                                       "player", "erased-player", 2));
    if (erased_stale_disconnect
        || (erased_stale_disconnect.error () != nullptr
         && zlink::framework::detail::boundary_state (*erased_stale_disconnect.error ()) != zlink::framework::detail::boundary_error_t::stale_generation)) {
        return 131;
    }
    const auto erased_missing_disconnect =
      erased_disconnect_runtime.notify_actor_disconnected_erased (
        zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("remote-session"),
                                       "player", "missing-player", 1));
    if (!erased_missing_disconnect) {
        return 132;
    }
    erased_disconnect_runtime.record_actor_spot (
      zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("remote-session"),
                                     "player", "missing-context-player", 1),
      zlink::framework::spot_rid_t::from_string ("missing-spot"));
    const auto erased_missing_context_disconnect =
      erased_disconnect_runtime.notify_actor_disconnected_erased (
        zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("remote-session"),
                                       "player", "missing-context-player", 1));
    if (!erased_missing_context_disconnect) {
        return 133;
    }
    zlink::framework::zlink_builder_t missing_factory_host;
    auto missing_factory_builder = missing_factory_host.add_spot_node ("missing-factory-stage");
    missing_factory_builder.add_spot<erased_disconnect_spot_t> ("stage");
    auto missing_factory_stage = missing_factory_builder.create_spot ("stage");
    auto missing_factory_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (missing_factory_builder);
    const auto missing_factory_ref =
      zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("remote-session"),
                                     "unknown", "missing-factory", 1);
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
        || !lifecycle_builder.find_spot (lifecycle_stage.spot_rid).result ().value ()) {
        return 60;
    }

    lifecycle_stage_spot->accept_join = false;
    auto rejected_actor =
      lifecycle_gateway.manager ().create ("player", "rejected-player").value ();
    auto rejected_context = rejected_actor.context ();
    auto rejected_runtime_join =
      rejected_context.join_spot (lifecycle_stage.spot_rid, std::string ("50")).async ().result ();
    lifecycle_stage_spot->accept_join = true;
    const auto *rejected_runtime_rejection =
      rejected_runtime_join ? std::get_if<zlink::framework::actor_join_rejected_t<zlink::framework::message_t>> (&rejected_runtime_join.value ()) : nullptr;
    if (rejected_runtime_rejection == nullptr
        || rejected_runtime_rejection->reply.decode<std::string> (manual_serializers) != "rejected"
        || lifecycle_stage_spot->joined_count != 1
        || rejected_context.actor_ref ().node_rid ().value () != "local") {
        return 61;
    }

    std::vector<std::string> lifecycle_entry_dispatch_payloads;
    lifecycle_gateway.on_join_entry_spot ([&] (const zlink::framework::actor_ref_t &actor_ref,
                                               zlink::framework::node_rid_t node_rid,
                                               const zlink::message_t &request) {
        lifecycle_entry_dispatch_payloads.push_back (request.to_string ());
        auto &actor_state = actor_ref.actor_id () == "destroy-player" ? destroy_actor_state
                            : actor_ref.actor_id () == "context-leave-player"
                              ? leave_actor_state
                              : lifecycle_actor_state;
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
        || lifecycle_actor_state.joined_value != 111) {
        return 62;
    }
    auto lifecycle_entry_rejoin =
      lifecycle_actor_context
        .join_entry_spot (zlink::framework::node_rid_t::from_string ("lifecycle-stage"),
                          std::string ("ignored-rejoin-payload"))
        .async ()
        .result ();
    if (!lifecycle_entry_rejoin || lifecycle_entry_spot->joined_count != 2
        || lifecycle_entry_spot->created_count != 1
        || lifecycle_entry_spot->created_payloads.size () != 1) {
        return 106;
    }
    auto entry_disconnect = lifecycle_runtime.notify_on_disconnect_actor (
      lifecycle_actor_context.actor_ref (), lifecycle_actor_state);
    if (!entry_disconnect || lifecycle_entry_spot->disconnected_count != 1) {
        return 69;
    }
    if (!lifecycle_gateway.manager ().find ("joined-player")) {
        return 86;
    }
    auto destroy_actor = lifecycle_gateway.manager ().create ("player", "destroy-player").value ();
    auto destroy_context = destroy_actor.context ();
    auto destroy_stage_join =
      destroy_context.join_spot (lifecycle_stage.spot_rid, std::string ("43")).async ().result ();
    if (!destroy_stage_join || lifecycle_stage_spot->joined_count != 2) {
        return 70;
    }
    auto direct_destroy = lifecycle_entry_context.destroy_actor (destroy_actor_state).result ();
    if (direct_destroy
        || direct_destroy.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 71;
    }
    /* An instance that was never joined on this node resolves to nothing:
     * destroy is a successful no-op and the live actor stays intact. */
    player_actor_factory_t unregistered_actor_state;
    auto unregistered_destroy =
      lifecycle_entry_context.destroy_actor (unregistered_actor_state).result ();
    if (!unregistered_destroy || !lifecycle_gateway.manager ().find ("destroy-player")) {
        return 107;
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
    auto destroy_result = lifecycle_entry_context.destroy_actor (destroy_actor_state).result ();
    /* The second call finds no registration for the instance anymore —
     * duplicate destroy (and any stale/superseded instance) is a
     * successful no-op. */
    auto duplicate_destroy = lifecycle_entry_context.destroy_actor (destroy_actor_state).result ();
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
    auto leave_actor =
      lifecycle_gateway.manager ().create ("player", "context-leave-player").value ();
    auto leave_context = leave_actor.context ();
    auto leave_stage_join =
      leave_context.join_spot (lifecycle_stage.spot_rid, std::string ("44")).async ().result ();
    if (!leave_stage_join || lifecycle_stage_spot->joined_count != 3) {
        return 76;
    }
    auto empty_leave =
      lifecycle_stage.context.leave_actor (zlink::framework::actor_ref_t{}, leave_actor_state)
        .result ();
    if (empty_leave || empty_leave.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 110;
    }
    auto wrong_spot_leave =
      lifecycle_stage.context.leave_actor (lifecycle_actor_context.actor_ref (), leave_actor_state)
        .result ();
    if (wrong_spot_leave
        || wrong_spot_leave.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 111;
    }
    auto stale_leave = lifecycle_stage.context
                         .leave_actor (zlink::framework::actor_ref_t (
                                         leave_context.actor_ref ().node_rid (),
                                         std::string (leave_context.actor_ref ().actor_type ()),
                                         std::string (leave_context.actor_ref ().actor_id ()),
                                         leave_context.actor_ref ().generation () + 1),
                                       leave_actor_state)
                         .result ();
    if (stale_leave
        || (stale_leave.error () != nullptr
         && zlink::framework::detail::boundary_state (*stale_leave.error ()) != zlink::framework::detail::boundary_error_t::stale_generation)) {
        return 112;
    }
    auto unjoined_actor =
      lifecycle_gateway.manager ().create ("player", "context-unjoined-player").value ();
    auto unjoined_leave = lifecycle_stage.context
                            .leave_actor (unjoined_actor.context ().actor_ref (), leave_actor_state)
                            .result ();
    if (!unjoined_leave) {
        return 113;
    }
    const auto stage_left_before_context_leave = lifecycle_stage_spot->left_count;
    const auto entry_joined_before_context_leave = lifecycle_entry_spot->joined_count;
    auto context_leave =
      lifecycle_stage.context.leave_actor (leave_context.actor_ref (), leave_actor_state).result ();
    if (!context_leave || lifecycle_stage_spot->left_count != stage_left_before_context_leave + 1
        || lifecycle_entry_spot->joined_count != entry_joined_before_context_leave + 1
        || leave_actor_state.ref_updates != 1
        || leave_actor_state.last_generation != context_leave.value ().generation ()) {
        return 77;
    }
    auto context_leave_destroy =
      lifecycle_entry_context.destroy_actor (leave_actor_state).result ();
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
        || !std::holds_alternative<zlink::framework::actor_join_accepted_t<zlink::framework::message_t>> (auto_destroy_initial_entry_join.value ())
        || auto_destroy_entry->joined_count != 1 || auto_destroy_entry->destroyed_count != 0) {
        return 95;
    }
    auto auto_destroy_stage_join =
      auto_destroy_context.join_spot (auto_destroy_stage_created.spot_rid, std::string ("46"))
        .async ()
        .result ();
    const auto *auto_destroy_stage_accepted =
      auto_destroy_stage_join ? std::get_if<zlink::framework::actor_join_accepted_t<zlink::framework::message_t>> (&auto_destroy_stage_join.value ()) : nullptr;
    if (auto_destroy_stage_accepted == nullptr || auto_destroy_stage->joined_count != 1) {
        return 96;
    }
    auto_destroy_entry->destroy_on_join = true;
    auto auto_destroy_leave =
      auto_destroy_stage_created.context
        .leave_actor (auto_destroy_stage_accepted->actor, auto_destroy_actor_state)
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
        std::get<zlink::framework::actor_join_accepted_t<zlink::framework::message_t>> (packet_leave_initial_join.value ()).actor,
        packet_leave_stage_created.spot_rid,
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

    auto remote_packet_leave_entry = std::make_shared<auto_destroy_entry_spot_t> ();
    auto remote_packet_leave_spot = std::make_shared<actor_packet_self_leave_spot_t> ();
    zlink::framework::zlink_builder_t remote_packet_leave_room_host;
    zlink::framework::detail::channel_runtime_t::from (remote_packet_leave_room_host.message_bus ())
      .bind_serializers (manual_serializers);
    auto remote_packet_leave_room_builder =
      remote_packet_leave_room_host.add_spot_node ("remote-packet-room-node");
    remote_packet_leave_room_builder.add_actor_factory<player_actor_factory_t> ("player")
      .add_spot<actor_packet_self_leave_spot_t> (
        "stage", [remote_packet_leave_spot] { return remote_packet_leave_spot; });
    auto remote_packet_leave_room_stage = remote_packet_leave_room_builder.create_spot ("stage");
    auto remote_packet_leave_room_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (remote_packet_leave_room_builder);

    zlink::framework::zlink_builder_t remote_packet_leave_entry_host;
    zlink::framework::detail::channel_runtime_t::from (
      remote_packet_leave_entry_host.message_bus ())
      .bind_serializers (manual_serializers);
    auto remote_packet_leave_entry_builder =
      remote_packet_leave_entry_host.add_spot_node ("remote-packet-entry-node");
    remote_packet_leave_entry_builder
      .add_entry_spot<auto_destroy_entry_spot_t> (
        [remote_packet_leave_entry] { return remote_packet_leave_entry; })
      .add_actor_factory<player_actor_factory_t> ("player");
    (void) remote_packet_leave_entry_builder.create_spot ("entry");
    auto remote_packet_leave_entry_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (remote_packet_leave_entry_builder);

    int remote_packet_entry_join_calls = 0;
    std::string remote_packet_entry_join_node;
    remote_packet_leave_room_runtime.on_actor_entry_spot_join (
      [&] (const zlink::framework::actor_ref_t &actor_ref, zlink::framework::node_rid_t node_rid,
           const zlink::message_t &request, const std::optional<zlink::message_t> &actor_snapshot) {
          ++remote_packet_entry_join_calls;
          remote_packet_entry_join_node = std::string (node_rid.value ());
          return remote_packet_leave_entry_runtime.join_actor_to_entry_spot_erased (
            actor_ref, std::move (node_rid), request, actor_snapshot);
      });
    const auto remote_packet_actor_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("remote-packet-entry-node"), "player",
      "remote-packet-leave-player", 1);
    auto remote_packet_stage_join =
      remote_packet_leave_room_runtime.join_remote_actor_to_spot_erased (
        remote_packet_actor_ref, remote_packet_leave_room_stage.spot_rid, zlink::message_t{},
        zlink::framework::actor_context_t{});
    if (!remote_packet_stage_join) {
        return 135;
    }
    remote_packet_leave_entry->destroy_on_join = true;
    auto remote_packet_leave_reply = remote_packet_leave_room_runtime.relay_actor_packet (
      remote_packet_actor_ref, zlink::framework::actor_context_t{}, "self.leave",
      zlink::message_t::from (std::string ("11")), packet_leave_provider, packet_leave_serializers);
    if (!remote_packet_leave_reply || !remote_packet_leave_reply.value ()
        || remote_packet_leave_reply.value ()->to_string () != "11") {
        return 136;
    }
    if (remote_packet_leave_spot->left_count != 1 || remote_packet_entry_join_calls != 1
        || remote_packet_entry_join_node != "remote-packet-entry-node") {
        return 137;
    }
    if (remote_packet_leave_entry->joined_count != 1) {
        return 138;
    }
    if (remote_packet_leave_entry->destroyed_count != 1) {
        return 140;
    }
    if (remote_packet_leave_entry->last_joined_moved_value != 11) {
        return 141;
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
        .leave_actor (context_leave_stage_join.value ().actor, context_leave_actor)
        .result ();
    if (!context_leave_thread_result || thread_probe_entry->joined_count != 2
        || thread_probe_entry->last_join_thread == context_leave_caller) {
        return 89;
    }

    auto stateful_source_spot = std::make_shared<stateful_relay_spot_t> ();
    auto stateful_target_spot = std::make_shared<stateful_relay_spot_t> ();
    zlink::framework::runtime::in_memory_location_store_t stateful_location_store;
    zlink::framework::runtime::location_runtime_t stateful_source_locations (
      stateful_location_store, {}, "stateful-source-owner");
    zlink::framework::runtime::location_runtime_t stateful_target_locations (
      stateful_location_store, {}, "stateful-target-owner");
    stateful_source_locations.start (zlink::routing_id_t::from ("stateful-source-node"));
    stateful_target_locations.start (zlink::routing_id_t::from ("stateful-target-node"));
    zlink::framework::runtime::location_lifecycle_t stateful_source_location_lifecycle (
      stateful_source_locations);
    zlink::framework::runtime::location_lifecycle_t stateful_target_location_lifecycle (
      stateful_target_locations);
    zlink::framework::zlink_builder_t stateful_source_host;
    zlink::framework::detail::channel_runtime_t::from (stateful_source_host.message_bus ())
      .bind_serializers (manual_serializers);
    auto stateful_source_builder = stateful_source_host.add_spot_node ("stateful-source-node");
    stateful_source_builder.add_actor_factory<stateful_relay_actor_factory_t> ("stateful-player")
      .add_actor_transfer_adapter<stateful_relay_actor_t, stateful_relay_transfer_t> (
        "stateful-player")
      .add_spot<stateful_relay_spot_t> ("stateful-source",
                                        [stateful_source_spot] { return stateful_source_spot; });
    zlink::framework::zlink_builder_t stateful_target_host;
    zlink::framework::detail::channel_runtime_t::from (stateful_target_host.message_bus ())
      .bind_serializers (manual_serializers);
    auto stateful_target_builder = stateful_target_host.add_spot_node ("stateful-target-node");
    stateful_target_builder.add_actor_factory<stateful_relay_actor_factory_t> ("stateful-player")
      .add_actor_transfer_adapter<stateful_relay_actor_t, stateful_relay_transfer_t> (
        "stateful-player")
      .add_spot<stateful_relay_spot_t> ("stateful-target",
                                        [stateful_target_spot] { return stateful_target_spot; });
    const auto stateful_source = stateful_source_builder.create_spot ("stateful-source");
    const auto stateful_target = stateful_target_builder.create_spot ("stateful-target");
    auto stateful_source_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (stateful_source_builder);
    auto stateful_target_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (stateful_target_builder);
    stateful_source_runtime.bind_location_lifecycle (stateful_source_location_lifecycle);
    stateful_target_runtime.bind_location_lifecycle (stateful_target_location_lifecycle);
    const auto stateful_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("stateful-source-node"), "stateful-player",
      "stateful-actor", 1);
    auto stateful_join = stateful_source_runtime.join_actor_to_spot_erased (
      stateful_ref, stateful_source.spot_rid, zlink::message_t{});
    if (!stateful_join) {
        return 155;
    }
    auto stateful_location =
      stateful_location_store
        .resolve_actor (zlink::framework::actor_location_key_t{"stateful-actor"})
        .result ()
        .value ();
    if (!stateful_location || !stateful_location->actor_ref
        || stateful_location->actor_ref->node_rid ().value () != "stateful-source-node"
        || !stateful_location->spot_rid
        || stateful_location->spot_rid->to_string () != stateful_source.spot_rid.value ()) {
        return 162;
    }
    auto stateful_instance =
      stateful_source_runtime.actor_instance<stateful_relay_actor_t> (stateful_join.value ().actor);
    if (!stateful_instance) {
        return 155;
    }
    stateful_instance->get ().state = "state-v7";
    auto stateful_admitted = stateful_target_runtime.admit_remote_actor_to_spot (
      "stateful-transfer-1", stateful_join.value ().actor, stateful_source.spot_rid,
      stateful_target.spot_rid, zlink::message_t{});
    auto stateful_transfer =
      stateful_source_runtime.transfer_actor_out (stateful_join.value ().actor);
    if (!stateful_transfer || stateful_transfer.value ().state.to_string () != "state-v7") {
        return 156;
    }
    auto stateful_left =
      stateful_source_runtime.leave_actor_for_remote_transfer (stateful_join.value ().actor);
    zlink::framework::detail::actor_gateway_runtime_t stateful_target_gateway;
    stateful_target_gateway.bind_serializers (manual_serializers);
    bool joined_push_received = false;
    stateful_target_gateway.bind_session_sink (
      stateful_join.value ().actor,
      [&joined_push_received] (std::string, const zlink::message_t &) {
          joined_push_received = true;
          return zlink::framework::task_t<void> (
            zlink::framework::result_t<void>::success ());
      });
    stateful_target_spot->push_on_joined = true;
    stateful_target_spot->block_next_joined ();
    auto stateful_commit_future = std::async (
      std::launch::async, [&stateful_target_runtime, &stateful_join, &stateful_target,
                           &stateful_target_gateway,
                           state = std::move (stateful_transfer.value ().state)] () mutable {
          return stateful_target_runtime.commit_remote_actor_to_spot (
            "stateful-transfer-1", stateful_join.value ().actor, stateful_target.spot_rid,
            std::move (state),
            stateful_target_gateway.actor_context (stateful_join.value ().actor));
      });
    if (!stateful_left || !stateful_admitted || !stateful_admitted.value ().accepted
        || !stateful_target_spot->wait_until_joined (std::chrono::seconds (1))) {
        stateful_target_spot->release_joined ();
        (void) stateful_commit_future.get ();
        return 157;
    }
    stateful_location = stateful_location_store
                          .resolve_actor (zlink::framework::actor_location_key_t{"stateful-actor"})
                          .result ()
                          .value ();
    zlink::framework::service_collection_t stateful_pending_services;
    auto stateful_pending_provider = stateful_pending_services.build_provider ();
    auto stateful_pending_packet = stateful_target_runtime.relay_actor_packet (
      stateful_join.value ().actor, {}, "state.read", zlink::message_t{}, stateful_pending_provider,
      manual_serializers);
    if (!stateful_location || !stateful_location->actor_ref
        || stateful_location->actor_ref->node_rid ().value () != "stateful-source-node"
        || !stateful_location->spot_rid
        || stateful_location->spot_rid->to_string () != stateful_source.spot_rid.value ()
        || stateful_pending_packet
        || stateful_pending_packet.error_kind ()
             != zlink::framework::framework_error_kind_t::actor_location_stale) {
        stateful_target_spot->release_joined ();
        (void) stateful_commit_future.get ();
        return 163;
    }
    stateful_target_spot->release_joined ();
    auto stateful_committed = stateful_commit_future.get ();
    if (!stateful_committed) {
        return 157;
    }
    if (!joined_push_received) {
        return 204;
    }
    stateful_location = stateful_location_store
                          .resolve_actor (zlink::framework::actor_location_key_t{"stateful-actor"})
                          .result ()
                          .value ();
    if (!stateful_location || !stateful_location->actor_ref
        || stateful_location->actor_ref->node_rid ().value () != "stateful-target-node"
        || !stateful_location->spot_rid
        || stateful_location->spot_rid->to_string () != stateful_target.spot_rid.value ()) {
        return 164;
    }
    const auto target_location_generation = stateful_location->generation;
    stateful_source_runtime.complete_remote_actor_transfer (
      stateful_join.value ().actor, stateful_committed.value ().actor,
      zlink::framework::spot_route_t{stateful_target_runtime.node_rid (), stateful_target.spot_rid,
                                     "stateful-target"});
    auto restored_stateful = stateful_target_runtime.actor_instance<stateful_relay_actor_t> (
      stateful_committed.value ().actor);
    if (!restored_stateful || restored_stateful->get ().state != "state-v7"
        || stateful_target_spot->joined_state != "state-v7") {
        return 157;
    }
    stateful_location = stateful_location_store
                          .resolve_actor (zlink::framework::actor_location_key_t{"stateful-actor"})
                          .result ()
                          .value ();
    if (!stateful_location || stateful_location->generation != target_location_generation
        || !stateful_location->actor_ref
        || stateful_location->actor_ref->node_rid ().value () != "stateful-target-node") {
        return 165;
    }
    stateful_source_locations.stop ();
    stateful_target_locations.stop ();

    // Regression: commit_remote_actor_to_spot must not hold the node mutex while
    // on_actor_joined runs. The callback may trigger work on another thread that
    // calls back into the runtime, which needs the node mutex.
    auto reentry_spot = std::make_shared<stateful_relay_spot_t> ();
    zlink::framework::zlink_builder_t reentry_host;
    zlink::framework::detail::channel_runtime_t::from (reentry_host.message_bus ())
      .bind_serializers (manual_serializers);
    auto reentry_builder = reentry_host.add_spot_node ("reentry-node");
    reentry_builder.add_actor_factory<stateful_relay_actor_factory_t> ("stateful-player")
      .add_spot<stateful_relay_spot_t> ("reentry-target",
                                        [reentry_spot] { return reentry_spot; });
    const auto reentry_target = reentry_builder.create_spot ("reentry-target");
    auto reentry_runtime = zlink::framework::detail::spot_node_runtime_t::from (reentry_builder);
    reentry_spot->joined_reentry_probe = [&reentry_runtime] {
        const auto blocked = reentry_runtime.transfer_actor_out (zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string ("reentry-source"), "stateful-player",
          "reentry-missing", 1));
        return !blocked;
    };
    const auto reentry_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("reentry-source"), "stateful-player",
      "reentry-actor", 1);
    auto reentry_admitted = reentry_runtime.admit_remote_actor_to_spot (
      "reentry-transfer-1", reentry_ref,
      zlink::framework::spot_rid_t::from_string ("reentry-source-spot"), reentry_target.spot_rid,
      zlink::message_t{});
    if (!reentry_admitted || !reentry_admitted.value ().accepted) {
        return 200;
    }
    auto reentry_committed = reentry_runtime.commit_remote_actor_to_spot (
      "reentry-transfer-1", reentry_ref, reentry_target.spot_rid, zlink::message_t{});
    if (!reentry_committed) {
        return 201;
    }
    if (!reentry_spot->joined_reentry_ok) {
        return 202;
    }

    // Regression: a stale location-loss notification must not erase the newer
    // forwarding route that a completed transfer recorded (generation fencing).
    zlink::framework::runtime::in_memory_location_store_t fencing_store;
    zlink::framework::runtime::location_runtime_t fencing_source_locations (
      fencing_store, {}, "fencing-source-owner");
    zlink::framework::runtime::location_runtime_t fencing_taker_locations (
      fencing_store, {}, "fencing-taker-owner");
    fencing_source_locations.start (zlink::routing_id_t::from ("fencing-source-node"));
    fencing_taker_locations.start (zlink::routing_id_t::from ("fencing-taker-node"));
    zlink::framework::runtime::location_lifecycle_t fencing_source_lifecycle (
      fencing_source_locations);
    zlink::framework::runtime::location_lifecycle_t fencing_taker_lifecycle (
      fencing_taker_locations);
    auto fencing_spot_instance = std::make_shared<stateful_relay_spot_t> ();
    zlink::framework::zlink_builder_t fencing_host;
    zlink::framework::detail::channel_runtime_t::from (fencing_host.message_bus ())
      .bind_serializers (manual_serializers);
    auto fencing_builder = fencing_host.add_spot_node ("fencing-source-node");
    fencing_builder.add_actor_factory<stateful_relay_actor_factory_t> ("stateful-player")
      .add_spot<stateful_relay_spot_t> ("fencing-spot",
                                        [fencing_spot_instance] { return fencing_spot_instance; });
    const auto fencing_spot = fencing_builder.create_spot ("fencing-spot");
    auto fencing_runtime = zlink::framework::detail::spot_node_runtime_t::from (fencing_builder);
    fencing_runtime.bind_location_lifecycle (fencing_source_lifecycle);
    const auto fencing_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("fencing-source-node"), "stateful-player",
      "fencing-actor", 1);
    auto fencing_join = fencing_runtime.join_actor_to_spot_erased (
      fencing_ref, fencing_spot.spot_rid, zlink::message_t{});
    if (!fencing_join || fencing_join.value ().result_code != 0) {
        return 203;
    }
    const auto fencing_moved = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("fencing-target-node"), "stateful-player",
      "fencing-actor", fencing_join.value ().actor.generation () + 1);
    fencing_runtime.record_actor_route (
      fencing_moved,
      zlink::framework::spot_route_t{
        zlink::framework::node_rid_t::from_string ("fencing-target-node"),
        zlink::framework::spot_rid_t::from_string ("fencing-target-spot"), "fencing-target"});
    auto fencing_taken = fencing_taker_lifecycle.claim_actor (
      zlink::framework::actor_location_t{
        .actor_id = "fencing-actor",
        .actor_type = "stateful-player",
        .actor_ref = fencing_moved,
        .node_rid = zlink::routing_id_t::from ("fencing-target-node"),
        .location_kind = zlink::spot_kind::user,
        .spot_mesh_name = "fencing-target",
        .spot_rid = zlink::routing_id_t::from ("fencing-target-spot"),
        .generation = 0},
      {}, true);
    if (fencing_taken.status != zlink::framework::location_write_status_t::stored) {
        return 204;
    }
    (void) fencing_source_lifecycle.renew_actor (
      zlink::framework::actor_location_key_t{"fencing-actor"});
    auto fencing_current = fencing_runtime.current_actor_ref (fencing_join.value ().actor);
    if (!fencing_current || fencing_current->generation () != fencing_moved.generation ()) {
        return 205;
    }
    fencing_source_locations.stop ();
    fencing_taker_locations.stop ();

    // In-flight handoff (§10): sends that arrive while the actor is moving are
    // preserved in arrival order, travel with the commit, and replay on the
    // target before any direct packet can overtake them; requests fail fast as
    // retriable. The straggler forwarding mapping is evicted once its window
    // elapses, leaving only the generation tombstone.
    auto handoff_source_spot = std::make_shared<stateful_relay_spot_t> ();
    auto handoff_target_spot = std::make_shared<stateful_relay_spot_t> ();
    zlink::framework::zlink_builder_t handoff_source_host;
    zlink::framework::detail::channel_runtime_t::from (handoff_source_host.message_bus ())
      .bind_serializers (manual_serializers);
    auto handoff_source_builder = handoff_source_host.add_spot_node ("handoff-source-node");
    handoff_source_builder.add_actor_factory<stateful_relay_actor_factory_t> ("stateful-player")
      .add_actor_transfer_adapter<stateful_relay_actor_t, stateful_relay_transfer_t> (
        "stateful-player")
      .add_spot<stateful_relay_spot_t> ("handoff-source",
                                        [handoff_source_spot] { return handoff_source_spot; });
    zlink::framework::zlink_builder_t handoff_target_host;
    zlink::framework::detail::channel_runtime_t::from (handoff_target_host.message_bus ())
      .bind_serializers (manual_serializers);
    auto handoff_target_builder = handoff_target_host.add_spot_node ("handoff-target-node");
    handoff_target_builder.add_actor_factory<stateful_relay_actor_factory_t> ("stateful-player")
      .add_actor_transfer_adapter<stateful_relay_actor_t, stateful_relay_transfer_t> (
        "stateful-player")
      .add_spot<stateful_relay_spot_t> ("handoff-target",
                                        [handoff_target_spot] { return handoff_target_spot; });
    const auto handoff_source = handoff_source_builder.create_spot ("handoff-source");
    const auto handoff_target = handoff_target_builder.create_spot ("handoff-target");
    auto handoff_source_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (handoff_source_builder);
    auto handoff_target_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (handoff_target_builder);
    const auto handoff_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("handoff-source-node"), "stateful-player",
      "handoff-actor", 1);
    auto handoff_join = handoff_source_runtime.join_actor_to_spot_erased (
      handoff_ref, handoff_source.spot_rid, zlink::message_t{});
    if (!handoff_join || handoff_join.value ().result_code != 0) {
        return 206;
    }
    auto handoff_instance =
      handoff_source_runtime.actor_instance<stateful_relay_actor_t> (handoff_join.value ().actor);
    if (!handoff_instance) {
        return 206;
    }
    handoff_instance->get ().state = "handoff-v1";
    auto handoff_admitted = handoff_target_runtime.admit_remote_actor_to_spot (
      "handoff-transfer-1", handoff_join.value ().actor, handoff_source.spot_rid,
      handoff_target.spot_rid, zlink::message_t{});
    auto handoff_transfer = handoff_source_runtime.transfer_actor_out (handoff_join.value ().actor);
    if (!handoff_admitted || !handoff_admitted.value ().accepted || !handoff_transfer) {
        return 207;
    }
    zlink::framework::service_collection_t handoff_services;
    auto handoff_provider = handoff_services.build_provider ();
    auto handoff_moving_send_1 = handoff_source_runtime.relay_actor_packet (
      handoff_join.value ().actor, {}, zlink::framework::detail::stream_message_kind_t::send, "state.note",
      zlink::message_t::from (std::string ("note-1")), handoff_provider, manual_serializers);
    auto handoff_moving_send_2 = handoff_source_runtime.relay_actor_packet (
      handoff_join.value ().actor, {}, zlink::framework::detail::stream_message_kind_t::send, "state.note",
      zlink::message_t::from (std::string ("note-2")), handoff_provider, manual_serializers);
    if (!handoff_moving_send_1 || !handoff_moving_send_2
        || !handoff_source_spot->notes_snapshot ().empty ()) {
        return 208;
    }
    zlink::framework::spot_actor_message_metadata_t handoff_request_metadata;
    handoff_request_metadata.values["__zlink.actorRequestId"] = "handoff-request-1";
    auto handoff_moving_request = handoff_source_runtime.relay_actor_packet (
      handoff_join.value ().actor, {}, "state.read", zlink::message_t{}, handoff_provider,
      manual_serializers, handoff_request_metadata);
    auto handoff_moving_request_retry = handoff_source_runtime.relay_actor_packet (
      handoff_join.value ().actor, {}, "state.read", zlink::message_t{}, handoff_provider,
      manual_serializers, handoff_request_metadata);
    if (handoff_moving_request
        || handoff_moving_request.error_kind ()
             != zlink::framework::framework_error_kind_t::actor_location_stale
        || !handoff_moving_request.error () || !handoff_moving_request.error ()->is_retriable ()
        || handoff_moving_request_retry
        || handoff_moving_request_retry.error_kind ()
             != zlink::framework::framework_error_kind_t::actor_location_stale) {
        return 209;
    }
    auto handoff_left =
      handoff_source_runtime.leave_actor_for_remote_transfer (handoff_join.value ().actor);
    if (!handoff_left) {
        return 210;
    }
    auto handoff_backlog =
      handoff_source_runtime.take_actor_handoff_backlog (handoff_join.value ().actor);
    // §10.2-1: the two moving sends AND the moving request are all preserved in
    // arrival order (the request also failed fast at 209, but is not dropped —
    // it reaches the committed target's handler for a best-effort late reply,
    // §10.5). The request carries is_request so the replay dispatches it as a
    // request rather than a send. A retry with the same request id does not add
    // a duplicate backlog entry.
    if (handoff_backlog.size () != 3 || handoff_backlog[0].packet_name != "state.note"
        || handoff_backlog[0].is_request || handoff_backlog[1].packet_name != "state.note"
        || handoff_backlog[1].is_request || handoff_backlog[2].packet_name != "state.read"
        || !handoff_backlog[2].is_request
        || !handoff_source_runtime.take_actor_handoff_backlog (handoff_join.value ().actor)
              .empty ()) {
        return 211;
    }
    auto handoff_committed = handoff_target_runtime.commit_remote_actor_to_spot (
      "handoff-transfer-1", handoff_join.value ().actor, handoff_target.spot_rid,
      std::move (handoff_transfer.value ().state), {}, std::move (handoff_backlog),
      &handoff_provider);
    if (!handoff_committed) {
        return 212;
    }
    auto handoff_direct_send = handoff_target_runtime.relay_actor_packet (
      handoff_committed.value ().actor, {}, zlink::framework::detail::stream_message_kind_t::send,
      "state.note", zlink::message_t::from (std::string ("note-3")), handoff_provider,
      manual_serializers);
    if (!handoff_direct_send) {
        return 213;
    }
    if (!handoff_target_spot->wait_for_notes (3, std::chrono::seconds (2))) {
        return 214;
    }
    const auto handoff_notes = handoff_target_spot->notes_snapshot ();
    if (handoff_notes.size () != 3 || handoff_notes[0] != "note-1" || handoff_notes[1] != "note-2"
        || handoff_notes[2] != "note-3") {
        return 214;
    }
    handoff_source_runtime.complete_remote_actor_transfer (
      handoff_join.value ().actor, handoff_committed.value ().actor,
      zlink::framework::spot_route_t{handoff_target_runtime.node_rid (), handoff_target.spot_rid,
                                     "handoff-target"});
    // Default window: the forwarding mapping must survive cleanup while active.
    if (handoff_source_runtime.cleanup_expired_actor_admissions () != 0
        || !handoff_source_runtime.actor_route (handoff_join.value ().actor)) {
        return 215;
    }
    // Second transfer with a zero window: the mapping is evicted on cleanup
    // while the first actor's active mapping stays, and only the generation
    // tombstone survives so stale refs keep resolving to the new generation.
    const auto handoff_evict_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("handoff-source-node"), "stateful-player",
      "handoff-actor-2", 1);
    auto handoff_evict_join = handoff_source_runtime.join_actor_to_spot_erased (
      handoff_evict_ref, handoff_source.spot_rid, zlink::message_t{});
    if (!handoff_evict_join || handoff_evict_join.value ().result_code != 0) {
        return 216;
    }
    auto handoff_evict_admitted = handoff_target_runtime.admit_remote_actor_to_spot (
      "handoff-transfer-2", handoff_evict_join.value ().actor, handoff_source.spot_rid,
      handoff_target.spot_rid, zlink::message_t{});
    auto handoff_evict_transfer =
      handoff_source_runtime.transfer_actor_out (handoff_evict_join.value ().actor);
    if (!handoff_evict_admitted || !handoff_evict_admitted.value ().accepted
        || !handoff_evict_transfer) {
        return 216;
    }
    auto handoff_evict_left =
      handoff_source_runtime.leave_actor_for_remote_transfer (handoff_evict_join.value ().actor);
    if (!handoff_evict_left) {
        return 216;
    }
    auto handoff_evict_committed = handoff_target_runtime.commit_remote_actor_to_spot (
      "handoff-transfer-2", handoff_evict_join.value ().actor, handoff_target.spot_rid,
      std::move (handoff_evict_transfer.value ().state));
    if (!handoff_evict_committed) {
        return 216;
    }
    handoff_source_runtime.set_actor_transfer_forward_window (std::chrono::milliseconds (0));
    handoff_source_runtime.complete_remote_actor_transfer (
      handoff_evict_join.value ().actor, handoff_evict_committed.value ().actor,
      zlink::framework::spot_route_t{handoff_target_runtime.node_rid (), handoff_target.spot_rid,
                                     "handoff-target"});
    if (!handoff_source_runtime.actor_route (handoff_evict_join.value ().actor)) {
        return 217;
    }
    if (handoff_source_runtime.cleanup_expired_actor_admissions () == 0
        || handoff_source_runtime.actor_route (handoff_evict_join.value ().actor)
        || !handoff_source_runtime.actor_route (handoff_join.value ().actor)) {
        return 217;
    }
    auto handoff_evict_current =
      handoff_source_runtime.current_actor_ref (handoff_evict_join.value ().actor);
    if (!handoff_evict_current
        || handoff_evict_current->generation ()
             != handoff_evict_committed.value ().actor.generation ()) {
        return 218;
    }

    auto local_move_probe = std::make_shared<stateful_lifecycle_probe_t> ();
    auto local_move_source_spot = std::make_shared<stateful_relay_spot_t> ();
    auto local_move_target_spot = std::make_shared<stateful_relay_spot_t> ();
    local_move_source_spot->lifecycle_probe = local_move_probe;
    local_move_target_spot->lifecycle_probe = local_move_probe;
    zlink::framework::zlink_builder_t local_move_host;
    zlink::framework::detail::channel_runtime_t::from (local_move_host.message_bus ())
      .bind_serializers (manual_serializers);
    auto local_move_builder = local_move_host.add_spot_node ("local-move-node");
    local_move_builder.add_actor_factory<stateful_relay_actor_factory_t> ("stateful-player")
      .add_spot<stateful_relay_spot_t> ("local-source",
                                        [local_move_source_spot] { return local_move_source_spot; })
      .add_spot<stateful_relay_spot_t> (
        "local-target", [local_move_target_spot] { return local_move_target_spot; });
    const auto local_source = local_move_builder.create_spot ("local-source");
    const auto local_target = local_move_builder.create_spot ("local-target");
    auto local_move_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (local_move_builder);
    const auto local_actor_ref =
      zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("local-move-node"),
                                     "stateful-player", "local-moving-actor", 1);
    auto local_initial_join = local_move_runtime.join_actor_to_spot_erased (
      local_actor_ref, local_source.spot_rid, zlink::message_t{});
    if (!local_initial_join) {
        return 166;
    }
    auto local_actor =
      local_move_runtime.actor_instance<stateful_relay_actor_t> (local_initial_join.value ().actor);
    if (!local_actor) {
        return 166;
    }
    local_actor->get ().state = "local-state";
    local_move_probe->clear ();
    local_move_target_spot->block_next_joined ();
    auto local_join_future =
      std::async (std::launch::async, [&local_move_runtime, &local_initial_join, &local_target] {
          return local_move_runtime.join_actor_to_spot_erased (
            local_initial_join.value ().actor, local_target.spot_rid, zlink::message_t{});
      });
    if (!local_move_target_spot->wait_until_joined (std::chrono::seconds (1))) {
        local_move_target_spot->release_joined ();
        (void) local_join_future.get ();
        return 167;
    }
    zlink::framework::service_collection_t local_move_services;
    auto local_move_provider = local_move_services.build_provider ();
    auto source_packet_during_move = local_move_runtime.relay_actor_packet (
      local_initial_join.value ().actor, {}, "state.read", zlink::message_t::from ("1"),
      local_move_provider, manual_serializers);
    const auto prospective_local_actor =
      zlink::framework::actor_ref_t (local_initial_join.value ().actor.node_rid (),
                                     std::string (local_initial_join.value ().actor.actor_type ()),
                                     std::string (local_initial_join.value ().actor.actor_id ()),
                                     local_initial_join.value ().actor.generation () + 1);
    auto target_packet_during_move = local_move_runtime.relay_actor_packet (
      prospective_local_actor, {}, "state.read", zlink::message_t::from ("2"), local_move_provider,
      manual_serializers);
    const auto events_during_move = local_move_probe->snapshot ();
    if (source_packet_during_move || target_packet_during_move
        || source_packet_during_move.error_kind ()
             != zlink::framework::framework_error_kind_t::actor_location_stale
        || target_packet_during_move.error_kind ()
             != zlink::framework::framework_error_kind_t::actor_location_stale
        || events_during_move != std::vector<std::string> ({"admission", "leave"})) {
        local_move_target_spot->release_joined ();
        (void) local_join_future.get ();
        return 168;
    }
    local_move_target_spot->release_joined ();
    auto local_joined = local_join_future.get ();
    if (!local_joined) {
        return 169;
    }
    auto local_follow_up = local_move_runtime.relay_actor_packet (
      local_joined.value ().actor, {}, "state.read", zlink::message_t::from ("3"),
      local_move_provider, manual_serializers);
    if (local_move_probe->snapshot () != std::vector<std::string> ({"admission", "leave", "joined"})
        || !local_follow_up || !local_follow_up.value ()
        || local_follow_up.value ()->to_string () != "local-state") {
        return 169;
    }

    auto failure_source_spot = std::make_shared<stateful_relay_spot_t> ();
    auto failure_target_spot = std::make_shared<stateful_relay_spot_t> ();
    zlink::framework::zlink_builder_t failure_source_host;
    zlink::framework::detail::channel_runtime_t::from (failure_source_host.message_bus ())
      .bind_serializers (manual_serializers);
    auto failure_source_builder = failure_source_host.add_spot_node ("failure-source-node");
    failure_source_builder.add_actor_factory<stateful_relay_actor_factory_t> ("failure-player")
      .add_actor_transfer_adapter<stateful_relay_actor_t, controllable_stateful_transfer_t> (
        "failure-player")
      .add_spot<stateful_relay_spot_t> ("failure-source",
                                        [failure_source_spot] { return failure_source_spot; });
    zlink::framework::zlink_builder_t failure_target_host;
    zlink::framework::detail::channel_runtime_t::from (failure_target_host.message_bus ())
      .bind_serializers (manual_serializers);
    auto failure_target_builder = failure_target_host.add_spot_node ("failure-target-node");
    failure_target_builder.add_actor_factory<stateful_relay_actor_factory_t> ("failure-player")
      .add_actor_transfer_adapter<stateful_relay_actor_t, controllable_stateful_transfer_t> (
        "failure-player")
      .add_spot<stateful_relay_spot_t> ("failure-target",
                                        [failure_target_spot] { return failure_target_spot; });
    const auto failure_source = failure_source_builder.create_spot ("failure-source");
    const auto failure_target = failure_target_builder.create_spot ("failure-target");
    auto failure_source_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (failure_source_builder);
    auto failure_target_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (failure_target_builder);
    zlink::framework::service_collection_t failure_services;
    auto failure_provider = failure_services.build_provider ();
    auto create_failure_actor = [&] (std::string actor_id) {
        const auto initial = zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string ("failure-source-node"), "failure-player",
          std::move (actor_id), 1);
        return failure_source_runtime.join_actor_to_spot_erased (initial, failure_source.spot_rid,
                                                                 zlink::message_t{});
    };
    auto admit_failure_actor = [&] (std::string transfer_id,
                                    const zlink::framework::actor_ref_t &actor) {
        return failure_target_runtime.admit_remote_actor_to_spot (
          std::move (transfer_id), actor, failure_source.spot_rid, failure_target.spot_rid,
          zlink::message_t{});
    };
    auto source_packet = [&] (const zlink::framework::actor_ref_t &actor) {
        return failure_source_runtime.relay_actor_packet (
          actor, {}, "state.read", zlink::message_t::from (std::string ("1")), failure_provider,
          manual_serializers);
    };

    auto transfer_out_actor = create_failure_actor ("fail-transfer-out");
    if (!transfer_out_actor) {
        return 170;
    }
    auto transfer_out_admission =
      admit_failure_actor ("fail-transfer-out", transfer_out_actor.value ().actor);
    if (!transfer_out_admission) {
        return 170;
    }
    controllable_stateful_transfer_t::fail_out = true;
    auto transfer_out_failure =
      failure_source_runtime.transfer_actor_out (transfer_out_actor.value ().actor);
    controllable_stateful_transfer_t::fail_out = false;
    auto transfer_out_source_packet = source_packet (transfer_out_actor.value ().actor);
    if (transfer_out_failure
        || !failure_source_runtime.actor_spot (transfer_out_actor.value ().actor)
        || !transfer_out_source_packet) {
        return 170;
    }

    auto leave_failure_actor = create_failure_actor ("fail-leave");
    if (!leave_failure_actor) {
        return 171;
    }
    auto leave_failure_admission =
      admit_failure_actor ("fail-leave", leave_failure_actor.value ().actor);
    if (!leave_failure_admission) {
        return 171;
    }
    auto leave_failure_state =
      failure_source_runtime.transfer_actor_out (leave_failure_actor.value ().actor);
    if (!leave_failure_state) {
        return 171;
    }
    failure_source_spot->fail_leave = true;
    auto leave_failure =
      failure_source_runtime.leave_actor_for_remote_transfer (leave_failure_actor.value ().actor);
    failure_source_spot->fail_leave = false;
    auto leave_failure_source_packet = source_packet (leave_failure_actor.value ().actor);
    if (leave_failure || !failure_source_runtime.actor_spot (leave_failure_actor.value ().actor)
        || !leave_failure_source_packet) {
        return 171;
    }

    auto transfer_in_actor = create_failure_actor ("fail-transfer-in");
    if (!transfer_in_actor) {
        return 172;
    }
    auto transfer_in_admission =
      admit_failure_actor ("fail-transfer-in", transfer_in_actor.value ().actor);
    if (!transfer_in_admission) {
        return 172;
    }
    auto transfer_in_state =
      failure_source_runtime.transfer_actor_out (transfer_in_actor.value ().actor);
    if (!transfer_in_state) {
        return 172;
    }
    auto transfer_in_left =
      failure_source_runtime.leave_actor_for_remote_transfer (transfer_in_actor.value ().actor);
    controllable_stateful_transfer_t::fail_in = true;
    auto transfer_in_failure = failure_target_runtime.commit_remote_actor_to_spot (
      "fail-transfer-in", transfer_in_actor.value ().actor, failure_target.spot_rid,
      std::move (transfer_in_state.value ().state));
    controllable_stateful_transfer_t::fail_in = false;
    failure_source_runtime.fail_remote_actor_transfer (transfer_in_actor.value ().actor, true);
    auto transfer_in_source_packet = source_packet (transfer_in_actor.value ().actor);
    if (!transfer_in_left || transfer_in_failure || transfer_in_source_packet
        || transfer_in_source_packet.error_kind ()
             != zlink::framework::framework_error_kind_t::actor_location_stale
        || failure_target_runtime.actor_instance<stateful_relay_actor_t> (
          transfer_in_actor.value ().actor)) {
        return 172;
    }

    auto joined_failure_actor = create_failure_actor ("fail-joined");
    if (!joined_failure_actor) {
        return 173;
    }
    auto joined_failure_admission =
      admit_failure_actor ("fail-joined", joined_failure_actor.value ().actor);
    if (!joined_failure_admission) {
        return 173;
    }
    auto joined_failure_state =
      failure_source_runtime.transfer_actor_out (joined_failure_actor.value ().actor);
    if (!joined_failure_state) {
        return 173;
    }
    auto joined_failure_left =
      failure_source_runtime.leave_actor_for_remote_transfer (joined_failure_actor.value ().actor);
    failure_target_spot->fail_joined = true;
    auto joined_failure = failure_target_runtime.commit_remote_actor_to_spot (
      "fail-joined", joined_failure_actor.value ().actor, failure_target.spot_rid,
      std::move (joined_failure_state.value ().state));
    failure_target_spot->fail_joined = false;
    failure_source_runtime.fail_remote_actor_transfer (joined_failure_actor.value ().actor, true);
    auto joined_failure_source_packet = source_packet (joined_failure_actor.value ().actor);
    auto joined_failure_target_packet = failure_target_runtime.relay_actor_packet (
      joined_failure_actor.value ().actor, {}, "state.read",
      zlink::message_t::from (std::string ("1")), failure_provider, manual_serializers);
    if (!joined_failure_left || joined_failure || joined_failure_source_packet
        || joined_failure_target_packet
        || joined_failure_source_packet.error_kind ()
             != zlink::framework::framework_error_kind_t::actor_location_stale
        || joined_failure_target_packet.error_kind ()
             != zlink::framework::framework_error_kind_t::actor_location_stale
        || !failure_target_runtime.actor_instance<stateful_relay_actor_t> (
          joined_failure_actor.value ().actor)) {
        return 173;
    }

    empty_relay_transfer_t::transfer_out_count = 0;
    empty_relay_transfer_t::transfer_in_count = 0;
    empty_relay_transfer_t::transfer_in_received_empty = false;
    zlink::framework::zlink_builder_t empty_transfer_source_host;
    auto empty_transfer_source_builder =
      empty_transfer_source_host.add_spot_node ("empty-transfer-source");
    empty_transfer_source_builder.add_actor_factory<relay_actor_factory_t> ("empty-player")
      .add_actor_transfer_adapter<relay_actor_t, empty_relay_transfer_t> ("empty-player")
      .add_spot<relay_spot_t> ("empty-source-room");
    zlink::framework::zlink_builder_t empty_transfer_target_host;
    auto empty_transfer_target_builder =
      empty_transfer_target_host.add_spot_node ("empty-transfer-target");
    empty_transfer_target_builder.add_actor_factory<relay_actor_factory_t> ("empty-player")
      .add_actor_transfer_adapter<relay_actor_t, empty_relay_transfer_t> ("empty-player")
      .add_spot<relay_spot_t> ("empty-target-room");
    const auto empty_source_spot =
      empty_transfer_source_builder.create_spot ("empty-source-room");
    const auto empty_target_spot =
      empty_transfer_target_builder.create_spot ("empty-target-room");
    auto empty_source_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (empty_transfer_source_builder);
    auto empty_target_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (empty_transfer_target_builder);
    const auto empty_source_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("empty-transfer-source"), "empty-player",
      "empty-state-actor", 1);
    auto empty_source_join = empty_source_runtime.join_actor_to_spot_erased (
      empty_source_ref, empty_source_spot.spot_rid, zlink::message_t{});
    if (!empty_source_join) {
        return 180;
    }
    auto empty_admission = empty_target_runtime.admit_remote_actor_to_spot (
      "empty-transfer-1", empty_source_join.value ().actor, empty_source_spot.spot_rid,
      empty_target_spot.spot_rid, zlink::message_t{});
    auto empty_state = empty_source_runtime.transfer_actor_out (empty_source_join.value ().actor);
    auto empty_left =
      empty_source_runtime.leave_actor_for_remote_transfer (empty_source_join.value ().actor);
    if (!empty_admission || !empty_admission.value ().accepted || !empty_state
        || !empty_state.value ().state.is_empty () || !empty_left) {
        return 180;
    }
    auto empty_committed = empty_target_runtime.commit_remote_actor_to_spot (
      "empty-transfer-1", empty_source_join.value ().actor, empty_target_spot.spot_rid,
      std::move (empty_state.value ().state));
    if (!empty_committed || empty_relay_transfer_t::transfer_out_count != 1
        || empty_relay_transfer_t::transfer_in_count != 1
        || !empty_relay_transfer_t::transfer_in_received_empty
        || !empty_target_runtime.actor_instance<relay_actor_t> (empty_committed.value ().actor)) {
        return 180;
    }

    zlink::framework::spot_node_builder_t relay_builder;
    zlink::framework::zlink_builder_t relay_host;
    relay_builder = relay_host.add_spot_node ("relay-stage");
    auto relay_entry_spot = std::make_shared<relay_entry_spot_t> ();
    relay_builder
      .add_entry_spot<relay_entry_spot_t> ([relay_entry_spot] { return relay_entry_spot; })
      .add_actor_factory<relay_actor_factory_t> ("relay-player")
      .add_spot<relay_spot_t> ("relay-room");
    (void) relay_builder.create_spot ("entry");
    auto relay_spot = relay_builder.create_spot ("relay-room");
    auto relay_runtime = zlink::framework::detail::spot_node_runtime_t::from (relay_builder);
    const auto routed_actor_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("play-a"), "relay-player", "routed-actor", 9);
    relay_spot_t::left_count = 0;
    relay_spot_t::disconnected_count = 0;
    relay_spot_t::joined_count = 0;
    const auto transfer_source_ref =
      zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("play-source"),
                                     "relay-player", "transferred-actor", 4);
    auto admitted = relay_runtime.admit_remote_actor_to_spot (
      "transfer-runtime-1", transfer_source_ref,
      zlink::framework::spot_rid_t::from_string ("source-room"), relay_spot.spot_rid,
      zlink::message_t{});
    if (!admitted || !admitted.value ().accepted
        || relay_runtime.actor_instance<relay_actor_t> (transfer_source_ref)) {
        return 144;
    }
    auto committed = relay_runtime.commit_remote_actor_to_spot (
      "transfer-runtime-1", transfer_source_ref, relay_spot.spot_rid, zlink::message_t{});
    const auto transferred_instance =
      relay_runtime.actor_instance<relay_actor_t> (transfer_source_ref);
    if (!committed || committed.value ().actor.generation () != 5
        || committed.value ().actor.node_rid ().value () != "relay-stage" || !transferred_instance
        || transferred_instance->get ().actor_id != "transferred-actor"
        || relay_spot_t::joined_count != 1) {
        return 145;
    }
    auto duplicate_commit = relay_runtime.commit_remote_actor_to_spot (
      "transfer-runtime-1", transfer_source_ref, relay_spot.spot_rid, zlink::message_t{});
    if (duplicate_commit
        || duplicate_commit.error_kind ()
             != zlink::framework::framework_error_kind_t::request_protocol_error) {
        return 146;
    }

    zlink::framework::zlink_builder_t expiring_admission_host;
    expiring_admission_host.default_request_timeout (std::chrono::milliseconds (5));
    auto expiring_admission_builder =
      expiring_admission_host.add_spot_node ("expiring-admission-node");
    expiring_admission_builder.add_actor_factory<relay_actor_factory_t> ("relay-player")
      .add_spot<relay_spot_t> ("expiring-room");
    const auto expiring_spot = expiring_admission_builder.create_spot ("expiring-room");
    auto expiring_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (expiring_admission_builder);
    const auto expiring_actor =
      zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("expired-source"),
                                     "relay-player", "expired-actor", 9);
    auto expiring_admitted = expiring_runtime.admit_remote_actor_to_spot (
      "expiring-transfer", expiring_actor,
      zlink::framework::spot_rid_t::from_string ("expired-source-spot"), expiring_spot.spot_rid,
      zlink::message_t{});
    zlink::framework::service_collection_t expiring_services;
    auto expiring_provider = expiring_services.build_provider ();
    zlink::framework::serializer_registry_t expiring_serializers;
    auto packet_during_pending =
      expiring_runtime.relay_actor_packet (expiring_actor, {}, "relay.request", zlink::message_t{},
                                           expiring_provider, expiring_serializers);
    if (!expiring_admitted || !expiring_admitted.value ().accepted || packet_during_pending
        || packet_during_pending.error_kind ()
             != zlink::framework::framework_error_kind_t::actor_location_stale) {
        return 159;
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (10));
    if (expiring_runtime.cleanup_expired_actor_admissions () != 1) {
        return 160;
    }
    auto expired_commit = expiring_runtime.commit_remote_actor_to_spot (
      "expiring-transfer", expiring_actor, expiring_spot.spot_rid, zlink::message_t{});
    if (expired_commit
        || expired_commit.error_kind ()
             != zlink::framework::framework_error_kind_t::request_protocol_error) {
        return 161;
    }
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
      relay_spot.context.leave_actor (routed_join.value ().actor, routed_instance->get ())
        .result ();
    if (!remote_leave || remote_leave.value ().node_rid ().value () != "play-a"
        || relay_spot.context.manager ().current_actor_ref (routed_actor_ref)
        || relay_runtime.actor_spot (routed_actor_ref) || relay_spot_t::left_count == 0) {
        return 82;
    }
    const auto source_transfer_ref =
      zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("relay-stage"),
                                     "relay-player", "source-transfer-actor", 1);
    auto source_transfer_join = relay_runtime.join_actor_to_spot_erased (
      source_transfer_ref, relay_spot.spot_rid, zlink::message_t{});
    auto source_transfer = relay_runtime.transfer_actor_out (source_transfer_join.value ().actor);
    if (!source_transfer || !source_transfer.value ().state.is_empty ()
        || source_transfer.value ().source_spot_rid.value () != relay_spot.spot_rid.value ()) {
        return 151;
    }
    auto source_left =
      relay_runtime.leave_actor_for_remote_transfer (source_transfer_join.value ().actor);
    if (!source_left || relay_runtime.actor_spot (source_transfer_join.value ().actor)) {
        return 152;
    }
    zlink::framework::service_collection_t moving_services;
    auto moving_provider = moving_services.build_provider ();
    zlink::framework::serializer_registry_t moving_serializers;
    auto moving_packet =
      relay_runtime.relay_actor_packet (source_transfer_join.value ().actor, {}, "relay.request",
                                        zlink::message_t{}, moving_provider, moving_serializers);
    if (moving_packet
        || moving_packet.error_kind ()
             != zlink::framework::framework_error_kind_t::actor_location_stale) {
        return 153;
    }
    const auto target_transfer_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("relay-target"), "relay-player",
      "source-transfer-actor", source_transfer_join.value ().actor.generation () + 1);
    relay_runtime.complete_remote_actor_transfer (
      source_transfer_join.value ().actor, target_transfer_ref,
      zlink::framework::spot_route_t{zlink::framework::node_rid_t::from_string ("relay-target"),
                                     zlink::framework::spot_rid_t::from_string ("target-room"),
                                     "target"});
    if (!relay_runtime.actor_instance<relay_actor_t> (source_transfer_join.value ().actor)
        || !relay_runtime.actor_route (target_transfer_ref)
        || relay_runtime.actor_route (target_transfer_ref)->node_rid.value () != "relay-target") {
        return 154;
    }
    relay_runtime.cleanup_expired_actor_admissions_at (
      std::chrono::steady_clock::now () + std::chrono::seconds (2));
    if (relay_runtime.actor_instance<relay_actor_t> (source_transfer_join.value ().actor)
        || !relay_runtime.actor_route (target_transfer_ref)
        || relay_runtime.actor_route (target_transfer_ref)->node_rid.value () != "relay-target") {
        return 154;
    }
    zlink::framework::serializer_registry_t route_join_serializers;
    zlink::framework::detail::register_spot_route_packet_serializers (route_join_serializers);
    const auto admission_packet = zlink::framework::detail::spot_actor_admission_route_request_t{
      .transfer_id = "transfer-1",
      .actor_node_rid = "play-a",
      .actor_type = "relay-player",
      .actor_id = "actor-1",
      .actor_generation = 7,
      .source_spot_rid = "source-spot",
      .target_spot_rid = "target-spot",
      .payload = {1, 2, 3}};
    const auto admission_encoded =
      route_join_serializers.get<zlink::framework::detail::spot_actor_admission_route_request_t> ()
        .serialize (admission_packet);
    const auto admission_decoded =
      route_join_serializers.get<zlink::framework::detail::spot_actor_admission_route_request_t> ()
        .deserialize (admission_encoded);
    if (admission_decoded.transfer_id != "transfer-1"
        || admission_decoded.source_spot_rid != "source-spot"
        || admission_decoded.target_spot_rid != "target-spot"
        || admission_decoded.payload != std::vector<std::uint8_t> ({1, 2, 3})) {
        return 142;
    }
    const auto commit_packet = zlink::framework::detail::spot_actor_commit_route_request_t{
      .transfer_id = "transfer-1",
      .actor_node_rid = "play-a",
      .actor_type = "relay-player",
      .actor_id = "actor-1",
      .actor_generation = 7,
      .target_spot_rid = "target-spot",
      .bound_session_node_rid = "session-a",
      .bound_session_rid = "session-1",
      .transfer_state = {4, 5, 6}};
    const auto commit_encoded =
      route_join_serializers.get<zlink::framework::detail::spot_actor_commit_route_request_t> ()
        .serialize (commit_packet);
    const auto commit_decoded =
      route_join_serializers.get<zlink::framework::detail::spot_actor_commit_route_request_t> ()
        .deserialize (commit_encoded);
    if (commit_decoded.transfer_id != "transfer-1"
        || commit_decoded.bound_session_node_rid != "session-a"
        || commit_decoded.bound_session_rid != "session-1"
        || commit_decoded.transfer_state != std::vector<std::uint8_t> ({4, 5, 6})) {
        return 143;
    }
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
      zlink::framework::detail::spot_actor_admission_route_request_t::packet_name;
    const auto routed_admission = zlink::framework::detail::spot_actor_admission_route_request_t{
      .transfer_id = "routed-transfer-1",
      .actor_node_rid = "play-source",
      .actor_type = "relay-player",
      .actor_id = "routed-transferred-actor",
      .actor_generation = 2,
      .source_spot_rid = "source-room",
      .target_spot_rid = std::string (relay_spot.spot_rid.value ()),
      .payload = {}};
    auto routed_admission_parts = route_join_envelope.encode_parts (
      route_join_header,
      std::type_index (typeid (zlink::framework::detail::spot_actor_admission_route_request_t)),
      &routed_admission, route_join_serializers);
    auto routed_admission_dispatch =
      route_join_dispatcher.dispatch (zlink::framework::detail::route_received_packet_t{
        zlink::routing_id_t::from (std::string ("play-source")), 88, routed_admission_parts});
    if (!routed_admission_dispatch || !routed_admission_dispatch.value ()) {
        return 147;
    }
    auto routed_admission_body =
      route_join_envelope.decode_body (routed_admission_dispatch.value ()->parts);
    const auto routed_admission_reply =
      route_join_serializers.get<zlink::framework::detail::spot_actor_admission_route_reply_t> ()
        .deserialize (
          zlink::framework::detail::encoded_payload_from_raw (routed_admission_body.value ()));
    if (!routed_admission_reply.accepted) {
        return 148;
    }
    route_join_header.message_name =
      zlink::framework::detail::spot_actor_commit_route_request_t::packet_name;
    const auto routed_commit = zlink::framework::detail::spot_actor_commit_route_request_t{
      .transfer_id = "routed-transfer-1",
      .actor_node_rid = "play-source",
      .actor_type = "relay-player",
      .actor_id = "routed-transferred-actor",
      .actor_generation = 2,
      .target_spot_rid = std::string (relay_spot.spot_rid.value ()),
      .bound_session_node_rid = "session-node",
      .bound_session_rid = "session-rid",
      .transfer_state = {}};
    auto routed_commit_parts = route_join_envelope.encode_parts (
      route_join_header,
      std::type_index (typeid (zlink::framework::detail::spot_actor_commit_route_request_t)),
      &routed_commit, route_join_serializers);
    auto routed_commit_dispatch =
      route_join_dispatcher.dispatch (zlink::framework::detail::route_received_packet_t{
        zlink::routing_id_t::from (std::string ("play-source")), 89, routed_commit_parts});
    if (!routed_commit_dispatch || !routed_commit_dispatch.value ()) {
        return 149;
    }
    auto routed_commit_body =
      route_join_envelope.decode_body (routed_commit_dispatch.value ()->parts);
    const auto routed_commit_reply =
      route_join_serializers.get<zlink::framework::detail::spot_actor_join_route_reply_t> ()
        .deserialize (
          zlink::framework::detail::encoded_payload_from_raw (routed_commit_body.value ()));
    if (routed_commit_reply.result_code != 0 || routed_commit_reply.actor_generation != 3
        || routed_commit_reply.actor_node_rid != "relay-stage") {
        return 150;
    }
    const auto transferred_session_route =
      route_join_actor_gateway.bound_session_route (zlink::framework::actor_ref_t (
        zlink::framework::node_rid_t::from_string (routed_commit_reply.actor_node_rid),
        routed_commit_reply.actor_type, routed_commit_reply.actor_id,
        routed_commit_reply.actor_generation));
    if (!transferred_session_route
        || transferred_session_route->node_rid.to_string () != "session-node"
        || !transferred_session_route->session_rid
        || transferred_session_route->session_rid->to_string () != "session-rid") {
        return 158;
    }
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
        .deserialize (
          zlink::framework::detail::encoded_payload_from_raw (route_join_reply_body.value ()));
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
    auto lazy_relay_actor = lazy_relay_gateway.manager ()
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
    auto dispatch_actor_a = entry_dispatch_gateway.manager ()
                              .create ("entry-dispatch-player", "actor-a", std::string ("create-a"))
                              .value ();
    auto dispatch_actor_b = entry_dispatch_gateway.manager ()
                              .create ("entry-dispatch-player", "actor-b", std::string ("create-b"))
                              .value ();
    auto relay_entry_dispatch = [&] (const zlink::framework::session_actor_t &actor, int value) {
        return entry_dispatch_runtime.relay_actor_packet (
          actor.ref (), actor.context (), "entry.block",
          zlink::message_t::from (std::to_string (value)), relay_provider, relay_serializers);
    };
    std::optional<zlink::framework::result_t<std::optional<zlink::message_t>>> dispatch_a_first;
    std::optional<zlink::framework::result_t<std::optional<zlink::message_t>>> dispatch_b_first;
    std::optional<zlink::framework::result_t<std::optional<zlink::message_t>>> dispatch_a_second;
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
        if (std::find (entry_dispatch_spot->starts.begin (), entry_dispatch_spot->starts.end (),
                       std::string ("actor-a:2"))
            != entry_dispatch_spot->starts.end ()) {
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
        .create ("entry-dispatch-player", "actor-await_turn", std::string ("create-await_turn"))
        .value ();
    auto local_actor_dispatch = entry_dispatch_runtime.relay_actor_packet (
      yield_actor.ref (), yield_actor.context (), "entry.await",
      zlink::message_t::from (std::string ("3")), relay_provider, relay_serializers);
    if (local_actor_dispatch) {
        return 125;
    }
    if (!local_actor_dispatch.error ()
        || std::string (local_actor_dispatch.error ()->what ()).find ("should not submit")
             == std::string::npos) {
        return 126;
    }

    auto close_after_actor_left = lifecycle_stage.context.close ().result ();
    if (!close_after_actor_left || !close_after_actor_left.value ()
        || lifecycle_builder.find_spot (lifecycle_stage.spot_rid).result ().value ()) {
        return 63;
    }
    auto close_entry_with_actor = lifecycle_entry.context.close ().result ();
    if (!close_entry_with_actor || close_entry_with_actor.value ()
        || !lifecycle_builder.find_spot (lifecycle_entry.spot_rid).result ().value ()) {
        return 64;
    }
    const auto entry_left_before_manual_leave = lifecycle_entry_spot->left_count;
    auto entry_leave =
      lifecycle_runtime.leave_actor (lifecycle_actor_context.actor_ref (), lifecycle_actor_state);
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
        || lifecycle_builder.find_spot (lifecycle_entry.spot_rid).result ().value ()) {
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
        .request_to<move_reply_t> (make_target_rid ("remote-node"),
                                   make_target_rid ("remote-spot"), move_request_t{1})
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
      .add_actor_send<&stage_spot_t::on_move> ("move");
    const auto handler_descriptors = context.handlers ().descriptors ();
    if (handler_descriptors.size () != 4
        || handler_descriptors[0].kind != zlink::framework::spot_handler_kind_t::packet
        || handler_descriptors[0].packet_name != "state.update"
        || handler_descriptors[1].kind != zlink::framework::spot_handler_kind_t::packet
        || handler_descriptors[1].packet_name != "state.context"
        || handler_descriptors[2].kind != zlink::framework::spot_handler_kind_t::packet
        || handler_descriptors[2].packet_name != "state.throw"
        || handler_descriptors[3].kind != zlink::framework::spot_handler_kind_t::actor_send
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
    spot_serializers.add<stage_closed_t> (
      [] (const stage_closed_t &value) {
          return zlink::framework::encoded_payload_t::from_string (std::to_string (value.value));
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return stage_closed_t{std::stoi (payload.to_string ())};
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
      stage_spot.on_actor_join ("player-1",
                                zlink::framework::message_t::from (std::string ("41")));
    if (!join_dispatch.accepted || !join_dispatch.reply
        || join_dispatch.reply->decode<std::string> (spot_serializers) != "42"
        || actor.joined_value != 0 || stage_spot.join_seen != 41) {
        return 23;
    }
    stage_spot.on_actor_joined (actor);
    if (stage_spot.joined_count != 1 || actor.joined_value != 100) {
        return 25;
    }
    stage_spot.accept_join = false;
    const auto rejected_join =
      stage_spot.on_actor_join ("player-1",
                                zlink::framework::message_t::from (std::string ("50")));
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
      route_actor_ref, context.spot_rid (), "move", zlink::message_t::from (std::string ("58")),
      typed_metadata);
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
    async_actor_gateway.on_join_spot ([&] (const zlink::framework::actor_ref_t &actor_ref,
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
              zlink::framework::node_rid_t::from_string ("async-spot-node"), "player",
              "async-join-actor", 31),
            zlink::message_t{}});
    });
    async_spot.set_join_context (async_actor_gateway.actor_context (zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("async-node"), "player", "async-join-actor", 30)));
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
        || spot_serializers.get<move_reply_t> ()
               .deserialize (
                 zlink::framework::detail::encoded_payload_from_raw (slow_result.value ()))
               .value
             != 78) {
        return 56;
    }
    if (quick_future.wait_for (std::chrono::milliseconds (500)) != std::future_status::ready) {
        return 57;
    }
    const auto quick_result = quick_future.get ();
    if (!quick_result
        || spot_serializers.get<move_reply_t> ()
               .deserialize (
                 zlink::framework::detail::encoded_payload_from_raw (quick_result.value ()))
               .value
             != 10
        || async_spot.quick_seen () != 1) {
        return 58;
    }

    async_spot.reset_probe ();
    auto yield_future =
      std::async (std::launch::async, [&] { return invoke_async ("async.slow-await", 2); });
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
        || spot_serializers.get<move_reply_t> ()
               .deserialize (
                 zlink::framework::detail::encoded_payload_from_raw (yield_quick_result.value ()))
               .value
             != 12
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
        || spot_serializers.get<move_reply_t> ()
               .deserialize (
                 zlink::framework::detail::encoded_payload_from_raw (yield_result.value ()))
               .value
             != 79) {
        return 63;
    }

    async_spot.reset_probe ();
    auto request_await_future =
      std::async (std::launch::async, [&] { return invoke_async ("async.request-await", 4); });
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
        || spot_serializers.get<move_reply_t> ()
               .deserialize (
                 zlink::framework::detail::encoded_payload_from_raw (request_quick_result.value ()))
               .value
             != 14
        || async_spot.quick_seen () != 1) {
        return 66;
    }
    request_source->complete (zlink::framework::result_t<int>::success (90));
    if (request_await_future.wait_for (std::chrono::milliseconds (500))
        != std::future_status::ready) {
        return 67;
    }
    const auto request_await_result = request_await_future.get ();
    if (!request_await_result
        || spot_serializers.get<move_reply_t> ()
               .deserialize (
                 zlink::framework::detail::encoded_payload_from_raw (request_await_result.value ()))
               .value
             != 94) {
        return 68;
    }

    async_spot.reset_probe ();
    auto join_await_future =
      std::async (std::launch::async, [&] { return invoke_async ("async.join-await", 5); });
    {
        std::unique_lock lock (async_join_mutex);
        if (!async_join_changed.wait_for (lock, std::chrono::milliseconds (500),
                                          [&] { return async_join_started; })) {
            return 69;
        }
    }
    auto join_quick_future =
      std::async (std::launch::async, [&] { return invoke_async ("async.quick", 15); });
    if (join_quick_future.wait_for (std::chrono::milliseconds (500)) != std::future_status::ready) {
        return 70;
    }
    const auto join_quick_result = join_quick_future.get ();
    if (!join_quick_result
        || spot_serializers.get<move_reply_t> ()
               .deserialize (
                 zlink::framework::detail::encoded_payload_from_raw (join_quick_result.value ()))
               .value
             != 16
        || async_spot.quick_seen () != 1) {
        return 71;
    }
    {
        std::lock_guard lock (async_join_mutex);
        async_join_release = true;
        async_join_changed.notify_all ();
    }
    if (join_await_future.wait_for (std::chrono::milliseconds (500)) != std::future_status::ready) {
        return 72;
    }
    const auto join_await_result = join_await_future.get ();
    if (!join_await_result
        || spot_serializers.get<move_reply_t> ()
               .deserialize (
                 zlink::framework::detail::encoded_payload_from_raw (join_await_result.value ()))
               .value
             != 36) {
        return 73;
    }

    stage_spot.on_leave_actor (actor);
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
    pubsub_created.context.publish ("stage.state.updated", stage_closed_t{17}).submit ();
    const auto subscription_deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (500);
    while ((subscription_spot->last_value != 9 || subscription_spot->closed_value != 17)
           && std::chrono::steady_clock::now () < subscription_deadline) {
        (void) pubsub_runtime.drain_subscriptions (spot_provider, spot_serializers);
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    pubsub_runtime.detach_native_node ();
    native_node->close ();
    native_context.shutdown ();
    native_context.term ();
    if (subscription_spot->last_value != 9 || subscription_spot->closed_value != 17) {
        return 92;
    }

    context
      .send_to (make_target_rid (std::string (remote_route->node_rid.value ())),
                make_target_rid (std::string (remote_route->spot_rid.value ())),
                state_update_t{2})
      .submit ();

    auto request_result =
      context
        .request_to<move_reply_t> (make_target_rid (std::string (remote_route->node_rid.value ())),
                                   make_target_rid (std::string (remote_route->spot_rid.value ())),
                                   move_request_t{3})
        .async ()
        .result ();
    if (request_result || (request_result.error () != nullptr
         && zlink::framework::detail::boundary_state (*request_result.error ()) != zlink::framework::detail::boundary_error_t::timed_out)) {
        return 16;
    }

    auto missing_route_result =
      context.request_to<move_reply_t> (default_target_rid (), default_target_rid (),
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

    // runtime-metrics §4.3 (RMETRIC-004): only a source-remote move measures
    // the out→commit-ack window; local moves and unknown keys complete without
    // a duration, so no local move can inflate zlink.actor.transfers.
    zlink::framework::detail::actor_transfer_coordinator_t transfer_metric_coordinator;
    if (!transfer_metric_coordinator.try_begin_source_remote ("metric-actor")) {
        return 20;
    }
    const auto remote_elapsed = transfer_metric_coordinator.complete_move ("metric-actor");
    if (!remote_elapsed || *remote_elapsed < std::chrono::steady_clock::duration::zero ()) {
        return 21;
    }
    if (!transfer_metric_coordinator.try_begin_local ("metric-actor")) {
        return 22;
    }
    if (transfer_metric_coordinator.complete_move ("metric-actor")) {
        return 23;
    }
    if (transfer_metric_coordinator.complete_move ("metric-actor")) {
        return 24;
    }

    return 0;
}
