/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/mesh/mesh_c_internal.hpp"
#include "services/mesh/mesh_wire.hpp"

#include "utils/err.hpp"
#include "utils/macros.hpp"

#include <string.h>

using namespace zlink::mesh;

namespace zlink
{
namespace mesh
{
//  Claims and reply tokens seal a pointer plus serial numbers. The live-node
//  registry keeps stale pointers from ever being dereferenced.
void seal_claim (const claim_body_t &body_, zlink_mesh_claim_t *out_)
{
    memset (out_, 0, sizeof (*out_));
    out_->opaque[0] = reinterpret_cast<uintptr_t> (body_.node);
    out_->opaque[1] = body_.node_generation;
    out_->opaque[2] = body_.serial;
    out_->opaque[3] = (static_cast<uint64_t> (body_.owner.kind) << 32)
                      | static_cast<uint32_t> (body_.domain);
}

int unseal_claim (const zlink_mesh_claim_t *claim_, claim_body_t *out_)
{
    if (!claim_) {
        errno = EFAULT;
        return -1;
    }
    out_->node = reinterpret_cast<mesh_node_t *> (static_cast<uintptr_t> (claim_->opaque[0]));
    out_->node_generation = claim_->opaque[1];
    out_->serial = claim_->opaque[2];
    out_->owner.kind = static_cast<owner_kind_t> (claim_->opaque[3] >> 32);
    out_->domain = static_cast<domain_t> (claim_->opaque[3] & 0xffffffffu);
    if (!out_->node || out_->serial == 0) {
        errno = ESTALE;
        return -1;
    }
    return 0;
}

void seal_reply_token (mesh_node_t *node_, uint64_t serial_, zlink_mesh_reply_token_t *out_)
{
    memset (out_, 0, sizeof (*out_));
    out_->opaque[0] = reinterpret_cast<uintptr_t> (node_);
    out_->opaque[1] = node_->lifecycle_generation;
    out_->opaque[2] = serial_;
    out_->opaque[3] = 0x524c5054; //  'RLPT' marker
}

int unseal_reply_token (const zlink_mesh_reply_token_t *token_,
                        mesh_node_t **node_out_,
                        uint64_t *serial_out_)
{
    if (!token_ || token_->opaque[3] != 0x524c5054 || token_->opaque[2] == 0) {
        errno = EINVAL;
        return -1;
    }
    *node_out_ = reinterpret_cast<mesh_node_t *> (static_cast<uintptr_t> (token_->opaque[0]));
    *serial_out_ = token_->opaque[2];
    return 0;
}
}
}

