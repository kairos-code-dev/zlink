/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/mesh/mesh_c_internal.hpp"

#include "api/socket/socket_api_internal.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/err.hpp"
#include "utils/macros.hpp"

#include <string.h>

using namespace zlink::mesh;

//  STREAM session service: owns the relation between one raw STREAM socket
//  and one MeshNode, the per-session actor bindings and the movement
//  barrier. The barrier data plane engages together with the transfer fence.

namespace
{
struct binding_t
{
    zlink_actor_ref_t actor;
    uint64_t binding_generation;
    uint64_t membership_epoch;
};

struct session_service_t
{
    session_service_t () :
        tag (0x4d535353),
        node (NULL),
        stream (NULL),
        state (ZLINK_STREAM_SESSION_CREATED),
        lifecycle_generation (1),
        next_binding_generation (1),
        last_error (0)
    {
    }
    bool check_tag () const { return tag == 0x4d535353; }

    uint32_t tag;
    mesh_node_t *node;
    void *stream;
    std::mutex mutex;
    zlink_stream_session_state_t state;
    uint64_t lifecycle_generation;
    uint64_t next_binding_generation;
    //  session rid bytes -> bindings
    std::map<std::string, std::vector<binding_t>> bindings;
    int32_t last_error;
};

std::mutex g_session_registry_mutex;
std::set<void *> g_live_sessions;
//  Streams already claimed by a service handle (one service per stream).
std::set<void *> g_claimed_streams;

session_service_t *as_session_service (void *handle_)
{
    if (!handle_)
        return NULL;
    {
        std::lock_guard<std::mutex> lock (g_session_registry_mutex);
        if (!g_live_sessions.count (handle_))
            return NULL;
    }
    session_service_t *service = static_cast<session_service_t *> (handle_);
    return service->check_tag () ? service : NULL;
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

//  Delivers a bind/unbind terminal completion to the node infrastructure
//  claim.
void complete_binding_operation (mesh_node_t *node_,
                                 zlink_mesh_operation_kind_t kind_,
                                 const zlink_mesh_operation_id_t &op_id_,
                                 int32_t result_,
                                 int32_t err_)
{
    pending_operation_t op;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator it =
          node_->operations.find (op_id_.low);
        if (it == node_->operations.end ())
            return;
        op = it->second;
        node_->operations.erase (it);
    }
    LIBZLINK_UNUSED (kind_);
    complete_operation (node_, op, result_, err_, NULL, NULL);
}
}

void *zlink_stream_session_service_new (void *mesh_node_, void *stream_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return NULL;
    }
    const socket_handle_t handle = as_socket_handle (stream_);
    if (!handle.socket) {
        errno = EFAULT;
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
    session_service_t *service = new (std::nothrow) session_service_t ();
    if (!service) {
        std::lock_guard<std::mutex> lock (node->mutex);
        node->stream_session_count -= 1;
        errno = ENOMEM;
        return NULL;
    }
    service->node = node;
    service->stream = stream_;
    g_live_sessions.insert (service);
    g_claimed_streams.insert (stream_);
    return service;
}

