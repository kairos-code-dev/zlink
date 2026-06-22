/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_runtime.hpp"

#include <zlink/framework/contracts/configuration/zlink_builder.hpp>

#include "runtime/channels/channel_runtime.hpp"
#include "runtime/diagnostics/dispatch_error_reporter.hpp"
#include "runtime/diagnostics/message_flow_tracer.hpp"
#include "runtime/dispatch/coroutine_executor.hpp"
#include "runtime/dispatch/offload_executor.hpp"
#include "runtime/execution/serial_execution_queue.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>

namespace zlink::framework
{

namespace
{

bool is_blank (const std::string &value)
{
    return std::all_of (value.begin (), value.end (),
                        [] (unsigned char ch) { return std::isspace (ch) != 0; });
}

class spot_worker_scheduler_t final : public detail::worker_scheduler_t
{
  public:
    spot_worker_scheduler_t (std::shared_ptr<runtime::offload_executor_t> workers,
                             std::weak_ptr<detail::spot_context_state_t> owner) :
        _workers (std::move (workers)), _owner (std::move (owner))
    {
    }

    bool try_schedule (std::function<void ()> work) override
    {
        return _workers && _workers->try_submit (std::move (work));
    }

    void post_owner (std::function<void ()> work) override
    {
        if (auto owner = _owner.lock ()) {
            (void) owner->try_post_serial ("worker-completion", std::move (work));
        }
    }

  private:
    std::shared_ptr<runtime::offload_executor_t> _workers;
    std::weak_ptr<detail::spot_context_state_t> _owner;
};

std::shared_ptr<runtime::offload_executor_t> framework_worker_executor ()
{
    const auto hardware_workers =
      static_cast<std::size_t> (std::max (1u, std::thread::hardware_concurrency ()));
    static auto executor = std::make_shared<runtime::offload_executor_t> (
      0, hardware_workers * 2, 1024, std::chrono::seconds (30));
    return executor;
}

std::shared_ptr<detail::worker_scheduler_t>
make_spot_worker_scheduler (const std::shared_ptr<detail::spot_context_state_t> &owner)
{
    return std::make_shared<spot_worker_scheduler_t> (framework_worker_executor (), owner);
}

void configure_spot_execution (const std::shared_ptr<detail::spot_context_state_t> &state)
{
    state->serial_executor = std::make_shared<runtime::offload_executor_t> (1);
    state->serial_queue =
      std::make_shared<runtime::serial_execution_queue_t> (*state->serial_executor);
    state->worker_scheduler = make_spot_worker_scheduler (state);
}

void attach_native_spot_locked (const std::shared_ptr<detail::spot_context_state_t> &state)
{
    if (!state || !state->node) {
        return;
    }
    auto native_node = state->node->native_node.lock ();
    if (!native_node) {
        return;
    }

    const auto rid = std::string (state->spot_rid.value ());
    auto native = state->native_spot.lock ();
    if (!native) {
        const auto found = state->node->native_spots_by_rid.find (rid);
        if (found == state->node->native_spots_by_rid.end ()) {
            auto [spot, created] =
              native_node->get_or_create_spot (zlink::routing_id_t::from (rid));
            (void) created;
            native = std::make_shared<zlink::service::spot_t> (std::move (spot));
            state->node->native_spots_by_rid.emplace (rid, native);
        } else {
            native = found->second;
        }
        state->native_spot = native;
    }

    for (const auto &handler : state->handlers) {
        if (handler.kind == spot_handler_kind_t::subscription && !handler.topic.empty ()) {
            native->set_subscription (handler.topic);
        }
    }
}

void report_spot_dispatch_error (
  const std::shared_ptr<detail::spot_node_builder_state_t> &state,
  dispatch_error_surface_t surface,
  dispatch_message_kind_t message_kind,
  dispatch_error_reason_t reason,
  dispatch_error_action_t action,
  std::optional<std::string> packet_name = std::nullopt,
  std::optional<std::string> topic = std::nullopt,
  std::optional<std::string> spot_rid = std::nullopt,
  std::optional<std::string> actor_id = std::nullopt)
{
    if (!state) {
        return;
    }
    detail::dispatch_error_reporter_t (state->dispatch)
      .report (message_dispatch_error_event_t{
        surface,
        message_kind,
        reason,
        action,
        std::move (packet_name),
        std::nullopt,
        std::move (topic),
        std::move (spot_rid),
        std::move (actor_id),
        std::nullopt,
        std::nullopt,
        nullptr});
}

void report_spot_dispatch_trace (
  const std::shared_ptr<detail::spot_node_builder_state_t> &state,
  message_flow_phase_t phase,
  dispatch_error_surface_t surface,
  dispatch_message_kind_t message_kind,
  std::optional<std::string> packet_name = std::nullopt,
  std::optional<std::string> topic = std::nullopt,
  std::optional<std::string> spot_rid = std::nullopt,
  std::optional<std::string> actor_id = std::nullopt)
{
    if (!state) {
        return;
    }
    detail::message_flow_tracer_t (state->dispatch)
      .trace (message_flow_event_t{
        phase,
        surface,
        message_kind,
        std::move (packet_name),
        std::nullopt,
        std::move (topic),
        std::nullopt,
        std::nullopt,
        std::move (spot_rid),
        std::move (actor_id),
        std::nullopt});
}

} // namespace

namespace detail
{

void spot_context_state_t::enter_callback ()
{
    std::lock_guard<std::mutex> lock (callback_mutex);
    if (callback_depth == 0) {
        callback_thread = std::this_thread::get_id ();
    }
    ++callback_depth;
}

void spot_context_state_t::leave_callback ()
{
    bool should_close = false;
    {
        std::lock_guard<std::mutex> lock (callback_mutex);
        if (callback_depth > 0) {
            --callback_depth;
        }
        if (callback_depth == 0) {
            callback_thread = std::thread::id ();
            should_close = close_requested;
        }
    }
    if (should_close) {
        (void) close_now ();
    }
}

bool spot_context_state_t::is_current_callback_thread () const
{
    std::lock_guard<std::mutex> lock (callback_mutex);
    return callback_depth > 0 && callback_thread == std::this_thread::get_id ();
}

bool spot_context_state_t::try_post_serial (std::string name, std::function<void ()> work)
{
    if (!serial_queue) {
        work ();
        return true;
    }
    return serial_queue->try_post (std::move (name), std::move (work));
}

bool spot_context_state_t::try_post_serial_async (
  std::string name,
  runtime::serial_execution_queue_t::async_work_t work)
{
    if (!serial_queue) {
        work ([] (std::function<void ()> completion) {
            if (completion) {
                completion ();
            }
        });
        return true;
    }
    return serial_queue->try_post_async (std::move (name), std::move (work));
}

bool spot_context_state_t::run_serial_sync (std::string name, std::function<void ()> work)
{
    if (!work) {
        return true;
    }
    if (is_current_callback_thread ()) {
        work ();
        return true;
    }

    std::exception_ptr error;
    const bool posted = try_post_serial (std::move (name), [&] {
        enter_callback ();
        try {
            work ();
        }
        catch (...) {
            error = std::current_exception ();
        }
        leave_callback ();
    });
    if (!posted) {
        return false;
    }
    drain_serial ();
    if (error) {
        std::rethrow_exception (error);
    }
    return true;
}

void spot_context_state_t::drain_serial ()
{
    if (serial_queue) {
        serial_queue->drain ();
    }
}

} // namespace detail

node_rid_t::node_rid_t (std::string value) : _value (std::move (value))
{
}

node_rid_t node_rid_t::from_string (std::string value)
{
    return node_rid_t (std::move (value));
}

std::string_view node_rid_t::value () const noexcept
{
    return _value;
}

bool node_rid_t::empty () const noexcept
{
    return _value.empty ();
}

spot_rid_t::spot_rid_t (std::string value) : _value (std::move (value))
{
}

spot_rid_t spot_rid_t::from_string (std::string value)
{
    return spot_rid_t (std::move (value));
}

std::string_view spot_rid_t::value () const noexcept
{
    return _value;
}

bool spot_rid_t::empty () const noexcept
{
    return _value.empty ();
}

spot_context_t::erased_request_call_t::erased_request_call_t (framework_exception_t error) :
    _error (std::move (error))
{
}

spot_context_t::spot_context_t () : _state (std::make_shared<detail::spot_context_state_t> ())
{
}

spot_context_t::spot_context_t (std::shared_ptr<detail::spot_context_state_t> state) :
    _state (std::move (state))
{
    if (_state) {
        _worker_scheduler = _state->worker_scheduler;
    }
}

spot_context_t::~spot_context_t () = default;
spot_context_t::spot_context_t (spot_context_t &&) noexcept = default;
spot_context_t &spot_context_t::operator= (spot_context_t &&) noexcept = default;

entry_spot_context_t::entry_spot_context_t () = default;

entry_spot_context_t::entry_spot_context_t (const spot_context_t &context) :
    spot_context_t (context._state)
{
}

entry_spot_context_t::entry_spot_context_t (std::shared_ptr<detail::spot_context_state_t> state) :
    spot_context_t (std::move (state))
{
}

entry_spot_context_t::~entry_spot_context_t () = default;
entry_spot_context_t::entry_spot_context_t (entry_spot_context_t &&) noexcept = default;
entry_spot_context_t &entry_spot_context_t::operator= (entry_spot_context_t &&) noexcept = default;

node_rid_t spot_context_t::node_rid () const
{
    return _state->node_rid;
}

spot_rid_t spot_context_t::spot_rid () const
{
    return _state->spot_rid;
}

std::string spot_context_t::spot_name () const
{
    return _state->spot_name;
}

spot_handler_registry_t spot_context_t::handlers ()
{
    return spot_handler_registry_t (_state);
}

spot_node_manager_t spot_context_t::manager () const
{
    return spot_node_manager_t (_state->node);
}

channel_client_t spot_context_t::outbound () const
{
    if (!_state->channel_runtime) {
        throw framework_exception_t (framework_error_kind_t::request_failed,
                                     "SPOT channel outbound runtime is not configured");
    }
    return channel_client_t (message_bus_t (_state->channel_runtime));
}

task_t<bool> spot_context_t::close ()
{
    return close_erased ();
}

task_t<bool> spot_context_t::close_erased ()
{
    if (!_state || !_state->node) {
        co_return result_t<bool>::success (false);
    }
    {
        std::lock_guard<std::recursive_mutex> node_lock (_state->node->mutex);
        if (_state->closed || _state->actor_count != 0) {
            co_return result_t<bool>::success (false);
        }
        if (_state->callback_depth != 0) {
            _state->close_requested = true;
            co_return result_t<bool>::success (true);
        }
        co_return result_t<bool>::success (_state->close_now ());
    }
}

task_t<actor_ref_t> spot_context_t::leaveActor_erased (
  const actor_ref_t &actor_ref,
  std::type_index actor_type,
  void *actor,
  std::function<void (void *, const actor_ref_t &)> update_actor_ref)
{
    if (!_state || !_state->node || actor_ref.empty ()) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::actor_route_not_found, "actor ref is empty"));
    }
    std::unique_lock<std::recursive_mutex> node_lock (_state->node->mutex);
    const auto key =
      std::string (actor_ref.actor_type ()) + ":" + std::string (actor_ref.actor_id ());
    const auto found_location = _state->node->actor_spot_rids.find (key);
    if (found_location == _state->node->actor_spot_rids.end ()) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::success (actor_ref));
    }
    if (found_location->second.value () != _state->spot_rid.value ()) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::actor_route_not_found, "actor is not joined to this SPOT"));
    }

    const auto found_generation = _state->node->actor_generations.find (key);
    if (found_generation != _state->node->actor_generations.end ()
        && found_generation->second != actor_ref.generation ()) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::actor_stale_generation, "actor generation is stale"));
    }

    if (_state->node_rid.empty () || actor_ref.node_rid ().value () != _state->node_rid.value ()) {
        try {
            auto &source_state = *_state;
            source_state.actor_count =
              source_state.actor_count == 0 ? 0 : source_state.actor_count - 1;
            _state->node->actor_spot_rids.erase (found_location);
            _state->node->actor_routes.erase (key);
            _state->node->actor_generations.erase (key);
            const auto source_admission = source_state.actor_admissions.find (actor_type);
            if (source_admission != source_state.actor_admissions.end ()
                && source_admission->second.onLeaveActor && source_state.spot_instance) {
                node_lock.unlock ();
                if (!source_state.run_serial_sync ("spot-lifecycle-leave", [&] {
                        source_admission->second.onLeaveActor (source_state.spot_instance.get (),
                                                              actor);
                    })) {
                    return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
                      framework_error_kind_t::request_rejected, "spot serial queue is full"));
                }
                node_lock.lock ();
            }
            return task_t<actor_ref_t> (result_t<actor_ref_t>::success (actor_ref));
        }
        catch (const framework_exception_t &error) {
            return task_t<actor_ref_t> (
              result_t<actor_ref_t>::failure (error.kind (), error.what (), error.is_retriable ()));
        }
        catch (const std::exception &error) {
            return task_t<actor_ref_t> (
              result_t<actor_ref_t>::failure (framework_error_kind_t::request_failed, error.what ()));
        }
        catch (...) {
            return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
              framework_error_kind_t::request_failed, "remote actor leave callback failed"));
        }
    }

    if (!_state->node->snapshot.entry_spot_name) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::spot_route_not_found, "entry spot is not registered"));
    }

    const auto entry_rid =
      _state->node->spot_rids_by_name.find (*_state->node->snapshot.entry_spot_name);
    if (entry_rid == _state->node->spot_rids_by_name.end ()) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::spot_route_not_found, "entry spot is not created"));
    }
    const auto entry_context =
      _state->node->spot_contexts_by_rid.find (std::string (entry_rid->second.value ()));
    if (entry_context == _state->node->spot_contexts_by_rid.end ()
        || !entry_context->second._state->spot_instance) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::spot_route_not_found, "entry spot context is not registered"));
    }

    try {
        auto &source_state = *_state;
        source_state.actor_count = source_state.actor_count == 0 ? 0 : source_state.actor_count - 1;
        _state->node->actor_spot_rids.erase (found_location);
        _state->node->actor_routes.erase (key);
        _state->node->actor_generations.erase (key);
        const auto source_left = source_state.onLeaveActor_callbacks.find (actor_type);
        if (source_left != source_state.onLeaveActor_callbacks.end ()
            && source_state.spot_instance) {
            node_lock.unlock ();
            if (!source_state.run_serial_sync ("spot-lifecycle-leave", [&] {
                    source_left->second (source_state.spot_instance.get (), actor);
                })) {
                return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
                  framework_error_kind_t::request_rejected, "spot serial queue is full"));
            }
            node_lock.lock ();
        }

        auto &entry_state = *entry_context->second._state;
        const auto committed =
          actor_ref_t (node_rid_t::from_string (std::string (_state->node_rid.value ())),
                       std::string (actor_ref.actor_type ()), std::string (actor_ref.actor_id ()),
                       actor_ref.generation () + 1);
        _state->node->actor_spot_rids[key] = entry_state.spot_rid;
        _state->node->actor_routes[key] =
          spot_route_t{node_rid_t::from_string (std::string (_state->node_rid.value ())),
                       entry_state.spot_rid, entry_state.spot_name};
        _state->node->actor_generations[key] = committed.generation ();
        entry_state.actor_count++;
        if (update_actor_ref) {
            update_actor_ref (actor, committed);
        }
        if (_state->node->update_actor_registry_ref) {
            auto updated = _state->node->update_actor_registry_ref (committed);
            if (!updated) {
                const auto *error = updated.error ();
                return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
                  updated.error_kind (),
                  error != nullptr ? error->what () : "actor registry ref update failed"));
            }
        }

        const auto entry_joined = entry_state.on_actor_joined_callbacks.find (actor_type);
        if (entry_joined != entry_state.on_actor_joined_callbacks.end () && entry_state.spot_instance) {
            node_lock.unlock ();
            if (!entry_state.run_serial_sync ("spot-lifecycle-join", [&] {
                    entry_joined->second (entry_state.spot_instance.get (), actor);
                })) {
                return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
                  framework_error_kind_t::request_rejected, "spot serial queue is full"));
            }
            node_lock.lock ();
        }
        return task_t<actor_ref_t> (result_t<actor_ref_t>::success (committed));
    }
    catch (const framework_exception_t &error) {
        return task_t<actor_ref_t> (
          result_t<actor_ref_t>::failure (error.kind (), error.what (), error.is_retriable ()));
    }
    catch (const std::exception &error) {
        return task_t<actor_ref_t> (
          result_t<actor_ref_t>::failure (framework_error_kind_t::request_failed, error.what ()));
    }
    catch (...) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::request_failed, "actor leave callback failed"));
    }
}

