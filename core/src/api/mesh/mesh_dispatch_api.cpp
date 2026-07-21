/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/mesh/mesh_c_internal.hpp"
#include "services/mesh/mesh_wire.hpp"

#include "utils/err.hpp"
#include "utils/macros.hpp"

#include <string.h>

using namespace zlink::mesh;

#ifdef ZLINK_BUILD_TESTS
namespace
{
std::atomic<int> g_force_reply_wire_alloc_fault (0);
std::atomic<int> g_reply_wire_pause_before_send (0);
std::atomic<int> g_reply_wire_before_send_paused (0);
std::atomic<int> g_reply_wire_submit_failure (0);
}

extern "C" void zlink_test_set_mesh_force_reply_wire_alloc_fault (int enabled_)
{
    g_force_reply_wire_alloc_fault.store (
      enabled_ != 0 ? 1 : 0, std::memory_order_release);
}

extern "C" int zlink_test_mesh_force_reply_wire_alloc_fault_pending ()
{
    return g_force_reply_wire_alloc_fault.load (std::memory_order_acquire);
}

extern "C" void zlink_test_mesh_pause_reply_wire_before_send (int enabled_)
{
    g_reply_wire_pause_before_send.store (
      enabled_ != 0 ? 1 : 0, std::memory_order_release);
    if (enabled_ == 0)
        g_reply_wire_before_send_paused.store (0, std::memory_order_release);
}

extern "C" int zlink_test_mesh_reply_wire_before_send_paused ()
{
    return g_reply_wire_before_send_paused.load (std::memory_order_acquire);
}

extern "C" void zlink_test_mesh_fail_next_reply_wire_submit (int enabled_)
{
    g_reply_wire_submit_failure.store (
      enabled_ != 0 ? 1 : 0, std::memory_order_release);
}
#endif

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

void seal_instance_token (mesh_node_t *node_,
                          uint64_t serial_,
                          zlink_instance_spot_activation_token_t *out_)
{
    memset (out_, 0, sizeof (*out_));
    out_->opaque[0] = reinterpret_cast<uintptr_t> (node_);
    out_->opaque[1] = node_->lifecycle_generation;
    out_->opaque[2] = serial_;
    out_->opaque[3] = 0x49535054; //  'ISPT'
}

int unseal_instance_token (const zlink_instance_spot_activation_token_t *token_,
                           mesh_node_t **node_out_,
                           uint64_t *serial_out_)
{
    if (!token_ || token_->opaque[3] != 0x49535054 || token_->opaque[2] == 0) {
        errno = EINVAL;
        return -1;
    }
    *node_out_ = reinterpret_cast<mesh_node_t *> (
      static_cast<uintptr_t> (token_->opaque[0]));
    *serial_out_ = token_->opaque[2];
    return 0;
}
}
}

namespace
{
//  Claims carry the owner key inline in a side table indexed by serial so the
//  32-byte public claim stays opaque while owner keys can exceed it. Serials
//  are allocated from one process-wide counter so the table cannot collide
//  across MeshNodes, and the storage is immortal so a release racing static
//  destruction stays a safe no-op.
struct claim_key_table_t
{
    std::mutex mutex;
    std::map<uint64_t, owner_id_t> keys;
    std::atomic<uint64_t> next_serial;
    claim_key_table_t () : next_serial (1) {}
};

claim_key_table_t &claim_keys ()
{
    static claim_key_table_t *instance = new claim_key_table_t ();
    return *instance;
}

uint64_t next_claim_serial ()
{
    return claim_keys ().next_serial.fetch_add (1);
}

void remember_claim_key (uint64_t serial_, const owner_id_t &owner_)
{
    claim_key_table_t &table = claim_keys ();
    std::lock_guard<std::mutex> lock (table.mutex);
    table.keys[serial_] = owner_;
}

int recall_claim_key (uint64_t serial_, owner_id_t *owner_out_)
{
    claim_key_table_t &table = claim_keys ();
    std::lock_guard<std::mutex> lock (table.mutex);
    std::map<uint64_t, owner_id_t>::iterator it = table.keys.find (serial_);
    if (it == table.keys.end ()) {
        errno = ESTALE;
        return -1;
    }
    *owner_out_ = it->second;
    return 0;
}

void forget_claim_key (uint64_t serial_)
{
    claim_key_table_t &table = claim_keys ();
    std::lock_guard<std::mutex> lock (table.mutex);
    table.keys.erase (serial_);
}

//  Releases one claim body against its (possibly destroyed) node.
zlink_close_result_t release_claim_body (const claim_body_t &body_)
{
    owner_id_t owner;
    if (recall_claim_key (body_.serial, &owner) != 0)
        return ZLINK_CLOSE_INVALID_HANDLE;

    mesh_node_pin_t node_pin (body_.node);

    mesh_node_t *node = node_pin.get ();
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
    if (owner.kind == owner_spot && !rearm)
        maybe_end_spot_locked (node, owner.key);
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
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = EFAULT;
        return ZLINK_HANDLER_INVALID_HANDLE;
    }
    std::unique_lock<std::mutex> lock (node->mutex);
    //  Re-entry from inside the handler is EDEADLK; a call from any other
    //  thread completes only after the callback that already started has
    //  returned (spec 02-dispatch).
    if (node->ready_handler_depth > 0
        && node->ready_handler_thread == std::this_thread::get_id ()) {
        errno = EDEADLK;
        return ZLINK_HANDLER_DEADLOCK;
    }
    while (node->ready_handler_depth > 0)
        node->cv.wait (lock);
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
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
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
            //  A running Spot timer handler owns this generation's turn; the
            //  ready entry stays armed for after the handler returns.
            if (domain == domain_application && owner_it->second.timer_turn_active) {
                ++it;
                continue;
            }

