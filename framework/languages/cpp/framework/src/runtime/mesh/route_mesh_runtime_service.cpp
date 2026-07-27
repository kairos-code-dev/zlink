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

mesh_node_state_t map_state (host::node_status_t::state_t state)
{
    switch (state) {
        case host::node_status_t::state_t::preparing:
            return mesh_node_state_t::starting;
        case host::node_status_t::state_t::serving:
            return mesh_node_state_t::serving;
        case host::node_status_t::state_t::draining:
            return mesh_node_state_t::draining;
        case host::node_status_t::state_t::stopped:
            return mesh_node_state_t::stopped;
        case host::node_status_t::state_t::error:
            return mesh_node_state_t::faulted;
    }
    return mesh_node_state_t::faulted;
}

bool native_ready (host::node_status_t::state_t state)
{
    return state == host::node_status_t::state_t::serving;
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
            if (pending.size () == capacity)
                pending.pop_front ();
            pending.push_back (std::move (event));
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
    location_runtime_query_t *location_runtime = nullptr;
    location_store_t *location_store = nullptr;
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

    void publish_application_claim_change (hub_t &hub)
    {
        const auto active_callbacks = hub.node->active_application_callbacks ();
        const auto pending_callbacks = hub.node->pending_application_callbacks ();
        const bool active = active_callbacks != 0;
        bool changed;
        {
            std::lock_guard lock (hub.mutex);
            changed = hub.application_claim_active != active;
            hub.application_claim_active = active;
            hub.pending_application_callbacks = pending_callbacks;
        }
        if (!changed)
            return;
        broadcast (
          hub,
          mesh_runtime_event_t{
            .identifier = "zlink.runtime.mesh_node.claim_changed",
            .sequence = next_sequence (hub.node->mesh_name ()),
            .timestamp = std::chrono::system_clock::now (),
            .mesh_name = hub.node->mesh_name (),
            .source_rid = hub.node->status ().routing_id (),
            .peer_rid = std::nullopt,
            .lifecycle_generation = std::nullopt,
            .descriptor_revision = std::nullopt,
            .channel_name = std::nullopt,
            .claim_domain = std::optional<std::string>{"application"},
            .message_kind = std::nullopt,
            .reason = std::optional<std::string>{
              active ? "active" : (pending_callbacks != 0 ? "pending" : "released")},
            .state = std::nullopt});
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
            if (failed)
                hub.location_last_failure = std::chrono::system_clock::now ();
        }
        if (!changed)
            return;
        broadcast (
          hub,
          mesh_runtime_event_t{
            .identifier = "zlink.runtime.location.store_changed",
            .sequence = next_sequence (hub.node->mesh_name ()),
            .timestamp = std::chrono::system_clock::now (),
            .mesh_name = hub.node->mesh_name (),
            .source_rid = hub.node->status ().routing_id (),
            .peer_rid = std::nullopt,
            .lifecycle_generation = std::nullopt,
            .descriptor_revision = std::nullopt,
            .channel_name = std::nullopt,
            .claim_domain = std::nullopt,
            .message_kind = std::nullopt,
            .reason = std::optional<std::string>{state},
            .state = std::nullopt});
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
  location_store_t *location_store) :
    _state (std::make_shared<state_t> ())
{
    _state->location_runtime = location_runtime;
    _state->location_store = location_store;
    for (auto &node : nodes)
        _state->hubs.emplace (node->mesh_name (),
                              std::make_shared<state_t::hub_t> (node));
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
            while (!hub->stopped.load (std::memory_order_acquire)) {
                state->publish_application_claim_change (*hub);
                state->poll_location (*hub);
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
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
    const auto descriptor =
      hub->node->native_node ().transport ().topology ().local_descriptor ();
    const auto peers = hub->node->native_node ().transport ().topology ().peers ();
    const auto mapped_state = map_state (status.state);

    std::vector<mesh_peer_snapshot_t> peer_snapshots;
    std::map<std::string, std::uint64_t> ready_remote_members;
    peer_snapshots.reserve (peers.size ());
    for (const auto &peer : peers) {
        std::vector<std::string> channel_names;
        for (const auto &channel : peer.descriptor.channels) {
            channel_names.push_back (channel.name);
            if (channel.weight > 0)
                ++ready_remote_members[channel.name];
        }
        std::sort (channel_names.begin (), channel_names.end ());
        peer_snapshots.push_back (mesh_peer_snapshot_t{
          .rid = zlink::routing_id_t::from (peer.descriptor.node_routing_id),
          .lifecycle_generation = peer.descriptor.lifecycle_generation,
          .descriptor_revision = peer.descriptor.descriptor_revision,
          .endpoint = peer.descriptor.advertised_endpoint,
          .admission_state = "ready",
          .ready = true,
          .drain_state =
            peer.descriptor.state == mesh::service_node_state_t::draining
              ? "draining"
              : "serving",
          .channel_names = std::move (channel_names),
          .last_failure = std::nullopt});
    }

    std::vector<mesh_channel_snapshot_t> channels;
    for (const auto &[name, weight] : hub->node->channel_weights ()) {
        const auto ready = (weight > 0 ? std::uint64_t{1} : std::uint64_t{0})
                           + ready_remote_members[name];
        channels.push_back (mesh_channel_snapshot_t{
          .channel_name = name,
          .local_weight = weight,
          .ready_member_count = ready,
          .selectable = ready != 0});
    }

    bool application_active;
    std::uint64_t pending_callbacks;
    location_runtime_snapshot_t location;
    {
        std::lock_guard lock (hub->mutex);
        application_active = hub->application_claim_active;
        pending_callbacks = hub->pending_application_callbacks;
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
    mesh_node_descriptor_t placement;
    placement.mesh_name = mesh_name;
    placement.rid = status.routing_id ();
    placement.lifecycle_generation = status.lifecycle_generation ();
    placement.descriptor_revision = descriptor.descriptor_revision;
    placement.endpoint = status.local_endpoint ();
    placement.object_role = object_role_t::server;
    placement.placement_weight = hub->node->placement_weight ();
    placement.capacity.actors.limit = hub->node->actor_limit ();
    placement.capacity.spots.limit = hub->node->spot_limit ();
    placement.activation_concurrency.limit =
      hub->node->activation_concurrency_limit ();
    if (_state->location_store != nullptr) {
        try {
            location_page_request_t page;
            for (;;) {
                auto listed =
                  _state->location_store->list_mesh_nodes (mesh_name, page);
                const auto &result = listed.result ();
                if (!result)
                    break;
                const auto &value = result.value ();
                const auto found = std::find_if (
                  value.items.begin (), value.items.end (),
                  [&status] (const mesh_node_descriptor_t &candidate) {
                      return candidate.rid == status.routing_id ()
                             && candidate.lifecycle_generation
                                  == status.lifecycle_generation ();
                  });
                if (found != value.items.end ()) {
                    placement = *found;
                    break;
                }
                if (!value.continuation_token)
                    break;
                page.continuation_token = value.continuation_token;
            }
        }
        catch (...) {
            // Monitoring keeps the last locally known limits when the
            // authoritative descriptor cannot be read.
        }
    }

    return mesh_node_snapshot_t{
      .mesh_name = std::move (mesh_name),
      .rid = status.routing_id (),
      .entry_spot_id = placement.entry_spot_id,
      .lifecycle_generation = status.lifecycle_generation (),
      .descriptor_revision = descriptor.descriptor_revision,
      .endpoint = status.local_endpoint (),
      .state = mapped_state,
      .object_role = placement.object_role,
      .application_version = placement.application_version,
      .placement_weight = placement.placement_weight,
      .object_capacity = placement.capacity,
      .activation_concurrency =
        activation_concurrency_snapshot_t{
          .active = placement.activation_concurrency.active,
          .limit = static_cast<std::uint32_t> (
            placement.activation_concurrency.limit)},
      .placement_reservation_failure_count = 0,
      .last_placement_reservation_failure = std::nullopt,
      .object_capabilities = placement.object_capabilities,
      .sequence = _state->next_sequence (descriptor.mesh_name),
      .observed_at = std::chrono::system_clock::now (),
      .descriptor_sources = {},
      .peers = std::move (peer_snapshots),
      .channels = std::move (channels),
      .instance_spots = {},
      .claims =
        mesh_claim_snapshot_t{
          .application_active = application_active,
          .pending_application_work = pending_callbacks,
          .infrastructure_active = false,
          .pending_infrastructure_work = 0},
      .location = std::move (location),
      .drain =
        mesh_drain_snapshot_t{
          .state = mapped_state,
          .deadline = deadline,
          .work_sealed = work_sealed || mapped_state == mesh_node_state_t::stopped,
          .pending_request_count = pending_callbacks,
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
      .peer_rid = std::nullopt,
      .lifecycle_generation = std::nullopt,
      .descriptor_revision = std::nullopt,
      .channel_name = std::nullopt,
      .claim_domain = std::nullopt,
      .message_kind = std::nullopt,
      .reason = std::nullopt,
      .state = map_state (status.state)});
    return std::make_unique<observation_t> (std::move (registered));
}

bool route_mesh_runtime_service_t::is_ready (std::string mesh_name) const
{
    return native_ready (_state->require_hub (mesh_name)->node->status ().state);
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