task_t<void> entry_spot_context_t::destroyActor_erased (const actor_ref_t &actor,
                                                        std::type_index actor_type,
                                                        void *actor_instance)
{
    (void) actor_type;
    (void) actor_instance;
    if (!_state || !_state->node || actor.empty ()) {
        return task_t<void> (result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                                      "actor ref is empty"));
    }
    std::lock_guard<std::recursive_mutex> node_lock (_state->node->mutex);
    if (actor.node_rid ().empty () || actor.node_rid ().value () != _state->node_rid.value ()) {
        return task_t<void> (result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                                      "actor is not owned by this Entry SPOT"));
    }

    const auto key = std::string (actor.actor_type ()) + ":" + std::string (actor.actor_id ());
    const auto found_location = _state->node->actor_spot_rids.find (key);
    if (found_location != _state->node->actor_spot_rids.end ()
        && found_location->second.value () != _state->spot_rid.value ()) {
        return task_t<void> (
          result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                   "actor must leave its current SPOT before destroy"));
    }

    const auto found_generation = _state->node->actor_generations.find (key);
    if (found_generation != _state->node->actor_generations.end ()
        && found_generation->second != actor.generation ()) {
        return task_t<void> (result_t<void>::success ());
    }
    if (_state->node->destroying_actors.contains (key)) {
        return task_t<void> (result_t<void>::success ());
    }

    if (found_location != _state->node->actor_spot_rids.end ()) {
        _state->node->destroying_actors.insert (key);
        _state->node->actor_spot_rids.erase (found_location);
        _state->node->actor_routes.erase (key);
        _state->node->actor_generations.erase (key);
        _state->node->actor_created_keys.erase (key);
        _state->node->actor_instances.erase (key);
        if (_state->actor_count > 0) {
            _state->actor_count--;
        }
        if (_state->node->destroy_actor_registry) {
            auto cleanup = _state->node->destroy_actor_registry (actor);
            _state->node->destroying_actors.erase (key);
            if (!cleanup) {
                const auto *error = cleanup.error ();
                return task_t<void> (result_t<void>::failure (
                  cleanup.error_kind (),
                  error != nullptr ? error->what () : "actor registry cleanup failed"));
            }
        } else {
            _state->node->destroying_actors.erase (key);
        }
    }

    return task_t<void> (result_t<void>::success ());
}