            mailbox.claimed = true;
            mailbox.claim_serial = next_claim_serial ();

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
    mesh_node_pin_t node_pin (body.node);
    mesh_node_t *node = node_pin.get ();
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
    bool application_capacity_recovered = false;
    while (!mailbox.records.empty () && batch->records.size () < batch->message_capacity) {
        const queued_record_t &head = *mailbox.records.front ();
        if (used_parts + head.parts.size () > batch->part_capacity
            || used_bytes + head.byte_size > batch->byte_capacity)
            break;

        std::unique_ptr<queued_record_t> record = std::move (mailbox.records.front ());
        mailbox.records.pop_front ();
        mailbox.pending_messages -= 1;
        mailbox.pending_bytes -= record->byte_size;
        if (body.domain == domain_application)
            application_capacity_recovered = true;

        zlink_mesh_receive_record_t view;
        memset (&view, 0, sizeof (view));
        view.struct_size = sizeof (view);
        view.version = 1;
        view.kind = record->kind;
        view.domain = body.domain == domain_application ? ZLINK_MESH_READY_APPLICATION
                                                        : ZLINK_MESH_READY_INFRASTRUCTURE;
        view.source_node_rid = rid_value (record->source_node_rid);
        view.source_spot_rid = rid_value (record->source_spot_rid);
        view.source_binding_generation = record->source_binding_generation;
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

    if (application_capacity_recovered)
        node->cv.notify_all ();

    batch->busy.store (false);
    lock.unlock ();
    if (application_capacity_recovered)
        notify_local_send_ready (node, owner);
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
namespace
{
int deliver_reply_route_attempt (mesh_node_t *node_,
                                 uint64_t serial_,
                                 int32_t terminal_result_,
                                 int32_t failure_errno_,
                                 std::vector<zlink_msg_t> *parts_,
                                 bool reservation_held_,
                                 bool forced_attempt_);

bool finish_reply_route_attempt (
  mesh_node_t *node_,
  uint64_t serial_,
  bool succeeded_,
  bool forced_attempt_,
  int32_t *force_result_out_,
  int32_t *force_errno_out_)
{
    std::lock_guard<std::mutex> lock (node_->mutex);
    std::unordered_map<uint64_t, reply_route_t>::iterator it =
      node_->reply_routes.find (serial_);
    if (it == node_->reply_routes.end ())
        return false;
    reply_route_t &route = it->second;
    if (succeeded_) {
        route.in_flight = false;
        route.consumed = true;
        route.force_terminal_pending = false;
        return false;
    }
    if (forced_attempt_) {
        //  Keep the terminal durable. ROUTER send-ready advances a retry
        //  epoch and retries each pending route at most once per edge.
        route.in_flight = false;
        return false;
    }
    if (!route.force_terminal_pending) {
        route.in_flight = false;
        return false;
    }
    *force_result_out_ = route.force_terminal_result;
    *force_errno_out_ = route.force_terminal_errno;
    route.force_terminal_pending = false;
    //  Transfer the existing reservation to the forced terminal. Keeping
    //  in_flight set prevents a retry from entering between the failed reply
    //  and the terminal attempt.
    return true;
}

void deliver_deferred_force_terminal (mesh_node_t *node_,
                                      uint64_t serial_,
                                      int32_t terminal_result_,
                                      int32_t failure_errno_)
{
    std::vector<zlink_msg_t> empty;
    (void) deliver_reply_route_attempt (
      node_, serial_, terminal_result_, failure_errno_, &empty, true, true);
}

int deliver_reply_route_attempt (mesh_node_t *node_,
                                 uint64_t serial_,
                                 int32_t terminal_result_,
                                 int32_t failure_errno_,
                                 std::vector<zlink_msg_t> *parts_,
                                 bool reservation_held_,
                                 bool forced_attempt_)
try
{
    zlink_mesh_operation_id_t local_operation_id;
    memset (&local_operation_id, 0, sizeof (local_operation_id));
    bool remote = false;
    bool transfer_relay = false;
    rid_bytes_t remote_origin;
    uint64_t remote_generation = 0;
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
        if (reservation_held_) {
            if (!route.in_flight) {
                errno = ESTALE;
                return -1;
            }
        } else {
            if (route.in_flight) {
                errno = EBUSY;
                return -1;
            }
            route.in_flight = true;
        }
        if (route.remote_origin) {
            remote = true;
            remote_origin = route.origin_rid;
            remote_generation = route.origin_generation;
            remote_correlation = route.origin_correlation;
            transfer_relay =
              route.kind == reply_route_t::kind_transfer_relay;
        } else {
            std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
              node_->operations.find (route.operation_id.low);
            if (op_it == node_->operations.end ()) {
                route.in_flight = false;
                route.consumed = true;
                route.force_terminal_pending = false;
                return 0; //  Requester already completed: drop by contract.
            }
            local_operation_id = op_it->second.id;
            deliver_local = true;
        }
    }
    if (remote) {
#ifdef ZLINK_BUILD_TESTS
        if (forced_attempt_
            && g_force_reply_wire_alloc_fault.exchange (
                 0, std::memory_order_acq_rel)
                 != 0)
            throw std::bad_alloc ();
#endif
        uint64_t expected_connection_id = 0;
        if (remote_generation != 0
            && !validate_remote_route_flight (
              node_, remote_origin, remote_generation,
              &expected_connection_id)) {
            int32_t ignored_result = 0;
            int32_t ignored_errno = 0;
            (void) finish_reply_route_attempt (
              node_, serial_, true, forced_attempt_, &ignored_result,
              &ignored_errno);
            return 0;
        }
#ifdef ZLINK_BUILD_TESTS
        if (!forced_attempt_
            && g_reply_wire_pause_before_send.load (
                 std::memory_order_acquire)
                 != 0) {
            g_reply_wire_before_send_paused.store (
              1, std::memory_order_release);
            while (g_reply_wire_pause_before_send.load (
                     std::memory_order_acquire)
                   != 0)
                std::this_thread::yield ();
            g_reply_wire_before_send_paused.store (
              0, std::memory_order_release);
        }
        const bool fail_wire_submit =
          !forced_attempt_
          && g_reply_wire_submit_failure.exchange (
               0, std::memory_order_acq_rel)
               != 0;
#else
        const bool fail_wire_submit = false;
#endif
        if (fail_wire_submit)
            errno = ENOTCONN;
        const zlink_submit_result_t rc =
          fail_wire_submit
            ? ZLINK_SUBMIT_NOT_CONNECTED
          : transfer_relay
            ? wire_submit_reply_relay (
                node_, remote_origin, remote_correlation, terminal_result_,
                failure_errno_, parts_->empty () ? NULL : &(*parts_)[0],
                parts_->size (), expected_connection_id)
            : wire_submit_reply (
                node_, remote_origin, remote_correlation, terminal_result_,
                failure_errno_, parts_->empty () ? NULL : &(*parts_)[0],
                parts_->size (), expected_connection_id);
        int32_t force_result = 0;
        int32_t force_errno = 0;
        const bool force = finish_reply_route_attempt (
          node_, serial_, rc == ZLINK_SUBMIT_OK, forced_attempt_,
          &force_result, &force_errno);
        if (force)
            deliver_deferred_force_terminal (
              node_, serial_, force_result, force_errno);
        return rc == ZLINK_SUBMIT_OK ? 0 : -1;
    }
    if (deliver_local) {
        errno = 0;
        const int rc = complete_pending_operation_by_id (
          node_, local_operation_id, terminal_result_, failure_errno_, NULL,
          parts_->empty () ? NULL : parts_);
        const bool completion_busy = rc == 0 && errno == EBUSY;
        int32_t force_result = 0;
        int32_t force_errno = 0;
        const bool force = finish_reply_route_attempt (
          node_, serial_, rc >= 0 && !completion_busy, forced_attempt_,
          &force_result, &force_errno);
        if (force)
            deliver_deferred_force_terminal (
              node_, serial_, force_result, force_errno);
        return rc < 0 || completion_busy ? -1 : 0;
    }
    return 0;
}
catch (const std::bad_alloc &) {
    //  Route reservation is acquired before the remote envelope is
    //  allocated. Restore the reservation on allocation failure so a
    //  normal reply remains retryable and a forced terminal remains
    //  durable for the next send-ready or lifecycle retry.
    std::lock_guard<std::mutex> lock (node_->mutex);
    std::unordered_map<uint64_t, reply_route_t>::iterator route =
      node_->reply_routes.find (serial_);
    if (route != node_->reply_routes.end () && !route->second.consumed) {
        route->second.in_flight = false;
        if (forced_attempt_) {
            route->second.force_terminal_pending = true;
            route->second.force_terminal_result = terminal_result_;
            route->second.force_terminal_errno = failure_errno_;
        }
    }
    errno = ENOMEM;
    return -1;
}
}

//  Relay delivery for transferred requests: consumes the route serial and
//  forwards the reply parts to the original requester (local completion or
//  another wire hop). Takes ownership of parts_ on success.
int deliver_reply_via_route (mesh_node_t *node_,
                             uint64_t serial_,
                             int32_t terminal_result_,
                             int32_t failure_errno_,
                             std::vector<zlink_msg_t> *parts_)
{
    return deliver_reply_route_attempt (
      node_, serial_, terminal_result_, failure_errno_, parts_, false, false);
}

int force_reply_via_route (mesh_node_t *node_,
                           uint64_t serial_,
                           int32_t terminal_result_,
                           int32_t failure_errno_)
{
    bool deliver_now = false;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        std::unordered_map<uint64_t, reply_route_t>::iterator it =
          node_->reply_routes.find (serial_);
        if (it == node_->reply_routes.end () || it->second.consumed)
            return 0;
        reply_route_t &route = it->second;
        route.force_terminal_pending = true;
        route.force_terminal_result = terminal_result_;
        route.force_terminal_errno = failure_errno_;
        if (!route.in_flight) {
            route.in_flight = true;
            deliver_now = true;
        }
    }
    if (!deliver_now)
        return 0;
    std::vector<zlink_msg_t> empty;
    return deliver_reply_route_attempt (
      node_, serial_, terminal_result_, failure_errno_, &empty, true, true);
}