namespace
{
//  Claims carry the owner key inline in a side table indexed by serial so the
//  32-byte public claim stays opaque while owner keys can exceed it.
std::mutex g_claim_key_mutex;
std::map<uint64_t, owner_id_t> g_claim_keys;

void remember_claim_key (uint64_t serial_, const owner_id_t &owner_)
{
    std::lock_guard<std::mutex> lock (g_claim_key_mutex);
    g_claim_keys[serial_] = owner_;
}

int recall_claim_key (uint64_t serial_, owner_id_t *owner_out_)
{
    std::lock_guard<std::mutex> lock (g_claim_key_mutex);
    std::map<uint64_t, owner_id_t>::iterator it = g_claim_keys.find (serial_);
    if (it == g_claim_keys.end ()) {
        errno = ESTALE;
        return -1;
    }
    *owner_out_ = it->second;
    return 0;
}

void forget_claim_key (uint64_t serial_)
{
    std::lock_guard<std::mutex> lock (g_claim_key_mutex);
    g_claim_keys.erase (serial_);
}

//  Releases one claim body against its (possibly destroyed) node.
zlink_close_result_t release_claim_body (const claim_body_t &body_)
{
    owner_id_t owner;
    if (recall_claim_key (body_.serial, &owner) != 0)
        return ZLINK_CLOSE_INVALID_HANDLE;

    mesh_node_t *node = as_mesh_node (body_.node);
    if (!node) {
        //  Node already destroyed: claim storage is gone with it.
        forget_claim_key (body_.serial);
        return ZLINK_CLOSE_OK;
    }
    std::unique_lock<std::mutex> lock (node->mutex);
    std::map<owner_id_t, owner_state_t>::iterator it = node->owners.find (owner);
    if (it == node->owners.end ()) {
        forget_claim_key (body_.serial);
        return ZLINK_CLOSE_OK;
    }
    mailbox_t &mailbox = it->second.domains[body_.domain];
    if (!mailbox.claimed || mailbox.claim_serial != body_.serial) {
        errno = ESTALE;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    mailbox.claimed = false;
    mailbox.claim_serial = 0;
    forget_claim_key (body_.serial);
    const bool rearm = !mailbox.records.empty ();
    node->cv.notify_all ();
    lock.unlock ();
    //  Re-arm through the public signalling path so a registered handler
    //  learns about the remaining work in this owner's mailbox.
    if (rearm)
        signal_ready (node, owner, body_.domain);
    return ZLINK_CLOSE_OK;
}
}

//  --- ready handler ---------------------------------------------------------

zlink_handler_result_t zlink_mesh_node_set_ready_handler (void *mesh_node_,
                                                          zlink_mesh_ready_handler_fn handler_,
                                                          void *userdata_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_HANDLER_INVALID_HANDLE;
    }
    std::unique_lock<std::mutex> lock (node->mutex);
    if (node->ready_handler_depth > 0) {
        errno = EDEADLK;
        return ZLINK_HANDLER_DEADLOCK;
    }
    if (handler_ && node->pollin_registered) {
        errno = EBUSY;
        return ZLINK_HANDLER_BUSY;
    }
    if (handler_ && node->ready_handler) {
        errno = EBUSY;
        return ZLINK_HANDLER_BUSY;
    }
    node->ready_handler = handler_;
    node->ready_handler_userdata = userdata_;
    return ZLINK_HANDLER_OK;
}

//  Level-triggered poller emulation: consume the pending signal byte and
//  re-signal while claimable work remains. Caller holds the node mutex.
static void rearm_pollin_signaler_locked (mesh_node_t *node_)
{
    if (!node_->pollin_registered || !node_->pollin_signaled
        || !node_->ready_signaler.valid ())
        return;
    (void) node_->ready_signaler.recv_failable ();
    if (!node_->ready.empty ())
        node_->ready_signaler.send ();
    else
        node_->pollin_signaled = false;
}

//  --- ready batch -------------------------------------------------------------

void *zlink_mesh_ready_batch_new (size_t record_capacity_)
{
    if (record_capacity_ == 0) {
        errno = EINVAL;
        return NULL;
    }
    ready_batch_t *batch = new (std::nothrow) ready_batch_t (record_capacity_);
    if (!batch) {
        errno = ENOMEM;
        return NULL;
    }
    batch->records.reserve (record_capacity_);
    batch->claims.reserve (record_capacity_);
    return batch;
}

zlink_config_result_t zlink_mesh_ready_batch_reset (void *batch_)
{
    ready_batch_t *batch = as_ready_batch (batch_);
    if (!batch) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    bool expected = false;
    if (!batch->busy.compare_exchange_strong (expected, true)) {
        errno = EBUSY;
        return ZLINK_CONFIG_BUSY;
    }
    for (size_t i = 0; i < batch->claims.size (); ++i) {
        if (!batch->claim_taken[i])
            (void) release_claim_body (batch->claims[i]);
    }
    batch->records.clear ();
    batch->claims.clear ();
    batch->claim_taken.clear ();
    batch->busy.store (false);
    return ZLINK_CONFIG_OK;
}