send_call_t spot_context_t::publish_erased (std::string topic,
                                            std::string packet_name,
                                            zlink::message_t payload)
{
    auto state = _state;
    return send_call_t (
      std::move (packet_name),
      [state, topic = std::move (topic), payload = std::move (payload)] (
        const std::string &submitted_packet_name,
        std::chrono::milliseconds,
        const send_call_t::metadata_map_t &) {
          if (!state) {
              return task_t<void> (result_t<void>::failure (
                framework_error_kind_t::request_protocol_error, "spot context is not configured"));
          }
          state->ordering_log.push_back ("publish:" + topic + ":" + submitted_packet_name + ":"
                                         + payload.to_string ());
          auto native = state->native_spot.lock ();
          if (native) {
              try {
                  auto outbound = payload;
                  std::move (native->publish (topic)).message (std::move (outbound)).submit ();
              }
              catch (const std::exception &error) {
                  return task_t<void> (result_t<void>::failure (
                    framework_error_kind_t::request_failed, error.what ()));
              }
          }
          return task_t<void> (result_t<void>::success ());
      });
}

serializer_registry_t *spot_context_t::serializer_registry () const noexcept
{
    if (!_state || !_state->channel_runtime) {
        return nullptr;
    }
    return _state->channel_runtime->serializers;
}

send_call_t spot_context_t::send_to_erased (node_rid_t node_rid, spot_rid_t spot_rid)
{
    if (node_rid.empty () || spot_rid.empty ()) {
        return send_call_t (result_t<void>::failure (framework_error_kind_t::spot_route_not_found,
                                                     "target spot route is empty"));
    }
    _state->ordering_log.push_back ("send_to:" + std::string (spot_rid.value ()));
    return send_call_t (result_t<void>::success ());
}

spot_context_t::erased_request_call_t spot_context_t::request_to_erased (node_rid_t node_rid,
                                                                         spot_rid_t spot_rid)
{
    if (node_rid.empty () || spot_rid.empty ()) {
        return erased_request_call_t (framework_exception_t (
          framework_error_kind_t::spot_route_not_found, "target spot route is empty"));
    }
    _state->ordering_log.push_back ("request_to:" + std::string (spot_rid.value ()));
    return erased_request_call_t (
      framework_exception_t (framework_error_kind_t::timeout,
                             "spot-to-spot reply was not completed by the local test runtime"));
}

spot_context_t &spot_context_t::register_packet_erased (std::string packet_name,
                                                        std::type_index payload_type)
{
    const auto duplicate = std::any_of (_state->packets.begin (), _state->packets.end (),
                                        [&] (const spot_packet_descriptor_t &descriptor) {
                                            return descriptor.packet_name == packet_name;
                                        });
    if (duplicate) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate spot packet registration");
    }
    _state->packets.push_back (spot_packet_descriptor_t{std::move (packet_name), payload_type});
    return *this;
}

std::vector<spot_packet_descriptor_t> spot_context_t::packet_registry () const
{
    return _state->packets;
}

spot_handler_registry_t::spot_handler_registry_t () :
    _state (std::make_shared<detail::spot_context_state_t> ())
{
}

spot_handler_registry_t::spot_handler_registry_t (
  std::shared_ptr<detail::spot_context_state_t> state) :
    _state (std::move (state))
{
}

spot_handler_registry_t::~spot_handler_registry_t () = default;
spot_handler_registry_t::spot_handler_registry_t (spot_handler_registry_t &&) noexcept = default;
spot_handler_registry_t &
spot_handler_registry_t::operator= (spot_handler_registry_t &&) noexcept = default;

spot_handler_registry_t &spot_handler_registry_t::add_handler_erased (spot_handler_kind_t kind,
                                                                      std::string packet_name,
                                                                      std::string topic,
                                                                      std::type_index handler_type,
                                                                      std::type_index payload_type,
                                                                      std::type_index actor_type,
                                                                      std::type_index reply_type,
                                                                      invoker_t invoker)
{
    const auto duplicate =
      std::any_of (_state->handlers.begin (), _state->handlers.end (),
                   [&] (const spot_handler_descriptor_t &descriptor) {
                       return descriptor.kind == kind && descriptor.packet_name == packet_name
                              && descriptor.topic == topic && descriptor.actor_type == actor_type;
                   });
    if (duplicate) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate spot handler registration");
    }

    _state->handlers.push_back (spot_handler_descriptor_t{kind, std::move (packet_name),
                                                          std::move (topic), handler_type,
                                                          payload_type, actor_type, reply_type});
    _state->handler_invokers.push_back (std::move (invoker));
    const auto &descriptor = _state->handlers.back ();
    if (descriptor.kind == spot_handler_kind_t::subscription && !descriptor.topic.empty ()) {
        if (auto native = _state->native_spot.lock ()) {
            native->set_subscription (descriptor.topic);
        }
    }
    return *this;
}

void spot_handler_registry_t::register_actor_admission_erased (
  std::type_index actor_type,
  detail::spot_actor_admission_callbacks_t callbacks)
{
    _state->actor_admissions[actor_type] = std::move (callbacks);
}

std::vector<spot_handler_descriptor_t> spot_handler_registry_t::descriptors () const
{
    return _state->handlers;
}

task_t<zlink::message_t>
spot_handler_registry_t::invoke_erased (spot_handler_kind_t kind,
                                        std::string_view packet_name,
                                        std::string_view topic,
                                        std::type_index actor_type,
                                        void *spot,
                                        void *actor,
                                        service_provider_t &services,
                                        serializer_registry_t &serializers,
                                        const zlink::message_t &message,
                                        spot_actor_message_metadata_t metadata) const
{
    for (std::size_t index = 0; index < _state->handlers.size (); ++index) {
        const auto &descriptor = _state->handlers[index];
        if (descriptor.kind == kind && descriptor.packet_name == packet_name
            && descriptor.topic == topic && descriptor.actor_type == actor_type) {
            auto owned_message = message;
            const auto handler_index = index;
            detail::task_completion_source_t<zlink::message_t> completion;
            auto task = completion.task ();
            auto state = _state;
            const auto posted = state->try_post_serial_async (
              "spot-handler",
              [state, handler_index, spot, actor, &services, &serializers,
               owned_message = std::move (owned_message), metadata = std::move (metadata),
               completion] (auto complete) mutable {
                  state->enter_callback ();
                  try {
                      auto handler_task = state->handler_invokers[handler_index](
                        spot, actor, services, serializers, owned_message, metadata);
                      detail::observe_task_completion (
                        handler_task,
                        [state, completion, complete] (const result_t<zlink::message_t> &result) mutable {
                            complete ([state, completion, result] () mutable {
                                state->leave_callback ();
                                if (result) {
                                    completion.complete (
                                      result_t<zlink::message_t>::success (result.value ()));
                                    return;
                                }
                                const auto *error = result.error ();
                                completion.complete (result_t<zlink::message_t>::failure (
                                  result.error_kind (),
                                  error != nullptr ? error->what () : "spot handler failed",
                                  error != nullptr && error->is_retriable ()));
                            });
                        });
                  }
                  catch (const framework_exception_t &error) {
                      complete ([state, completion, error] () mutable {
                          state->leave_callback ();
                          completion.complete (result_t<zlink::message_t>::failure (
                            error.kind (), error.what (), error.is_retriable ()));
                      });
                  }
                  catch (const std::exception &error) {
                      const auto message = std::string (error.what ());
                      complete ([state, completion, message = std::move (message)] () mutable {
                          state->leave_callback ();
                          completion.complete (result_t<zlink::message_t>::failure (
                            framework_error_kind_t::request_failed, std::move (message)));
                      });
                  }
                  catch (...) {
                      complete ([state, completion] () mutable {
                          state->leave_callback ();
                          completion.complete (result_t<zlink::message_t>::failure (
                            framework_error_kind_t::request_failed,
                            "spot handler threw an exception"));
                      });
                  }
              });
            if (!posted) {
                return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
                  framework_error_kind_t::request_rejected, "spot serial queue is full"));
            }
            return task;
        }
    }
    std::ostringstream error_message;
    error_message << "spot handler is not registered: packet='" << packet_name << "', topic='"
                  << topic << "', actor_type='" << actor_type.name () << "', registered=[";
    for (std::size_t index = 0; index < _state->handlers.size (); ++index) {
        if (index > 0) {
            error_message << "; ";
        }
        const auto &descriptor = _state->handlers[index];
        error_message << "packet='" << descriptor.packet_name << "', topic='" << descriptor.topic
                      << "', actor_type='" << descriptor.actor_type.name () << "'";
    }
    error_message << "]";
    return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
      framework_error_kind_t::handler_not_found, error_message.str ()));
}