void retry_forced_reply_routes (mesh_node_t *node_)
{
    struct retry_scope_t
    {
        explicit retry_scope_t (mesh_node_t *node__) :
            node (node__), armed (true)
        {
        }
        ~retry_scope_t ()
        {
            if (!armed)
                return;
            std::lock_guard<std::mutex> lock (node->mutex);
            node->forced_reply_retry_active = false;
        }
        mesh_node_t *node;
        bool armed;
    };

    uint64_t epoch = 0;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        if (node_->forced_reply_retry_active)
            return;
        node_->forced_reply_retry_active = true;
        epoch = ++node_->force_retry_epoch;
        if (epoch == 0)
            epoch = ++node_->force_retry_epoch;
    }
    retry_scope_t retry_scope (node_);
    while (true) {
        uint64_t serial = 0;
        int32_t terminal_result = 0;
        int32_t failure_errno = 0;
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            for (std::unordered_map<uint64_t, reply_route_t>::iterator route =
                   node_->reply_routes.begin ();
                 route != node_->reply_routes.end (); ++route) {
                reply_route_t &reply = route->second;
                if (reply.force_terminal_pending && !reply.in_flight
                    && reply.force_retry_epoch != epoch) {
                    reply.force_retry_epoch = epoch;
                    serial = route->first;
                    terminal_result = reply.force_terminal_result;
                    failure_errno = reply.force_terminal_errno;
                    break;
                }
            }
        }
        if (serial == 0) {
            std::lock_guard<std::mutex> lock (node_->mutex);
            node_->forced_reply_retry_active = false;
            retry_scope.armed = false;
            return;
        }
        try {
            (void) force_reply_via_route (
              node_, serial, terminal_result, failure_errno);
        }
        catch (const std::bad_alloc &) {
            std::lock_guard<std::mutex> lock (node_->mutex);
            std::unordered_map<uint64_t, reply_route_t>::iterator route =
              node_->reply_routes.find (serial);
            if (route != node_->reply_routes.end ()
                && !route->second.consumed)
                route->second.in_flight = false;
        }
    }
}

