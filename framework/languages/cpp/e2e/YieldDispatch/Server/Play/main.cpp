/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/codecs.hpp"
#include "../Shared/env.hpp"

#include <zlink/framework.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace yd = zlink::framework::e2e::yield_dispatch;
namespace yd_server = zlink::framework::e2e::yield_dispatch::server;

struct yield_actor_t
{
    explicit yield_actor_t (std::string actor_id) : actor_id (std::move (actor_id)) {}

    void set_actor_ref (const zlink::framework::actor_ref_t &value)
    {
        actor_ref = value;
        actor_id = std::string (value.actor_id ());
    }

    void set_actor_context (zlink::framework::actor_context_t value)
    {
        context = std::move (value);
    }

    std::string actor_id;
    zlink::framework::actor_ref_t actor_ref;
    zlink::framework::actor_context_t context;
};

struct yield_actor_factory_t
{
    yield_actor_t create (std::string actor_id) const
    {
        return yield_actor_t (std::move (actor_id));
    }
};

class yield_probe_spot_t;

struct yield_timer_handler_t
{
    zlink::framework::task_t<void>
    handle (yield_probe_spot_t &spot, const zlink::framework::timer_tick_t &tick) const;
};

struct yield_timer_state_t
{
    std::string request_id;
    std::string timer_name;
    std::string mode;
    int delay_ms = 0;
    std::uint64_t tick_count = 0;
    bool active = true;
    zlink::framework::timer_t timer;
};

struct yield_timer_tick_state_t
{
    std::string request_id;
    std::string timer_name;
    std::string mode;
    int delay_ms = 0;
    std::uint64_t tick_number = 0;
};

class evidence_store_t
{
  public:
    explicit evidence_store_t (std::string node_rid) : node_rid (std::move (node_rid)) {}

    void add (std::string entry)
    {
        {
            std::lock_guard lock (_mutex);
            _entries.push_back (std::move (entry));
        }
        _changed.notify_all ();
    }

    yd::yield_evidence_reply_t snapshot (const std::string &request_id) const
    {
        std::lock_guard lock (_mutex);
        std::vector<std::string> selected;
        for (const auto &entry : _entries) {
            if (entry.find ("request=" + request_id) != std::string::npos) {
                selected.push_back (entry);
            }
        }
        return {.request_id = request_id, .evidence = std::move (selected)};
    }

    yd::yield_evidence_reply_t wait (const yd::yield_evidence_wait_req_t &request)
    {
        const auto deadline =
          std::chrono::steady_clock::now () + std::chrono::milliseconds (request.timeout_milliseconds);
        std::unique_lock lock (_mutex);
        _changed.wait_until (lock, deadline, [&] {
            return contains_locked (request.request_id, request.marker);
        });
        std::vector<std::string> selected;
        for (const auto &entry : _entries) {
            if (entry.find ("request=" + request.request_id) != std::string::npos) {
                selected.push_back (entry);
            }
        }
        return {.request_id = request.request_id, .evidence = std::move (selected)};
    }

    std::string node_rid;