spot_node_builder_t::spot_node_builder_t () :
    _state (std::make_shared<detail::spot_node_builder_state_t> (""))
{
}

spot_node_builder_t::spot_node_builder_t (
  std::shared_ptr<detail::spot_node_builder_state_t> state) :
    _state (std::move (state))
{
}

spot_node_builder_t::~spot_node_builder_t () = default;
spot_node_builder_t::spot_node_builder_t (spot_node_builder_t &&) noexcept = default;
spot_node_builder_t &spot_node_builder_t::operator= (spot_node_builder_t &&) noexcept = default;

spot_node_builder_t &spot_node_builder_t::bind (std::string endpoint)
{
    _state->snapshot.bind_endpoint = std::move (endpoint);
    return *this;
}

spot_node_builder_t &spot_node_builder_t::enable_router (std::string endpoint)
{
    _state->snapshot.router_bind_endpoint = endpoint;
    if (_state->snapshot.bind_endpoint.empty ()) {
        _state->snapshot.bind_endpoint = std::move (endpoint);
    }
    return *this;
}

spot_node_builder_t &spot_node_builder_t::enable_router (std::string endpoint,
                                                         zlink::routing_id_t routing_id)
{
    enable_router (std::move (endpoint));
    _state->snapshot.router_routing_id = std::move (routing_id);
    return *this;
}

spot_node_builder_t &spot_node_builder_t::connect_router (std::string endpoint)
{
    if (endpoint.empty () || is_blank (endpoint)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "SPOT router manual endpoint is required");
    }
    _state->snapshot.router_manual_connections.push_back (std::move (endpoint));
    return *this;
}

spot_node_builder_t &spot_node_builder_t::enable_pub_sub (std::string endpoint)
{
    _state->snapshot.pub_bind_endpoint = endpoint;
    if (_state->snapshot.bind_endpoint.empty ()) {
        _state->snapshot.bind_endpoint = std::move (endpoint);
    }
    return *this;
}

spot_node_builder_t &spot_node_builder_t::enable_pub_sub (std::string endpoint,
                                                          zlink::routing_id_t routing_id)
{
    enable_pub_sub (std::move (endpoint));
    _state->snapshot.pub_routing_id = std::move (routing_id);
    return *this;
}

spot_node_builder_t &spot_node_builder_t::connect_pub_sub (std::string endpoint)
{
    if (endpoint.empty () || is_blank (endpoint)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "SPOT pub/sub manual endpoint is required");
    }
    _state->snapshot.pub_sub_manual_connections.push_back (std::move (endpoint));
    return *this;
}

spot_node_builder_t &spot_node_builder_t::connect_peer_pub (std::string endpoint)
{
    return connect_pub_sub (std::move (endpoint));
}

spot_node_builder_t &spot_node_builder_t::enable_actor_gateway ()
{
    _state->snapshot.actor_gateway_enabled = true;
    return *this;
}

spot_node_builder_t &spot_node_builder_t::use_discovery (std::string channel_name)
{
    if (channel_name.empty ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "spot discovery channel name is required");
    }
    _state->snapshot.discovery_channel_name = std::move (channel_name);
    return *this;
}

spot_node_builder_t &spot_node_builder_t::use_registry_spot_remote_addresses ()
{
    if (!_state->resolvers.empty ()) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "registry spot remote address resolver cannot be combined with custom spot resolvers");
    }
    _state->snapshot.registry_spot_remote_addresses_enabled = true;
    _state->snapshot.registry_spot_route_channel.reset ();
    return *this;
}

spot_node_builder_t &
spot_node_builder_t::use_registry_spot_remote_addresses (std::string route_channel_name)
{
    if (route_channel_name.empty ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "registry spot remote address route channel is required");
    }
    if (!_state->resolvers.empty ()) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "registry spot remote address resolver cannot be combined with custom spot resolvers");
    }
    _state->snapshot.registry_spot_remote_addresses_enabled = true;
    _state->snapshot.registry_spot_route_channel = std::move (route_channel_name);
    return *this;
}

spot_node_builder_t &spot_node_builder_t::use_registry_spot_resolver ()
{
    return use_registry_spot_remote_addresses ();
}

spot_node_builder_t &
spot_node_builder_t::use_registry_spot_resolver (std::string route_channel_name)
{
    return use_registry_spot_remote_addresses (std::move (route_channel_name));
}

spot_node_builder_t &
spot_node_builder_t::accept_routes_from_channel (std::string route_channel_name,
                                                 std::string endpoint)
{
    return accept_routes_from_channel (std::move (route_channel_name),
                                       std::vector<std::string>{std::move (endpoint)});
}

spot_node_builder_t &
spot_node_builder_t::accept_routes_from_channel (std::string route_channel_name,
                                                 std::vector<std::string> manual_connections)
{
    if (route_channel_name.empty () || is_blank (route_channel_name)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "accepted SPOT route channel name is required");
    }
    for (const auto &endpoint : manual_connections) {
        if (endpoint.empty () || is_blank (endpoint)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "accepted SPOT route manual endpoint is required");
        }
    }
    const auto duplicate =
      std::any_of (_state->snapshot.accepted_route_channels.begin (),
                   _state->snapshot.accepted_route_channels.end (), [&] (const auto &accepted) {
                       return accepted.channel_name == route_channel_name;
                   });
    if (duplicate) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate accepted SPOT route channel");
    }
    _state->snapshot.accepted_route_channels.push_back (accepted_spot_route_channel_t{
      std::move (route_channel_name), std::move (manual_connections)});
    return *this;
}

spot_node_builder_t &
spot_node_builder_t::attach_channel_client (std::string channel_name,
                                            std::vector<std::string> manual_connections)
{
    if (channel_name.empty () || is_blank (channel_name)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "attached client/server channel client name is required");
    }
    for (const auto &endpoint : manual_connections) {
        if (endpoint.empty () || is_blank (endpoint)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "attached channel client manual endpoint is required");
        }
    }
    _state->snapshot.attached_channel_client_details.push_back (
      attached_channel_client_t{channel_name, std::move (manual_connections)});
    _state->snapshot.attached_channel_clients.push_back (std::move (channel_name));
    return *this;
}

spot_node_builder_t &
spot_node_builder_t::attach_publisher (std::string channel_name,
                                       std::vector<std::string> manual_connections)
{
    if (channel_name.empty () || is_blank (channel_name)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "attached SPOT publisher channel name is required");
    }
    for (const auto &endpoint : manual_connections) {
        if (endpoint.empty () || is_blank (endpoint)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "attached SPOT publisher manual endpoint is required");
        }
    }
    _state->snapshot.attached_publisher_details.push_back (
      attached_publisher_t{channel_name, std::move (manual_connections)});
    _state->snapshot.attached_publishers.push_back (std::move (channel_name));
    return *this;
}

spot_node_builder_t &spot_node_builder_t::add_spot_factory (std::string spot_name,
                                                            std::type_index spot_type,
                                                            bool entry_spot)
{
    const auto [_, inserted] = _state->spot_factories.emplace (spot_name, spot_type);
    if (!inserted) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate spot factory registration");
    }
    if (entry_spot) {
        if (_state->snapshot.entry_spot_name) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "entry spot is already registered");
        }
        _state->snapshot.entry_spot_name = spot_name;
    }
    _state->snapshot.spot_names.push_back (std::move (spot_name));
    return *this;
}

spot_node_builder_t &spot_node_builder_t::add_actor_factory_erased (
  std::string actor_type,
  std::type_index actor_instance_type,
  std::function<std::shared_ptr<void> (std::string)> create_instance,
  std::function<void (void *, const actor_ref_t &, void *)> configure_instance)
{
    if (!create_instance || !configure_instance) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "actor factory callback must not be empty");
    }
    const auto [_, inserted] = _state->actor_factories.emplace (
      actor_type, detail::spot_node_builder_state_t::actor_factory_registration_t{
                    actor_instance_type, std::move (create_instance),
                    std::move (configure_instance)});
    if (!inserted) {
        throw framework_exception_t (framework_error_kind_t::actor_already_exists,
                                     "duplicate actor factory registration");
    }
    _state->snapshot.actor_types.push_back (std::move (actor_type));
    return *this;
}

void spot_node_builder_t::register_lifecycle_erased (std::string spot_name,
                                                     detail::spot_lifecycle_callbacks_t callbacks)
{
    _state->spot_lifecycles[std::move (spot_name)] = std::move (callbacks);
}

spot_node_builder_t &spot_node_builder_t::add_spot_resolver (
  std::string name, std::function<std::optional<spot_route_t> (spot_rid_t)> resolver)
{
    if (name.empty () || !resolver) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "spot resolver requires a name and callback");
    }
    if (_state->snapshot.registry_spot_remote_addresses_enabled) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "custom spot resolvers cannot be combined with registry spot remote address resolver");
    }
    const auto [_, inserted] = _state->resolvers.emplace (std::move (name), std::move (resolver));
    if (!inserted) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate spot resolver registration");
    }
    return *this;
}

