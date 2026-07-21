/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/mesh/mesh_c_internal.hpp"
#include "api/mesh/mesh_stream_session_internal.hpp"
#include "services/mesh/mesh_wire.hpp"

#include "api/socket/socket_api_internal.hpp"
#include "sockets/common/socket_base.hpp"
#include "sockets/stream/stream.hpp"
#include "utils/err.hpp"
#include "utils/macros.hpp"

#include <string.h>

using namespace zlink::mesh;

//  STREAM session service: owns the relation between one raw STREAM socket
//  and one MeshNode, the per-session actor bindings and the movement
//  barrier. The barrier data plane engages together with the transfer fence.

namespace
{
#ifdef ZLINK_BUILD_TESTS
std::atomic<int> g_pause_before_local_actor_admit (0);
std::atomic<int> g_local_actor_admit_paused (0);
std::atomic<int> g_local_actor_admit_pause_claimed (0);
std::atomic<int> g_pause_before_bound_session_admit (0);
std::atomic<int> g_bound_session_admit_paused (0);
std::atomic<int> g_bound_session_admit_pause_claimed (0);
#endif

struct binding_t
{
    binding_t () {}
    binding_t (binding_t &&) noexcept = default;
    binding_t &operator= (binding_t &&) noexcept = default;
    binding_t (const binding_t &) = delete;
    binding_t &operator= (const binding_t &) = delete;

    zlink_actor_ref_t actor;
    uint64_t binding_generation;
    uint64_t membership_epoch;
    uint64_t transfer_serial;
    zlink_actor_transfer_id_t transfer_id;
    rid_bytes_t target_node_rid;
    uint64_t participant_id;
    uint64_t allowance_messages;
    uint64_t allowance_bytes;
    uint64_t pending_bytes;
    //  Target-to-session frames can reach the source after target activation
    //  but before source commit. The binding owns that reverse FIFO so the
    //  ingress thread never drops an already accepted wire message.
    uint64_t pending_egress_bytes;
    uint64_t acked_high_water;
    bool terminal_sealed;
    bool closing;
    std::deque<std::unique_ptr<queued_record_t>> pending;
    std::deque<std::unique_ptr<queued_record_t>> pending_egress;
};

struct binding_announcement_t
{
    zlink_actor_ref_t actor;
    uint64_t binding_generation;
};

struct submit_order_state_t
{
    submit_order_state_t () : epoch (1) {}

    std::mutex mutex;
    //  Protected by session_service_t::mutex. A physical disconnect or
    //  reconnect invalidates submissions that captured an earlier epoch.
    uint64_t epoch;
};

struct session_service_t
{
    session_service_t () :
        tag (0x4d535353),
        node (NULL),
        stream (NULL),
        state (ZLINK_STREAM_SESSION_CREATED),
        lifecycle_generation (1),
        last_error (0),
        destroying (false)
    {
    }
    bool check_tag () const { return tag == 0x4d535353; }

    uint32_t tag;
    mesh_node_t *node;
    void *stream;
    //  Serializes service start, shutdown and destroy without participating in
    //  STREAM session callbacks. Observer callbacks use only the state mutex.
    std::mutex lifecycle_mutex;
    std::mutex mutex;
    std::condition_variable cv;
    zlink_stream_session_state_t state;
    uint64_t lifecycle_generation;
    std::set<std::string> active_sessions;
    //  Prevents a route snapshot taken during asynchronous pipe teardown from
    //  restoring a session after its disconnect observer event.
    std::set<std::string> disconnected_sessions;
    //  session rid bytes -> bindings
    std::map<std::string, std::vector<binding_t>> bindings;
    //  Orders submissions per connected session without coupling unrelated
    //  sessions to one mailbox's backpressure wait. The session owns its gate
    //  so the hot submit path neither allocates nor scans unrelated sessions.
    std::map<std::string, std::shared_ptr<submit_order_state_t>> submit_order;
    int32_t last_error;
    bool destroying;
};

std::mutex g_session_registry_mutex;
std::set<void *> g_live_sessions;
std::map<void *, std::shared_ptr<session_service_t>> g_session_owners;
//  Streams already claimed by a service handle (one service per stream).
std::set<void *> g_claimed_streams;

std::shared_ptr<session_service_t> as_session_service (void *handle_)
{
    if (!handle_)
        return std::shared_ptr<session_service_t> ();
    std::lock_guard<std::mutex> lock (g_session_registry_mutex);
    std::map<void *, std::shared_ptr<session_service_t>>::iterator owner =
      g_session_owners.find (handle_);
    if (owner == g_session_owners.end () || !owner->second->check_tag ())
        return std::shared_ptr<session_service_t> ();
    return owner->second;
}

zlink::stream_t *service_stream (session_service_t *service_)
{
    const socket_handle_t handle = as_socket_handle (service_->stream);
    return is_stream_type (handle) ? static_cast<zlink::stream_t *> (handle.socket) : NULL;
}

bool live_session (session_service_t *service_, const std::string &session_key_)
{
    if (service_->active_sessions.count (session_key_) != 0)
        return true;
    if (service_->disconnected_sessions.count (session_key_) != 0)
        return false;

    //  The observer is the steady-state source of session lifecycle updates,
    //  but a connection can cross the service-start boundary before the
    //  observer records it. Reconcile only against routes that the STREAM
    //  still owns, so a disconnected routing id is never resurrected from a
    //  stale service-side snapshot.
    zlink::stream_t *stream = service_stream (service_);
    if (!stream)
        return false;
    std::vector<zlink_routing_id_t> active_rids;
    stream->peer_routing_ids (&active_rids);
    for (size_t i = 0; i < active_rids.size (); ++i) {
        const std::string active_key (
          reinterpret_cast<const char *> (active_rids[i].data), active_rids[i].size);
        if (active_key == session_key_) {
            service_->active_sessions.insert (session_key_);
            return true;
        }
    }
    return false;
}

std::shared_ptr<submit_order_state_t>
submit_order_locked (session_service_t *service_, const std::string &session_key_)
{
    std::map<std::string, std::shared_ptr<submit_order_state_t>>::iterator existing =
      service_->submit_order.find (session_key_);
    if (existing != service_->submit_order.end ())
        return existing->second;

    std::shared_ptr<submit_order_state_t> state;
    try {
        state = std::make_shared<submit_order_state_t> ();
        service_->submit_order[session_key_] = state;
    }
    catch (const std::bad_alloc &) {
        errno = ENOMEM;
        return std::shared_ptr<submit_order_state_t> ();
    }
    return state;
}

void remove_disconnected_bindings_locked (session_service_t *service_)
{
    for (std::map<std::string, std::vector<binding_t>>::iterator session =
           service_->bindings.begin ();
         session != service_->bindings.end ();) {
        if (!live_session (service_, session->first)) {
            for (std::vector<binding_t>::iterator binding = session->second.begin ();
                 binding != session->second.end ();) {
                if (binding->transfer_serial == 0)
                    binding = session->second.erase (binding);
                else
                    ++binding;
            }
        }
        if (session->second.empty ())
            session = service_->bindings.erase (session);
        else
            ++session;
    }
}

void on_stream_session_event (void *userdata_,
                              const zlink_routing_id_t *peer_rid_,
                              bool connected_)
{
    session_service_t *service = static_cast<session_service_t *> (userdata_);
    if (!service || !peer_rid_ || peer_rid_->size == 0)
        return;
    const std::string key (reinterpret_cast<const char *> (peer_rid_->data), peer_rid_->size);
    std::vector<binding_announcement_t> removed_bindings;
    std::unique_lock<std::mutex> lock (service->mutex);
    std::shared_ptr<submit_order_state_t> order;
    std::unique_lock<std::mutex> order_lock;
    std::map<std::string, std::shared_ptr<submit_order_state_t>>::iterator found =
      service->submit_order.find (key);
    if (found != service->submit_order.end ())
        order = found->second;
    if (order) {
        order->epoch += 1;
        //  Join an in-flight submit before changing this session's physical
        //  lifetime. New submits use the same gate until it is invalidated.
        lock.unlock ();
        order_lock = std::unique_lock<std::mutex> (order->mutex);
        lock.lock ();
        found = service->submit_order.find (key);
        if (found != service->submit_order.end ()
            && found->second == order)
            service->submit_order.erase (found);
    }
    if (connected_ && service->state == ZLINK_STREAM_SESSION_STARTED) {
        service->disconnected_sessions.erase (key);
        service->active_sessions.insert (key);
    } else if (!connected_) {
        service->active_sessions.erase (key);
        service->disconnected_sessions.insert (key);
        std::map<std::string, std::vector<binding_t>>::iterator bindings =
          service->bindings.find (key);
        if (bindings != service->bindings.end ()) {
            for (std::vector<binding_t>::iterator binding = bindings->second.begin ();
                 binding != bindings->second.end ();) {
                if (binding->transfer_serial == 0) {
                    try {
                        binding_announcement_t removed;
                        removed.actor = binding->actor;
                        removed.binding_generation =
                          binding->binding_generation;
                        removed_bindings.push_back (removed);
                    }
                    catch (const std::bad_alloc &) {
                        //  The physical binding is still removed. A peer that
                        //  retains the old route cannot pass owner-side
                        //  generation validation.
                    }
                    binding = bindings->second.erase (binding);
                } else
                    ++binding;
            }
            if (bindings->second.empty ())
                service->bindings.erase (bindings);
        }
    }
    service->cv.notify_all ();
    lock.unlock ();
    for (size_t i = 0; i < removed_bindings.size (); ++i)
        wire_broadcast_bound_session_bind (
          service->node, removed_bindings[i].actor, 0,
          removed_bindings[i].binding_generation);
}

bool transfer_work_pending_locked (const session_service_t *service_)
{
    for (std::map<std::string, std::vector<binding_t>>::const_iterator session =
           service_->bindings.begin ();
         session != service_->bindings.end (); ++session) {
        for (size_t i = 0; i < session->second.size (); ++i) {
            if (session->second[i].transfer_serial != 0
                || !session->second[i].pending.empty ()
                || !session->second[i].pending_egress.empty ())
                return true;
        }
    }
    return false;
}

int check_actor_ref (const zlink_actor_ref_t *actor_)
{
    if (!actor_) {
        errno = EINVAL;
        return -1;
    }
    const size_t len = strnlen (actor_->actor_id, sizeof (actor_->actor_id));
    if (len == 0 || len > ZLINK_ACTOR_ID_MAX || actor_->generation == 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

zlink_submit_result_t send_stream_complete (void *stream_,
                                            const zlink_routing_id_t *session_rid_,
                                            const zlink_msg_t *parts_,
                                            size_t part_count_,
                                            zlink_send_flags_t flags_)
{
    size_t total_size = 0;
    for (size_t i = 0; i < part_count_; ++i) {
        const size_t part_size = zlink_msg_size (&parts_[i]);
        if (part_size > SIZE_MAX - total_size) {
            errno = EMSGSIZE;
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        }
        total_size += part_size;
    }

    zlink_msg_t complete;
    if (zlink_msg_init_size (&complete, total_size) != 0) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    unsigned char *destination = static_cast<unsigned char *> (zlink_msg_data (&complete));
    size_t offset = 0;
    for (size_t i = 0; i < part_count_; ++i) {
        const size_t part_size = zlink_msg_size (&parts_[i]);
        if (part_size != 0) {
            memcpy (destination + offset,
                    zlink_msg_data (const_cast<zlink_msg_t *> (&parts_[i])), part_size);
            offset += part_size;
        }
    }

    const zlink_submit_result_t result =
      zlink_send_part_rid (stream_, session_rid_, &complete, flags_, ZLINK_PART_FINAL);
    if (result != ZLINK_SUBMIT_OK)
        zlink_msg_close (&complete);
    return result;
}

int copy_session_record_parts (const zlink_msg_t *parts_,
                               size_t part_count_,
                               queued_record_t *record_)
{
    try {
        record_->parts.resize (part_count_);
    }
    catch (const std::bad_alloc &) {
        errno = ENOMEM;
        return -1;
    }
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_init (&record_->parts[i]);
    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_copy (&record_->parts[i], const_cast<zlink_msg_t *> (&parts_[i])) != 0) {
            errno = EFAULT;
            return -1;
        }
        record_->byte_size += zlink_msg_size (&record_->parts[i]);
    }
    return 0;
}

uint64_t pending_reply_serial (mesh_node_t *node_, const queued_record_t &record_)
{
    if (!record_.has_reply_token)
        return 0;
    mesh_node_t *token_node = NULL;
    uint64_t reply_serial = 0;
    zlink_mesh_reply_token_t token = record_.reply_token;
    if (unseal_reply_token (&token, &token_node, &reply_serial) != 0 || token_node != node_)
        return 0;
    return reply_serial;
}

void send_next_participant_locked (session_service_t *service_, binding_t *binding_)
{
    if (!binding_ || binding_->participant_id == 0
        || binding_->acked_high_water >= binding_->pending.size ())
        return;
    const uint64_t sequence = binding_->acked_high_water + 1;
    const queued_record_t &record = *binding_->pending[sequence - 1];
    if (wire_submit_transfer_data (service_->node, binding_->target_node_rid,
                                   binding_->transfer_id, binding_->participant_id, sequence,
                                   record, pending_reply_serial (service_->node, record))
        != ZLINK_SUBMIT_OK)
        service_->last_error = errno;
}

//  Delivers a STREAM lifecycle terminal completion to the node
//  infrastructure claim. The optional callback runs only after every
//  fallible completion-admission step has succeeded.
int complete_binding_operation (mesh_node_t *node_,
                                zlink_mesh_operation_kind_t kind_,
                                const zlink_mesh_operation_id_t &op_id_,
                                int32_t result_,
                                int32_t err_,
                                pending_operation_commit_fn commit_locked_ = NULL,
                                void *commit_userdata_ = NULL)
{
    pending_operation_t op;
    try {
        std::lock_guard<std::mutex> lock (node_->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator it =
          node_->operations.find (op_id_.low);
        if (it == node_->operations.end ())
            return 0;
        op = it->second;
    }
    catch (const std::bad_alloc &) {
        errno = ENOMEM;
        return -1;
    }
    LIBZLINK_UNUSED (kind_);
    return complete_pending_operation_with_commit (
      node_, op, result_, err_, NULL, NULL, commit_locked_, commit_userdata_);
}

struct stream_unbind_commit_t
{
    session_service_t *service;
    const std::string *session_key;
    size_t binding_index;
    bool remove_binding;
};

bool commit_stream_unbind_locked (mesh_node_t *, void *userdata_)
{
    stream_unbind_commit_t *commit =
      static_cast<stream_unbind_commit_t *> (userdata_);
    if (!commit->remove_binding)
        return true;
    std::map<std::string, std::vector<binding_t>>::iterator current =
      commit->service->bindings.find (*commit->session_key);
    if (current == commit->service->bindings.end ()
        || commit->binding_index >= current->second.size ())
        return false;
    current->second.erase (current->second.begin () + commit->binding_index);
    if (current->second.empty ())
        commit->service->bindings.erase (current);
    return true;
}

struct stream_close_commit_t
{
    session_service_t *service;
    zlink_routing_id_t session_rid;
    uint64_t binding_generation;
};

void clear_stream_binding_closing (const stream_close_commit_t &commit_)
{
    std::lock_guard<std::mutex> lock (commit_.service->mutex);
    const std::string session_key (
      reinterpret_cast<const char *> (commit_.session_rid.data),
      commit_.session_rid.size);
    std::map<std::string, std::vector<binding_t>>::iterator current =
      commit_.service->bindings.find (session_key);
    if (current == commit_.service->bindings.end ())
        return;
    for (size_t i = 0; i < current->second.size (); ++i) {
        if (current->second[i].binding_generation
            == commit_.binding_generation)
            current->second[i].closing = false;
    }
}

template <typename Action>
void for_each_actor_binding (mesh_node_t *node_,
                             const zlink_actor_ref_t &actor_,
                             const Action &action_)
{
    std::lock_guard<std::mutex> registry_lock (g_session_registry_mutex);
    for (std::set<void *>::iterator service_it = g_live_sessions.begin ();
         service_it != g_live_sessions.end (); ++service_it) {
        session_service_t *service = static_cast<session_service_t *> (*service_it);
        if (service->node != node_)
            continue;
        std::lock_guard<std::mutex> service_lock (service->mutex);
        remove_disconnected_bindings_locked (service);
        for (std::map<std::string, std::vector<binding_t>>::iterator session_it =
               service->bindings.begin ();
             session_it != service->bindings.end (); ++session_it) {
            for (size_t i = 0; i < session_it->second.size (); ++i) {
                binding_t &binding = session_it->second[i];
                if (strncmp (binding.actor.actor_id, actor_.actor_id,
                             sizeof (binding.actor.actor_id))
                      == 0
                    && binding.actor.generation == actor_.generation)
                    action_ (service, binding);
            }
        }
    }
}
}

void *zlink_stream_session_service_new (void *mesh_node_, void *stream_)
{
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = EFAULT;
        return NULL;
    }
    const socket_handle_t handle = as_socket_handle (stream_);
    if (!handle.socket) {
        errno = EFAULT;
        return NULL;
    }
    if (!is_stream_type (handle)) {
        errno = EINVAL;
        return NULL;
    }