#ifdef ZLINK_BUILD_TESTS
extern "C" int zlink_test_mesh_force_reply_token (
  const zlink_mesh_reply_token_t *token_, int32_t terminal_result_,
  int32_t failure_errno_)
{
    mesh_node_t *raw_node = NULL;
    uint64_t serial = 0;
    if (unseal_reply_token (token_, &raw_node, &serial) != 0)
        return -1;
    mesh_node_pin_t pin (raw_node);
    mesh_node_t *node = pin.get ();
    if (!node)
        return -1;
    return force_reply_via_route (
      node, serial, terminal_result_, failure_errno_);
}

extern "C" int zlink_test_mesh_deferred_force_reply_token (
  const zlink_mesh_reply_token_t *token_, int32_t terminal_result_,
  int32_t failure_errno_)
{
    mesh_node_t *raw_node = NULL;
    uint64_t serial = 0;
    if (unseal_reply_token (token_, &raw_node, &serial) != 0)
        return -1;
    mesh_node_pin_t pin (raw_node);
    mesh_node_t *node = pin.get ();
    if (!node)
        return -1;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, reply_route_t>::iterator route =
          node->reply_routes.find (serial);
        if (route == node->reply_routes.end () || route->second.consumed)
            return -1;
        route->second.in_flight = true;
        route->second.force_terminal_pending = true;
        route->second.force_terminal_result = terminal_result_;
        route->second.force_terminal_errno = failure_errno_;
    }
    deliver_deferred_force_terminal (
      node, serial, terminal_result_, failure_errno_);
    return 0;
}