spot_node_snapshot_t spot_node_builder_t::snapshot () const
{
    return _state->snapshot;
}

spot_create_result_t spot_node_builder_t::create_spot (std::string spot_name)
{
    return detail::spot_node_runtime_t (_state).create_spot (std::move (spot_name));
}

spot_create_result_t spot_node_builder_t::create_spot (std::string spot_name,
                                                       zlink::message_t request)
{
    return detail::spot_node_runtime_t (_state).create_spot (std::move (spot_name),
                                                             std::move (request));
}

spot_create_result_t spot_node_builder_t::get_or_create_spot (std::string spot_name,
                                                              spot_rid_t spot_rid)
{
    return detail::spot_node_runtime_t (_state).get_or_create_spot (std::move (spot_name),
                                                                    std::move (spot_rid));
}

spot_create_result_t spot_node_builder_t::get_or_create_spot (std::string spot_name,
                                                              spot_rid_t spot_rid,
                                                              zlink::message_t request)
{
    return detail::spot_node_runtime_t (_state).get_or_create_spot (
      std::move (spot_name), std::move (spot_rid), std::move (request));
}

std::optional<spot_info_t> spot_node_builder_t::find_spot (spot_rid_t spot_rid) const
{
    return detail::spot_node_runtime_t (_state).find_spot (std::move (spot_rid));
}

std::vector<spot_info_t> spot_node_builder_t::list_spots () const
{
    return detail::spot_node_runtime_t (_state).list_spots ();
}

task_t<bool> spot_node_builder_t::close_spot (spot_rid_t spot_rid)
{
    return detail::spot_node_runtime_t (_state).close_spot (std::move (spot_rid));
}

std::optional<std::string> spot_node_builder_t::spot_name_for (spot_rid_t spot_rid) const
{
    return detail::spot_node_runtime_t (_state).spot_name_for (std::move (spot_rid));
}

std::optional<spot_route_t> spot_node_builder_t::resolve_spot (spot_rid_t spot_rid) const
{
    return detail::spot_node_runtime_t (_state).resolve_spot (std::move (spot_rid));
}

spot_node_builder_t zlink_builder_t::add_spot_node (std::string spot_node_name)
{
    auto state = std::make_shared<detail::spot_node_builder_state_t> (std::move (spot_node_name));
    state->channel_runtime = _state->runtime;
    state->registry_runtime = _state->registry_runtime;
    _state->spot_nodes[state->snapshot.name] = state;
    return spot_node_builder_t (state);
}

std::vector<spot_node_snapshot_t> zlink_builder_t::spot_nodes () const
{
    std::vector<spot_node_snapshot_t> result;
    result.reserve (_state->spot_nodes.size ());
    for (const auto &[_, state] : _state->spot_nodes) {
        result.push_back (state->snapshot);
    }
    return result;
}

} // namespace zlink::framework

namespace zlink::framework::detail
{

spot_node_runtime_t::spot_node_runtime_t (std::shared_ptr<spot_node_builder_state_t> state) :
    _state (std::move (state))
{
}

} // namespace zlink::framework::detail

namespace zlink::framework
{

spot_node_manager_t::spot_node_manager_t () :
    _state (std::make_shared<detail::spot_node_builder_state_t> (""))
{
}

spot_node_manager_t::spot_node_manager_t (
  std::shared_ptr<detail::spot_node_builder_state_t> state) :
    _state (std::move (state))
{
}

spot_node_manager_t::~spot_node_manager_t () = default;
spot_node_manager_t::spot_node_manager_t (spot_node_manager_t &&) noexcept = default;
spot_node_manager_t &spot_node_manager_t::operator= (spot_node_manager_t &&) noexcept = default;

spot_create_result_t spot_node_manager_t::create_spot (std::string spot_name)
{
    return detail::spot_node_runtime_t (_state).create_spot (std::move (spot_name));
}

spot_create_result_t spot_node_manager_t::create_spot (std::string spot_name,
                                                       zlink::message_t request)
{
    return detail::spot_node_runtime_t (_state).create_spot (std::move (spot_name),
                                                             std::move (request));
}

spot_create_result_t spot_node_manager_t::get_or_create_spot (std::string spot_name,
                                                              spot_rid_t spot_rid)
{
    return detail::spot_node_runtime_t (_state).get_or_create_spot (std::move (spot_name),
                                                                    std::move (spot_rid));
}

spot_create_result_t spot_node_manager_t::get_or_create_spot (std::string spot_name,
                                                              spot_rid_t spot_rid,
                                                              zlink::message_t request)
{
    return detail::spot_node_runtime_t (_state).get_or_create_spot (
      std::move (spot_name), std::move (spot_rid), std::move (request));
}

std::optional<spot_info_t> spot_node_manager_t::find_spot (spot_rid_t spot_rid) const
{
    return detail::spot_node_runtime_t (_state).find_spot (std::move (spot_rid));
}

std::vector<spot_info_t> spot_node_manager_t::list_spots () const
{
    return detail::spot_node_runtime_t (_state).list_spots ();
}

task_t<bool> spot_node_manager_t::close_spot (spot_rid_t spot_rid)
{
    return detail::spot_node_runtime_t (_state).close_spot (std::move (spot_rid));
}

std::optional<std::string> spot_node_manager_t::spot_name_for (spot_rid_t spot_rid) const
{
    return detail::spot_node_runtime_t (_state).spot_name_for (std::move (spot_rid));
}

std::optional<spot_route_t> spot_node_manager_t::resolve_spot (spot_rid_t spot_rid) const
{
    return detail::spot_node_runtime_t (_state).resolve_spot (std::move (spot_rid));
}

std::optional<actor_ref_t>
spot_node_manager_t::current_actor_ref (const actor_ref_t &actor_ref) const
{
    return detail::spot_node_runtime_t (_state).current_actor_ref (actor_ref);
}

result_t<std::optional<zlink::message_t>>
spot_node_manager_t::relay_actor_packet (const actor_ref_t &actor_ref,
                                         actor_context_t actor_context,
                                         std::string_view packet_name,
                                         const zlink::message_t &message,
                                         service_provider_t &services,
                                         serializer_registry_t &serializers,
                                         spot_actor_message_metadata_t metadata)
{
    if (_state->actor_packet_relay) {
        return _state->actor_packet_relay (actor_ref, std::move (actor_context), packet_name,
                                           message, services, serializers, std::move (metadata));
    }
    return detail::spot_node_runtime_t (_state).relay_actor_packet (
      actor_ref, std::move (actor_context), packet_name, message, services, serializers,
      std::move (metadata));
}

} // namespace zlink::framework

namespace zlink::framework::detail
{

spot_node_manager_t spot_node_runtime_t::manager () const
{
    return spot_node_manager_t (_state);
}

result_t<actor_join_reply_t>
spot_node_runtime_t::join_actor_to_spot_erased (const actor_ref_t &actor_ref,
                                                spot_rid_t spot_rid,
                                                const zlink::message_t &request)
{
    if (actor_ref.empty ()) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::actor_route_not_found,
                                                      "actor ref is empty");
    }
    auto context = find_context (spot_rid);
    if (!context || !context->_state->spot_instance) {
        std::optional<std::string> dynamic_spot_name;
        for (const auto &[spot_name, _] : _state->spot_factories) {
            if (_state->snapshot.entry_spot_name && spot_name == *_state->snapshot.entry_spot_name) {
                continue;
            }
            if (dynamic_spot_name) {
                dynamic_spot_name.reset ();
                break;
            }
            dynamic_spot_name = spot_name;
        }
        if (dynamic_spot_name) {
            (void) get_or_create_spot (*dynamic_spot_name, spot_rid, request);
            context = find_context (spot_rid);
        }
        if (!context || !context->_state->spot_instance) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::spot_route_not_found, "target spot is not registered");
        }
    }
    const auto actor_factory = _state->actor_factories.find (std::string (actor_ref.actor_type ()));
    if (actor_factory == _state->actor_factories.end ()) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::actor_route_not_found, "actor factory is not registered");
    }
    const auto actor = _state->actor_instances.find (actor_key (actor_ref));
    if (actor == _state->actor_instances.end () || !actor->second) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::actor_route_not_found, "actor instance is not registered");
    }
    const auto admission =
      context->_state->actor_admissions.find (actor_factory->second.actor_type);
    if (admission == context->_state->actor_admissions.end () || !admission->second.join) {
        report_spot_dispatch_error (_state,
                                    dispatch_error_surface_t::spot_actor,
                                    dispatch_message_kind_t::actor_request,
                                    dispatch_error_reason_t::handler_missing,
                                    dispatch_error_action_t::reply_error,
                                    "actor.join",
                                    std::nullopt,
                                    std::string (spot_rid.value ()),
                                    std::string (actor_ref.actor_id ()));
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::handler_not_found,
          "spot actor join callback is not registered");
    }

    const auto response =
      admission->second.join (context->_state->spot_instance.get (), actor->second.get (), request);
    if (!response.accepted) {
        return result_t<actor_join_reply_t>::success (
          actor_join_reply_t{1, actor_ref, response.reply.value_or (zlink::message_t{})});
    }

    const auto key = actor_key (actor_ref);
    if (const auto previous = _state->actor_spot_rids.find (key);
        previous != _state->actor_spot_rids.end ()) {
        if (auto previous_context = find_context (previous->second)) {
            auto &previous_state = *previous_context->_state;
            if (previous_state.actor_count > 0) {
                previous_state.actor_count--;
            }
            if (const auto previous_admission =
                  previous_state.actor_admissions.find (actor_factory->second.actor_type);
                previous_admission != previous_state.actor_admissions.end ()
                && previous_admission->second.onLeaveActor && previous_state.spot_instance) {
                previous_admission->second.onLeaveActor (previous_state.spot_instance.get (),
                                                         actor->second.get ());
            }
        }
        _state->actor_spot_rids.erase (previous);
        _state->actor_routes.erase (key);
        _state->actor_generations.erase (key);
    }

    auto &target_state = *context->_state;
    _state->actor_spot_rids[key] = target_state.spot_rid;
    _state->actor_routes[key] =
      spot_route_t{node_rid_t::from_string (_state->snapshot.name), target_state.spot_rid,
                   target_state.spot_name};
    _state->actor_generations[key] = actor_ref.generation () + 1;
    target_state.actor_count++;
    auto committed = actor_ref_t (
      node_rid_t::from_string (_state->snapshot.name), std::string (actor_ref.actor_type ()),
      std::string (actor_ref.actor_id ()), actor_ref.generation () + 1);
    actor_factory->second.configure_instance (actor->second.get (), committed, nullptr);
    if (admission->second.entry_spot && _state->actor_created_keys.insert (key).second
        && admission->second.onCreateActor) {
        admission->second.onCreateActor (target_state.spot_instance.get (), actor->second.get ());
    }
    if (admission->second.on_actor_joined) {
        admission->second.on_actor_joined (target_state.spot_instance.get (), actor->second.get ());
    }
    return result_t<actor_join_reply_t>::success (
      actor_join_reply_t{0, committed, response.reply.value_or (zlink::message_t{})});
}