  private:
    bool contains_locked (const std::string &request_id, const std::string &marker) const
    {
        for (const auto &entry : _entries) {
            if (entry.find ("request=" + request_id) != std::string::npos
                && entry.find (marker) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    mutable std::mutex _mutex;
    std::condition_variable _changed;
    std::vector<std::string> _entries;
};

class ensure_spot_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<evidence_store_t,
                                          zlink::framework::spot_node_manager_t>;
    using request_type = yd::ensure_spot_req_t;
    using reply_type = yd::ensure_spot_reply_t;

    ensure_spot_handler_t (evidence_store_t &evidence,
                           zlink::framework::spot_node_manager_t &spots) :
        _evidence (evidence), _spots (spots)
    {
    }

    yd::ensure_spot_reply_t handle (const yd::ensure_spot_req_t &request,
                                    const zlink::framework::route_handler_context_t &)
    {
        const auto rid = zlink::framework::spot_rid_t::from_string (request.spot_rid);
        const auto created = _spots.get_or_create_spot (yd::probe_spot_name, rid);
        _evidence.add ("spot-ensured|rid=" + _evidence.node_rid + "|spot="
                       + std::string (created.spot_rid.value ()) + "|request="
                       + request.spot_rid);
        return {.spot_rid = std::string (created.spot_rid.value ()),
                .node_rid = _evidence.node_rid};
    }

  private:
    evidence_store_t &_evidence;
    zlink::framework::spot_node_manager_t &_spots;
};

class bind_yield_actors_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<evidence_store_t,
                                          zlink::framework::spot_node_manager_t>;
    using request_type = yd::bind_yield_actors_req_t;
    using reply_type = yd::bind_yield_actors_reply_t;

    bind_yield_actors_handler_t (evidence_store_t &evidence,
                                 zlink::framework::spot_node_manager_t &spots) :
        _evidence (evidence), _spots (spots)
    {
    }

    yd::bind_yield_actors_reply_t handle (const yd::bind_yield_actors_req_t &request,
                                          const zlink::framework::route_handler_context_t &)
    {
        const auto spot_rid = zlink::framework::spot_rid_t::from_string (request.spot_rid);
        (void) _spots.get_or_create_spot (yd::probe_spot_name, spot_rid);
        yd::bind_yield_actors_reply_t reply{.spot_rid = request.spot_rid};
        for (const auto &actor_id : request.actor_ids) {
            auto actor_ref = zlink::framework::actor_ref_t (
              zlink::framework::node_rid_t::from_string (_evidence.node_rid), yd::actor_type,
              actor_id, 0);
            if (auto current = _spots.current_actor_ref (actor_ref)) {
                actor_ref = *current;
            } else {
                actor_ref = zlink::framework::actor_ref_t (
                  zlink::framework::node_rid_t::from_string (_evidence.node_rid),
                  yd::actor_type, actor_id, ++_generation);
            }
            _evidence.add ("bind-actor|rid=" + _evidence.node_rid + "|spot="
                           + request.spot_rid + "|actor=" + actor_id + "|generation="
                           + std::to_string (actor_ref.generation ()));
            reply.actors.push_back (
              {.actor_id = actor_id,
               .node_rid = std::string (actor_ref.node_rid ().value ()),
               .generation = actor_ref.generation ()});
        }
        return reply;
    }

  private:
    evidence_store_t &_evidence;
    zlink::framework::spot_node_manager_t &_spots;
    static inline std::atomic_uint64_t _generation{0};
};

class evidence_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<evidence_store_t>;
    using request_type = yd::yield_evidence_req_t;
    using reply_type = yd::yield_evidence_reply_t;

    explicit evidence_handler_t (evidence_store_t &evidence) : _evidence (evidence) {}

    yd::yield_evidence_reply_t handle (const yd::yield_evidence_req_t &request,
                                       const zlink::framework::route_handler_context_t &)
    {
        return _evidence.snapshot (request.request_id);
    }

  private:
    evidence_store_t &_evidence;
};

class evidence_wait_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<evidence_store_t>;
    using request_type = yd::yield_evidence_wait_req_t;
    using reply_type = yd::yield_evidence_reply_t;

    explicit evidence_wait_handler_t (evidence_store_t &evidence) : _evidence (evidence) {}

    yd::yield_evidence_reply_t handle (const yd::yield_evidence_wait_req_t &request,
                                       const zlink::framework::route_handler_context_t &)
    {
        return _evidence.wait (request);
    }

  private:
    evidence_store_t &_evidence;
};