zlink_config_result_t zlink_stream_session_service_start (void *service_)
{
    session_service_t *service = as_session_service (service_);
    if (!service) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::lock_guard<std::mutex> lock (service->mutex);
    if (service->state != ZLINK_STREAM_SESSION_CREATED) {
        errno = EBUSY;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    service->state = ZLINK_STREAM_SESSION_STARTED;
    return ZLINK_CONFIG_OK;
}

zlink_request_result_t zlink_stream_session_service_shutdown (void *service_, uint32_t timeout_ms_)
{
    LIBZLINK_UNUSED (timeout_ms_);
    session_service_t *service = as_session_service (service_);
    if (!service) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock (service->mutex);
    if (service->state == ZLINK_STREAM_SESSION_STOPPED)
        return ZLINK_REQUEST_OK;
    service->state = ZLINK_STREAM_SESSION_STOPPED;
    return ZLINK_REQUEST_OK;
}

zlink_close_result_t zlink_stream_session_service_destroy (void **service_p_)
{
    if (!service_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    session_service_t *service = as_session_service (*service_p_);
    if (!service) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    mesh_node_t *node = as_mesh_node (service->node);
    if (node) {
        std::lock_guard<std::mutex> lock (node->mutex);
        if (node->stream_session_count > 0)
            node->stream_session_count -= 1;
    }
    {
        std::lock_guard<std::mutex> lock (g_session_registry_mutex);
        g_live_sessions.erase (service);
        g_claimed_streams.erase (service->stream);
    }
    service->tag = 0xdeadbeef;
    delete service;
    *service_p_ = NULL;
    return ZLINK_CLOSE_OK;
}

zlink_config_result_t zlink_stream_session_service_status (void *service_,
                                                           zlink_stream_session_status_t *status_out_)
{
    session_service_t *service = as_session_service (service_);
    if (!service) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (check_versioned (status_out_) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock (service->mutex);
    zlink_stream_session_status_t out;
    init_versioned (&out);
    out.state = service->state;
    out.lifecycle_generation = service->lifecycle_generation;
    out.session_count = service->bindings.size ();
    uint64_t binding_count = 0;
    for (std::map<std::string, std::vector<binding_t>>::const_iterator it =
           service->bindings.begin ();
         it != service->bindings.end (); ++it)
        binding_count += it->second.size ();
    out.binding_count = binding_count;
    out.last_error = service->last_error;
    *status_out_ = out;
    return ZLINK_CONFIG_OK;
}

zlink_submit_result_t zlink_stream_session_bind_actor (void *service_,
                                                       const zlink_routing_id_t *session_rid_,
                                                       const zlink_actor_ref_t *actor_,
                                                       zlink_mesh_operation_id_t *operation_id_out_,
                                                       uint32_t timeout_ms_)
{
    session_service_t *service = as_session_service (service_);
    if (!service) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!session_rid_ || session_rid_->size == 0 || check_actor_ref (actor_) != 0
        || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    mesh_node_t *node = as_mesh_node (service->node);
    if (!node) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    //  Validate the actor generation and membership epoch on this node.
    {
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
    }

    std::unique_lock<std::mutex> lock (service->mutex);
    if (service->state != ZLINK_STREAM_SESSION_STARTED) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_INVALID_STATE;
    }
    const std::string session_key (reinterpret_cast<const char *> (session_rid_->data),
                                   session_rid_->size);
    //  Same actor generation may bind to only one session at a time; the
    //  identical binding is an idempotent success.
    for (std::map<std::string, std::vector<binding_t>>::iterator it = service->bindings.begin ();
         it != service->bindings.end (); ++it) {
        for (size_t i = 0; i < it->second.size (); ++i) {
            const binding_t &binding = it->second[i];
            if (strncmp (binding.actor.actor_id, actor_->actor_id, sizeof (binding.actor.actor_id))
                  == 0
                && binding.actor.generation == actor_->generation) {
                if (it->first == session_key) {
                    const zlink_mesh_operation_id_t op_id = register_operation (
                      node, ZLINK_MESH_OPERATION_STREAM_BIND, node_owner (), timeout_ms_);
                    *operation_id_out_ = op_id;
                    lock.unlock ();
                    complete_binding_operation (node, ZLINK_MESH_OPERATION_STREAM_BIND, op_id,
                                                ZLINK_REQUEST_OK, 0);
                    return ZLINK_SUBMIT_OK;
                }
                errno = EBUSY;
                return ZLINK_SUBMIT_INVALID_STATE;
            }
        }
    }

    binding_t binding;
    binding.actor = *actor_;
    binding.binding_generation = service->next_binding_generation++;
    binding.membership_epoch = 0;
    service->bindings[session_key].push_back (binding);

    const zlink_mesh_operation_id_t op_id =
      register_operation (node, ZLINK_MESH_OPERATION_STREAM_BIND, node_owner (), timeout_ms_);
    *operation_id_out_ = op_id;
    lock.unlock ();
    complete_binding_operation (node, ZLINK_MESH_OPERATION_STREAM_BIND, op_id, ZLINK_REQUEST_OK, 0);
    return ZLINK_SUBMIT_OK;
}

zlink_submit_result_t zlink_stream_session_unbind_actor (void *service_,
                                                         const zlink_routing_id_t *session_rid_,
                                                         const zlink_actor_ref_t *actor_,
                                                         uint64_t expected_binding_generation_,
                                                         zlink_mesh_operation_id_t *operation_id_out_,
                                                         uint32_t timeout_ms_)
{
    session_service_t *service = as_session_service (service_);
    if (!service) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!session_rid_ || session_rid_->size == 0 || check_actor_ref (actor_) != 0
        || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    mesh_node_t *node = as_mesh_node (service->node);
    if (!node) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    std::unique_lock<std::mutex> lock (service->mutex);
    const std::string session_key (reinterpret_cast<const char *> (session_rid_->data),
                                   session_rid_->size);
    std::map<std::string, std::vector<binding_t>>::iterator it = service->bindings.find (session_key);
    if (it != service->bindings.end ()) {
        for (size_t i = 0; i < it->second.size (); ++i) {
            binding_t &binding = it->second[i];
            if (strncmp (binding.actor.actor_id, actor_->actor_id, sizeof (binding.actor.actor_id))
                  == 0
                && binding.actor.generation == actor_->generation) {
                if (binding.binding_generation != expected_binding_generation_) {
                    errno = ESTALE;
                    return ZLINK_SUBMIT_INVALID_STATE;
                }
                it->second.erase (it->second.begin () + i);
                if (it->second.empty ())
                    service->bindings.erase (it);
                const zlink_mesh_operation_id_t op_id = register_operation (
                  node, ZLINK_MESH_OPERATION_STREAM_UNBIND, node_owner (), timeout_ms_);
                *operation_id_out_ = op_id;
                lock.unlock ();
                complete_binding_operation (node, ZLINK_MESH_OPERATION_STREAM_UNBIND, op_id,
                                            ZLINK_REQUEST_OK, 0);
                return ZLINK_SUBMIT_OK;
            }
        }
    }
    //  A missing binding unbinds idempotently only for expected generation 0.
    if (expected_binding_generation_ != 0) {
        errno = ESTALE;
        return ZLINK_SUBMIT_INVALID_STATE;
    }
    const zlink_mesh_operation_id_t op_id =
      register_operation (node, ZLINK_MESH_OPERATION_STREAM_UNBIND, node_owner (), timeout_ms_);
    *operation_id_out_ = op_id;
    lock.unlock ();
    complete_binding_operation (node, ZLINK_MESH_OPERATION_STREAM_UNBIND, op_id, ZLINK_REQUEST_OK,
                                0);
    return ZLINK_SUBMIT_OK;
}

zlink_config_result_t zlink_stream_session_bindings (void *service_,
                                                     const zlink_routing_id_t *session_rid_,
                                                     zlink_stream_session_binding_t *entries_,
                                                     size_t *count_inout_)
{
    session_service_t *service = as_session_service (service_);
    if (!service) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!session_rid_ || session_rid_->size == 0 || !count_inout_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock (service->mutex);
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
    session_service_t *service = as_session_service (service_);
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

    {
        std::lock_guard<std::mutex> lock (service->mutex);
        if (service->state != ZLINK_STREAM_SESSION_STARTED) {
            errno = ESHUTDOWN;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        const std::string session_key (reinterpret_cast<const char *> (session_rid_->data),
                                       session_rid_->size);
        std::map<std::string, std::vector<binding_t>>::iterator it =
          service->bindings.find (session_key);
        bool bound = false;
        if (it != service->bindings.end ()) {
            for (size_t i = 0; i < it->second.size (); ++i) {
                if (strncmp (it->second[i].actor.actor_id, actor_->actor_id,
                             sizeof (it->second[i].actor.actor_id))
                      == 0
                    && it->second[i].actor.generation == actor_->generation) {
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

    //  Local actor route: enqueue into the actor mailbox as a request/send.
    if (operation_id_out_)
        return zlink_mesh_node_request_to_actor (service->node, actor_, metadata_, parts_,
                                                 part_count_, operation_id_out_, flags_,
                                                 timeout_ms_);
    return zlink_mesh_node_send_to_actor (service->node, actor_, metadata_, parts_, part_count_,
                                          flags_);
}
}

zlink_submit_result_t zlink_stream_session_send_to_actor (void *service_,
                                                          const zlink_routing_id_t *session_rid_,
                                                          const zlink_actor_ref_t *actor_,
                                                          const zlink_mesh_metadata_view_t *actor_metadata_,
                                                          const zlink_msg_t *parts_,
                                                          size_t part_count_,
                                                          zlink_send_flags_t flags_)
{
    return session_to_actor_submit (service_, session_rid_, actor_, actor_metadata_, parts_,
                                    part_count_, NULL, flags_, 0);
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
{
    if (!operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    return session_to_actor_submit (service_, session_rid_, actor_, actor_metadata_, parts_,
                                    part_count_, operation_id_out_, flags_, timeout_ms_);
}

zlink_submit_result_t zlink_mesh_node_actor_send_bound_session (void *mesh_node_,
                                                                const zlink_actor_ref_t *actor_,
                                                                const zlink_msg_t *parts_,
                                                                size_t part_count_,
                                                                zlink_send_flags_t flags_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (check_actor_ref (actor_) != 0 || !parts_ || part_count_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    //  Snapshot the current binding across the live services of this node.
    void *stream = NULL;
    zlink_routing_id_t session_rid;
    memset (&session_rid, 0, sizeof (session_rid));
    {
        std::lock_guard<std::mutex> registry_lock (g_session_registry_mutex);
        for (std::set<void *>::iterator it = g_live_sessions.begin (); it != g_live_sessions.end ();
             ++it) {
            session_service_t *service = static_cast<session_service_t *> (*it);
            if (service->node != node)
                continue;
            std::lock_guard<std::mutex> lock (service->mutex);
            for (std::map<std::string, std::vector<binding_t>>::iterator bind_it =
                   service->bindings.begin ();
                 bind_it != service->bindings.end (); ++bind_it) {
                for (size_t i = 0; i < bind_it->second.size (); ++i) {
                    const binding_t &binding = bind_it->second[i];
                    if (strncmp (binding.actor.actor_id, actor_->actor_id,
                                 sizeof (binding.actor.actor_id))
                          == 0
                        && binding.actor.generation == actor_->generation) {
                        stream = service->stream;
                        session_rid.size = static_cast<uint8_t> (
                          std::min (bind_it->first.size (), sizeof (session_rid.data)));
                        memcpy (session_rid.data, bind_it->first.data (), session_rid.size);
                    }
                }
            }
        }
    }
    if (!stream) {
        errno = ENOENT;
        return ZLINK_SUBMIT_NOT_FOUND;
    }

    //  Borrowed input: copy each part before the consuming raw send.
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_t copy;
        zlink_msg_init (&copy);
        if (zlink_msg_copy (&copy, const_cast<zlink_msg_t *> (&parts_[i])) != 0) {
            errno = EFAULT;
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        }
        const zlink_submit_result_t rc =
          zlink_send_part_rid (stream, &session_rid, &copy, flags_, ZLINK_PART_FINAL);
        if (rc != ZLINK_SUBMIT_OK) {
            zlink_msg_close (&copy);
            return rc;
        }
    }
    return ZLINK_SUBMIT_OK;
}

zlink_submit_result_t zlink_mesh_node_actor_close_bound_session (void *mesh_node_,
                                                                 const zlink_actor_ref_t *actor_,
                                                                 uint64_t expected_binding_generation_,
                                                                 zlink_mesh_operation_id_t *operation_id_out_,
                                                                 uint32_t timeout_ms_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (check_actor_ref (actor_) != 0 || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> registry_lock (g_session_registry_mutex);
    for (std::set<void *>::iterator it = g_live_sessions.begin (); it != g_live_sessions.end ();
         ++it) {
        session_service_t *service = static_cast<session_service_t *> (*it);
        if (service->node != node)
            continue;
        std::lock_guard<std::mutex> lock (service->mutex);
        for (std::map<std::string, std::vector<binding_t>>::iterator bind_it =
               service->bindings.begin ();
             bind_it != service->bindings.end (); ++bind_it) {
            for (size_t i = 0; i < bind_it->second.size (); ++i) {
                binding_t &binding = bind_it->second[i];
                if (strncmp (binding.actor.actor_id, actor_->actor_id,
                             sizeof (binding.actor.actor_id))
                      == 0
                    && binding.actor.generation == actor_->generation) {
                    if (binding.binding_generation != expected_binding_generation_) {
                        errno = ESTALE;
                        return ZLINK_SUBMIT_INVALID_STATE;
                    }
                    bind_it->second.erase (bind_it->second.begin () + i);
                    if (bind_it->second.empty ())
                        service->bindings.erase (bind_it);
                    const zlink_mesh_operation_id_t op_id = register_operation (
                      node, ZLINK_MESH_OPERATION_STREAM_CLOSE,
                      actor_owner (actor_->actor_id, actor_->generation), timeout_ms_);
                    *operation_id_out_ = op_id;
                    complete_binding_operation (node, ZLINK_MESH_OPERATION_STREAM_CLOSE, op_id,
                                                ZLINK_REQUEST_OK, 0);
                    return ZLINK_SUBMIT_OK;
                }
            }
        }
    }
    errno = ENOENT;
    return ZLINK_SUBMIT_NOT_FOUND;
}