result_t<actor_join_reply_t>
spot_node_runtime_t::join_remote_actor_to_spot_erased (const actor_ref_t &actor_ref,
                                                       spot_rid_t spot_rid,
                                                       const zlink::message_t &request,
                                                       actor_context_t actor_context)
{
    if (actor_ref.empty ()) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::actor_route_not_found,
                                                      "actor ref is empty");
    }
    auto context = find_context (spot_rid);
    if (!context || !context->_state->spot_instance) {
        std::optional<std::string> dynamic_spot_name;
        for (const auto &[spot_name, _] : _state->spot_factories) {
            if (_state->snapshot.entry_spot_name && spot_name == *_state->snapshot.entry_spot_name) {
                continue;
            }
            if (dynamic_spot_name) {
                dynamic_spot_name.reset ();
                break;
            }
            dynamic_spot_name = spot_name;
        }
        if (dynamic_spot_name) {
            (void) get_or_create_spot (*dynamic_spot_name, spot_rid, request);
            context = find_context (spot_rid);
        }
        if (!context || !context->_state->spot_instance) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::spot_route_not_found, "target spot is not registered");
        }
    }

    const auto actor_factory = _state->actor_factories.find (std::string (actor_ref.actor_type ()));
    if (actor_factory == _state->actor_factories.end ()) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::actor_route_not_found, "actor factory is not registered");
    }

    const auto key = actor_key (actor_ref);
    auto &actor_instance = _state->actor_instances[key];
    if (!actor_instance) {
        actor_instance =
          actor_factory->second.create_instance (std::string (actor_ref.actor_id ()));
        if (!actor_instance) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::actor_route_not_found, "actor factory returned null");
        }
    }
    actor_factory->second.configure_instance (actor_instance.get (), actor_ref, &actor_context);

    const auto admission =
      context->_state->actor_admissions.find (actor_factory->second.actor_type);
    if (admission == context->_state->actor_admissions.end () || !admission->second.join) {
        report_spot_dispatch_error (_state,
                                    dispatch_error_surface_t::spot_actor,
                                    dispatch_message_kind_t::actor_request,
                                    dispatch_error_reason_t::handler_missing,
                                    dispatch_error_action_t::reply_error,
                                    "actor.join",
                                    std::nullopt,
                                    std::string (spot_rid.value ()),
                                    std::string (actor_ref.actor_id ()));
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::handler_not_found,
          "spot actor join callback is not registered");
    }

    const auto response = admission->second.join (
      context->_state->spot_instance.get (), actor_instance.get (), request);
    if (!response.accepted) {
        return result_t<actor_join_reply_t>::success (
          actor_join_reply_t{1, actor_ref, response.reply.value_or (zlink::message_t{})});
    }

    if (const auto previous = _state->actor_spot_rids.find (key);
        previous != _state->actor_spot_rids.end ()) {
        if (auto previous_context = find_context (previous->second)) {
            auto &previous_state = *previous_context->_state;
            if (previous_state.actor_count > 0) {
                previous_state.actor_count--;
            }
            if (const auto previous_admission =
                  previous_state.actor_admissions.find (actor_factory->second.actor_type);
                previous_admission != previous_state.actor_admissions.end ()
                && previous_admission->second.onLeaveActor && previous_state.spot_instance) {
                previous_admission->second.onLeaveActor (previous_state.spot_instance.get (),
                                                         actor_instance.get ());
            }
        }
        _state->actor_spot_rids.erase (previous);
        _state->actor_routes.erase (key);
        _state->actor_generations.erase (key);
    }

    auto &target_state = *context->_state;
    _state->actor_spot_rids[key] = target_state.spot_rid;
    _state->actor_routes[key] =
      spot_route_t{node_rid_t::from_string (_state->snapshot.name), target_state.spot_rid,
                   target_state.spot_name};
    _state->actor_generations[key] = actor_ref.generation ();
    target_state.actor_count++;
    if (admission->second.on_actor_joined) {
        admission->second.on_actor_joined (target_state.spot_instance.get (),
                                           actor_instance.get ());
    }
    return result_t<actor_join_reply_t>::success (
      actor_join_reply_t{0, actor_ref, response.reply.value_or (zlink::message_t{})});
}

result_t<actor_join_reply_t>
spot_node_runtime_t::join_actor_to_entry_spot_erased (const actor_ref_t &actor_ref,
                                                      node_rid_t spot_node_rid,
                                                      const zlink::message_t &request)
{
    if (spot_node_rid.empty () || spot_node_rid.value () != _state->snapshot.name) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::spot_route_not_found, "spot node rid does not match this node");
    }
    if (!_state->snapshot.entry_spot_name) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::spot_route_not_found, "entry spot is not registered");
    }
    const auto entry_rid = _state->spot_rids_by_name.find (*_state->snapshot.entry_spot_name);
    if (entry_rid == _state->spot_rids_by_name.end ()) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::spot_route_not_found, "entry spot is not created");
    }
    return join_actor_to_spot_erased (actor_ref, entry_rid->second, request);
}

void spot_node_runtime_t::on_destroy_actor (
  std::function<result_t<void> (const actor_ref_t &)> destroy_actor)
{
    _state->destroy_actor_registry = std::move (destroy_actor);
}

void spot_node_runtime_t::on_actor_ref_updated (
  std::function<result_t<void> (const actor_ref_t &)> update_actor)
{
    _state->update_actor_registry_ref = std::move (update_actor);
}

void spot_node_runtime_t::on_actor_packet_relay (
  std::function<result_t<std::optional<zlink::message_t>> (
    const actor_ref_t &,
    actor_context_t,
    std::string_view,
    const zlink::message_t &,
    service_provider_t &,
    serializer_registry_t &,
    spot_actor_message_metadata_t)> relay)
{
    _state->actor_packet_relay = std::move (relay);
}