class yield_probe_spot_t : public zlink::framework::spot_t
{
  public:
    explicit yield_probe_spot_t (evidence_store_t &evidence) : _evidence (evidence) {}

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ()
          .add_handler<&yield_probe_spot_t::hold_req> (yd::hold_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::hold_command> (yd::hold_command_t::packet_name)
          .add_handler<&yield_probe_spot_t::yield_req> (yd::yield_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::yield_command> (yd::yield_command_t::packet_name)
          .add_handler<&yield_probe_spot_t::worker_yield_req> (
            yd::worker_yield_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::worker_yield_command> (
            yd::worker_yield_command_t::packet_name)
          .add_handler<&yield_probe_spot_t::remote_spot_yield_req> (
            yd::remote_spot_yield_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::timer_start_command> (
            yd::timer_start_command_t::packet_name)
          .add_handler<&yield_probe_spot_t::timer_stop_command> (
            yd::timer_stop_command_t::packet_name)
          .add_handler<&yield_probe_spot_t::probe_req> (yd::probe_req_t::packet_name)
          .add_handler<&yield_probe_spot_t::probe_command> (yd::probe_command_t::packet_name);
    }

    zlink::framework::task_t<yd::yield_dispatch_reply_t> hold_req (const yd::hold_req_t &request)
    {
        co_await hold_impl (request.request_id, request.delay_ms);
        co_return reply ("YD-A1", request.request_id, "hold-completed");
    }

    zlink::framework::task_t<void> hold_command (const yd::hold_command_t &request)
    {
        co_await hold_impl (request.request_id, request.delay_ms);
    }

    zlink::framework::task_t<yd::yield_dispatch_reply_t> yield_req (const yd::yield_req_t &request)
    {
        co_await yield_impl (request.request_id, request.delay_ms, request.correlation_id);
        co_return reply ("YD-A2", request.request_id, "yield-completed");
    }

    zlink::framework::task_t<void> yield_command (const yd::yield_command_t &request)
    {
        co_await yield_impl (request.request_id, request.delay_ms, request.correlation_id);
    }

    zlink::framework::task_t<yd::yield_dispatch_reply_t>
    worker_yield_req (const yd::worker_yield_req_t &request)
    {
        co_await worker_yield_impl (request.request_id, request.delay_ms);
        co_return reply ("YD-A4", request.request_id, "worker-yield-completed");
    }

    zlink::framework::task_t<void> worker_yield_command (
      const yd::worker_yield_command_t &request)
    {
        co_await worker_yield_impl (request.request_id, request.delay_ms);
    }

    zlink::framework::task_t<yd::yield_dispatch_reply_t>
    remote_spot_yield_req (const yd::remote_spot_yield_req_t &request)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        _evidence.add ("remote-yield-started|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|request=" + request.request_id + "|target="
                       + request.target_spot_rid + "|handler=spot");
        auto call =
          _context
            .request_to<yd::yield_dispatch_reply_t> (
              zlink::framework::node_rid_t::from_string ("play-b"),
              zlink::framework::spot_rid_t::from_string (request.target_spot_rid),
              yd::yield_req_t{.request_id = request.request_id,
                              .delay_ms = request.delay_ms,
                              .correlation_id = "remote-spot"})
            .packet_name (yd::yield_req_t::packet_name)
            .timeout (std::chrono::milliseconds (5000));
        _evidence.add ("remote-yield-released|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|request=" + request.request_id + "|target="
                       + request.target_spot_rid + "|handler=spot");
        auto target_reply = co_await call.yield ();
        _evidence.add ("remote-yield-resumed|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|request=" + request.request_id + "|target="
                       + request.target_spot_rid + "|targetNode=" + target_reply.node_rid
                       + "|handler=spot");
        _evidence.add ("remote-yield-completed|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|request=" + request.request_id + "|target="
                       + request.target_spot_rid + "|targetNode=" + target_reply.node_rid
                       + "|handler=spot");
        co_return reply ("YD-D2", request.request_id, "remote-yield-completed");
    }

    void timer_start_command (const yd::timer_start_command_t &request)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        {
            std::lock_guard lock (_timer_mutex);
            if (_timers.find (request.timer_name) != _timers.end ()) {
                _evidence.add ("timer-start-duplicate-ignored|rid=" + _evidence.node_rid
                               + "|spot=" + spot_rid + "|request=" + request.request_id
                               + "|timer=" + request.timer_name + "|mode=" + request.mode);
                return;
            }
            _timers.emplace (request.timer_name,
                             yield_timer_state_t{.request_id = request.request_id,
                                                 .timer_name = request.timer_name,
                                                 .mode = request.mode,
                                                 .delay_ms = request.delay_ms});
        }

        zlink::framework::timer_options_t options;
        options.overrun_policy = zlink::framework::timer_overrun_policy_t::delay_next_tick;
        auto timer = _context.add_timer<yield_timer_handler_t> (
          request.timer_name, std::chrono::milliseconds (request.period_ms), options);
        {
            std::lock_guard lock (_timer_mutex);
            if (auto found = _timers.find (request.timer_name); found != _timers.end ()) {
                found->second.timer = std::move (timer);
            }
        }
        _evidence.add ("timer-started|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request.request_id + "|timer=" + request.timer_name
                       + "|mode=" + request.mode);
    }

    void timer_stop_command (const yd::timer_stop_command_t &request)
    {
        std::vector<zlink::framework::timer_t> timers;
        {
            std::lock_guard lock (_timer_mutex);
            for (auto it = _timers.begin (); it != _timers.end ();) {
                if (it->second.request_id == request.request_id) {
                    timers.push_back (std::move (it->second.timer));
                    it = _timers.erase (it);
                } else {
                    ++it;
                }
            }
        }
        for (auto &timer : timers) {
            timer.cancel ();
        }
    }

    yd::yield_dispatch_reply_t probe_req (const yd::probe_req_t &request)
    {
        probe_impl (request.request_id, request.marker);
        return reply ("YD-PROBE", request.request_id, request.marker);
    }

    void probe_command (const yd::probe_command_t &request)
    {
        probe_impl (request.request_id, request.marker);
    }

    zlink::framework::task_t<void> handle_timer_tick (
      const zlink::framework::timer_tick_t &tick)
    {
        auto state = next_timer_tick (tick.name);
        if (!state) {
            co_return;
        }

        const auto spot_rid = std::string (_context.spot_rid ().value ());
        const auto tick_id = std::to_string (state->tick_number);
        if (state->mode == "fast") {
            _evidence.add ("timer-fast-started|rid=" + _evidence.node_rid + "|spot="
                           + spot_rid + "|request=" + state->request_id + "|timer="
                           + state->timer_name + "|tick=" + tick_id + "|handler=timer");
            _evidence.add ("timer-fast-completed|rid=" + _evidence.node_rid + "|spot="
                           + spot_rid + "|request=" + state->request_id + "|timer="
                           + state->timer_name + "|tick=" + tick_id + "|handler=timer");
            deactivate_timer (state->timer_name);
            co_return;
        }

        if (state->tick_number == 1
            && (state->mode == "yield-on-first" || state->mode == "yield-then-next")) {
            _evidence.add ("timer-yield-started|rid=" + _evidence.node_rid + "|spot="
                           + spot_rid + "|request=" + state->request_id + "|timer="
                           + state->timer_name + "|tick=" + tick_id + "|handler=timer");
            auto call =
              _context.outbound ()
                .request (yd::delay_channel,
                          yd::delay_req_t{.request_id = state->request_id,
                                          .delay_ms = state->delay_ms,
                                          .marker = state->timer_name})
                .packet_name (yd::delay_req_t::packet_name)
                .timeout (std::chrono::milliseconds (5000));
            _evidence.add ("timer-yield-released|rid=" + _evidence.node_rid + "|spot="
                           + spot_rid + "|request=" + state->request_id + "|timer="
                           + state->timer_name + "|tick=" + tick_id + "|handler=timer");
            co_await call.yield<yd::delay_reply_t> ();
            _evidence.add ("timer-yield-resumed|rid=" + _evidence.node_rid + "|spot="
                           + spot_rid + "|request=" + state->request_id + "|timer="
                           + state->timer_name + "|tick=" + tick_id + "|handler=timer");
            _evidence.add ("timer-yield-completed|rid=" + _evidence.node_rid + "|spot="
                           + spot_rid + "|request=" + state->request_id + "|timer="
                           + state->timer_name + "|tick=" + tick_id + "|handler=timer");
            if (state->mode == "yield-on-first") {
                deactivate_timer (state->timer_name);
            }
            co_return;
        }

        if (state->mode == "yield-then-next" && state->tick_number == 2) {
            _evidence.add ("timer-next-started|rid=" + _evidence.node_rid + "|spot="
                           + spot_rid + "|request=" + state->request_id + "|timer="
                           + state->timer_name + "|tick=" + tick_id + "|handler=timer");
            _evidence.add ("timer-next-completed|rid=" + _evidence.node_rid + "|spot="
                           + spot_rid + "|request=" + state->request_id + "|timer="
                           + state->timer_name + "|tick=" + tick_id + "|handler=timer");
            deactivate_timer (state->timer_name);
        }
        co_return;
    }

  private:
    void deactivate_timer (const std::string &timer_name)
    {
        std::lock_guard lock (_timer_mutex);
        if (auto found = _timers.find (timer_name); found != _timers.end ()) {
            found->second.active = false;
        }
    }

    std::optional<yield_timer_tick_state_t> next_timer_tick (const std::string &timer_name)
    {
        std::lock_guard lock (_timer_mutex);
        auto found = _timers.find (timer_name);
        if (found == _timers.end ()) {
            return std::nullopt;
        }
        auto &state = found->second;
        if (!state.active) {
            return std::nullopt;
        }
        ++state.tick_count;
        return yield_timer_tick_state_t{.request_id = state.request_id,
                                        .timer_name = state.timer_name,
                                        .mode = state.mode,
                                        .delay_ms = state.delay_ms,
                                        .tick_number = state.tick_count};
    }

    zlink::framework::task_t<void> hold_impl (const std::string &request_id, int delay_ms)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        _evidence.add ("hold-started|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request_id + "|handler=spot");
        co_await _context.outbound ()
          .request (yd::delay_channel,
                    yd::delay_req_t{.request_id = request_id,
                                    .delay_ms = delay_ms,
                                    .marker = "hold"})
          .packet_name (yd::delay_req_t::packet_name)
          .timeout (std::chrono::milliseconds (5000))
          .async<yd::delay_reply_t> ();
        _evidence.add ("hold-resumed|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request_id + "|handler=spot");
        _evidence.add ("hold-completed|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request_id + "|handler=spot");
        co_return;
    }

    zlink::framework::task_t<void>
    yield_impl (const std::string &request_id, int delay_ms, const std::string &correlation_id)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        _evidence.add ("yield-started|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request_id + "|correlation="
                       + correlation_id + "|handler=spot");
        auto call =
          _context.outbound ()
            .request (yd::delay_channel,
                      yd::delay_req_t{.request_id = request_id,
                                      .delay_ms = delay_ms,
                                      .marker = "yield"})
            .packet_name (yd::delay_req_t::packet_name)
            .timeout (std::chrono::milliseconds (5000));
        _evidence.add ("yield-released|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request_id + "|correlation="
                       + correlation_id + "|handler=spot");
        co_await call.yield<yd::delay_reply_t> ();
        _evidence.add ("yield-resumed|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request_id + "|correlation="
                       + correlation_id + "|handler=spot");
        _evidence.add ("yield-completed|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request_id + "|correlation="
                       + correlation_id + "|handler=spot");
        co_return;
    }

    zlink::framework::task_t<void>
    worker_yield_impl (const std::string &request_id, int delay_ms)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        _evidence.add ("worker-yield-started|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request_id + "|handler=spot");
        auto call = _context.run_worker ([request_id, delay_ms] {
            std::this_thread::sleep_for (std::chrono::milliseconds (delay_ms));
            return request_id;
        });
        _evidence.add ("worker-yield-released|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request_id + "|handler=spot");
        co_await call.yield ();
        _evidence.add ("worker-yield-resumed|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request_id + "|handler=spot");
        _evidence.add ("worker-yield-completed|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request_id + "|handler=spot");
        co_return;
    }

    void probe_impl (const std::string &request_id, const std::string &marker)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        _evidence.add ("probe-started|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request_id + "|marker=" + marker
                       + "|handler=spot");
        _evidence.add ("probe-completed|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|request=" + request_id + "|marker=" + marker
                       + "|handler=spot");
    }

    yd::yield_dispatch_reply_t
    reply (std::string scenario_id, std::string request_id, std::string marker) const
    {
        return {.scenario_id = std::move (scenario_id),
                .request_id = std::move (request_id),
                .spot_rid = std::string (_context.spot_rid ().value ()),
                .node_rid = _evidence.node_rid,
                .marker = std::move (marker)};
    }

    evidence_store_t &_evidence;
    zlink::framework::spot_context_t _context;
    std::mutex _timer_mutex;
    std::map<std::string, yield_timer_state_t> _timers;
};

zlink::framework::task_t<void>
yield_timer_handler_t::handle (yield_probe_spot_t &spot,
                               const zlink::framework::timer_tick_t &tick) const
{
    co_await spot.handle_timer_tick (tick);
}

class yield_entry_spot_t : public zlink::framework::entry_spot_t
{
  public:
    explicit yield_entry_spot_t (evidence_store_t &evidence) : _evidence (evidence) {}

    void configure (zlink::framework::entry_spot_context_t &context)
    {
        _context = context;
        context.handlers ()
          .add_actor_packet<&yield_entry_spot_t::actor_yield_req> (
            yd::actor_yield_req_t::packet_name)
          .add_actor_packet<&yield_entry_spot_t::actor_fast_req> (
            yd::actor_fast_req_t::packet_name)
          .add_actor_packet<&yield_entry_spot_t::actor_join_yield_req> (
            yd::actor_join_yield_req_t::packet_name);
    }

    void configure (zlink::framework::spot_context_t &context)
    {
        zlink::framework::entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (yield_actor_t &actor, const zlink::framework::message_t &request_message)
    {
        const auto request = request_message.decode<yd::delay_req_t> ();
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        _evidence.add ("actor-join-target-started|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|request="
                       + request.request_id + "|handler=entry");
        std::this_thread::sleep_for (std::chrono::milliseconds (request.delay_ms));
        _evidence.add ("actor-join-target-completed|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|request="
                       + request.request_id + "|handler=entry");
        return zlink::framework::spot_actor_join_response_t::accept (
          yd::delay_reply_t{.request_id = request.request_id,
                            .marker = request.marker,
                            .node_rid = _evidence.node_rid});
    }

    zlink::framework::task_t<yd::actor_yield_reply_t>
    actor_yield_req (yield_actor_t &actor,
                     zlink::framework::spot_actor_request_context_t &,
                     const yd::actor_yield_req_t &request)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        const auto mailbox = "actor:" + actor.actor_id;
        _evidence.add ("actor-yield-started|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|actor=" + actor.actor_id + "|mailbox=" + mailbox + "|request="
                       + request.request_id + "|handler=actor");
        auto call =
          _context.outbound ()
            .request (yd::delay_channel,
                      yd::delay_req_t{.request_id = request.request_id,
                                      .delay_ms = request.delay_ms,
                                      .marker = "actor-" + actor.actor_id})
            .packet_name (yd::delay_req_t::packet_name)
            .timeout (std::chrono::milliseconds (5000));
        _evidence.add ("actor-yield-released|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|actor=" + actor.actor_id + "|mailbox=" + mailbox + "|request="
                       + request.request_id + "|handler=actor");
        co_await call.yield<yd::delay_reply_t> ();
        _evidence.add ("actor-yield-resumed|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|actor=" + actor.actor_id + "|mailbox=" + mailbox + "|request="
                       + request.request_id + "|handler=actor");
        _evidence.add ("actor-yield-completed|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|actor=" + actor.actor_id + "|mailbox=" + mailbox + "|request="
                       + request.request_id + "|handler=actor");
        co_return actor_reply ("YD-B", request.request_id, actor.actor_id,
                               "actor-yield-completed");
    }

    yd::actor_yield_reply_t actor_fast_req (yield_actor_t &actor,
                                            zlink::framework::spot_actor_request_context_t &,
                                            const yd::actor_fast_req_t &request)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        const auto mailbox = "actor:" + actor.actor_id;
        _evidence.add ("actor-fast-started|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|actor=" + actor.actor_id + "|mailbox=" + mailbox + "|request="
                       + request.request_id + "|marker=" + request.marker + "|handler=actor");
        _evidence.add ("actor-fast-completed|rid=" + _evidence.node_rid + "|spot=" + spot_rid
                       + "|actor=" + actor.actor_id + "|mailbox=" + mailbox + "|request="
                       + request.request_id + "|marker=" + request.marker + "|handler=actor");
        return actor_reply ("YD-B", request.request_id, actor.actor_id, request.marker);
    }

    zlink::framework::task_t<yd::actor_yield_reply_t>
    actor_join_yield_req (yield_actor_t &actor,
                          zlink::framework::spot_actor_request_context_t &,
                          const yd::actor_join_yield_req_t &request)
    {
        const auto spot_rid = std::string (_context.spot_rid ().value ());
        const auto mailbox = "actor:" + actor.actor_id;
        _evidence.add ("actor-join-yield-started|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|mailbox=" + mailbox
                       + "|request=" + request.request_id + "|target_node="
                       + request.target_node_rid + "|handler=actor");
        auto call = actor.context
                      .join_entry_spot (zlink::framework::node_rid_t::from_string (
                                          request.target_node_rid),
                                        yd::delay_req_t{.request_id = request.request_id,
                                                        .delay_ms = 350,
                                                        .marker = "join"})
                      .timeout (std::chrono::milliseconds (5000));
        _evidence.add ("actor-join-yield-released|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|mailbox=" + mailbox
                       + "|request=" + request.request_id + "|target_node="
                       + request.target_node_rid + "|handler=actor");
        const auto joined = co_await call.yield<yd::delay_reply_t> ();
        const auto accepted = joined.result_code == 0 ? "true" : "false";
        _evidence.add ("actor-join-yield-resumed|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|mailbox=" + mailbox
                       + "|request=" + request.request_id + "|target_node="
                       + request.target_node_rid + "|accepted=" + accepted
                       + "|handler=actor");
        _evidence.add ("actor-join-yield-completed|rid=" + _evidence.node_rid + "|spot="
                       + spot_rid + "|actor=" + actor.actor_id + "|mailbox=" + mailbox
                       + "|request=" + request.request_id + "|target_node="
                       + request.target_node_rid + "|accepted=" + accepted
                       + "|handler=actor");
        co_return actor_reply ("YD-B3", request.request_id, actor.actor_id,
                               "actor-join-yield-completed");
    }

  private:
    yd::actor_yield_reply_t
    actor_reply (std::string scenario_id,
                 std::string request_id,
                 std::string actor_id,
                 std::string marker) const
    {
        return {.scenario_id = std::move (scenario_id),
                .request_id = std::move (request_id),
                .actor_id = std::move (actor_id),
                .spot_rid = std::string (_context.spot_rid ().value ()),
                .node_rid = _evidence.node_rid,
                .marker = std::move (marker)};
    }

    evidence_store_t &_evidence;
    zlink::framework::entry_spot_context_t _context;
};

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    const auto log_dir = yd_server::env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto node_rid = yd_server::env_or ("ZLINK_CPP_E2E_NODE_RID", "play-a");
    const auto http_endpoint = yd_server::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto registry_router = yd_server::env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto control_endpoint = yd_server::env_or ("ZLINK_CPP_E2E_CONTROL_ENDPOINT");
    const auto delay_endpoint = yd_server::env_or ("ZLINK_CPP_E2E_DELAY_ENDPOINT");
    const auto spot_route_endpoint = yd_server::env_or ("ZLINK_CPP_E2E_SPOT_ROUTE_ENDPOINT");
    const auto spot_router_endpoint = yd_server::env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto spot_pub_endpoint = yd_server::env_or ("ZLINK_CPP_E2E_SPOT_PUB_ENDPOINT");

    auto app = app_t::create ();
    app.logging ().use_file (log_dir + "/" + node_rid + ".log").set_min_level (log_level_t::debug);
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        auto evidence = std::make_unique<evidence_store_t> (node_rid);
        auto *evidence_ptr = evidence.get ();
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + node_rid + "-flow.log")
          .trace_label ("cpp-yd-" + node_rid);
        options.services ()
          .add_singleton<evidence_store_t> (std::move (evidence))
          .add_transient<bind_yield_actors_handler_t, evidence_store_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<ensure_spot_handler_t, evidence_store_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<evidence_handler_t, evidence_store_t> ()
          .add_transient<evidence_wait_handler_t, evidence_store_t> ();
        yd_server::configure_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (registry_router);
        options.add_client_server_channel (yd::delay_channel)
          .enable_client (delay_endpoint)
          .set_routing_id (zlink::routing_id_t::from (node_rid));
        options.add_route_mesh (yd::control_channel)
          .enable_server (control_endpoint)
          .enable_client ()
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .add_request_handler<bind_yield_actors_handler_t, yd::bind_yield_actors_req_t,
                               yd::bind_yield_actors_reply_t> (
            yd::bind_yield_actors_req_t::packet_name, &bind_yield_actors_handler_t::handle)
          .add_request_handler<ensure_spot_handler_t, yd::ensure_spot_req_t,
                               yd::ensure_spot_reply_t> (
            yd::ensure_spot_req_t::packet_name, &ensure_spot_handler_t::handle)
          .add_request_handler<evidence_handler_t, yd::yield_evidence_req_t,
                               yd::yield_evidence_reply_t> (
            yd::yield_evidence_req_t::packet_name, &evidence_handler_t::handle)
          .add_request_handler<evidence_wait_handler_t, yd::yield_evidence_wait_req_t,
                               yd::yield_evidence_reply_t> (
            yd::yield_evidence_wait_req_t::packet_name, &evidence_wait_handler_t::handle);
        options.add_spot_mesh (yd::spot_channel)
          .use_registry_spot_resolver (yd::control_channel)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .enable_router (spot_router_endpoint)
          .enable_pub_sub (spot_pub_endpoint)
          .add_entry_spot<yield_entry_spot_t> (
            [evidence_ptr] { return std::make_shared<yield_entry_spot_t> (*evidence_ptr); })
          .add_spot<yield_probe_spot_t> (
            yd::probe_spot_name,
            [evidence_ptr] { return std::make_shared<yield_probe_spot_t> (*evidence_ptr); })
          .add_actor_factory<yield_actor_factory_t> (yd::actor_type);
        options.http ().listen (http_endpoint).map_health ("/health");
    });
    return app.run (argc, argv);
}