extern "C" int zlink_test_mesh_reply_route_state (
  const zlink_mesh_reply_token_t *token_, int *in_flight_out_,
  int *force_pending_out_, int *consumed_out_)
{
    mesh_node_t *raw_node = NULL;
    uint64_t serial = 0;
    if (unseal_reply_token (token_, &raw_node, &serial) != 0)
        return -1;
    mesh_node_pin_t pin (raw_node);
    mesh_node_t *node = pin.get ();
    if (!node)
        return -1;
    std::lock_guard<std::mutex> lock (node->mutex);
    std::unordered_map<uint64_t, reply_route_t>::const_iterator route =
      node->reply_routes.find (serial);
    if (route == node->reply_routes.end ())
        return -1;
    if (in_flight_out_)
        *in_flight_out_ = route->second.in_flight ? 1 : 0;
    if (force_pending_out_)
        *force_pending_out_ =
          route->second.force_terminal_pending ? 1 : 0;
    if (consumed_out_)
        *consumed_out_ = route->second.consumed ? 1 : 0;
    return 0;
}
#endif
}
}

zlink_submit_result_t zlink_mesh_reply (const zlink_mesh_reply_token_t *token_,
                                        const zlink_msg_t *parts_,
                                        size_t part_count_,
                                        zlink_send_flags_t flags_)
try {
    LIBZLINK_UNUSED (flags_);
    if (!parts_ || part_count_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    mesh_node_t *node_ptr = NULL;
    uint64_t serial = 0;
    if (unseal_reply_token (token_, &node_ptr, &serial) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    mesh_node_pin_t node_pin (node_ptr);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    //  Prepare borrowed payload references before reserving or consuming the
    //  one-shot token. Allocation failure therefore leaves the token and the
    //  requester operation untouched and retryable.
#ifdef ZLINK_BUILD_TESTS
    test_maybe_throw_alloc ();
#endif
    std::vector<zlink_msg_t> reply_parts (part_count_);
    struct reply_parts_guard_t
    {
        explicit reply_parts_guard_t (std::vector<zlink_msg_t> *parts_) :
            parts (parts_), initialized (0)
        {
        }
        ~reply_parts_guard_t ()
        {
            const size_t count = std::min (initialized, parts->size ());
            for (size_t i = 0; i < count; ++i)
                zlink_msg_close (&(*parts)[i]);
        }
        std::vector<zlink_msg_t> *parts;
        size_t initialized;
    } reply_parts_guard (&reply_parts);
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_init (&reply_parts[i]);
        reply_parts_guard.initialized += 1;
        if (zlink_msg_copy (&reply_parts[i],
                            const_cast<zlink_msg_t *> (&parts_[i]))
            != 0) {
            errno = EFAULT;
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        }
    }

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
        if (route.requester_node_generation != node->lifecycle_generation) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
    }
    const int delivery_rc = deliver_reply_route_attempt (
      node, serial, ZLINK_REQUEST_OK, 0, &reply_parts, false, false);
    if (delivery_rc == 0)
        return ZLINK_SUBMIT_OK;
    if (errno == ENOMEM)
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    if (errno == EBUSY || errno == EALREADY || errno == ESTALE)
        return ZLINK_SUBMIT_INVALID_STATE;
    return ZLINK_SUBMIT_INTERNAL_ERROR;
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}
