/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/mesh/route_mesh_runtime_service.hpp"

#include <zlink/framework/contracts/errors/error.hpp>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <thread>
#include <utility>

namespace zlink::framework::runtime
{

namespace
{

mesh_node_state_t map_state (zlink::mesh_node_state_t state)
{
    switch (state) {
        case zlink::mesh_node_state_t::created:
            return mesh_node_state_t::starting;
        case zlink::mesh_node_state_t::started:
        case zlink::mesh_node_state_t::partial_ready:
        case zlink::mesh_node_state_t::ready:
            return mesh_node_state_t::serving;
        case zlink::mesh_node_state_t::draining:
            return mesh_node_state_t::draining;
        case zlink::mesh_node_state_t::stopped:
            return mesh_node_state_t::stopped;
        case zlink::mesh_node_state_t::error:
            return mesh_node_state_t::faulted;
    }
    return mesh_node_state_t::faulted;
}

bool native_ready (zlink::mesh_node_state_t state)
{
    return state == zlink::mesh_node_state_t::started
           || state == zlink::mesh_node_state_t::partial_ready
           || state == zlink::mesh_node_state_t::ready;
}

std::chrono::system_clock::time_point event_time (std::uint64_t timestamp_ms)
{
    return std::chrono::system_clock::time_point{std::chrono::milliseconds (timestamp_ms)};
}

std::optional<std::uint64_t> positive (std::uint64_t value)
{
    return value == 0 ? std::nullopt : std::optional<std::uint64_t>{value};
}

std::optional<std::string> text (const std::string &value)
{
    return value.empty () ? std::nullopt : std::optional<std::string>{value};
}

std::string peer_admission_state (zlink::mesh_peer_state_t state)
{
    switch (state) {
        case zlink::mesh_peer_state_t::configured:
            return "configured";
        case zlink::mesh_peer_state_t::connecting:
            return "connecting";
        case zlink::mesh_peer_state_t::admitted:
            return "ready";
        case zlink::mesh_peer_state_t::draining:
            return "draining";
        case zlink::mesh_peer_state_t::closed:
            return "disconnected";
        case zlink::mesh_peer_state_t::error:
            return "rejected";
    }
    return "rejected";
}

std::string peer_drain_state (zlink::mesh_peer_state_t state)
{
    return state == zlink::mesh_peer_state_t::draining ? "draining" : "serving";
}

std::string event_identifier (zlink::mesh_monitor_event_kind_t kind)
{
    switch (kind) {
        case zlink::mesh_monitor_event_kind_t::state_changed:
            return "zlink.runtime.mesh_node.state_changed";
        case zlink::mesh_monitor_event_kind_t::channel_changed:
            return "zlink.runtime.mesh_node.channel_changed";
        case zlink::mesh_monitor_event_kind_t::backpressured:
            return "zlink.runtime.mesh_node.multicast_backpressured";
        case zlink::mesh_monitor_event_kind_t::multicast_dropped:
            return "zlink.runtime.mesh_node.multicast_dropped";
        case zlink::mesh_monitor_event_kind_t::claim_revoked:
            return "zlink.runtime.mesh_node.claim_changed";
        case zlink::mesh_monitor_event_kind_t::peer_connecting:
        case zlink::mesh_monitor_event_kind_t::peer_admitted:
        case zlink::mesh_monitor_event_kind_t::peer_draining:
        case zlink::mesh_monitor_event_kind_t::peer_closed:
        case zlink::mesh_monitor_event_kind_t::peer_rejected:
        case zlink::mesh_monitor_event_kind_t::protocol_error:
            return "zlink.runtime.mesh_node.peer_changed";
        default:
            return {};
    }
}

std::optional<std::string> event_reason (zlink::mesh_monitor_event_kind_t kind)
{
    switch (kind) {
        case zlink::mesh_monitor_event_kind_t::peer_connecting:
            return "connecting";
        case zlink::mesh_monitor_event_kind_t::peer_admitted:
            return "ready";
        case zlink::mesh_monitor_event_kind_t::peer_draining:
            return "draining";
        case zlink::mesh_monitor_event_kind_t::peer_closed:
            return "disconnected";
        case zlink::mesh_monitor_event_kind_t::peer_rejected:
            return "HandshakeFailed";
        case zlink::mesh_monitor_event_kind_t::protocol_error:
            return "rejected";
        case zlink::mesh_monitor_event_kind_t::backpressured:
            return "backpressure";
        default:
            return std::nullopt;
    }
}

bool carries_target_counts (zlink::mesh_monitor_event_kind_t kind)
{
    return kind == zlink::mesh_monitor_event_kind_t::multicast_committed
           || kind == zlink::mesh_monitor_event_kind_t::multicast_dropped
           || kind == zlink::mesh_monitor_event_kind_t::backpressured;
}

framework_exception_t invalid_runtime_call (std::string message)
{
    return framework_exception_t (framework_error_kind_t::request_protocol_error,
                                  std::move (message));
}

} // namespace

struct route_mesh_runtime_service_t::state_t :
    public std::enable_shared_from_this<route_mesh_runtime_service_t::state_t>
{
    struct observer_t : public std::enable_shared_from_this<observer_t>
    {
        observer_t (std::size_t capacity_,
                    std::function<void (const mesh_runtime_event_t &)> callback_) :
            capacity (capacity_), callback (std::move (callback_))
        {
        }

        ~observer_t () { close (); }

        void start ()
        {
            const auto self = shared_from_this ();
            worker = std::thread ([self] {
                for (;;) {
                    std::optional<mesh_runtime_event_t> event;
                    {
                        std::unique_lock lock (self->mutex);
                        self->ready.wait (
                          lock, [&] {
                              return self->closed || !self->pending.empty ();
                          });
                        if (self->closed && self->pending.empty ())
                            return;
                        event.emplace (std::move (self->pending.front ()));
                        self->pending.pop_front ();
                    }
                    try {
                        self->callback (*event);
                    }
                    catch (...) {
                        std::lock_guard lock (self->mutex);
                        self->closed = true;
                        self->pending.clear ();
                        self->ready.notify_all ();
                        return;
                    }
                }
            });
        }

        void enqueue (mesh_runtime_event_t event)
        {
            std::lock_guard lock (mutex);
            if (closed)
                return;
            if (pending.size () == capacity) {
                const auto same = std::find_if (
                  pending.begin (), pending.end (),
                  [&] (const mesh_runtime_event_t &queued) {
                      return queued.identifier == event.identifier;
                  });
                if (same != pending.end ())
                    pending.erase (same);
                else
                    pending.pop_front ();
                pending.push_back (std::move (event));
            } else {
                pending.push_back (std::move (event));
            }
            ready.notify_one ();
        }

        void close () noexcept
        {
            {
                std::lock_guard lock (mutex);
                closed = true;
                pending.clear ();
            }
            ready.notify_all ();
            if (worker.joinable ()) {
                if (worker.get_id () == std::this_thread::get_id ())
                    worker.detach ();
                else
                    worker.join ();
            }
        }

        std::size_t capacity;
        std::function<void (const mesh_runtime_event_t &)> callback;
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<mesh_runtime_event_t> pending;
        bool closed = false;
        std::thread worker;
    };

    struct hub_t
    {
        explicit hub_t (std::shared_ptr<detail::mesh_node_runtime_t> node_) :
            node (std::move (node_))
        {
        }

        std::shared_ptr<detail::mesh_node_runtime_t> node;
        std::mutex mutex;
        std::vector<std::weak_ptr<observer_t>> observers;
        logical_multicast_snapshot_t multicast{};
        bool application_claim_active = false;
        std::uint64_t pending_application_callbacks = 0;
        std::string location_state = "not_configured";
        std::optional<std::chrono::system_clock::time_point>
          location_last_success;
        std::optional<std::chrono::system_clock::time_point>
          location_last_failure;
        std::chrono::steady_clock::time_point next_location_poll{};
        std::atomic_bool stopped{false};
        std::thread pump;
    };

    std::map<std::string, std::shared_ptr<hub_t>> hubs;
    location_runtime_query_t *location_runtime;
    drain_callback_t drain_callback;
    await_drained_callback_t await_drained_callback;
    mutable std::mutex sequence_mutex;
    mutable std::map<std::string, std::uint64_t> sequences;
    mutable std::mutex drain_mutex;
    std::optional<std::chrono::system_clock::time_point> drain_deadline;
    bool work_sealed = false;
    std::atomic_bool stopped{false};

    std::uint64_t next_sequence (const std::string &mesh_name) const
    {
        std::lock_guard lock (sequence_mutex);
        return ++sequences[mesh_name];
    }

    std::shared_ptr<hub_t> require_hub (const std::string &mesh_name) const
    {
        if (mesh_name.empty ())
            throw invalid_runtime_call ("mesh_name is required");
        const auto found = hubs.find (mesh_name);
        if (found == hubs.end ())
            throw invalid_runtime_call ("RouteMesh is not configured: " + mesh_name);
        return found->second;
    }

    void require_mesh_scoped_drain_supported () const
    {
        if (hubs.size () != 1) {
            throw invalid_runtime_call (
              "mesh-scoped drain is unavailable when this host contains multiple "
              "RouteMesh instances");
        }
    }

    mesh_runtime_event_t map_event (const hub_t &hub,
                                    const zlink::mesh_monitor_event_t &native)
    {
        const auto status = hub.node->status ();
        const bool counts = carries_target_counts (native.kind);
        return mesh_runtime_event_t{
          .identifier = event_identifier (native.kind),
          .sequence = next_sequence (hub.node->mesh_name ()),
          .timestamp = event_time (native.timestamp_ms),
          .mesh_name = hub.node->mesh_name (),
          .source_rid = status.routing_id (),
          .peer_rid =
            native.peer_rid.size () == 0
              ? std::nullopt
              : std::optional<zlink::routing_id_t>{native.peer_rid},
          .lifecycle_generation = positive (native.peer_lifecycle_generation),
          .descriptor_revision = positive (native.peer_descriptor_revision),
          .channel_name = text (native.channel_name),
          .claim_domain =
            native.kind == zlink::mesh_monitor_event_kind_t::claim_revoked
              ? std::optional<std::string>{"application"}
              : std::nullopt,
          .message_kind = std::nullopt,
          .remote_snapshot_count =
            counts ? std::optional<std::uint64_t>{native.snapshot_remote_target_count}
                   : std::nullopt,
          .remote_admitted_count =
            counts ? std::optional<std::uint64_t>{native.admitted_remote_target_count}
                   : std::nullopt,
          .remote_dropped_count =
            counts ? std::optional<std::uint64_t>{native.dropped_remote_target_count}
                   : std::nullopt,
          .local_snapshot_count =
            counts ? std::optional<std::uint64_t>{native.snapshot_local_spot_count}
                   : std::nullopt,
          .local_admitted_count =
            counts ? std::optional<std::uint64_t>{native.admitted_local_spot_count}
                   : std::nullopt,
          .local_dropped_count =
            counts ? std::optional<std::uint64_t>{native.dropped_local_spot_count}
                   : std::nullopt,
          .reason = event_reason (native.kind),
          .state =
            native.kind == zlink::mesh_monitor_event_kind_t::state_changed
              ? std::optional<mesh_node_state_t>{map_state (native.mesh_state)}
              : std::nullopt};
    }

    void broadcast (hub_t &hub, const mesh_runtime_event_t &event)
    {
        std::vector<std::shared_ptr<observer_t>> current;
        {
            std::lock_guard lock (hub.mutex);
            auto write = hub.observers.begin ();
            for (auto read = hub.observers.begin (); read != hub.observers.end (); ++read) {
                if (auto observer = read->lock ()) {
                    current.push_back (observer);
                    *write++ = *read;
                }
            }
            hub.observers.erase (write, hub.observers.end ());
        }
        for (const auto &observer : current)
            observer->enqueue (event);
    }

    void update_multicast (hub_t &hub,
                           const zlink::mesh_monitor_event_t &native,
                           const zlink::mesh_monitor_status_t &monitor_status)
    {
        std::lock_guard lock (hub.mutex);
        hub.multicast.submitted = monitor_status.multicast_messages;
        hub.multicast.backpressured = monitor_status.backpressured_submits;
        hub.multicast.dropped = monitor_status.multicast_dropped_targets;
        if (carries_target_counts (native.kind)) {
            hub.multicast.remote_snapshot_count = native.snapshot_remote_target_count;
            hub.multicast.remote_admitted_count = native.admitted_remote_target_count;
            hub.multicast.remote_dropped_count = native.dropped_remote_target_count;
            hub.multicast.local_snapshot_count = native.snapshot_local_spot_count;
            hub.multicast.local_admitted_count = native.admitted_local_spot_count;
            hub.multicast.local_dropped_count = native.dropped_local_spot_count;
        }
    }

    void publish_application_claim_change (hub_t &hub)
    {
        const auto active_callbacks =
          hub.node->active_application_callbacks ();
        const auto pending_callbacks =
          hub.node->pending_application_callbacks ();
        const bool active = active_callbacks != 0;
        bool active_changed;
        {
            std::lock_guard lock (hub.mutex);
            active_changed = hub.application_claim_active != active;
            hub.application_claim_active = active;
            hub.pending_application_callbacks = pending_callbacks;
        }
        if (!active_changed)
            return;
        const auto status = hub.node->status ();
        broadcast (
          hub,
          mesh_runtime_event_t{
            .identifier = "zlink.runtime.mesh_node.claim_changed",
            .sequence = next_sequence (hub.node->mesh_name ()),
            .timestamp = std::chrono::system_clock::now (),
            .mesh_name = hub.node->mesh_name (),
            .source_rid = status.routing_id (),
            .claim_domain = std::optional<std::string>{"application"},
            .reason =
              std::optional<std::string>{
                active ? "active"
                       : (pending_callbacks != 0 ? "pending" : "released")}});
    }

    void poll_location (hub_t &hub)
    {
        if (location_runtime == nullptr)
            return;
        const auto now = std::chrono::steady_clock::now ();
        {
            std::lock_guard lock (hub.mutex);
            if (now < hub.next_location_poll)
                return;
            hub.next_location_poll = now + std::chrono::milliseconds (100);
        }

        std::string state = "degraded";
        std::optional<std::chrono::system_clock::time_point> last_success;
        bool failed = true;
        try {
            auto query = location_runtime->get_status ();
            const auto &result = query.result ();
            if (result) {
                state = result.value ().store_healthy ? "ready" : "degraded";
                last_success = result.value ().last_refresh_at;
                failed = result.value ().last_error.has_value ();
            }
        }
        catch (...) {
        }

        bool changed;
        {
            std::lock_guard lock (hub.mutex);
            changed = hub.location_state != state;
            hub.location_state = state;
            if (last_success)
                hub.location_last_success = last_success;
            if (failed && !hub.location_last_failure)
                hub.location_last_failure = std::chrono::system_clock::now ();
        }
        if (!changed)
            return;
        const auto status = hub.node->status ();
        broadcast (
          hub,
          mesh_runtime_event_t{
            .identifier = "zlink.runtime.location.store_changed",
            .sequence = next_sequence (hub.node->mesh_name ()),
            .timestamp = std::chrono::system_clock::now (),
            .mesh_name = hub.node->mesh_name (),
            .source_rid = status.routing_id (),
            .reason = std::optional<std::string>{state}});
    }
};

namespace
{

class observation_t final : public mesh_runtime_observation_t
{
  public:
    explicit observation_t (
      std::shared_ptr<route_mesh_runtime_service_t::state_t::observer_t> observer) :
        _observer (std::move (observer))
    {
    }