result_t<std::optional<zlink::message_t>>
spot_node_runtime_t::relay_actor_packet (const actor_ref_t &actor_ref,
                                         actor_context_t actor_context,
                                         std::string_view packet_name,
                                         const zlink::message_t &message,
                                         service_provider_t &services,
                                         serializer_registry_t &serializers,
                                         spot_actor_message_metadata_t metadata)
{
    if (actor_ref.empty ()) {
        return result_t<std::optional<zlink::message_t>>::failure (
          framework_error_kind_t::actor_route_not_found, "actor ref is empty");
    }

    const auto actor_type_key = std::string (actor_ref.actor_type ());
    const auto found_factory = _state->actor_factories.find (actor_type_key);
    if (found_factory == _state->actor_factories.end ()) {
        return result_t<std::optional<zlink::message_t>>::failure (
          framework_error_kind_t::actor_route_not_found, "actor factory is not registered");
    }

    const auto key = actor_key (actor_ref);
    auto &actor_instance_slot = _state->actor_instances[key];
    if (!actor_instance_slot) {
        actor_instance_slot =
          found_factory->second.create_instance (std::string (actor_ref.actor_id ()));
        if (!actor_instance_slot) {
            return result_t<std::optional<zlink::message_t>>::failure (
              framework_error_kind_t::actor_route_not_found, "actor factory returned null");
        }
    }
    auto actor_instance = actor_instance_slot;
    found_factory->second.configure_instance (actor_instance.get (), actor_ref, &actor_context);

    auto found_location = _state->actor_spot_rids.find (key);
    if (found_location == _state->actor_spot_rids.end ()) {
        if (!_state->snapshot.entry_spot_name) {
            return result_t<std::optional<zlink::message_t>>::failure (
              framework_error_kind_t::spot_route_not_found, "entry spot is not registered");
        }
        const auto entry_rid = _state->spot_rids_by_name.find (*_state->snapshot.entry_spot_name);
        if (entry_rid == _state->spot_rids_by_name.end ()) {
            return result_t<std::optional<zlink::message_t>>::failure (
              framework_error_kind_t::spot_route_not_found, "entry spot is not created");
        }
        _state->actor_spot_rids[key] = entry_rid->second;
        _state->actor_generations[key] = actor_ref.generation ();
        found_location = _state->actor_spot_rids.find (key);
    }

    const auto found_generation = _state->actor_generations.find (key);
    if (found_generation != _state->actor_generations.end ()
        && found_generation->second != actor_ref.generation ()) {
        return result_t<std::optional<zlink::message_t>>::failure (
          framework_error_kind_t::actor_stale_generation, "actor generation is stale");
    }

    auto context = find_context (found_location->second);
    if (!context || !context->_state->spot_instance) {
        return result_t<std::optional<zlink::message_t>>::failure (
          framework_error_kind_t::spot_route_not_found,
          "actor spot context is not registered. node=" + _state->snapshot.name
            + ", actor=" + std::string (actor_ref.actor_id ())
            + ", spot=" + std::string (found_location->second.value ()));
    }

    auto reply =
      spot_handler_registry_t (context->_state)
        .invoke_erased (spot_handler_kind_t::actor_packet, packet_name, {},
                        found_factory->second.actor_type, context->_state->spot_instance.get (),
                        actor_instance.get (), services, serializers, message, std::move (metadata))
        .result ();
    if (!reply) {
        const auto *error = reply.error ();
        report_spot_dispatch_error (
          _state,
          dispatch_error_surface_t::spot_actor,
          dispatch_message_kind_t::actor_request,
          dispatch_reason_from_error (reply.error_kind ()),
          dispatch_error_action_t::reply_error,
          std::string (packet_name),
          std::nullopt,
          std::string (found_location->second.value ()),
          std::string (actor_ref.actor_id ()));
        return result_t<std::optional<zlink::message_t>>::failure (
          reply.error_kind (), error != nullptr ? error->what () : "actor packet relay failed");
    }
    report_spot_dispatch_trace (_state, message_flow_phase_t::replied,
                                dispatch_error_surface_t::spot_actor,
                                dispatch_message_kind_t::actor_request, std::string (packet_name),
                                std::nullopt, std::string (found_location->second.value ()),
                                std::string (actor_ref.actor_id ()));
    return result_t<std::optional<zlink::message_t>>::success (std::move (reply.value ()));
}

spot_node_runtime_t spot_node_runtime_t::from (const spot_node_builder_t &builder)
{
    return spot_node_runtime_t (builder._state);
}

std::optional<spot_node_runtime_t> spot_node_runtime_t::from (const zlink_builder_t &builder,
                                                              const std::string &spot_node_name)
{
    const auto found = builder._state->spot_nodes.find (spot_node_name);
    if (found == builder._state->spot_nodes.end ()) {
        return std::nullopt;
    }
    return spot_node_runtime_t (found->second);
}

spot_create_result_t spot_node_runtime_t::create_spot (std::string spot_name)
{
    return create_spot (std::move (spot_name), zlink::message_t{});
}

spot_create_result_t spot_node_runtime_t::create_spot (std::string spot_name,
                                                       zlink::message_t request)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto found = _state->spot_factories.find (spot_name);
    if (found == _state->spot_factories.end ()) {
        throw framework_exception_t (framework_error_kind_t::spot_create_failed,
                                     "spot factory is not registered");
    }
    const auto lifecycle =
      _state->spot_lifecycles.find (spot_name) != _state->spot_lifecycles.end ()
        ? _state->spot_lifecycles.at (spot_name)
        : spot_lifecycle_callbacks_t{};

    auto rid = spot_rid_t::from_string (_state->snapshot.name + ":" + spot_name + ":"
                                        + std::to_string (_state->next_spot_id++));
    auto context_state = std::make_shared<spot_context_state_t> ();
    context_state->node = _state;
    context_state->channel_runtime = _state->channel_runtime;
    context_state->node_rid = node_rid_t::from_string (_state->snapshot.name);
    context_state->spot_rid = rid;
    context_state->spot_name = spot_name;
    context_state->lifecycle = lifecycle;
    configure_spot_execution (context_state);
    spot_context_t context (context_state);
    entry_spot_context_t entry_context (context_state);
    std::optional<zlink::message_t> create_reply;

    if (lifecycle.create_instance) {
        context_state->spot_instance = lifecycle.create_instance ();
        if (!context_state->spot_instance) {
            throw framework_exception_t (framework_error_kind_t::spot_create_failed,
                                         "SPOT factory returned null");
        }
        if (lifecycle.configure_entry) {
            lifecycle.configure_entry (context_state->spot_instance.get (), entry_context);
        } else if (lifecycle.configure) {
            lifecycle.configure (context_state->spot_instance.get (), context);
        }
        const auto response = lifecycle.on_create
                                ? lifecycle.on_create (context_state->spot_instance.get (), request)
                                : spot_create_response_t::accept ();
        if (!response.accepted) {
            return spot_create_result_t{rid, spot_create_state_t::rejected, response.reply,
                                        context};
        }
        create_reply = response.reply;
        if (lifecycle.on_initialize) {
            lifecycle.on_initialize (context_state->spot_instance.get ());
        }
    }

    attach_native_spot_locked (context_state);
    _state->spot_rids_by_name[spot_name] = rid;
    _state->spot_names_by_rid[std::string (rid.value ())] = spot_name;
    _state->spot_contexts_by_rid[std::string (rid.value ())] = context;
    if (_state->registry_runtime) {
        registry_runtime_t (_state->registry_runtime)
          .add_spot_route (
            spot_route_t{node_rid_t::from_string (_state->snapshot.name), rid, spot_name});
    }
    return spot_create_result_t{rid, spot_create_state_t::created, create_reply, context};
}

spot_create_result_t spot_node_runtime_t::get_or_create_spot (std::string spot_name,
                                                              spot_rid_t spot_rid)
{
    return get_or_create_spot (std::move (spot_name), std::move (spot_rid), zlink::message_t{});
}

spot_create_result_t spot_node_runtime_t::get_or_create_spot (std::string spot_name,
                                                              spot_rid_t spot_rid,
                                                              zlink::message_t request)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto rid_value = std::string (spot_rid.value ());
    if (const auto existing = _state->spot_contexts_by_rid.find (rid_value);
        existing != _state->spot_contexts_by_rid.end ()) {
        return spot_create_result_t{spot_rid, spot_create_state_t::existing, std::nullopt,
                                    existing->second};
    }
    const auto found = _state->spot_factories.find (spot_name);
    if (found == _state->spot_factories.end ()) {
        throw framework_exception_t (framework_error_kind_t::spot_create_failed,
                                     "spot factory is not registered");
    }
    const auto lifecycle =
      _state->spot_lifecycles.find (spot_name) != _state->spot_lifecycles.end ()
        ? _state->spot_lifecycles.at (spot_name)
        : spot_lifecycle_callbacks_t{};

    auto context_state = std::make_shared<spot_context_state_t> ();
    context_state->node = _state;
    context_state->channel_runtime = _state->channel_runtime;
    context_state->node_rid = node_rid_t::from_string (_state->snapshot.name);
    context_state->spot_rid = spot_rid;
    context_state->spot_name = spot_name;
    context_state->lifecycle = lifecycle;
    configure_spot_execution (context_state);
    spot_context_t context (context_state);
    entry_spot_context_t entry_context (context_state);
    std::optional<zlink::message_t> create_reply;

    if (lifecycle.create_instance) {
        context_state->spot_instance = lifecycle.create_instance ();
        if (!context_state->spot_instance) {
            throw framework_exception_t (framework_error_kind_t::spot_create_failed,
                                         "SPOT factory returned null");
        }
        if (lifecycle.configure_entry) {
            lifecycle.configure_entry (context_state->spot_instance.get (), entry_context);
        } else if (lifecycle.configure) {
            lifecycle.configure (context_state->spot_instance.get (), context);
        }
        const auto response = lifecycle.on_create
                                ? lifecycle.on_create (context_state->spot_instance.get (), request)
                                : spot_create_response_t::accept ();
        if (!response.accepted) {
            return spot_create_result_t{spot_rid, spot_create_state_t::rejected, response.reply,
                                        context};
        }
        create_reply = response.reply;
        if (lifecycle.on_initialize) {
            lifecycle.on_initialize (context_state->spot_instance.get ());
        }
    }

    attach_native_spot_locked (context_state);
    _state->spot_rids_by_name[spot_name] = spot_rid;
    _state->spot_names_by_rid[rid_value] = spot_name;
    _state->spot_contexts_by_rid[rid_value] = context;
    if (_state->registry_runtime) {
        registry_runtime_t (_state->registry_runtime)
          .add_spot_route (spot_route_t{node_rid_t::from_string (_state->snapshot.name), spot_rid,
                                        spot_name});
    }
    return spot_create_result_t{spot_rid, spot_create_state_t::created, create_reply, context};
}