    std::lock_guard<std::mutex> registry_lock (g_session_registry_mutex);
    if (g_claimed_streams.count (stream_)) {
        errno = EEXIST;
        return NULL;
    }
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
            errno = ESHUTDOWN;
            return NULL;
        }
        node->stream_session_count += 1;
    }
    std::shared_ptr<session_service_t> service (
      new (std::nothrow) session_service_t ());
    if (!service.get ()) {
        std::lock_guard<std::mutex> lock (node->mutex);
        node->stream_session_count -= 1;
        errno = ENOMEM;
        return NULL;
    }
    service->node = node;
    service->stream = stream_;
    g_live_sessions.insert (service.get ());
    g_session_owners[service.get ()] = service;
    g_claimed_streams.insert (stream_);
    return service.get ();
}

zlink_config_result_t zlink_stream_session_service_start (void *service_)
{
    std::shared_ptr<session_service_t> service_owner = as_session_service (service_);
    session_service_t *service = service_owner.get ();
    if (!service) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::lock_guard<std::mutex> lifecycle_lock (service->lifecycle_mutex);
    const socket_handle_t stream_handle = as_socket_handle (service->stream);
    std::set<std::string> bound_endpoints;
    {
        std::lock_guard<std::mutex> lock (service->mutex);
        if (service->state != ZLINK_STREAM_SESSION_CREATED) {
            errno = EBUSY;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        if (!stream_handle.socket) {
            errno = ESHUTDOWN;
            return ZLINK_CONFIG_INVALID_STATE;
        }
    }
    stream_handle.socket->socket_bound_endpoints (&bound_endpoints);
    if (bound_endpoints.empty ()) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    zlink::stream_t *stream = static_cast<zlink::stream_t *> (stream_handle.socket);
    {
        std::lock_guard<std::mutex> lock (service->mutex);
        service->state = ZLINK_STREAM_SESSION_STARTED;
    }
    stream->set_session_observer (&on_stream_session_event, service);
    std::vector<zlink_routing_id_t> active_rids;
    stream->peer_routing_ids (&active_rids);
    {
        std::lock_guard<std::mutex> lock (service->mutex);
        for (size_t i = 0; i < active_rids.size (); ++i) {
            const std::string key (
              reinterpret_cast<const char *> (active_rids[i].data),
              active_rids[i].size);
            if (service->disconnected_sessions.count (key) == 0)
                service->active_sessions.insert (key);
        }
    }
    return ZLINK_CONFIG_OK;
}

zlink_request_result_t zlink_stream_session_service_shutdown (void *service_, uint32_t timeout_ms_)
{
    std::shared_ptr<session_service_t> service_owner = as_session_service (service_);
    session_service_t *service = service_owner.get ();
    if (!service) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lifecycle_lock (service->lifecycle_mutex);
    std::unique_lock<std::mutex> lock (service->mutex);
    if (service->state == ZLINK_STREAM_SESSION_STOPPED)
        return ZLINK_REQUEST_OK;
    service->state = ZLINK_STREAM_SESSION_DRAINING;
    service->cv.notify_all ();
    const uint64_t deadline = timeout_ms_ == 0 ? now_ms () : now_ms () + timeout_ms_;
    while (transfer_work_pending_locked (service)) {
        if (timeout_ms_ == 0 || now_ms () >= deadline) {
            errno = ETIMEDOUT;
            return ZLINK_REQUEST_TIMED_OUT;
        }
        service->cv.wait_for (lock, std::chrono::milliseconds (
                                     std::min<uint64_t> (50, deadline - now_ms ())));
    }
    service->state = ZLINK_STREAM_SESSION_STOPPED;
    return ZLINK_REQUEST_OK;
}

zlink_close_result_t zlink_stream_session_service_destroy (void **service_p_)
{
    if (!service_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    std::shared_ptr<session_service_t> service_owner = as_session_service (*service_p_);
    session_service_t *service = service_owner.get ();
    if (!service) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    std::lock_guard<std::mutex> lifecycle_lock (service->lifecycle_mutex);
    {
        std::lock_guard<std::mutex> lock (service->mutex);
        if (service->destroying) {
            errno = EBUSY;
            return ZLINK_CLOSE_BUSY;
        }
        service->destroying = true;
        service->state = ZLINK_STREAM_SESSION_STOPPED;
        service->cv.notify_all ();
    }
    zlink::stream_t *stream = service_stream (service);
    if (stream)
        stream->clear_session_observer (service);
    mesh_node_pin_t node_pin (service->node);
    mesh_node_t *node = node_pin.get ();
    std::vector<uint64_t> terminate_operations;
    std::vector<uint64_t> remove_reply_routes;
    std::vector<binding_announcement_t> removed_bindings;
    {
        std::lock_guard<std::mutex> lock (service->mutex);
        for (std::map<std::string, std::vector<binding_t>>::iterator session =
               service->bindings.begin ();
             session != service->bindings.end (); ++session) {
            for (size_t binding_index = 0; binding_index < session->second.size ();
                 ++binding_index) {
                binding_t &binding = session->second[binding_index];
                try {
                    binding_announcement_t removed;
                    removed.actor = binding.actor;
                    removed.binding_generation =
                      binding.binding_generation;
                    removed_bindings.push_back (removed);
                }
                catch (const std::bad_alloc &) {
                    //  Owner-side validation still prevents stale delivery
                    //  if this best-effort tombstone snapshot is incomplete.
                }
                for (std::deque<std::unique_ptr<queued_record_t>>::iterator record =
                       binding.pending.begin ();
                     record != binding.pending.end (); ++record) {
                    if ((*record)->operation_id.low != 0)
                        terminate_operations.push_back ((*record)->operation_id.low);
                    if ((*record)->has_reply_token && node) {
                        mesh_node_t *token_node = NULL;
                        uint64_t reply_serial = 0;
                        zlink_mesh_reply_token_t token = (*record)->reply_token;
                        if (unseal_reply_token (&token, &token_node, &reply_serial) == 0
                            && token_node == node)
                            remove_reply_routes.push_back (reply_serial);
                    }
                }
            }
        }
        service->bindings.clear ();
        service->active_sessions.clear ();
        service->disconnected_sessions.clear ();
        service->submit_order.clear ();
        service->state = ZLINK_STREAM_SESSION_STOPPED;
    }
    if (node) {
        std::lock_guard<std::mutex> lock (node->mutex);
        if (node->stream_session_count > 0)
            node->stream_session_count -= 1;
    }
    {
        std::lock_guard<std::mutex> lock (g_session_registry_mutex);
        g_live_sessions.erase (service);
        g_session_owners.erase (service);
        g_claimed_streams.erase (service->stream);
    }
    service->tag = 0xdeadbeef;
    *service_p_ = NULL;
    if (node) {
        for (size_t i = 0; i < removed_bindings.size (); ++i)
            wire_broadcast_bound_session_bind (
              node, removed_bindings[i].actor, 0,
              removed_bindings[i].binding_generation);
        for (size_t i = 0; i < terminate_operations.size (); ++i) {
            pending_operation_t op;
            bool found = false;
            {
                std::lock_guard<std::mutex> lock (node->mutex);
                std::unordered_map<uint64_t, pending_operation_t>::iterator operation =
                  node->operations.find (terminate_operations[i]);
                if (operation != node->operations.end ()) {
                    op = operation->second;
                    found = true;
                }
            }
            if (found)
                (void) complete_pending_operation (
                  node, op, ZLINK_REQUEST_TERMINATED, ESHUTDOWN, NULL, NULL);
        }
        std::lock_guard<std::mutex> lock (node->mutex);
        for (size_t i = 0; i < remove_reply_routes.size (); ++i)
            node->reply_routes.erase (remove_reply_routes[i]);
    }
    return ZLINK_CLOSE_OK;
}

zlink_config_result_t zlink_stream_session_service_status (void *service_,
                                                           zlink_stream_session_status_t *status_out_)
{
    std::shared_ptr<session_service_t> service_owner = as_session_service (service_);
    session_service_t *service = service_owner.get ();
    if (!service) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (check_versioned (status_out_) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock (service->mutex);
    remove_disconnected_bindings_locked (service);
    zlink_stream_session_status_t out;
    init_versioned (&out);
    out.state = service->state;
    out.lifecycle_generation = service->lifecycle_generation;
    out.session_count = service->active_sessions.size ();
    uint64_t binding_count = 0;
    uint64_t pending_message_count = 0;
    uint64_t pending_byte_count = 0;
    for (std::map<std::string, std::vector<binding_t>>::const_iterator it =
           service->bindings.begin ();
         it != service->bindings.end (); ++it) {
        binding_count += it->second.size ();
        for (size_t i = 0; i < it->second.size (); ++i) {
            pending_message_count += it->second[i].pending.size ()
                                     + it->second[i].pending_egress.size ();
            pending_byte_count += it->second[i].pending_bytes
                                  + it->second[i].pending_egress_bytes;
        }
    }
    out.binding_count = binding_count;
    out.pending_message_count = pending_message_count;
    out.pending_byte_count = pending_byte_count;
    out.last_error = service->last_error;
    *status_out_ = out;
    return ZLINK_CONFIG_OK;
}

zlink_submit_result_t zlink_stream_session_bind_actor (void *service_,
                                                       const zlink_routing_id_t *session_rid_,
                                                       const zlink_actor_ref_t *actor_,
                                                       zlink_mesh_operation_id_t *operation_id_out_,
                                                       uint32_t timeout_ms_)
try {
    std::shared_ptr<session_service_t> service_owner = as_session_service (service_);
    session_service_t *service = service_owner.get ();
    if (!service) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!session_rid_ || session_rid_->size == 0 || check_actor_ref (actor_) != 0
        || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    mesh_node_pin_t node_pin (service->node);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_INVALID_STATE;
    }
    uint64_t membership_epoch = 0;
    const bool remote_actor =
      rid_bytes (actor_->node_rid) != node->routing_id;
    //  Validate the actor generation and retain the authority version that
    //  this binding represents. A remote ActorRef is validated by its owner;
    //  the session node retains the supplied generation and announces the
    //  reverse route over the Mesh wire.
    if (!remote_actor) {
        std::lock_guard<std::mutex> lock (node->mutex);
        const std::string id (actor_->actor_id);
        std::map<std::string, actor_state_t>::iterator it = node->actors.find (id);
        if (it == node->actors.end ()) {
            errno = ENOENT;
            return ZLINK_SUBMIT_NOT_FOUND;
        }
        if (it->second.generation != actor_->generation) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        membership_epoch = it->second.membership_epoch;
    }

    const std::string session_key (reinterpret_cast<const char *> (session_rid_->data),
                                   session_rid_->size);
    if (remote_actor) {
        const rid_bytes_t owner_rid = rid_bytes (actor_->node_rid);
        bool idempotent = false;
        bool fenced = false;
        bool conflict = false;
        uint64_t binding_generation = 0;
        {
            std::lock_guard<std::mutex> registry_lock (
              g_session_registry_mutex);
            {
                std::lock_guard<std::mutex> service_lock (service->mutex);
                remove_disconnected_bindings_locked (service);
                if (service->state != ZLINK_STREAM_SESSION_STARTED) {
                    errno = ESHUTDOWN;
                    return ZLINK_SUBMIT_INVALID_STATE;
                }
                if (!live_session (service, session_key)) {
                    errno = ENOTCONN;
                    return ZLINK_SUBMIT_NOT_CONNECTED;
                }
            }
            for (std::set<void *>::iterator live = g_live_sessions.begin ();
                 live != g_live_sessions.end (); ++live) {
                session_service_t *candidate =
                  static_cast<session_service_t *> (*live);
                if (candidate->node != node)
                    continue;
                std::lock_guard<std::mutex> candidate_lock (
                  candidate->mutex);
                remove_disconnected_bindings_locked (candidate);
                for (std::map<std::string, std::vector<binding_t>>::const_iterator
                       session = candidate->bindings.begin ();
                     session != candidate->bindings.end (); ++session) {
                    for (size_t i = 0; i < session->second.size (); ++i) {
                        const binding_t &bound = session->second[i];
                        if (strncmp (bound.actor.actor_id, actor_->actor_id,
                                     sizeof (bound.actor.actor_id))
                              != 0
                            || bound.actor.generation
                                 != actor_->generation)
                            continue;
                        const bool same_binding =
                          candidate == service
                          && session->first == session_key;
                        fenced = fenced || bound.transfer_serial != 0;
                        idempotent = idempotent || same_binding;
                        conflict = conflict || !same_binding;
                        if (same_binding)
                            binding_generation =
                              bound.binding_generation;
                    }
                }
            }
        }
        if (fenced) {
            errno = EAGAIN;
            return ZLINK_SUBMIT_BACKPRESSURED;
        }
        if (conflict) {
            errno = EBUSY;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        {
            std::lock_guard<std::mutex> lock (node->mutex);
            bool admitted = false;
            for (size_t i = 0; i < node->peers.size (); ++i) {
                if (node->peers[i].state == ZLINK_MESH_PEER_ADMITTED
                    && node->peers[i].rid == owner_rid) {
                    admitted = true;
                    break;
                }
            }
            if (!admitted) {
                errno = ENOTCONN;
                return ZLINK_SUBMIT_NOT_CONNECTED;
            }
        }
        if (!idempotent)
            binding_generation =
              node->next_stream_binding_generation.fetch_add (
                1, std::memory_order_relaxed);

        operation_submission_t remote_submission (
          node, true, ZLINK_MESH_OPERATION_STREAM_BIND, node_owner (),
          timeout_ms_);
        if (!remote_submission.valid ())
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        const zlink_mesh_operation_id_t remote_op_id =
          remote_submission.operation_id ();
        {
            std::lock_guard<std::mutex> lock (node->mutex);
            std::unordered_map<uint64_t, pending_operation_t>::iterator op =
              node->operations.find (remote_op_id.low);
            if (op == node->operations.end ()) {
                errno = ESHUTDOWN;
                return ZLINK_SUBMIT_INVALID_STATE;
            }
            for (std::unordered_map<uint64_t, pending_operation_t>::const_iterator
                   other = node->operations.begin ();
                 other != node->operations.end (); ++other) {
                if (other->first == remote_op_id.low
                    || !other->second.stream_bind_validation
                    || other->second.stream_bind_actor.generation
                         != actor_->generation
                    || strncmp (
                         other->second.stream_bind_actor.actor_id,
                         actor_->actor_id,
                         sizeof (actor_->actor_id))
                         != 0)
                    continue;
                if (other->first < remote_op_id.low) {
                    errno = EBUSY;
                    return ZLINK_SUBMIT_INVALID_STATE;
                }
            }
            op->second.stream_bind_validation = true;
            op->second.stream_bind_service = service_;
            op->second.stream_bind_session_rid = rid_bytes (*session_rid_);
            op->second.stream_bind_actor = *actor_;
            op->second.stream_bind_generation = binding_generation;
            op->second.stream_bind_owner_rid = owner_rid;
        }
        const zlink_submit_result_t submit_result =
          wire_submit_bound_session_bind (
            node, owner_rid, remote_op_id.low, *actor_,
            binding_generation);
        if (submit_result != ZLINK_SUBMIT_OK)
            return submit_result;
        remote_submission.commit ();
        *operation_id_out_ = remote_op_id;
        return ZLINK_SUBMIT_OK;
    }

    operation_submission_t submission (
      node, true, ZLINK_MESH_OPERATION_STREAM_BIND, node_owner (), 0);
    if (!submission.valid ())
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    const zlink_mesh_operation_id_t op_id = submission.operation_id ();
    bool idempotent = false;
    bool fenced = false;
    bool conflict = false;
    bool inserted = false;
    uint64_t inserted_generation = 0;
    {
        //  The registry lock makes the one-binding rule atomic across all
        //  services attached to this MeshNode.
        std::lock_guard<std::mutex> registry_lock (g_session_registry_mutex);
        {
            std::lock_guard<std::mutex> service_lock (service->mutex);
            remove_disconnected_bindings_locked (service);
            if (service->state != ZLINK_STREAM_SESSION_STARTED) {
                errno = ESHUTDOWN;
                return ZLINK_SUBMIT_INVALID_STATE;
            }
            if (!live_session (service, session_key)) {
                errno = ENOTCONN;
                return ZLINK_SUBMIT_NOT_CONNECTED;
            }
        }
        for (std::set<void *>::iterator live = g_live_sessions.begin ();
             live != g_live_sessions.end (); ++live) {
            session_service_t *candidate = static_cast<session_service_t *> (*live);
            if (candidate->node != node)
                continue;
            std::lock_guard<std::mutex> candidate_lock (candidate->mutex);
            remove_disconnected_bindings_locked (candidate);
            for (std::map<std::string, std::vector<binding_t>>::const_iterator session =
                   candidate->bindings.begin ();
                 session != candidate->bindings.end (); ++session) {
                for (size_t i = 0; i < session->second.size (); ++i) {
                    const binding_t &bound = session->second[i];
                    if (strncmp (bound.actor.actor_id, actor_->actor_id,
                                 sizeof (bound.actor.actor_id))
                          != 0
                        || bound.actor.generation != actor_->generation)
                        continue;
                    const bool same_binding =
                      candidate == service && session->first == session_key;
                    fenced = fenced || bound.transfer_serial != 0;
                    idempotent = idempotent || same_binding;
                    conflict = conflict || !same_binding;
                }
            }
        }
        if (!idempotent && !fenced && !conflict) {
            std::lock_guard<std::mutex> service_lock (service->mutex);
            binding_t binding;
            binding.actor = *actor_;
            binding.binding_generation =
              node->next_stream_binding_generation.fetch_add (
                1, std::memory_order_relaxed);
            inserted_generation = binding.binding_generation;
            binding.membership_epoch = membership_epoch;
            binding.transfer_serial = 0;
            memset (&binding.transfer_id, 0, sizeof (binding.transfer_id));
            binding.participant_id = 0;
            binding.allowance_messages = 0;
            binding.allowance_bytes = 0;
            binding.pending_bytes = 0;
            binding.pending_egress_bytes = 0;
            binding.acked_high_water = 0;
            binding.terminal_sealed = false;
            binding.closing = false;
            service->bindings[session_key].push_back (std::move (binding));
            inserted = true;
        }
    }
    if (fenced) {
        errno = EAGAIN;
        return ZLINK_SUBMIT_BACKPRESSURED;
    }
    if (conflict) {
        errno = EBUSY;
        return ZLINK_SUBMIT_INVALID_STATE;
    }
    if (!remote_actor) {
        //  The actor was validated before the binding locks were taken; an
        //  actor destroy may have finished its removal pass in between. Both
        //  success shapes re-validate — the inserting call for its own
        //  insert, and an idempotent call because the binding it observed
        //  may be a concurrent insert that is itself about to roll back —
        //  so no call ever reports success for a destroyed generation.
        bool stale = false;
        {
            std::lock_guard<std::mutex> lock (node->mutex);
            const std::string id (actor_->actor_id);
            std::map<std::string, actor_state_t>::iterator it = node->actors.find (id);
            stale = it == node->actors.end () || it->second.generation != actor_->generation
                    || it->second.draining;
        }
        if (stale) {
            session_bindings_remove_actor (node, *actor_);
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
    }

    const int completion_rc = complete_binding_operation (
      node, ZLINK_MESH_OPERATION_STREAM_BIND, op_id, ZLINK_REQUEST_OK, 0);
    if (completion_rc < 0) {
        if (inserted) {
            std::lock_guard<std::mutex> lock (service->mutex);
            std::map<std::string, std::vector<binding_t>>::iterator bindings =
              service->bindings.find (session_key);
            if (bindings != service->bindings.end ()) {
                for (std::vector<binding_t>::iterator binding =
                       bindings->second.begin ();
                     binding != bindings->second.end (); ++binding) {
                    if (binding->binding_generation == inserted_generation) {
                        bindings->second.erase (binding);
                        break;
                    }
                }
                if (bindings->second.empty ())
                    service->bindings.erase (bindings);
            }
        }
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    if (completion_rc == 0) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_INVALID_STATE;
    }
    submission.commit ();
    *operation_id_out_ = op_id;
    if (inserted)
        wire_broadcast_bound_session_bind (
          node, *actor_, inserted_generation);
    return ZLINK_SUBMIT_OK;
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_stream_session_unbind_actor (void *service_,
                                                         const zlink_routing_id_t *session_rid_,
                                                         const zlink_actor_ref_t *actor_,
                                                         uint64_t expected_binding_generation_,
                                                         zlink_mesh_operation_id_t *operation_id_out_,
                                                         uint32_t timeout_ms_)
try {
    std::shared_ptr<session_service_t> service_owner = as_session_service (service_);
    session_service_t *service = service_owner.get ();
    if (!service) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!session_rid_ || session_rid_->size == 0 || check_actor_ref (actor_) != 0
        || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    mesh_node_pin_t node_pin (service->node);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_INVALID_STATE;
    }
    LIBZLINK_UNUSED (timeout_ms_);

    const std::string session_key (reinterpret_cast<const char *> (session_rid_->data),
                                   session_rid_->size);
    std::shared_ptr<submit_order_state_t> submit_order;
    {
        std::lock_guard<std::mutex> lock (service->mutex);
        remove_disconnected_bindings_locked (service);
        if (service->bindings.find (session_key)
            != service->bindings.end ()) {
            submit_order = submit_order_locked (service, session_key);
            if (!submit_order)
                return ZLINK_SUBMIT_OUT_OF_MEMORY;
        }
    }
    std::unique_lock<std::mutex> submit_lock;
    if (submit_order)
        submit_lock =
          std::unique_lock<std::mutex> (submit_order->mutex);
    std::unique_lock<std::mutex> lock (service->mutex);
    remove_disconnected_bindings_locked (service);
    operation_submission_t submission (
      node, true, ZLINK_MESH_OPERATION_STREAM_UNBIND, node_owner (), 0);
    if (!submission.valid ())
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    const zlink_mesh_operation_id_t op_id = submission.operation_id ();
    size_t binding_index = 0;
    bool remove_binding = false;
    std::map<std::string, std::vector<binding_t>>::iterator it = service->bindings.find (session_key);
    if (it != service->bindings.end ()) {
        for (size_t i = 0; i < it->second.size (); ++i) {
            binding_t &binding = it->second[i];
            if (strncmp (binding.actor.actor_id, actor_->actor_id, sizeof (binding.actor.actor_id))
                  == 0
                && binding.actor.generation == actor_->generation) {
                if (binding.transfer_serial != 0) {
                    errno = EBUSY;
                    return ZLINK_SUBMIT_INVALID_STATE;
                }
                if (binding.binding_generation != expected_binding_generation_) {
                    errno = ESTALE;
                    return ZLINK_SUBMIT_INVALID_STATE;
                }
                binding_index = i;
                remove_binding = true;
                break;
            }
        }
    }
    //  A missing binding unbinds idempotently only for expected generation 0.
    if (!remove_binding && expected_binding_generation_ != 0) {
        errno = ESTALE;
        return ZLINK_SUBMIT_INVALID_STATE;
    }
    stream_unbind_commit_t unbind_commit;
    unbind_commit.service = service;
    unbind_commit.session_key = &session_key;
    unbind_commit.binding_index = binding_index;
    unbind_commit.remove_binding = remove_binding;
    const int completion_rc = complete_binding_operation (
      node, ZLINK_MESH_OPERATION_STREAM_UNBIND, op_id, ZLINK_REQUEST_OK, 0,
      &commit_stream_unbind_locked, &unbind_commit);
    if (completion_rc < 0)
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    if (completion_rc == 0) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_INVALID_STATE;
    }
    submission.commit ();
    *operation_id_out_ = op_id;
    lock.unlock ();
    if (remove_binding)
        wire_broadcast_bound_session_bind (
          node, *actor_, 0, expected_binding_generation_);
    return ZLINK_SUBMIT_OK;
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_config_result_t zlink_stream_session_bindings (void *service_,
                                                     const zlink_routing_id_t *session_rid_,
                                                     zlink_stream_session_binding_t *entries_,
                                                     size_t *count_inout_)
{
    std::shared_ptr<session_service_t> service_owner = as_session_service (service_);
    session_service_t *service = service_owner.get ();
    if (!service) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!session_rid_ || session_rid_->size == 0 || !count_inout_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock (service->mutex);
    remove_disconnected_bindings_locked (service);
    const std::string session_key (reinterpret_cast<const char *> (session_rid_->data),
                                   session_rid_->size);
    std::map<std::string, std::vector<binding_t>>::iterator it = service->bindings.find (session_key);
    const size_t count = it != service->bindings.end () ? it->second.size () : 0;
    if (!entries_) {
        *count_inout_ = count;
        return ZLINK_CONFIG_OK;
    }
    if (*count_inout_ < count) {
        *count_inout_ = count;
        errno = ENOBUFS;
        return ZLINK_CONFIG_BUFFER_TOO_SMALL;
    }
    //  Validate every output element before writing any: an invalid element
    //  must not leave partially written output behind.
    for (size_t i = 0; i < count; ++i) {
        if (check_versioned (&entries_[i]) != 0)
            return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i) {
        zlink_stream_session_binding_t out;
        init_versioned (&out);
        out.session_rid = *session_rid_;
        out.actor = it->second[i].actor;
        out.binding_generation = it->second[i].binding_generation;
        out.membership_epoch = it->second[i].membership_epoch;
        entries_[i] = out;
    }
    *count_inout_ = count;
    return ZLINK_CONFIG_OK;
}

namespace
{
bool has_actor_binding_locked (session_service_t *service_,
                               const std::string &session_key_,
                               const zlink_actor_ref_t *actor_,
                               uint64_t *binding_generation_out_)
{
    const std::map<std::string, std::vector<binding_t>>::const_iterator session =
      service_->bindings.find (session_key_);
    if (session == service_->bindings.end ())
        return false;
    for (size_t i = 0; i < session->second.size (); ++i) {
        const binding_t &binding = session->second[i];
        if (strncmp (binding.actor.actor_id, actor_->actor_id,
                     sizeof (binding.actor.actor_id))
              == 0
            && binding.actor.generation == actor_->generation)
        {
            if (binding_generation_out_)
                *binding_generation_out_ = binding.binding_generation;
            return true;
        }
    }
    return false;
}

zlink_submit_result_t session_to_actor_submit (void *service_,
                                               const zlink_routing_id_t *session_rid_,
                                               const zlink_actor_ref_t *actor_,
                                               const zlink_mesh_metadata_view_t *metadata_,
                                               const zlink_msg_t *parts_,
                                               size_t part_count_,
                                               zlink_mesh_operation_id_t *operation_id_out_,
                                               zlink_send_flags_t flags_,
                                               uint32_t timeout_ms_)
{
    LIBZLINK_UNUSED (flags_);
    LIBZLINK_UNUSED (timeout_ms_);
    std::shared_ptr<session_service_t> service_owner = as_session_service (service_);
    session_service_t *service = service_owner.get ();
    if (!service) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!session_rid_ || session_rid_->size == 0 || check_actor_ref (actor_) != 0 || !parts_
        || part_count_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (metadata_
        && (!metadata_->data || metadata_->size == 0
            || validate_metadata (metadata_->data, metadata_->size) != 0)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (operation_id_out_)
        memset (operation_id_out_, 0, sizeof (*operation_id_out_));

    const std::string session_key (
      reinterpret_cast<const char *> (session_rid_->data), session_rid_->size);
    std::shared_ptr<submit_order_state_t> submit_order;
    uint64_t submit_epoch = 0;
    uint64_t source_binding_generation = 0;
    {
        std::lock_guard<std::mutex> lock (service->mutex);
        remove_disconnected_bindings_locked (service);
        if (service->state != ZLINK_STREAM_SESSION_STARTED) {
            errno = ESHUTDOWN;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        if (!has_actor_binding_locked (
              service, session_key, actor_, &source_binding_generation)) {
            errno = ENOENT;
            return ZLINK_SUBMIT_NOT_FOUND;
        }
        submit_order = submit_order_locked (service, session_key);
        if (!submit_order)
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        submit_epoch = submit_order->epoch;
    }

    std::unique_lock<std::mutex> submit_lock (submit_order->mutex);
    {
        std::unique_lock<std::mutex> lock (service->mutex);
        remove_disconnected_bindings_locked (service);
        if (submit_order->epoch != submit_epoch) {
            errno = ENOTCONN;
            return ZLINK_SUBMIT_NOT_CONNECTED;
        }
        if (service->state != ZLINK_STREAM_SESSION_STARTED) {
            errno = ESHUTDOWN;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        std::map<std::string, std::vector<binding_t>>::iterator it =
          service->bindings.find (session_key);
        bool bound = false;
        if (it != service->bindings.end ()) {
            for (size_t i = 0; i < it->second.size (); ++i) {
                if (strncmp (it->second[i].actor.actor_id, actor_->actor_id,
                             sizeof (it->second[i].actor.actor_id))
                      == 0
                    && it->second[i].actor.generation == actor_->generation) {
                    if (it->second[i].closing) {
                        errno = ENOTCONN;
                        return ZLINK_SUBMIT_NOT_CONNECTED;
                    }
                    if (it->second[i].transfer_serial != 0) {
                        binding_t &binding = it->second[i];
                        size_t record_bytes = metadata_ ? metadata_->size : 0;
                        for (size_t p = 0; p < part_count_; ++p) {
                            const size_t part_size = zlink_msg_size (&parts_[p]);
                            if (part_size > SIZE_MAX - record_bytes) {
                                errno = EMSGSIZE;
                                return ZLINK_SUBMIT_INVALID_ARGUMENT;
                            }
                            record_bytes += part_size;
                        }
                        if (binding.participant_id == 0
                            || binding.terminal_sealed) {
                            const uint64_t transfer_serial =
                              binding.transfer_serial;
                            lock.unlock ();
                            submit_lock.unlock ();
                            register_local_send_ready_interest (
                              service->node,
                              make_actor_send_ready_interest (
                                node_owner (), *actor_),
                              actor_owner (actor_->actor_id,
                                           actor_->generation),
                              record_bytes, transfer_serial);
                            errno = EAGAIN;
                            return ZLINK_SUBMIT_BACKPRESSURED;
                        }
                        if (binding.pending.size () + 1 > binding.allowance_messages
                            || record_bytes > binding.allowance_bytes - binding.pending_bytes) {
                            const uint64_t transfer_serial =
                              binding.transfer_serial;
                            lock.unlock ();
                            submit_lock.unlock ();
                            register_local_send_ready_interest (
                              service->node,
                              make_actor_send_ready_interest (
                                node_owner (), *actor_),
                              actor_owner (actor_->actor_id,
                                           actor_->generation),
                              record_bytes, transfer_serial);
                            errno = EAGAIN;
                            return ZLINK_SUBMIT_BACKPRESSURED;
                        }

                        std::unique_ptr<queued_record_t> record (
                          new (std::nothrow) queued_record_t ());
                        if (!record.get ()) {
                            errno = ENOMEM;
                            return ZLINK_SUBMIT_OUT_OF_MEMORY;
                        }
                        const bool is_request = operation_id_out_ != NULL;
                        try {
                            record->kind = is_request ? ZLINK_MESH_RECORD_ACTOR_REQUEST
                                                      : ZLINK_MESH_RECORD_ACTOR_SEND;
                            record->source_node_rid = service->node->routing_id;
                            record->source_spot_rid = rid_bytes (*session_rid_);
                            record->source_binding_generation =
                              binding.binding_generation;
                            if (metadata_) {
                                record->has_metadata = true;
                                record->application_metadata.assign (metadata_->data,
                                                                     metadata_->data
                                                                       + metadata_->size);
                                record->byte_size += metadata_->size;
                            }
                            if (copy_session_record_parts (parts_, part_count_, record.get ())
                                != 0)
                                return errno == ENOMEM ? ZLINK_SUBMIT_OUT_OF_MEMORY
                                                       : ZLINK_SUBMIT_INTERNAL_ERROR;
                        }
                        catch (const std::bad_alloc &) {
                            errno = ENOMEM;
                            return ZLINK_SUBMIT_OUT_OF_MEMORY;
                        }

                        operation_submission_t submission (
                          service->node, is_request,
                          ZLINK_MESH_OPERATION_ACTOR_REQUEST, node_owner (),
                          is_request ? timeout_ms_ : 0);
                        if (!submission.valid ())
                            return ZLINK_SUBMIT_OUT_OF_MEMORY;
                        const zlink_mesh_operation_id_t op_id =
                          submission.operation_id ();
                        uint64_t reply_serial = 0;
                        if (is_request) {
                            reply_route_t route;
                            route.kind = reply_route_t::kind_generic;
                            route.requester = node_owner ();
                            route.operation_kind = ZLINK_MESH_OPERATION_ACTOR_REQUEST;
                            memset (&route.join_actor, 0, sizeof (route.join_actor));
                            if (!submission.add_reply_route (route, &reply_serial))
                                return ZLINK_SUBMIT_OUT_OF_MEMORY;
                            record->operation_id = op_id;
                            record->operation_kind = ZLINK_MESH_OPERATION_ACTOR_REQUEST;
                            record->has_reply_token = true;
                            seal_reply_token (service->node, reply_serial,
                                              &record->reply_token);
                        }

                        binding.pending_bytes += record->byte_size;
                        binding.pending.push_back (std::move (record));
                        send_next_participant_locked (service, &binding);
                        if (is_request) {
                            submission.commit ();
                            *operation_id_out_ = op_id;
                        }
                        return ZLINK_SUBMIT_OK;
                    }
                    source_binding_generation = it->second[i].binding_generation;
                    bound = true;
                    break;
                }
            }
        }
        if (!bound) {
            errno = ENOENT;
            return ZLINK_SUBMIT_NOT_FOUND;
        }

    }

    //  The binding decision and local mailbox admission are one per-session
    //  ordered operation. Unrelated sessions retain independent backpressure
    //  progress.
#ifdef ZLINK_BUILD_TESTS
    int expected = 0;
    if (g_pause_before_local_actor_admit.load (std::memory_order_acquire) != 0
        && g_local_actor_admit_pause_claimed.compare_exchange_strong (
          expected, 1, std::memory_order_acq_rel)) {
        g_local_actor_admit_paused.store (1, std::memory_order_release);
        while (g_pause_before_local_actor_admit.load (std::memory_order_acquire) != 0)
            std::this_thread::yield ();
        g_local_actor_admit_paused.store (0, std::memory_order_release);
    }
#endif
    {
        std::lock_guard<std::mutex> lock (service->mutex);
        remove_disconnected_bindings_locked (service);
        if (submit_order->epoch != submit_epoch
            || !has_actor_binding_locked (
              service, session_key, actor_, &source_binding_generation)) {
            errno = ENOTCONN;
            return ZLINK_SUBMIT_NOT_CONNECTED;
        }
    }
    return submit_stream_session_to_actor (
      service->node, rid_bytes (*session_rid_), source_binding_generation,
      actor_, metadata_, parts_,
      part_count_, operation_id_out_, flags_, timeout_ms_);
}
}

#ifdef ZLINK_BUILD_TESTS
extern "C" void zlink_test_stream_session_pause_before_local_actor_admit (int pause_)
{
    if (pause_ != 0)
        g_local_actor_admit_pause_claimed.store (0, std::memory_order_release);
    g_pause_before_local_actor_admit.store (pause_ != 0 ? 1 : 0,
                                            std::memory_order_release);
}

extern "C" int zlink_test_stream_session_local_actor_admit_paused ()
{
    return g_local_actor_admit_paused.load (std::memory_order_acquire);
}

extern "C" void zlink_test_stream_session_pause_bound_session_admit (
  int pause_)
{
    if (pause_ == 0)
        g_bound_session_admit_pause_claimed.store (
          0, std::memory_order_release);
    g_pause_before_bound_session_admit.store (
      pause_ != 0 ? 1 : 0, std::memory_order_release);
}

extern "C" int zlink_test_stream_session_bound_session_admit_paused ()
{
    return g_bound_session_admit_paused.load (
      std::memory_order_acquire);
}

extern "C" void zlink_test_stream_session_fence_actor (
  void *service_, const zlink_actor_ref_t *actor_, uint64_t transfer_serial_)
{
    const std::shared_ptr<session_service_t> service =
      as_session_service (service_);
    if (service && actor_)
        stream_sessions_fence_actor (
          service->node, *actor_, transfer_serial_);
}

extern "C" void zlink_test_stream_session_abort_actor (
  void *service_, const zlink_actor_ref_t *actor_, uint64_t transfer_serial_)
{
    const std::shared_ptr<session_service_t> service =
      as_session_service (service_);
    if (service && actor_)
        stream_sessions_abort_actor (
          service->node, *actor_, transfer_serial_);
}

extern "C" void zlink_test_stream_session_forget_active_session (
  void *service_, const zlink_routing_id_t *session_rid_)
{
    const std::shared_ptr<session_service_t> service =
      as_session_service (service_);
    if (!service || !session_rid_)
        return;
    const std::string key (
      reinterpret_cast<const char *> (session_rid_->data), session_rid_->size);
    std::lock_guard<std::mutex> lock (service->mutex);
    service->active_sessions.erase (key);
}
#endif

zlink_submit_result_t zlink_stream_session_send_to_actor (void *service_,
                                                          const zlink_routing_id_t *session_rid_,
                                                          const zlink_actor_ref_t *actor_,
                                                          const zlink_mesh_metadata_view_t *actor_metadata_,
                                                          const zlink_msg_t *parts_,
                                                          size_t part_count_,
                                                          zlink_send_flags_t flags_)
try {
    return session_to_actor_submit (service_, session_rid_, actor_, actor_metadata_, parts_,
                                    part_count_, NULL, flags_, 0);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t
zlink_stream_session_request_to_actor (void *service_,
                                       const zlink_routing_id_t *session_rid_,
                                       const zlink_actor_ref_t *actor_,
                                       const zlink_mesh_metadata_view_t *actor_metadata_,
                                       const zlink_msg_t *parts_,
                                       size_t part_count_,
                                       zlink_mesh_operation_id_t *operation_id_out_,
                                       zlink_send_flags_t flags_,
                                       uint32_t timeout_ms_)
try {
    if (!operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    return session_to_actor_submit (service_, session_rid_, actor_, actor_metadata_, parts_,
                                    part_count_, operation_id_out_, flags_, timeout_ms_);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_mesh_node_actor_send_bound_session (void *mesh_node_,
                                                                const zlink_actor_ref_t *actor_,
                                                                uint64_t expected_binding_generation_,
                                                                const zlink_msg_t *parts_,
                                                                size_t part_count_,
                                                                zlink_send_flags_t flags_)
try {
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (check_actor_ref (actor_) != 0 || expected_binding_generation_ == 0
        || !parts_ || part_count_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    //  Select the binding and its per-session admission gate while the
    //  registry owns the service lifetime.
    std::shared_ptr<session_service_t> selected_service;
    std::shared_ptr<submit_order_state_t> submit_order;
    std::string session_key;
    uint64_t submit_epoch = 0;
    bool stale_binding = false;
    bool transfer_fenced = false;
    {
        std::lock_guard<std::mutex> registry_lock (g_session_registry_mutex);
        for (std::set<void *>::iterator it = g_live_sessions.begin (); it != g_live_sessions.end ();
             ++it) {
            session_service_t *service = static_cast<session_service_t *> (*it);
            if (service->node != node)
                continue;
            std::lock_guard<std::mutex> lock (service->mutex);
            remove_disconnected_bindings_locked (service);
            for (std::map<std::string, std::vector<binding_t>>::iterator bind_it =
                   service->bindings.begin ();
                 bind_it != service->bindings.end (); ++bind_it) {
                for (size_t i = 0; i < bind_it->second.size (); ++i) {
                    const binding_t &binding = bind_it->second[i];
                    if (strncmp (binding.actor.actor_id, actor_->actor_id,
                                 sizeof (binding.actor.actor_id))
                          == 0
                        && binding.actor.generation == actor_->generation) {
                        if (binding.binding_generation
                            != expected_binding_generation_) {
                            stale_binding = true;
                            continue;
                        }
                        if (binding.closing)
                            continue;
                        if (binding.transfer_serial != 0) {
                            transfer_fenced = true;
                            continue;
                        }
                        const std::map<void *, std::shared_ptr<session_service_t>>::const_iterator
                          owner = g_session_owners.find (service);
                        if (owner == g_session_owners.end ())
                            continue;
                        selected_service = owner->second;
                        session_key = bind_it->first;
                        submit_order = submit_order_locked (service, session_key);
                        if (!submit_order)
                            return ZLINK_SUBMIT_OUT_OF_MEMORY;
                        submit_epoch = submit_order->epoch;
                    }
                }
            }
        }
    }
    if (stale_binding) {
        errno = ESTALE;
        return ZLINK_SUBMIT_INVALID_STATE;
    }
    if (transfer_fenced) {
        errno = EAGAIN;
        return ZLINK_SUBMIT_BACKPRESSURED;
    }
    if (!selected_service) {
        rid_bytes_t source_node_rid;
        zlink_actor_ref_t bound_actor = *actor_;
        {
            std::lock_guard<std::mutex> lock (node->mutex);
            const std::map<std::string, bound_session_route_t>::const_iterator route =
              node->bound_session_routes.find (actor_->actor_id);
            if (route == node->bound_session_routes.end ()) {
                errno = ENOENT;
                return ZLINK_SUBMIT_NOT_FOUND;
            }
            if (route->second.actor_generation != actor_->generation) {
                const std::map<std::string, actor_state_t>::const_iterator current_actor =
                  node->actors.find (actor_->actor_id);
                if (current_actor == node->actors.end ()
                    || current_actor->second.generation != actor_->generation) {
                    errno = ENOENT;
                    return ZLINK_SUBMIT_NOT_FOUND;
                }
                //  A binding survives actor transfer even when the target
                //  materializes with a new actor generation. Preserve the
                //  binding generation on the relay envelope so the source
                //  session node can validate the original binding.
                bound_actor.generation = route->second.actor_generation;
            }
            if (route->second.binding_generation
                != expected_binding_generation_) {
                errno = ESTALE;
                return ZLINK_SUBMIT_INVALID_STATE;
            }
            source_node_rid = route->second.source_node_rid;
        }
        return wire_submit_bound_session (
          node, source_node_rid, bound_actor,
          expected_binding_generation_, parts_, part_count_, flags_);
    }

    std::lock_guard<std::mutex> submit_lock (submit_order->mutex);
    void *stream = NULL;
    zlink_routing_id_t session_rid;
    memset (&session_rid, 0, sizeof (session_rid));
    {
        std::lock_guard<std::mutex> lock (selected_service->mutex);
        remove_disconnected_bindings_locked (selected_service.get ());
        if (submit_order->epoch != submit_epoch) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        bool matched = false;
        const std::map<std::string, std::vector<binding_t>>::const_iterator bindings =
          selected_service->bindings.find (session_key);
        if (bindings != selected_service->bindings.end ()) {
            for (size_t i = 0; i < bindings->second.size (); ++i) {
                const binding_t &binding = bindings->second[i];
                if (strncmp (binding.actor.actor_id, actor_->actor_id,
                             sizeof (binding.actor.actor_id)) != 0
                    || binding.actor.generation != actor_->generation)
                    continue;
                if (binding.binding_generation
                    != expected_binding_generation_) {
                    errno = ESTALE;
                    return ZLINK_SUBMIT_INVALID_STATE;
                }
                if (binding.closing) {
                    errno = ENOTCONN;
                    return ZLINK_SUBMIT_NOT_CONNECTED;
                }
                if (binding.transfer_serial != 0) {
                    errno = EAGAIN;
                    return ZLINK_SUBMIT_BACKPRESSURED;
                }
                matched = true;
                break;
            }
        }
        if (!matched) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        stream = selected_service->stream;
        session_rid.size = static_cast<uint8_t> (
          std::min (session_key.size (), sizeof (session_rid.data)));
        memcpy (session_rid.data, session_key.data (), session_rid.size);
    }

    //  Raw STREAM has one byte stream rather than multipart frame boundaries.
    //  Coalesce the borrowed parts and perform one consuming submit while the
    //  per-session gate prevents unbind/rebind from changing its generation.
    return send_stream_complete (stream, &session_rid, parts_, part_count_, flags_);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_mesh_node_actor_close_bound_session (void *mesh_node_,
                                                                 const zlink_actor_ref_t *actor_,
                                                                 uint64_t expected_binding_generation_,
                                                                 zlink_mesh_operation_id_t *operation_id_out_,
                                                                 uint32_t timeout_ms_)
try {
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (check_actor_ref (actor_) != 0 || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    LIBZLINK_UNUSED (timeout_ms_);
    operation_submission_t submission (
      node, true, ZLINK_MESH_OPERATION_STREAM_CLOSE,
      actor_owner (actor_->actor_id, actor_->generation), 0);
    if (!submission.valid ())
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    const zlink_mesh_operation_id_t op_id = submission.operation_id ();

    std::unique_lock<std::mutex> registry_lock (g_session_registry_mutex);
    for (std::set<void *>::iterator it = g_live_sessions.begin (); it != g_live_sessions.end ();
         ++it) {
        session_service_t *service = static_cast<session_service_t *> (*it);
        if (service->node != node)
            continue;
        std::unique_lock<std::mutex> lock (service->mutex);
        remove_disconnected_bindings_locked (service);
        for (std::map<std::string, std::vector<binding_t>>::iterator bind_it =
               service->bindings.begin ();
             bind_it != service->bindings.end (); ++bind_it) {
            for (size_t i = 0; i < bind_it->second.size (); ++i) {
                binding_t &binding = bind_it->second[i];
                if (strncmp (binding.actor.actor_id, actor_->actor_id,
                             sizeof (binding.actor.actor_id))
                      == 0
                    && binding.actor.generation == actor_->generation) {
                    if (binding.transfer_serial != 0) {
                        errno = EBUSY;
                        return ZLINK_SUBMIT_INVALID_STATE;
                    }
                    if (binding.binding_generation != expected_binding_generation_) {
                        errno = ESTALE;
                        return ZLINK_SUBMIT_INVALID_STATE;
                    }
                    if (binding.closing) {
                        errno = EBUSY;
                        return ZLINK_SUBMIT_INVALID_STATE;
                    }
                    stream_close_commit_t close_commit;
                    memset (&close_commit.session_rid, 0,
                            sizeof (close_commit.session_rid));
                    close_commit.service = service;
                    close_commit.session_rid.size = static_cast<uint8_t> (
                      std::min (bind_it->first.size (),
                                sizeof (close_commit.session_rid.data)));
                    memcpy (close_commit.session_rid.data, bind_it->first.data (),
                            close_commit.session_rid.size);
                    close_commit.binding_generation =
                      expected_binding_generation_;
                    binding.closing = true;
                    lock.unlock ();
                    pending_operation_completion_t prepared_completion;
                    const int prepare_rc =
                      prepare_pending_operation_completion (
                        node, op_id, &prepared_completion);
                    if (prepare_rc < 0) {
                        clear_stream_binding_closing (close_commit);
                        return ZLINK_SUBMIT_OUT_OF_MEMORY;
                    }
                    if (prepare_rc == 0) {
                        clear_stream_binding_closing (close_commit);
                        errno = ESHUTDOWN;
                        return ZLINK_SUBMIT_INVALID_STATE;
                    }
                    const zlink_connect_result_t disconnect_result =
                      zlink_disconnect_rid (
                        service->stream, &close_commit.session_rid);
                    if (disconnect_result != ZLINK_CONNECT_OK) {
                        cancel_pending_operation_completion (
                          node, &prepared_completion);
                        clear_stream_binding_closing (close_commit);
                        if (disconnect_result == ZLINK_CONNECT_NOT_FOUND)
                            return ZLINK_SUBMIT_NOT_CONNECTED;
                        if (disconnect_result == ZLINK_CONNECT_BUSY)
                            return ZLINK_SUBMIT_INVALID_STATE;
                        return ZLINK_SUBMIT_INTERNAL_ERROR;
                    }
                    const int completion_rc =
                      commit_prepared_pending_operation (
                        node, &prepared_completion, ZLINK_REQUEST_OK, 0);
                    if (completion_rc != 1) {
                        errno = ESHUTDOWN;
                        return ZLINK_SUBMIT_INVALID_STATE;
                    }
                    submission.commit ();
                    *operation_id_out_ = op_id;
                    registry_lock.unlock ();
                    wire_broadcast_bound_session_bind (
                      node, *actor_, 0,
                      expected_binding_generation_);
                    return ZLINK_SUBMIT_OK;
                }
            }
        }
    }
    errno = ENOENT;
    return ZLINK_SUBMIT_NOT_FOUND;
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

namespace zlink
{
namespace mesh
{
bool stream_session_owns_socket (void *socket_)
{
    std::lock_guard<std::mutex> registry_lock (g_session_registry_mutex);
    return g_claimed_streams.count (socket_) != 0;
}

void stream_sessions_apply_remote_bind_reply (
  mesh_node_t *node_,
  const pending_operation_t &op_,
  const rid_bytes_t &source_rid_,
  int32_t terminal_result_,
  int32_t failure_errno_,
  uint64_t binding_generation_,
  uint64_t membership_epoch_)
try {
    if (!op_.stream_bind_validation
        || op_.stream_bind_owner_rid != source_rid_) {
        (void) complete_pending_operation (
          node_, op_, ZLINK_REQUEST_PROTOCOL_ERROR, EPROTO, NULL, NULL);
        return;
    }
    if (terminal_result_ != ZLINK_REQUEST_OK) {
        (void) complete_pending_operation (
          node_, op_, terminal_result_, failure_errno_, NULL, NULL);
        return;
    }
    if (binding_generation_ == 0 || membership_epoch_ == 0
        || binding_generation_ != op_.stream_bind_generation) {
        (void) complete_pending_operation (
          node_, op_, ZLINK_REQUEST_PROTOCOL_ERROR, EPROTO, NULL, NULL);
        return;
    }

    const std::shared_ptr<session_service_t> service_owner =
      as_session_service (op_.stream_bind_service);
    session_service_t *service = service_owner.get ();
    if (!service || service->node != node_) {
        (void) complete_pending_operation (
          node_, op_, ZLINK_REQUEST_TERMINATED, ESHUTDOWN, NULL, NULL);
        return;
    }

    const std::string session_key (
      op_.stream_bind_session_rid.begin (),
      op_.stream_bind_session_rid.end ());
    bool inserted = false;
    bool idempotent = false;
    bool conflict = false;
    bool session_unavailable = false;
    {
        std::lock_guard<std::mutex> registry_lock (
          g_session_registry_mutex);
        {
            std::lock_guard<std::mutex> service_lock (service->mutex);
            remove_disconnected_bindings_locked (service);
            if (service->destroying
                || service->state != ZLINK_STREAM_SESSION_STARTED
                || !live_session (service, session_key)) {
                session_unavailable = true;
            }
        }
        for (std::set<void *>::iterator live = g_live_sessions.begin ();
             !session_unavailable && live != g_live_sessions.end ();
             ++live) {
            session_service_t *candidate =
              static_cast<session_service_t *> (*live);
            if (candidate->node != node_)
                continue;
            std::lock_guard<std::mutex> candidate_lock (
              candidate->mutex);
            remove_disconnected_bindings_locked (candidate);
            for (std::map<std::string, std::vector<binding_t>>::iterator
                   session = candidate->bindings.begin ();
                 session != candidate->bindings.end (); ++session) {
                for (size_t i = 0; i < session->second.size (); ++i) {
                    binding_t &bound = session->second[i];
                    if (strncmp (
                          bound.actor.actor_id,
                          op_.stream_bind_actor.actor_id,
                          sizeof (bound.actor.actor_id))
                          != 0
                        || bound.actor.generation
                             != op_.stream_bind_actor.generation)
                        continue;
                    const bool same_binding =
                      candidate == service
                      && session->first == session_key
                      && bound.binding_generation
                           == binding_generation_;
                    idempotent = idempotent || same_binding;
                    conflict = conflict || !same_binding
                               || bound.transfer_serial != 0;
                    if (same_binding)
                        bound.membership_epoch = membership_epoch_;
                }
            }
        }
        if (!session_unavailable && !idempotent && !conflict) {
            std::lock_guard<std::mutex> service_lock (service->mutex);
            binding_t binding;
            binding.actor = op_.stream_bind_actor;
            binding.binding_generation = binding_generation_;
            binding.membership_epoch = membership_epoch_;
            binding.transfer_serial = 0;
            memset (&binding.transfer_id, 0, sizeof (binding.transfer_id));
            binding.participant_id = 0;
            binding.allowance_messages = 0;
            binding.allowance_bytes = 0;
            binding.pending_bytes = 0;
            binding.pending_egress_bytes = 0;
            binding.acked_high_water = 0;
            binding.terminal_sealed = false;
            binding.closing = false;
            service->bindings[session_key].push_back (std::move (binding));
            inserted = true;
        }
    }
    if (session_unavailable) {
        (void) complete_pending_operation (
          node_, op_, ZLINK_REQUEST_NOT_CONNECTED, ENOTCONN, NULL, NULL);
        return;
    }
    if (conflict) {
        (void) wire_submit_bound_session_bind (
          node_, source_rid_, 0, op_.stream_bind_actor, 0,
          binding_generation_);
        (void) complete_pending_operation (
          node_, op_, ZLINK_REQUEST_CONFLICT, EBUSY, NULL, NULL);
        return;
    }

    const int completion_rc = complete_binding_operation (
      node_, ZLINK_MESH_OPERATION_STREAM_BIND, op_.id,
      ZLINK_REQUEST_OK, 0);
    if (completion_rc == 1)
        return;

    if (inserted) {
        std::lock_guard<std::mutex> lock (service->mutex);
        std::map<std::string, std::vector<binding_t>>::iterator bindings =
          service->bindings.find (session_key);
        if (bindings != service->bindings.end ()) {
            for (std::vector<binding_t>::iterator binding =
                   bindings->second.begin ();
                 binding != bindings->second.end (); ++binding) {
                if (binding->binding_generation == binding_generation_) {
                    bindings->second.erase (binding);
                    break;
                }
            }
            if (bindings->second.empty ())
                service->bindings.erase (bindings);
        }
    }
    (void) wire_submit_bound_session_bind (
      node_, source_rid_, 0, op_.stream_bind_actor, 0,
      binding_generation_);
}
catch (const std::bad_alloc &) {
    (void) complete_pending_operation (
      node_, op_, ZLINK_REQUEST_INTERNAL_ERROR, ENOMEM, NULL, NULL);
}

zlink_submit_result_t stream_sessions_deliver_bound_session (
  mesh_node_t *node_,
  const zlink_actor_ref_t &actor_,
  uint64_t expected_binding_generation_,
  std::vector<zlink_msg_t> *parts_)
try {
    if (!node_ || expected_binding_generation_ == 0 || !parts_
        || parts_->empty ()) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    uint64_t record_bytes = 0;
    for (size_t i = 0; i < parts_->size (); ++i) {
        const size_t part_size = zlink_msg_size (&(*parts_)[i]);
        if (part_size > UINT64_MAX - record_bytes) {
            errno = EMSGSIZE;
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        }
        record_bytes += part_size;
    }

    std::vector<std::shared_ptr<session_service_t>> services;
    {
        std::lock_guard<std::mutex> registry_lock (g_session_registry_mutex);
        for (std::set<void *>::const_iterator service_it = g_live_sessions.begin ();
             service_it != g_live_sessions.end (); ++service_it) {
            std::map<void *, std::shared_ptr<session_service_t>>::const_iterator
              owner = g_session_owners.find (*service_it);
            if (owner != g_session_owners.end ()
                && owner->second->node == node_)
                services.push_back (owner->second);
        }
    }

    for (size_t service_index = 0; service_index < services.size ();
         ++service_index) {
        session_service_t *service = services[service_index].get ();
        for (;;) {
            std::shared_ptr<submit_order_state_t> submit_order;
            std::string session_key;
            uint64_t submit_epoch = 0;
            {
                std::unique_lock<std::mutex> service_lock (service->mutex);
                if (service->destroying
                    || (service->state != ZLINK_STREAM_SESSION_STARTED
                        && service->state != ZLINK_STREAM_SESSION_DRAINING)) {
                    errno = ENOTCONN;
                    return ZLINK_SUBMIT_NOT_CONNECTED;
                }
                remove_disconnected_bindings_locked (service);
                for (std::map<std::string, std::vector<binding_t>>::iterator
                       session = service->bindings.begin ();
                     session != service->bindings.end ()
                     && !submit_order;
                     ++session) {
                    for (size_t i = 0; i < session->second.size (); ++i) {
                        const binding_t &binding = session->second[i];
                        if (strncmp (binding.actor.actor_id, actor_.actor_id,
                                     sizeof (binding.actor.actor_id))
                              != 0
                            || binding.actor.generation
                                 != actor_.generation)
                            continue;
                        if (binding.binding_generation
                            != expected_binding_generation_) {
                            errno = ESTALE;
                            return ZLINK_SUBMIT_INVALID_STATE;
                        }
                        session_key = session->first;
                        submit_order =
                          submit_order_locked (service, session_key);
                        if (!submit_order)
                            return ZLINK_SUBMIT_OUT_OF_MEMORY;
                        submit_epoch = submit_order->epoch;
                        break;
                    }
                }
            }
            if (!submit_order)
                break;

#ifdef ZLINK_BUILD_TESTS
            int expected = 0;
            if (g_pause_before_bound_session_admit.load (
                  std::memory_order_acquire)
                  != 0
                && g_bound_session_admit_pause_claimed.compare_exchange_strong (
                  expected, 1, std::memory_order_acq_rel)) {
                g_bound_session_admit_paused.store (
                  1, std::memory_order_release);
                while (g_pause_before_bound_session_admit.load (
                         std::memory_order_acquire)
                       != 0)
                    std::this_thread::yield ();
                g_bound_session_admit_paused.store (
                  0, std::memory_order_release);
            }
#endif

            //  Unbind/rebind and physical delivery use the same per-session
            //  gate. Revalidate after acquiring it, then retain it through
            //  the STREAM FIFO submit or transfer FIFO admission.
            std::unique_lock<std::mutex> submit_lock (
              submit_order->mutex);
            std::unique_lock<std::mutex> service_lock (service->mutex);
            remove_disconnected_bindings_locked (service);
            if (submit_order->epoch != submit_epoch) {
                errno = ESTALE;
                return ZLINK_SUBMIT_INVALID_STATE;
            }
            binding_t *matched_binding = NULL;
            const std::map<std::string, std::vector<binding_t>>::iterator
              bindings = service->bindings.find (session_key);
            if (bindings != service->bindings.end ()) {
                for (size_t i = 0; i < bindings->second.size (); ++i) {
                    binding_t &binding = bindings->second[i];
                    if (strncmp (binding.actor.actor_id, actor_.actor_id,
                                 sizeof (binding.actor.actor_id))
                          != 0
                        || binding.actor.generation != actor_.generation)
                        continue;
                    if (binding.binding_generation
                        != expected_binding_generation_) {
                        errno = ESTALE;
                        return ZLINK_SUBMIT_INVALID_STATE;
                    }
                    matched_binding = &binding;
                    break;
                }
            }
            if (!matched_binding) {
                errno = ESTALE;
                return ZLINK_SUBMIT_INVALID_STATE;
            }
            if (matched_binding->closing) {
                errno = ENOTCONN;
                return ZLINK_SUBMIT_NOT_CONNECTED;
            }
            if (matched_binding->transfer_serial == 0) {
                void *stream = service->stream;
                zlink_routing_id_t session_rid;
                memset (&session_rid, 0, sizeof (session_rid));
                session_rid.size = static_cast<uint8_t> (
                  std::min (session_key.size (), sizeof (session_rid.data)));
                memcpy (session_rid.data, session_key.data (),
                        session_rid.size);
                service_lock.unlock ();
                return send_stream_complete (
                  stream, &session_rid, &(*parts_)[0], parts_->size (),
                  ZLINK_SEND_FLAGS_NONE);
            }

            //  Reverse traffic is owned by the source binding. A binding
            //  that was not present in the readiness exchange has no target
            //  participant allowance, so it uses the source node's bounded
            //  mailbox budget. When either bound is full, retain the current
            //  wire frame on this ingress stack and stop reading until
            //  commit, abort, shutdown, or capacity release wakes us. This
            //  turns pressure into transport backpressure without dropping a
            //  frame whose target submit already succeeded.
            const uint64_t allowance_messages =
              matched_binding->participant_id == 0
                ? node_->effective_message_budget ()
                : matched_binding->allowance_messages;
            const uint64_t allowance_bytes =
              matched_binding->participant_id == 0
                ? node_->effective_byte_budget ()
                : matched_binding->allowance_bytes;
            const uint64_t pending_messages =
              matched_binding->pending.size ()
              + matched_binding->pending_egress.size ();
            const uint64_t pending_bytes =
              matched_binding->pending_bytes
              + matched_binding->pending_egress_bytes;
            const bool message_full = pending_messages >= allowance_messages;
            const bool byte_full = pending_bytes > allowance_bytes
                                   || record_bytes
                                        > allowance_bytes - pending_bytes;
            if (message_full || byte_full) {
                service->last_error = EAGAIN;
                //  Commit and abort use the same submit-order gate before
                //  updating this binding. Release it before waiting for a
                //  lifecycle or capacity change; the next loop iteration
                //  reacquires the gate and validates its epoch and binding
                //  generation again before admitting the retained frame.
                submit_lock.unlock ();
                service->cv.wait (service_lock);
                continue;
            }

            std::unique_ptr<queued_record_t> record (
              new (std::nothrow) queued_record_t ());
            if (!record.get ()) {
                service->last_error = ENOMEM;
                errno = ENOMEM;
                return ZLINK_SUBMIT_OUT_OF_MEMORY;
            }
            record->byte_size = record_bytes;
            try {
                matched_binding->pending_egress.push_back (
                  std::unique_ptr<queued_record_t> ());
            }
            catch (const std::bad_alloc &) {
                service->last_error = ENOMEM;
                errno = ENOMEM;
                return ZLINK_SUBMIT_OUT_OF_MEMORY;
            }
            record->parts.swap (*parts_);
            matched_binding->pending_egress.back () = std::move (record);
            matched_binding->pending_egress_bytes += record_bytes;
            service->cv.notify_all ();
            return ZLINK_SUBMIT_OK;
        }
    }

    //  A non-local binding can still be reached through the route installed
    //  by an earlier transfer. Resolve its physical source node without
    //  re-entering the public caller validation path.
    rid_bytes_t source_node_rid;
    zlink_actor_ref_t bound_actor = actor_;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        const std::map<std::string, bound_session_route_t>::const_iterator route =
          node_->bound_session_routes.find (actor_.actor_id);
        if (route == node_->bound_session_routes.end ()) {
            errno = ENOENT;
            return ZLINK_SUBMIT_NOT_FOUND;
        }
        if (route->second.actor_generation != actor_.generation) {
            const std::map<std::string, actor_state_t>::const_iterator current_actor =
              node_->actors.find (actor_.actor_id);
            if (current_actor == node_->actors.end ()
                || current_actor->second.generation != actor_.generation) {
                errno = ENOENT;
                return ZLINK_SUBMIT_NOT_FOUND;
            }
            bound_actor.generation = route->second.actor_generation;
        }
        if (route->second.binding_generation
            != expected_binding_generation_) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        source_node_rid = route->second.source_node_rid;
    }
    return wire_submit_bound_session (
      node_, source_node_rid, bound_actor, expected_binding_generation_,
      &(*parts_)[0], parts_->size (), ZLINK_SEND_FLAGS_NONE);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

void stream_sessions_replay_bound_session_bindings (
  mesh_node_t *node_, const rid_bytes_t &peer_rid_)
try {
    std::vector<binding_announcement_t> announcements;
    {
        std::lock_guard<std::mutex> registry_lock (g_session_registry_mutex);
        for (std::set<void *>::const_iterator service_it =
               g_live_sessions.begin ();
             service_it != g_live_sessions.end (); ++service_it) {
            session_service_t *service =
              static_cast<session_service_t *> (*service_it);
            if (service->node != node_)
                continue;
            std::lock_guard<std::mutex> service_lock (service->mutex);
            remove_disconnected_bindings_locked (service);
            for (std::map<std::string, std::vector<binding_t>>::const_iterator
                   session = service->bindings.begin ();
                 session != service->bindings.end (); ++session) {
                for (size_t i = 0; i < session->second.size (); ++i) {
                    const binding_t &binding = session->second[i];
                    if (binding.closing)
                        continue;
                    binding_announcement_t announcement;
                    announcement.actor = binding.actor;
                    announcement.binding_generation =
                      binding.binding_generation;
                    announcements.push_back (announcement);
                }
            }
        }
    }
    for (size_t i = 0; i < announcements.size (); ++i)
        (void) wire_submit_bound_session_bind (
          node_, peer_rid_, 0, announcements[i].actor,
          announcements[i].binding_generation);
}
catch (const std::bad_alloc &) {
    //  Admission remains valid. A later bind transition or peer reconnect
    //  can replay the route without weakening owner-side generation checks.
}

bool session_bindings_pending (mesh_node_t *node_, const zlink_actor_ref_t &actor_)
{
    bool pending = false;
    for_each_actor_binding (node_, actor_,
                            [&pending] (session_service_t *, binding_t &binding_) {
                                if (!binding_.pending.empty ()
                                    || !binding_.pending_egress.empty ())
                                    pending = true;
                            });
    return pending;
}

void session_bindings_remove_actor (mesh_node_t *node_, const zlink_actor_ref_t &actor_)
{
    std::vector<binding_announcement_t> removed_bindings;
    std::unique_lock<std::mutex> registry_lock (g_session_registry_mutex);
    for (std::set<void *>::iterator service_it = g_live_sessions.begin ();
         service_it != g_live_sessions.end (); ++service_it) {
        session_service_t *service = static_cast<session_service_t *> (*service_it);
        if (service->node != node_)
            continue;
        std::lock_guard<std::mutex> service_lock (service->mutex);
        for (std::map<std::string, std::vector<binding_t>>::iterator session_it =
               service->bindings.begin ();
             session_it != service->bindings.end (); ++session_it) {
            std::vector<binding_t> &bindings = session_it->second;
            for (size_t i = bindings.size (); i > 0; --i) {
                binding_t &binding = bindings[i - 1];
                if (strncmp (binding.actor.actor_id, actor_.actor_id,
                             sizeof (binding.actor.actor_id))
                      == 0
                    && binding.actor.generation == actor_.generation)
                {
                    try {
                        binding_announcement_t removed;
                        removed.actor = binding.actor;
                        removed.binding_generation =
                          binding.binding_generation;
                        removed_bindings.push_back (removed);
                    }
                    catch (const std::bad_alloc &) {
                    }
                    bindings.erase (bindings.begin () + (i - 1));
                }
            }
        }
    }
    registry_lock.unlock ();
    for (size_t i = 0; i < removed_bindings.size (); ++i)
        wire_broadcast_bound_session_bind (
          node_, removed_bindings[i].actor, 0,
          removed_bindings[i].binding_generation);
}

void set_stream_session_transfer_fence (binding_t *binding_,
                                        uint64_t transfer_serial_)
{
    binding_->transfer_serial = transfer_serial_;
    memset (&binding_->transfer_id, 0, sizeof (binding_->transfer_id));
    binding_->target_node_rid.clear ();
    binding_->participant_id = 0;
    binding_->allowance_messages = 0;
    binding_->allowance_bytes = 0;
    binding_->pending_bytes = 0;
    binding_->pending_egress_bytes = 0;
    binding_->acked_high_water = 0;
    binding_->terminal_sealed = false;
    binding_->pending.clear ();
    binding_->pending_egress.clear ();
}

void stream_sessions_fence_actor (mesh_node_t *node_,
                                  const zlink_actor_ref_t &actor_,
                                  uint64_t transfer_serial_)
{
    std::vector<std::shared_ptr<session_service_t>> services;
    {
        std::lock_guard<std::mutex> registry_lock (
          g_session_registry_mutex);
        for (std::set<void *>::const_iterator service_it =
               g_live_sessions.begin ();
             service_it != g_live_sessions.end (); ++service_it) {
            std::map<void *, std::shared_ptr<session_service_t>>::const_iterator
              owner = g_session_owners.find (*service_it);
            if (owner != g_session_owners.end ()
                && owner->second->node == node_)
                services.push_back (owner->second);
        }
    }

    //  Never wait on a session gate while holding the global registry. A
    //  backpressured session must not block unrelated service lookup/destroy.
    for (size_t service_index = 0; service_index < services.size ();
         ++service_index) {
        session_service_t *service = services[service_index].get ();
        if (service->node != node_)
            continue;

        std::vector<std::string> session_keys;
        {
            std::lock_guard<std::mutex> service_lock (service->mutex);
            remove_disconnected_bindings_locked (service);
            for (std::map<std::string, std::vector<binding_t>>::const_iterator
                   session = service->bindings.begin ();
                 session != service->bindings.end (); ++session) {
                for (size_t i = 0; i < session->second.size (); ++i) {
                    const binding_t &binding = session->second[i];
                    if (strncmp (binding.actor.actor_id, actor_.actor_id,
                                 sizeof (binding.actor.actor_id))
                          == 0
                        && binding.actor.generation == actor_.generation) {
                        session_keys.push_back (session->first);
                        break;
                    }
                }
            }
        }

        for (size_t key_index = 0; key_index < session_keys.size ();
             ++key_index) {
            std::shared_ptr<submit_order_state_t> order;
            {
                std::lock_guard<std::mutex> service_lock (service->mutex);
                order = submit_order_locked (
                  service, session_keys[key_index]);
            }
            if (!order) {
                //  No submit can create a gate while the service mutex is
                //  held. Install the barrier directly so a later successful
                //  allocation still observes the transfer.
                std::lock_guard<std::mutex> service_lock (service->mutex);
                std::map<std::string, std::vector<binding_t>>::iterator session =
                  service->bindings.find (session_keys[key_index]);
                if (session != service->bindings.end ()) {
                    for (size_t i = 0; i < session->second.size (); ++i) {
                        binding_t &binding = session->second[i];
                        if (strncmp (
                              binding.actor.actor_id, actor_.actor_id,
                              sizeof (binding.actor.actor_id))
                              == 0
                            && binding.actor.generation
                                 == actor_.generation)
                            set_stream_session_transfer_fence (
                              &binding, transfer_serial_);
                    }
                }
                continue;
            }

            //  The transfer barrier and submits for this session use one
            //  ordering gate. A submit admitted before the fence completes
            //  first; later submits observe transfer_serial and enter the
            //  bounded transfer path.
            std::lock_guard<std::mutex> order_lock (order->mutex);
            std::lock_guard<std::mutex> service_lock (service->mutex);
            std::map<std::string, std::vector<binding_t>>::iterator session =
              service->bindings.find (session_keys[key_index]);
            if (session == service->bindings.end ())
                continue;
            for (size_t i = 0; i < session->second.size (); ++i) {
                binding_t &binding = session->second[i];
                if (strncmp (binding.actor.actor_id, actor_.actor_id,
                             sizeof (binding.actor.actor_id))
                      != 0
                    || binding.actor.generation != actor_.generation)
                    continue;
                set_stream_session_transfer_fence (
                  &binding, transfer_serial_);
            }
        }
    }
}

void stream_sessions_negotiate_actor (
  mesh_node_t *node_,
  const zlink_actor_ref_t &actor_,
  uint64_t transfer_serial_,
  const zlink_actor_transfer_id_t &transfer_id_,
  const rid_bytes_t &target_node_rid_,
  uint64_t offered_messages_,
  uint64_t offered_bytes_,
  std::vector<transfer_participant_descriptor_t> *participants_out_)
{
    participants_out_->clear ();
    size_t participant_count = 0;
    for_each_actor_binding (node_, actor_, [&participant_count, transfer_serial_] (
                                            session_service_t *, binding_t &binding_) {
        if (binding_.transfer_serial == transfer_serial_)
            ++participant_count;
    });
    if (participant_count == 0)
        return;

    uint64_t participant_id = 0;
    uint64_t remaining_messages = offered_messages_;
    uint64_t remaining_bytes = offered_bytes_;
    size_t remaining_participants = participant_count;
    for_each_actor_binding (
      node_, actor_,
      [&] (session_service_t *, binding_t &binding_) {
          if (binding_.transfer_serial != transfer_serial_)
              return;
          const uint64_t message_allowance =
            remaining_participants == 0 ? 0 : remaining_messages / remaining_participants;
          const uint64_t byte_allowance =
            remaining_participants == 0 ? 0 : remaining_bytes / remaining_participants;
          ++participant_id;
          binding_.transfer_id = transfer_id_;
          binding_.target_node_rid = target_node_rid_;
          binding_.participant_id = participant_id;
          binding_.allowance_messages = message_allowance;
          binding_.allowance_bytes = byte_allowance;
          binding_.pending_bytes = 0;
          binding_.pending_egress_bytes = 0;
          binding_.acked_high_water = 0;
          binding_.terminal_sealed = false;

          transfer_participant_descriptor_t descriptor;
          descriptor.participant_id = participant_id;
          descriptor.binding_generation = binding_.binding_generation;
          descriptor.allowance_messages = message_allowance;
          descriptor.allowance_bytes = byte_allowance;
          participants_out_->push_back (descriptor);

          remaining_messages -= message_allowance;
          remaining_bytes -= byte_allowance;
          --remaining_participants;
      });
}

void stream_sessions_ack_actor (mesh_node_t *node_,
                                const zlink_actor_ref_t &actor_,
                                uint64_t transfer_serial_,
                                uint64_t participant_id_,
                                uint64_t high_water_)
{
    for_each_actor_binding (
      node_, actor_,
      [=] (session_service_t *service_, binding_t &binding_) {
          if (binding_.transfer_serial != transfer_serial_
              || binding_.participant_id != participant_id_)
              return;
          if (high_water_ > binding_.acked_high_water
              && high_water_ <= binding_.pending.size ())
              binding_.acked_high_water = high_water_;
          send_next_participant_locked (service_, &binding_);
      });
}

void stream_sessions_seal_actor (
  mesh_node_t *node_,
  const zlink_actor_ref_t &actor_,
  uint64_t transfer_serial_,
  std::vector<transfer_participant_terminal_t> *terminals_out_)
{
    terminals_out_->clear ();
    for_each_actor_binding (
      node_, actor_,
      [=] (session_service_t *service_, binding_t &binding_) {
          if (binding_.transfer_serial != transfer_serial_ || binding_.participant_id == 0)
              return;
          binding_.terminal_sealed = true;
          transfer_participant_terminal_t terminal;
          terminal.participant_id = binding_.participant_id;
          terminal.high_water = binding_.pending.size ();
          terminals_out_->push_back (terminal);
          send_next_participant_locked (service_, &binding_);
      });
}

int stream_sessions_commit_actor (mesh_node_t *node_,
                                  const zlink_actor_ref_t &actor_,
                                  uint64_t transfer_serial_,
                                  const rid_bytes_t &target_node_rid_,
                                  uint64_t membership_epoch_)
{
    std::shared_ptr<session_service_t> service_owner;
    std::string session_key;
    {
        std::lock_guard<std::mutex> registry_lock (g_session_registry_mutex);
        for (std::set<void *>::iterator service_it = g_live_sessions.begin ();
             service_it != g_live_sessions.end () && !service_owner;
             ++service_it) {
            std::map<void *, std::shared_ptr<session_service_t>>::iterator owner =
              g_session_owners.find (*service_it);
            if (owner == g_session_owners.end ()
                || owner->second->node != node_)
                continue;
            session_service_t *service = owner->second.get ();
            std::lock_guard<std::mutex> service_lock (service->mutex);
            for (std::map<std::string, std::vector<binding_t>>::iterator session =
                   service->bindings.begin ();
                 session != service->bindings.end (); ++session) {
                for (size_t i = 0; i < session->second.size (); ++i) {
                    binding_t &binding = session->second[i];
                    if (binding.transfer_serial == transfer_serial_
                        && strncmp (binding.actor.actor_id, actor_.actor_id,
                                    sizeof (binding.actor.actor_id))
                             == 0
                        && binding.actor.generation == actor_.generation) {
                        service_owner = owner->second;
                        session_key = session->first;
                        break;
                    }
                }
                if (service_owner)
                    break;
            }
        }
    }
    if (!service_owner)
        return 0;

    session_service_t *service = service_owner.get ();
    std::shared_ptr<submit_order_state_t> order;
    {
        std::lock_guard<std::mutex> service_lock (service->mutex);
        order = submit_order_locked (service, session_key);
    }
    if (!order)
        return -1;

    //  Use the same per-session gate as session ingress. Once the last queued
    //  reverse frame has been submitted, clearing transfer_serial under the
    //  service mutex makes later frames use the ordinary direct path, after
    //  this FIFO, without a gap in ownership.
    std::lock_guard<std::mutex> order_lock (order->mutex);
    for (;;) {
        std::unique_lock<std::mutex> service_lock (service->mutex);
        std::map<std::string, std::vector<binding_t>>::iterator session =
          service->bindings.find (session_key);
        if (session == service->bindings.end ()) {
            errno = ENOTCONN;
            return -1;
        }
        binding_t *binding = NULL;
        for (size_t i = 0; i < session->second.size (); ++i) {
            if (session->second[i].transfer_serial == transfer_serial_
                && strncmp (session->second[i].actor.actor_id,
                            actor_.actor_id,
                            sizeof (session->second[i].actor.actor_id))
                     == 0
                && session->second[i].actor.generation
                     == actor_.generation) {
                binding = &session->second[i];
                break;
            }
        }
        if (!binding) {
            errno = ESTALE;
            return -1;
        }

        if (binding->pending_egress.empty ()) {
            binding->actor.node_rid = rid_value (target_node_rid_);
            binding->membership_epoch = membership_epoch_;
            binding->transfer_serial = 0;
            binding->participant_id = 0;
            binding->allowance_messages = 0;
            binding->allowance_bytes = 0;
            binding->pending_bytes = 0;
            binding->pending_egress_bytes = 0;
            binding->acked_high_water = 0;
            binding->terminal_sealed = false;
            binding->pending.clear ();
            service->cv.notify_all ();
            return 0;
        }

        zlink_routing_id_t session_rid;
        memset (&session_rid, 0, sizeof (session_rid));
        session_rid.size = static_cast<uint8_t> (
          std::min (session_key.size (), sizeof (session_rid.data)));
        memcpy (session_rid.data, session_key.data (), session_rid.size);
        queued_record_t *const record =
          binding->pending_egress.front ().get ();
        const zlink_submit_result_t send_result = send_stream_complete (
          service->stream, &session_rid, &record->parts[0],
          record->parts.size (),
          ZLINK_SEND_FLAGS_NONE);
        if (send_result == ZLINK_SUBMIT_OK) {
            binding->pending_egress_bytes -= record->byte_size;
            binding->pending_egress.pop_front ();
            service->cv.notify_all ();
            continue;
        }

        const int failure_errno = errno;
        service->last_error = failure_errno;
        errno = failure_errno;
        return -1;
    }
}

void stream_sessions_abort_actor (mesh_node_t *node_,
                                  const zlink_actor_ref_t &actor_,
                                  uint64_t transfer_serial_)
{
    std::vector<std::unique_ptr<queued_record_t>> restore;
    for_each_actor_binding (node_, actor_, [transfer_serial_, &restore] (
                                          session_service_t *service_, binding_t &binding_) {
        if (binding_.transfer_serial == transfer_serial_) {
            while (!binding_.pending.empty ()) {
                restore.push_back (std::move (binding_.pending.front ()));
                binding_.pending.pop_front ();
            }
            binding_.transfer_serial = 0;
            binding_.participant_id = 0;
            binding_.allowance_messages = 0;
            binding_.allowance_bytes = 0;
            binding_.pending_bytes = 0;
            binding_.pending_egress_bytes = 0;
            binding_.acked_high_water = 0;
            binding_.terminal_sealed = false;
            binding_.pending_egress.clear ();
            service_->cv.notify_all ();
        }
    });
    const owner_id_t owner = actor_owner (actor_.actor_id, actor_.generation);
    for (size_t i = 0; i < restore.size (); ++i)
        (void) admit_record (node_, owner, domain_application, restore[i], false, 0);
}
}
}