zlink_close_result_t zlink_mesh_ready_batch_destroy (void **batch_p_)
{
    if (!batch_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    ready_batch_t *batch = as_ready_batch (*batch_p_);
    if (!batch) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    for (size_t i = 0; i < batch->claims.size (); ++i) {
        if (!batch->claim_taken[i])
            (void) release_claim_body (batch->claims[i]);
    }
    batch->tag = 0xdeadbeef;
    delete batch;
    *batch_p_ = NULL;
    return ZLINK_CLOSE_OK;
}

size_t zlink_mesh_ready_batch_count (const void *batch_)
{
    ready_batch_t *batch = as_ready_batch (const_cast<void *> (batch_));
    return batch ? batch->records.size () : 0;
}

const zlink_mesh_ready_record_t *zlink_mesh_ready_batch_data (const void *batch_)
{
    ready_batch_t *batch = as_ready_batch (const_cast<void *> (batch_));
    if (!batch || batch->records.empty ())
        return NULL;
    return &batch->records[0];
}

zlink_recv_result_t zlink_mesh_node_drain_ready (void *mesh_node_,
                                                 zlink_mesh_ready_domain_mask_t domains_,
                                                 void *batch_,
                                                 uint32_t *has_residue_out_,
                                                 zlink_recv_flags_t flags_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    ready_batch_t *batch = as_ready_batch (batch_);
    if (!node || !batch || !has_residue_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    if (domains_ == ZLINK_MESH_READY_NONE || (domains_ & ~ZLINK_MESH_READY_ALL) != 0) {
        errno = EINVAL;
        return ZLINK_RECV_INVALID_STATE;
    }
    bool expected = false;
    if (!batch->busy.compare_exchange_strong (expected, true)) {
        errno = EBUSY;
        return ZLINK_RECV_BUSY;
    }
    if (!batch->records.empty ()) {
        batch->busy.store (false);
        errno = EBUSY;
        return ZLINK_RECV_BUSY;
    }

    std::unique_lock<std::mutex> lock (node->mutex);
    const bool blocking = (flags_ & ZLINK_RECV_FLAGS_DONTWAIT) == 0;
    const int rcvtimeo = node->rcvtimeo_ms;
    const uint64_t deadline = blocking && rcvtimeo > 0 ? now_ms () + rcvtimeo : 0;

    while (true) {
        //  Collect claimable owners in the requested domains.
        for (std::set<std::pair<owner_id_t, int>>::iterator it = node->ready.begin ();
             it != node->ready.end () && batch->records.size () < batch->capacity;) {
            const owner_id_t &owner = it->first;
            const domain_t domain = static_cast<domain_t> (it->second);
            const zlink_mesh_ready_domain_mask_t bit = domain == domain_application
                                                         ? ZLINK_MESH_READY_APPLICATION
                                                         : ZLINK_MESH_READY_INFRASTRUCTURE;
            if ((domains_ & bit) == 0) {
                ++it;
                continue;
            }
            std::map<owner_id_t, owner_state_t>::iterator owner_it = node->owners.find (owner);
            if (owner_it == node->owners.end ()) {
                node->ready.erase (it++);
                continue;
            }
            mailbox_t &mailbox = owner_it->second.domains[domain];
            if (mailbox.claimed || mailbox.records.empty ()) {
                node->ready.erase (it++);
                continue;
            }
            //  Transfer fence: the frozen application lane is not claimable.
            if (domain == domain_application
                && owner_it->second.fenced_transfer_serial != 0) {
                node->ready.erase (it++);
                continue;
            }

            mailbox.claimed = true;
            mailbox.claim_serial = node->next_claim_serial++;

            zlink_mesh_ready_record_t record;
            memset (&record, 0, sizeof (record));
            record.struct_size = sizeof (record);
            record.version = 1;
            record.owner_kind = static_cast<zlink_mesh_owner_kind_t> (owner.kind);
            record.domain = bit;
            record.spot_rid = owner_it->second.spot_rid;
            record.actor = owner_it->second.actor;

            claim_body_t body;
            body.node = node;
            body.node_generation = node->lifecycle_generation;
            body.owner = owner;
            body.domain = domain;
            body.serial = mailbox.claim_serial;
            remember_claim_key (body.serial, owner);

            batch->records.push_back (record);
            batch->claims.push_back (body);
            batch->claim_taken.push_back (false);
            node->ready.erase (it++);
        }

        if (!batch->records.empty ())
            break;
        if (!blocking) {
            rearm_pollin_signaler_locked (node);
            batch->busy.store (false);
            errno = EAGAIN;
            return ZLINK_RECV_NO_DATA;
        }
        if (node->state == ZLINK_MESH_NODE_STOPPED) {
            batch->busy.store (false);
            errno = ESHUTDOWN;
            return ZLINK_RECV_INVALID_STATE;
        }
        const uint64_t now = now_ms ();
        if (deadline != 0 && now >= deadline) {
            batch->busy.store (false);
            errno = ETIMEDOUT;
            return ZLINK_RECV_NO_DATA;
        }
        node->cv.wait_for (lock,
                           std::chrono::milliseconds (deadline != 0 ? deadline - now : 100));
    }

    //  Residue: any claimable ready entry left in the requested domains.
    uint32_t residue = 0;
    for (std::set<std::pair<owner_id_t, int>>::iterator it = node->ready.begin ();
         it != node->ready.end (); ++it) {
        const zlink_mesh_ready_domain_mask_t bit = it->second == domain_application
                                                     ? ZLINK_MESH_READY_APPLICATION
                                                     : ZLINK_MESH_READY_INFRASTRUCTURE;
        if (domains_ & bit) {
            residue = 1;
            break;
        }
    }
    *has_residue_out_ = residue;
    rearm_pollin_signaler_locked (node);
    batch->busy.store (false);
    return ZLINK_RECV_OK;
}

zlink_config_result_t
zlink_mesh_ready_batch_take_claim (void *batch_, size_t index_, zlink_mesh_claim_t *claim_out_)
{
    ready_batch_t *batch = as_ready_batch (batch_);
    if (!batch || !claim_out_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (index_ >= batch->records.size ()) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (batch->claim_taken[index_]) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    batch->claim_taken[index_] = true;
    seal_claim (batch->claims[index_], claim_out_);
    return ZLINK_CONFIG_OK;
}

zlink_close_result_t zlink_mesh_claim_release (zlink_mesh_claim_t *claim_)
{
    claim_body_t body;
    if (unseal_claim (claim_, &body) != 0)
        return ZLINK_CLOSE_INVALID_HANDLE;
    const zlink_close_result_t rc = release_claim_body (body);
    if (rc == ZLINK_CLOSE_OK)
        memset (claim_, 0, sizeof (*claim_));
    return rc;
}

//  --- receive batch -------------------------------------------------------------

void *zlink_mesh_receive_batch_new (size_t message_capacity_,
                                    size_t part_capacity_,
                                    size_t byte_capacity_)
{
    if (message_capacity_ == 0 || part_capacity_ == 0 || byte_capacity_ == 0) {
        errno = EINVAL;
        return NULL;
    }
    receive_batch_t *batch =
      new (std::nothrow) receive_batch_t (message_capacity_, part_capacity_, byte_capacity_);
    if (!batch) {
        errno = ENOMEM;
        return NULL;
    }
    batch->records.reserve (message_capacity_);
    batch->parts.reserve (part_capacity_);
    return batch;
}

zlink_config_result_t zlink_mesh_receive_batch_reset (void *batch_)
{
    receive_batch_t *batch = as_receive_batch (batch_);
    if (!batch) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    bool expected = false;
    if (!batch->busy.compare_exchange_strong (expected, true)) {
        errno = EBUSY;
        return ZLINK_CONFIG_BUSY;
    }
    batch->clear ();
    batch->busy.store (false);
    return ZLINK_CONFIG_OK;
}

zlink_close_result_t zlink_mesh_receive_batch_destroy (void **batch_p_)
{
    if (!batch_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    receive_batch_t *batch = as_receive_batch (*batch_p_);
    if (!batch) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    batch->tag = 0xdeadbeef;
    delete batch;
    *batch_p_ = NULL;
    return ZLINK_CLOSE_OK;
}

size_t zlink_mesh_receive_batch_count (const void *batch_)
{
    receive_batch_t *batch = as_receive_batch (const_cast<void *> (batch_));
    return batch ? batch->records.size () : 0;
}

const zlink_mesh_receive_record_t *zlink_mesh_receive_batch_data (const void *batch_)
{
    receive_batch_t *batch = as_receive_batch (const_cast<void *> (batch_));
    if (!batch || batch->records.empty ())
        return NULL;
    return &batch->records[0];
}

size_t zlink_mesh_receive_batch_part_count (const void *batch_)
{
    receive_batch_t *batch = as_receive_batch (const_cast<void *> (batch_));
    return batch ? batch->parts.size () : 0;
}

const zlink_msg_t *zlink_mesh_receive_batch_parts (const void *batch_)
{
    receive_batch_t *batch = as_receive_batch (const_cast<void *> (batch_));
    if (!batch || batch->parts.empty ())
        return NULL;
    return &batch->parts[0];
}

zlink_recv_result_t zlink_mesh_claim_recv_batch (zlink_mesh_claim_t *claim_,
                                                 void *batch_,
                                                 zlink_mesh_receive_requirements_t *required_out_,
                                                 zlink_recv_flags_t flags_)
{
    receive_batch_t *batch = as_receive_batch (batch_);
    if (!batch) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    claim_body_t body;
    if (unseal_claim (claim_, &body) != 0)
        return ZLINK_RECV_INVALID_STATE;
    mesh_node_t *node = as_mesh_node (body.node);
    if (!node) {
        errno = ESHUTDOWN;
        return ZLINK_RECV_INVALID_STATE;
    }

    bool expected = false;
    if (!batch->busy.compare_exchange_strong (expected, true)) {
        errno = EBUSY;
        return ZLINK_RECV_BUSY;
    }
    if (!batch->records.empty ()) {
        batch->busy.store (false);
        errno = EBUSY;
        return ZLINK_RECV_BUSY;
    }

    owner_id_t owner;
    if (recall_claim_key (body.serial, &owner) != 0) {
        batch->busy.store (false);
        errno = ESTALE;
        return ZLINK_RECV_INVALID_STATE;
    }

    std::unique_lock<std::mutex> lock (node->mutex);
    std::map<owner_id_t, owner_state_t>::iterator owner_it = node->owners.find (owner);
    if (owner_it == node->owners.end ()) {
        batch->busy.store (false);
        errno = ESTALE;
        return ZLINK_RECV_INVALID_STATE;
    }
    mailbox_t &mailbox = owner_it->second.domains[body.domain];
    if (!mailbox.claimed || mailbox.claim_serial != body.serial) {
        batch->busy.store (false);
        errno = ESTALE;
        return ZLINK_RECV_INVALID_STATE;
    }
    if (mailbox.revoked) {
        batch->busy.store (false);
        errno = ESHUTDOWN;
        return ZLINK_RECV_INVALID_STATE;
    }

    const bool blocking = (flags_ & ZLINK_RECV_FLAGS_DONTWAIT) == 0;
    while (mailbox.records.empty ()) {
        if (!blocking) {
            rearm_pollin_signaler_locked (node);
            batch->busy.store (false);
            errno = EAGAIN;
            return ZLINK_RECV_NO_DATA;
        }
        node->cv.wait_for (lock, std::chrono::milliseconds (100));
        if (mailbox.revoked || node->state == ZLINK_MESH_NODE_STOPPED) {
            batch->busy.store (false);
            errno = ESHUTDOWN;
            return ZLINK_RECV_INVALID_STATE;
        }
    }

    //  First message must fit; report requirements otherwise.
    {
        const queued_record_t &first = *mailbox.records.front ();
        if (first.parts.size () > batch->part_capacity || first.byte_size > batch->byte_capacity
            || batch->message_capacity < 1) {
            if (required_out_ && check_versioned (required_out_) == 0) {
                required_out_->message_count = 1;
                required_out_->part_count = first.parts.size ();
                required_out_->byte_count = first.byte_size;
            }
            batch->busy.store (false);
            errno = ENOBUFS;
            return ZLINK_RECV_BUFFER_TOO_SMALL;
        }
    }

    size_t used_parts = 0;
    size_t used_bytes = 0;
    while (!mailbox.records.empty () && batch->records.size () < batch->message_capacity) {
        const queued_record_t &head = *mailbox.records.front ();
        if (used_parts + head.parts.size () > batch->part_capacity
            || used_bytes + head.byte_size > batch->byte_capacity)
            break;

        std::unique_ptr<queued_record_t> record = std::move (mailbox.records.front ());
        mailbox.records.pop_front ();
        mailbox.pending_messages -= 1;
        mailbox.pending_bytes -= record->byte_size;

        zlink_mesh_receive_record_t view;
        memset (&view, 0, sizeof (view));
        view.struct_size = sizeof (view);
        view.version = 1;
        view.kind = record->kind;
        view.domain = body.domain == domain_application ? ZLINK_MESH_READY_APPLICATION
                                                        : ZLINK_MESH_READY_INFRASTRUCTURE;
        view.source_node_rid = rid_value (record->source_node_rid);
        view.source_spot_rid = rid_value (record->source_spot_rid);
        view.source_actor = record->source_actor;
        view.operation_id = record->operation_id;
        view.operation_kind = record->operation_kind;
        if (record->has_reply_token)
            view.reply_token = record->reply_token;
        if (!record->channel_name.empty ()) {
            view.channel_name = record->channel_name.c_str ();
            view.channel_name_size = record->channel_name.size ();
        }
        if (!record->topic.empty ()) {
            view.topic = record->topic.c_str ();
            view.topic_size = record->topic.size ();
        }
        if (record->has_metadata) {
            view.application_metadata =
              record->application_metadata.empty () ? NULL : &record->application_metadata[0];
            view.application_metadata_size = record->application_metadata.size ();
        }
        if (!record->kind_data.empty ()) {
            view.kind_data = &record->kind_data[0];
            view.kind_data_size = record->kind_data.size ();
        }
        view.part_offset = batch->parts.size ();
        view.part_count = record->parts.size ();
        view.terminal_result = record->terminal_result;
        view.failure_errno = record->failure_errno;

        for (size_t i = 0; i < record->parts.size (); ++i)
            batch->parts.push_back (record->parts[i]);
        used_parts += record->parts.size ();
        used_bytes += record->byte_size;
        record->parts.clear ();

        batch->records.push_back (view);
        batch->storage.push_back (std::move (record));
    }

    if (mailbox.records.empty ())
        node->ready.erase (std::make_pair (owner, static_cast<int> (body.domain)));

    batch->busy.store (false);
    return ZLINK_RECV_OK;
}

zlink_config_result_t zlink_mesh_receive_batch_retain_message (const void *batch_,
                                                               size_t record_index_,
                                                               zlink_msg_t *parts_out_,
                                                               size_t *part_count_inout_)
{
    receive_batch_t *batch = as_receive_batch (const_cast<void *> (batch_));
    if (!batch || !part_count_inout_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (record_index_ >= batch->records.size ()) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    const zlink_mesh_receive_record_t &record = batch->records[record_index_];
    if (*part_count_inout_ < record.part_count || !parts_out_) {
        *part_count_inout_ = record.part_count;
        errno = ENOBUFS;
        return ZLINK_CONFIG_BUFFER_TOO_SMALL;
    }
    for (size_t i = 0; i < record.part_count; ++i) {
        zlink_msg_init (&parts_out_[i]);
        if (zlink_msg_copy (&parts_out_[i], &batch->parts[record.part_offset + i]) != 0) {
            for (size_t j = 0; j <= i; ++j)
                zlink_msg_close (&parts_out_[j]);
            errno = EFAULT;
            return ZLINK_CONFIG_INTERNAL_ERROR;
        }
    }
    *part_count_inout_ = record.part_count;
    return ZLINK_CONFIG_OK;
}

//  --- reply -----------------------------------------------------------------------

namespace zlink
{
namespace mesh
{
//  Relay delivery for transferred requests: consumes the route serial and
//  forwards the reply parts to the original requester (local completion or
//  another wire hop). Takes ownership of parts_ on success.
int deliver_reply_via_route (mesh_node_t *node_,
                             uint64_t serial_,
                             int32_t terminal_result_,
                             int32_t failure_errno_,
                             std::vector<zlink_msg_t> *parts_)
{
    pending_operation_t op;
    bool remote = false;
    rid_bytes_t remote_origin;
    uint64_t remote_correlation = 0;
    bool deliver_local = false;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        std::unordered_map<uint64_t, reply_route_t>::iterator it =
          node_->reply_routes.find (serial_);
        if (it == node_->reply_routes.end ()) {
            errno = ESTALE;
            return -1;
        }
        reply_route_t &route = it->second;
        if (route.consumed) {
            errno = EALREADY;
            return -1;
        }
        route.consumed = true;
        if (route.remote_origin) {
            remote = true;
            remote_origin = route.origin_rid;
            remote_correlation = route.origin_correlation;
        } else {
            std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
              node_->operations.find (route.operation_id.low);
            if (op_it == node_->operations.end ())
                return 0; //  Requester already completed: drop by contract.
            op = op_it->second;
            node_->operations.erase (op_it);
            deliver_local = true;
        }
    }
    if (remote) {
        const zlink_submit_result_t rc = wire_submit_reply (
          node_, remote_origin, remote_correlation, terminal_result_, failure_errno_,
          parts_->empty () ? NULL : &(*parts_)[0], parts_->size ());
        return rc == ZLINK_SUBMIT_OK ? 0 : -1;
    }
    if (deliver_local)
        complete_operation (node_, op, terminal_result_, failure_errno_, NULL,
                            parts_->empty () ? NULL : parts_);
    return 0;
}
}
}

zlink_submit_result_t zlink_mesh_reply (const zlink_mesh_reply_token_t *token_,
                                        const zlink_msg_t *parts_,
                                        size_t part_count_,
                                        zlink_send_flags_t flags_)
{
    LIBZLINK_UNUSED (flags_);
    if (!parts_ || part_count_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    mesh_node_t *node_ptr = NULL;
    uint64_t serial = 0;
    if (unseal_reply_token (token_, &node_ptr, &serial) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    mesh_node_t *node = as_mesh_node (node_ptr);
    if (!node) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    pending_operation_t op;
    bool remote = false;
    bool transfer_relay = false;
    rid_bytes_t remote_origin;
    uint64_t remote_correlation = 0;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        //  A stopped node has no usable source route left; drains in progress
        //  still accept replies so held claims can finish their turn.
        if (node->state == ZLINK_MESH_NODE_STOPPED) {
            errno = ESHUTDOWN;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        std::unordered_map<uint64_t, reply_route_t>::iterator it = node->reply_routes.find (serial);
        if (it == node->reply_routes.end ()) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        reply_route_t &route = it->second;
        if (route.kind != reply_route_t::kind_generic
            && route.kind != reply_route_t::kind_transfer_relay) {
            errno = EINVAL;
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        }
        if (route.consumed) {
            errno = EALREADY;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        if (route.requester_node_generation != node->lifecycle_generation) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        if (route.remote_origin) {
            //  The requester lives on an admitted peer: consume the token
            //  and answer over the wire with the requester-side correlation.
            route.consumed = true;
            remote = true;
            remote_origin = route.origin_rid;
            remote_correlation = route.origin_correlation;
            transfer_relay = route.kind == reply_route_t::kind_transfer_relay;
        } else {
            std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
              node->operations.find (route.operation_id.low);
            if (op_it == node->operations.end ()) {
                //  The requester timed out or was shut down: consume the
                //  token, drop the reply, and do not produce a second
                //  completion.
                route.consumed = true;
                return ZLINK_SUBMIT_OK;
            }
            op = op_it->second;
            node->operations.erase (op_it);
            route.consumed = true;
        }
    }

    if (remote) {
        if (transfer_relay)
            return wire_submit_reply_relay (node, remote_origin, remote_correlation,
                                            ZLINK_REQUEST_OK, 0, parts_, part_count_);
        return wire_submit_reply (node, remote_origin, remote_correlation, ZLINK_REQUEST_OK, 0,
                                  parts_, part_count_);
    }

    //  Borrowed input: reference the payload without consuming caller parts.
    std::vector<zlink_msg_t> reply_parts (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_init (&reply_parts[i]);
        if (zlink_msg_copy (&reply_parts[i], const_cast<zlink_msg_t *> (&parts_[i])) != 0) {
            for (size_t j = 0; j <= i; ++j)
                zlink_msg_close (&reply_parts[j]);
            errno = EFAULT;
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        }
    }
    complete_operation (node, op, ZLINK_REQUEST_OK, 0, NULL, &reply_parts);
    return ZLINK_SUBMIT_OK;
}