std::optional<spot_info_t> spot_node_runtime_t::find_spot (spot_rid_t spot_rid) const
{
    if (const auto name = spot_name_for (spot_rid)) {
        return spot_info_t{std::move (spot_rid), *name};
    }
    return std::nullopt;
}

std::vector<spot_info_t> spot_node_runtime_t::list_spots () const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    std::vector<spot_info_t> spots;
    spots.reserve (_state->spot_names_by_rid.size ());
    for (const auto &[rid, name] : _state->spot_names_by_rid) {
        spots.push_back (spot_info_t{spot_rid_t::from_string (rid), name});
    }
    return spots;
}

task_t<bool> spot_node_runtime_t::close_spot (spot_rid_t spot_rid)
{
    std::optional<spot_context_t> context;
    {
        std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
        const auto found = _state->spot_contexts_by_rid.find (std::string (spot_rid.value ()));
        if (found == _state->spot_contexts_by_rid.end ()) {
            co_return result_t<bool>::success (false);
        }
        context = found->second;
    }
    co_return result_t<bool>::success (context->close ().result ().value ());
}

node_rid_t spot_node_runtime_t::node_rid () const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    return node_rid_t::from_string (_state->snapshot.name);
}

std::optional<std::string> spot_node_runtime_t::spot_name_for (spot_rid_t spot_rid) const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto found = _state->spot_names_by_rid.find (std::string (spot_rid.value ()));
    if (found == _state->spot_names_by_rid.end ()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<spot_route_t> spot_node_runtime_t::resolve_spot (spot_rid_t spot_rid) const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto found = _state->spot_names_by_rid.find (std::string (spot_rid.value ()));
    if (found != _state->spot_names_by_rid.end ()) {
        return spot_route_t{node_rid_t::from_string (_state->snapshot.name), std::move (spot_rid),
                            found->second};
    }
    for (const auto &[_, resolver] : _state->resolvers) {
        if (auto route = resolver (spot_rid)) {
            return route;
        }
    }
    if (_state->snapshot.registry_spot_remote_addresses_enabled && _state->registry_runtime) {
        auto route =
          registry_runtime_t (_state->registry_runtime).resolve_spot_remote_address (spot_rid);
        if (route) {
            return route.value ();
        }
    }
    return std::nullopt;
}

std::optional<spot_rid_t> spot_node_runtime_t::actor_spot (const actor_ref_t &actor_ref) const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto found = _state->actor_spot_rids.find (actor_key (actor_ref));
    if (found == _state->actor_spot_rids.end ()) {
        return std::nullopt;
    }
    return found->second;
}

void spot_node_runtime_t::record_actor_spot (const actor_ref_t &actor_ref, spot_rid_t spot_rid)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto key = actor_key (actor_ref);
    auto name = spot_name_for (spot_rid).value_or ("");
    _state->actor_spot_rids[key] = spot_rid;
    _state->actor_routes[key] =
      spot_route_t{node_rid_t::from_string (_state->snapshot.name), std::move (spot_rid),
                   std::move (name)};
    _state->actor_generations[key] = actor_ref.generation ();
}

std::optional<spot_route_t> spot_node_runtime_t::actor_route (const actor_ref_t &actor_ref) const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto found = _state->actor_routes.find (actor_key (actor_ref));
    if (found == _state->actor_routes.end ()) {
        return std::nullopt;
    }
    return found->second;
}

void spot_node_runtime_t::record_actor_route (const actor_ref_t &actor_ref, spot_route_t route)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto key = actor_key (actor_ref);
    _state->actor_spot_rids[key] = route.spot_rid;
    _state->actor_routes[key] = std::move (route);
    _state->actor_generations[key] = actor_ref.generation ();
}

std::optional<actor_ref_t>
spot_node_runtime_t::current_actor_ref (const actor_ref_t &actor_ref) const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto found = _state->actor_generations.find (actor_key (actor_ref));
    if (found == _state->actor_generations.end ()) {
        return std::nullopt;
    }
    return actor_ref_t (actor_ref.node_rid (), std::string (actor_ref.actor_type ()),
                        std::string (actor_ref.actor_id ()), found->second);
}

const std::vector<std::string> &
spot_node_runtime_t::ordering_log (const spot_context_t &context) const
{
    return context._state->ordering_log;
}

void spot_node_runtime_t::attach_native_node (std::shared_ptr<zlink::service::spot_node_t> node)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    _state->native_node = std::move (node);
    for (auto &[_, context] : _state->spot_contexts_by_rid) {
        attach_native_spot_locked (context._state);
    }
}

void spot_node_runtime_t::detach_native_node ()
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    _state->native_node.reset ();
    _state->native_spots_by_rid.clear ();
    for (auto &[_, context] : _state->spot_contexts_by_rid) {
        context._state->native_spot.reset ();
    }
}

std::vector<spot_context_t> spot_node_runtime_t::active_contexts () const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    std::vector<spot_context_t> contexts;
    contexts.reserve (_state->spot_contexts_by_rid.size ());
    for (const auto &[_, context] : _state->spot_contexts_by_rid) {
        if (!context._state->closed && !context._state->native_spot.expired ()) {
            contexts.push_back (context);
        }
    }
    return contexts;
}

result_t<void> spot_node_runtime_t::dispatch_subscription (
  const spot_context_t &context,
  std::string topic,
  const zlink::message_t &message,
  service_provider_t &services,
  serializer_registry_t &serializers) const
{
    if (!context._state || !context._state->spot_instance) {
        return result_t<void>::failure (framework_error_kind_t::spot_route_not_found,
                                        "spot context is not registered");
    }
    report_spot_dispatch_trace (_state, message_flow_phase_t::received,
                                dispatch_error_surface_t::spot_subscription,
                                dispatch_message_kind_t::publish, std::nullopt, topic,
                                std::string (context._state->spot_rid.value ()));
    std::optional<std::string> packet_name;
    for (const auto &descriptor : context._state->handlers) {
        if (descriptor.kind == spot_handler_kind_t::subscription && descriptor.topic == topic) {
            packet_name = descriptor.packet_name;
            break;
        }
    }
    if (!packet_name) {
        report_spot_dispatch_error (_state,
                                    dispatch_error_surface_t::spot_subscription,
                                    dispatch_message_kind_t::publish,
                                    dispatch_error_reason_t::handler_missing,
                                    dispatch_error_action_t::drop,
                                    std::nullopt,
                                    topic,
                                    std::string (context._state->spot_rid.value ()));
        return result_t<void>::success ();
    }
    auto result = spot_handler_registry_t (context._state)
                    .invoke_erased (spot_handler_kind_t::subscription, *packet_name, topic,
                                    std::type_index (typeid (void)),
                                    context._state->spot_instance.get (), nullptr, services,
                                    serializers, message)
                    .result ();
    if (!result) {
        const auto *error = result.error ();
        report_spot_dispatch_error (_state,
                                    dispatch_error_surface_t::spot_subscription,
                                    dispatch_message_kind_t::publish,
                                    dispatch_reason_from_error (result.error_kind ()),
                                    dispatch_error_action_t::drop,
                                    *packet_name,
                                    topic,
                                    std::string (context._state->spot_rid.value ()));
        return result_t<void>::failure (
          result.error_kind (), error != nullptr ? error->what () : "spot subscription failed");
    }
    report_spot_dispatch_trace (_state, message_flow_phase_t::dispatched,
                                dispatch_error_surface_t::spot_subscription,
                                dispatch_message_kind_t::publish, *packet_name, topic,
                                std::string (context._state->spot_rid.value ()));
    return result_t<void>::success ();
}

std::size_t spot_node_runtime_t::drain_subscriptions (service_provider_t &services,
                                                      serializer_registry_t &serializers) const
{
    std::size_t dispatched = 0;
    for (const auto &context : active_contexts ()) {
        auto native = context._state->native_spot.lock ();
        if (!native) {
            continue;
        }
        while (true) {
            zlink::topic_message_t inbound;
            const int rc = native->subscribe (inbound, zlink::recv_flags_t::dontwait);
            if (rc == static_cast<int> (zlink::recv_result_t::no_data)) {
                break;
            }
            if (rc != static_cast<int> (zlink::recv_result_t::ok)) {
                break;
            }
            if (inbound.parts ().empty ()) {
                continue;
            }
            auto dispatched_result = dispatch_subscription (
              context, inbound.topic (), inbound.parts ().front (), services, serializers);
            if (dispatched_result) {
                ++dispatched;
            }
        }
    }
    return dispatched;
}

} // namespace zlink::framework::detail