    ~observation_t () override { close (); }

    void close () override
    {
        if (_observer) {
            _observer->close ();
            _observer.reset ();
        }
    }

  private:
    std::shared_ptr<route_mesh_runtime_service_t::state_t::observer_t> _observer;
};

} // namespace

route_mesh_runtime_service_t::route_mesh_runtime_service_t (
  std::vector<std::shared_ptr<detail::mesh_node_runtime_t>> nodes,
  location_runtime_query_t *location_runtime,
  drain_callback_t drain,
  await_drained_callback_t await_drained) :
    _state (std::make_shared<state_t> ())
{
    _state->location_runtime = location_runtime;
    _state->drain_callback = std::move (drain);
    _state->await_drained_callback = std::move (await_drained);
    for (auto &node : nodes) {
        _state->hubs.emplace (node->mesh_name (), std::make_shared<state_t::hub_t> (node));
    }
}

route_mesh_runtime_service_t::~route_mesh_runtime_service_t ()
{
    stop ();
}

void route_mesh_runtime_service_t::start ()
{
    _state->stopped.store (false, std::memory_order_release);
    for (const auto &[_, hub] : _state->hubs) {
        if (hub->pump.joinable ())
            continue;
        hub->stopped.store (false, std::memory_order_release);
        const auto state = _state;
        hub->pump = std::thread ([state, hub] {
            try {
                auto monitor = hub->node->native_node ().open_monitor ();
                while (!hub->stopped.load (std::memory_order_acquire)) {
                    state->publish_application_claim_change (*hub);
                    state->poll_location (*hub);
                    auto event = monitor.recv (zlink::recv_flags_t::dontwait);
                    if (!event) {
                        std::this_thread::sleep_for (std::chrono::milliseconds (1));
                        continue;
                    }
                    state->update_multicast (*hub, *event, monitor.status ());
                    auto mapped = state->map_event (*hub, *event);
                    if (!mapped.identifier.empty ()) {
                        state->broadcast (*hub, mapped);
                        if (event->kind == zlink::mesh_monitor_event_kind_t::state_changed) {
                            mapped.identifier = "zlink.runtime.mesh_node.drain_changed";
                            mapped.sequence = state->next_sequence (mapped.mesh_name);
                            state->broadcast (*hub, mapped);
                        }
                    }
                }
                monitor.close ();
            }
            catch (...) {
                hub->stopped.store (true, std::memory_order_release);
            }
        });
    }
}

void route_mesh_runtime_service_t::stop () noexcept
{
    if (_state->stopped.exchange (true, std::memory_order_acq_rel))
        return;
    for (const auto &[_, hub] : _state->hubs)
        hub->stopped.store (true, std::memory_order_release);
    for (const auto &[_, hub] : _state->hubs) {
        if (hub->pump.joinable ())
            hub->pump.join ();
        std::vector<std::shared_ptr<state_t::observer_t>> observers;
        {
            std::lock_guard lock (hub->mutex);
            for (const auto &weak : hub->observers) {
                if (auto observer = weak.lock ())
                    observers.push_back (std::move (observer));
            }
            hub->observers.clear ();
        }
        for (const auto &observer : observers)
            observer->close ();
    }
}

mesh_node_snapshot_t
route_mesh_runtime_service_t::snapshot (std::string mesh_name) const
{
    const auto hub = _state->require_hub (mesh_name);
    const auto status = hub->node->status ();
    const auto peers = hub->node->native_node ().peers ();
    const auto state = map_state (status.state ());

    std::vector<mesh_peer_snapshot_t> peer_snapshots;
    peer_snapshots.reserve (peers.size ());
    std::map<std::string, std::uint64_t> ready_remote_members;
    bool manual = false;
    bool discovery = false;
    for (const auto &peer : peers) {
        manual = manual || peer.source () == zlink::mesh_peer_source_t::manual
                 || peer.source () == zlink::mesh_peer_source_t::mixed;
        discovery = discovery || peer.source () == zlink::mesh_peer_source_t::discovery
                    || peer.source () == zlink::mesh_peer_source_t::mixed;
        std::vector<std::string> channel_names;
        for (const auto &channel : hub->node->native_node ().peer_channels (
               peer.routing_id (), peer.lifecycle_generation ())) {
            channel_names.push_back (channel.name);
            if (peer.state () == zlink::mesh_peer_state_t::admitted
                && channel.weight > 0)
                ++ready_remote_members[channel.name];
        }
        std::sort (channel_names.begin (), channel_names.end ());
        peer_snapshots.push_back (mesh_peer_snapshot_t{
          .rid = peer.routing_id (),
          .lifecycle_generation = peer.lifecycle_generation (),
          .descriptor_revision = peer.descriptor_revision (),
          .endpoint = peer.endpoint (),
          .admission_state = peer_admission_state (peer.state ()),
          .ready = peer.state () == zlink::mesh_peer_state_t::admitted,
          .drain_state = peer_drain_state (peer.state ()),
          .channel_names = std::move (channel_names),
          .last_failure =
            peer.last_error () == 0
              ? std::nullopt
              : std::optional<std::string>{"errno " + std::to_string (peer.last_error ())}});
    }

    const auto local_channels = hub->node->channel_weights ();
    std::vector<mesh_channel_snapshot_t> channel_snapshots;
    channel_snapshots.reserve (local_channels.size ());
    for (const auto &[channel_name, weight] : local_channels) {
        const std::uint64_t ready_members =
          (weight > 0 ? 1 : 0) + ready_remote_members[channel_name];
        channel_snapshots.push_back (mesh_channel_snapshot_t{
          .channel_name = channel_name,
          .local_weight = weight,
          .ready_member_count = ready_members,
          .selectable = ready_members > 0});
    }

    std::vector<std::string> descriptor_sources;
    if (manual && discovery)
        descriptor_sources.push_back ("manual_and_redis");
    else if (manual)
        descriptor_sources.push_back ("manual");
    else if (discovery)
        descriptor_sources.push_back ("redis");

    logical_multicast_snapshot_t multicast;
    bool application_claim_active;
    std::uint64_t pending_application_callbacks;
    {
        std::lock_guard lock (hub->mutex);
        multicast = hub->multicast;
        application_claim_active = hub->application_claim_active;
        pending_application_callbacks = hub->pending_application_callbacks;
    }
    multicast.submitted = std::max (multicast.submitted, status.multicast_submitted ());
    multicast.dropped = std::max (multicast.dropped, status.multicast_dropped_targets ());

    location_runtime_snapshot_t location;
    {
        std::lock_guard lock (hub->mutex);
        location = location_runtime_snapshot_t{
          .state = hub->location_state,
          .last_success_at = hub->location_last_success,
          .last_failure_at = hub->location_last_failure};
    }

    std::optional<std::chrono::system_clock::time_point> deadline;
    bool work_sealed;
    {
        std::lock_guard lock (_state->drain_mutex);
        deadline = _state->drain_deadline;
        work_sealed = _state->work_sealed;
    }
    return mesh_node_snapshot_t{
      .mesh_name = std::move (mesh_name),
      .rid = status.routing_id (),
      .lifecycle_generation = status.lifecycle_generation (),
      .descriptor_revision = status.descriptor_revision (),
      .endpoint = status.local_endpoint (),
      .state = state,
      .sequence = _state->next_sequence (status.mesh_name ()),
      .observed_at = std::chrono::system_clock::now (),
      .descriptor_sources = std::move (descriptor_sources),
      .peers = std::move (peer_snapshots),
      .channels = std::move (channel_snapshots),
      .multicast = multicast,
      .claims =
        mesh_claim_snapshot_t{
          .application_active = application_claim_active,
          .pending_application_work =
            status.pending_application_messages ()
            + pending_application_callbacks,
          .infrastructure_active =
            status.pending_infrastructure_messages () != 0,
          .pending_infrastructure_work = status.pending_infrastructure_messages ()},
      .location = std::move (location),
      .drain =
        mesh_drain_snapshot_t{
          .state = state,
          .deadline = deadline,
          .work_sealed = work_sealed || state == mesh_node_state_t::stopped,
          .pending_request_count = status.pending_application_messages (),
          .pending_transfer_count = 0,
          .pending_stream_barrier_count = 0}};
}

std::unique_ptr<mesh_runtime_observation_t>
route_mesh_runtime_service_t::observe (
  std::string mesh_name,
  std::size_t capacity,
  std::function<void (const mesh_runtime_event_t &)> observer)
{
    if (capacity == 0)
        throw invalid_runtime_call ("capacity must be positive");
    if (!observer)
        throw invalid_runtime_call ("observer is required");
    const auto hub = _state->require_hub (mesh_name);
    auto registered =
      std::make_shared<state_t::observer_t> (capacity, std::move (observer));
    registered->start ();
    {
        std::lock_guard lock (hub->mutex);
        hub->observers.push_back (registered);
    }
    const auto status = hub->node->status ();
    registered->enqueue (mesh_runtime_event_t{
      .identifier = "zlink.runtime.mesh_node.state_changed",
      .sequence = _state->next_sequence (mesh_name),
      .timestamp = std::chrono::system_clock::now (),
      .mesh_name = std::move (mesh_name),
      .source_rid = status.routing_id (),
      .state = map_state (status.state ())});
    return std::make_unique<observation_t> (std::move (registered));
}

bool route_mesh_runtime_service_t::is_ready (std::string mesh_name) const
{
    return native_ready (_state->require_hub (mesh_name)->node->status ().state ());
}

task_t<drain_result_t>
route_mesh_runtime_service_t::drain (std::string mesh_name,
                                     std::chrono::milliseconds deadline)
{
    _state->require_hub (mesh_name);
    if (deadline < std::chrono::milliseconds::zero ())
        throw invalid_runtime_call ("deadline must not be negative");
    // The current host drain callback tears down every hosted MeshNode. Do not
    // report a mesh-scoped success when another RouteMesh would also stop.
    // This guard runs before the shared deadline or callback is mutated.
    _state->require_mesh_scoped_drain_supported ();
    {
        std::lock_guard lock (_state->drain_mutex);
        if (!_state->drain_deadline)
            _state->drain_deadline = std::chrono::system_clock::now () + deadline;
    }
    auto task = _state->drain_callback (deadline);
    const auto state = _state;
    observe_task_completion (
      task, [state] (const result_t<drain_result_t> &result) {
          if (result) {
              std::lock_guard lock (state->drain_mutex);
              state->work_sealed = true;
          }
      });
    return task;
}

task_t<drain_result_t>
route_mesh_runtime_service_t::await_drained (std::string mesh_name)
{
    _state->require_hub (mesh_name);
    // await_drained has the same mesh-scoped contract as drain. The app-global
    // waiter cannot represent one mesh in a multi-mesh host.
    _state->require_mesh_scoped_drain_supported ();
    return _state->await_drained_callback ();
}

route_mesh_runtime_host_service_t::route_mesh_runtime_host_service_t (
  std::shared_ptr<route_mesh_runtime_service_t> runtime) :
    _runtime (std::move (runtime))
{
}

void route_mesh_runtime_host_service_t::start (service_provider_t &)
{
    _runtime->start ();
}

void route_mesh_runtime_host_service_t::request_stop () noexcept
{
    _runtime->stop ();
}

void route_mesh_runtime_host_service_t::stop () noexcept
{
    _runtime->stop ();
}

} // namespace zlink::framework::runtime
