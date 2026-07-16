/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/mesh/mesh_runtime.hpp"

#include "core/ctx.hpp"
#include "utils/clock.hpp"
#include "utils/err.hpp"

#include <string.h>
#include <algorithm>

namespace
{
std::mutex g_registry_mutex;
std::map<std::string, zlink::mesh::mesh_node_t *> g_nodes_by_name;
std::set<void *> g_live_nodes;
std::set<void *> g_live_facades;
std::set<void *> g_live_publishers;
std::set<void *> g_live_monitors;

bool valid_utf8 (const unsigned char *data_, size_t size_)
{
    size_t i = 0;
    while (i < size_) {
        const unsigned char c = data_[i];
        size_t extra;
        if (c < 0x80)
            extra = 0;
        else if ((c & 0xE0) == 0xC0 && c >= 0xC2)
            extra = 1;
        else if ((c & 0xF0) == 0xE0)
            extra = 2;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4)
            extra = 3;
        else
            return false;
        if (i + extra + 1 > size_)
            return false;
        for (size_t j = 1; j <= extra; ++j) {
            if ((data_[i + j] & 0xC0) != 0x80)
                return false;
        }
        i += extra + 1;
    }
    return true;
}
}

namespace zlink
{
namespace mesh
{
uint64_t now_ms ()
{
    static clock_t clock;
    return clock.now_ms ();
}

rid_bytes_t rid_bytes (const zlink_routing_id_t &rid_)
{
    return rid_bytes_t (rid_.data, rid_.data + rid_.size);
}

zlink_routing_id_t rid_value (const rid_bytes_t &bytes_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    rid.size = static_cast<uint8_t> (std::min<size_t> (bytes_.size (), sizeof (rid.data)));
    if (rid.size > 0)
        memcpy (rid.data, &bytes_[0], rid.size);
    return rid;
}

bool rid_equal (const zlink_routing_id_t &a_, const zlink_routing_id_t &b_)
{
    return a_.size == b_.size && memcmp (a_.data, b_.data, a_.size) == 0;
}

queued_record_t::queued_record_t () :
    kind (ZLINK_MESH_RECORD_NODE_SEND),
    has_reply_token (false),
    has_metadata (false),
    terminal_result (0),
    failure_errno (0),
    byte_size (0)
{
    memset (&source_actor, 0, sizeof (source_actor));
    memset (&operation_id, 0, sizeof (operation_id));
    operation_kind = static_cast<zlink_mesh_operation_kind_t> (0);
    memset (&reply_token, 0, sizeof (reply_token));
}

queued_record_t::queued_record_t (queued_record_t &&other_) noexcept
{
    *this = std::move (other_);
}

queued_record_t &queued_record_t::operator= (queued_record_t &&other_) noexcept
{
    if (this == &other_)
        return *this;
    for (size_t i = 0; i < parts.size (); ++i)
        zlink_msg_close (&parts[i]);
    kind = other_.kind;
    source_node_rid = std::move (other_.source_node_rid);
    source_spot_rid = std::move (other_.source_spot_rid);
    source_actor = other_.source_actor;
    operation_id = other_.operation_id;
    operation_kind = other_.operation_kind;
    reply_token = other_.reply_token;
    has_reply_token = other_.has_reply_token;
    channel_name = std::move (other_.channel_name);
    topic = std::move (other_.topic);
    application_metadata = std::move (other_.application_metadata);
    has_metadata = other_.has_metadata;
    terminal_result = other_.terminal_result;
    failure_errno = other_.failure_errno;
    kind_data = std::move (other_.kind_data);
    parts = std::move (other_.parts);
    other_.parts.clear ();
    byte_size = other_.byte_size;
    return *this;
}

queued_record_t::~queued_record_t ()
{
    for (size_t i = 0; i < parts.size (); ++i)
        zlink_msg_close (&parts[i]);
}

mailbox_t::mailbox_t () :
    pending_messages (0), pending_bytes (0), claimed (false), claim_serial (0), revoked (false)
{
}

owner_state_t::owner_state_t () : draining (false), fenced_transfer_serial (0)
{
    memset (&actor, 0, sizeof (actor));
    memset (&spot_rid, 0, sizeof (spot_rid));
}

peer_state_t::peer_state_t () :
    intent_id (0),
    source (ZLINK_MESH_PEER_MANUAL),
    state (ZLINK_MESH_PEER_CONFIGURED),
    inbound (false),
    lifecycle_generation (0),
    descriptor_revision (0),
    has_expected_rid (false),
    last_error (0),
    last_changed_ms (0)
{
}

spot_state_t::spot_state_t () :
    generation (0),
    kind (ZLINK_SPOT_KIND_USER),
    facade_count (0),
    timer_count (0),
    active_actor_count (0),
    draining (false),
    publish_nodrop (1),
    last_error (0),
    last_changed_ms (0)
{
}

spot_facade_t::spot_facade_t () : tag (0x4d455348), node (NULL), generation (0)
{
}

actor_state_t::actor_state_t () :
    generation (0), membership_epoch (0), spot_generation (0), draining (false)
{
}

transfer_state_t::transfer_state_t () :
    serial (0),
    role (ZLINK_ACTOR_TRANSFER_SOURCE),
    expected_epoch (0),
    committed_epoch (0),
    node_generation (0),
    phase (ZLINK_ACTOR_TRANSFER_PREPARING),
    final_sequence (0),
    reserve_messages (0),
    reserve_bytes (0),
    deadline_ms (0),
    ready_exchanged (false),
    acked_high_water (0)
{
    memset (&transfer_id, 0, sizeof (transfer_id));
    memset (&actor, 0, sizeof (actor));
}

monitor_state_t::monitor_state_t () :
    tag (0x4d4d4f4e),
    node (NULL),
    mask (0),
    handler (NULL),
    handler_userdata (NULL),
    handler_active (false),
    closed (false)
{
    memset (&counters, 0, sizeof (counters));
    counters.struct_size = sizeof (counters);
    counters.version = 1;
}

publisher_t::publisher_t () : tag (0x4d505542), node (NULL), nodrop (1)
{
}

ready_batch_t::ready_batch_t (size_t capacity_) : tag (0x4d524459), capacity (capacity_), busy (false)
{
}

receive_batch_t::receive_batch_t (size_t message_capacity_,
                                  size_t part_capacity_,
                                  size_t byte_capacity_) :
    tag (0x4d524356),
    message_capacity (message_capacity_),
    part_capacity (part_capacity_),
    byte_capacity (byte_capacity_),
    busy (false)
{
}

receive_batch_t::~receive_batch_t ()
{
    clear ();
}

void receive_batch_t::clear ()
{
    records.clear ();
    parts.clear ();
    storage.clear ();
}

mesh_node_t::mesh_node_t (ctx_t *ctx_) :
    tag (0x4d4e4f44),
    ctx (ctx_),
    state (ZLINK_MESH_NODE_CREATED),
    lifecycle_generation (1),
    descriptor_revision (0),
    last_error (0),
    last_changed_ms (now_ms ()),
    router_hwm_profile (ZLINK_AUTO_HWM_PROFILE_BALANCED),
    router_hwm_override (0),
    mailbox_message_budget (0),
    mailbox_byte_budget (0),
    max_msg_size (-1),
    sndtimeo_ms (1000),
    rcvtimeo_ms (1000),
    next_intent_id (1),
    next_claim_serial (1),
    ready_handler (NULL),
    ready_handler_userdata (NULL),
    ready_handler_depth (0),
    pollin_registered (false),
    pollin_signaled (false),
    next_operation_serial (1),
    next_reply_serial (1),
    next_spot_generation (1),
    next_actor_generation (1),
    next_transfer_serial (1),
    publisher_count (0),
    monitor_count (0),
    stream_session_count (0),
    monitor (NULL),
    router_socket (NULL),
    router_monitor (NULL),
    io_stop (false)
{
}

mesh_node_t::~mesh_node_t ()
{
    tag = 0xdeadbeef;
}

uint64_t mesh_node_t::effective_message_budget () const
{
    if (mailbox_message_budget > 0)
        return mailbox_message_budget;
    switch (router_hwm_profile) {
        case ZLINK_AUTO_HWM_PROFILE_COMPACT:
            return 256;
        case ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY:
            return 1024;
        case ZLINK_AUTO_HWM_PROFILE_THROUGHPUT:
            return 65536;
        case ZLINK_AUTO_HWM_PROFILE_BALANCED:
        default:
            return 8192;
    }
}

uint64_t mesh_node_t::effective_byte_budget () const
{
    if (mailbox_byte_budget > 0)
        return mailbox_byte_budget;
    switch (router_hwm_profile) {
        case ZLINK_AUTO_HWM_PROFILE_COMPACT:
            return 1ull << 20;
        case ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY:
            return 8ull << 20;
        case ZLINK_AUTO_HWM_PROFILE_THROUGHPUT:
            return 256ull << 20;
        case ZLINK_AUTO_HWM_PROFILE_BALANCED:
        default:
            return 64ull << 20;
    }
}

mesh_node_t *find_node_by_name (const std::string &name_)
{
    std::lock_guard<std::mutex> lock (g_registry_mutex);
    std::map<std::string, mesh_node_t *>::iterator it = g_nodes_by_name.find (name_);
    return it != g_nodes_by_name.end () ? it->second : NULL;
}

int register_node (mesh_node_t *node_)
{
    std::lock_guard<std::mutex> lock (g_registry_mutex);
    if (g_nodes_by_name.count (node_->mesh_name)) {
        errno = EEXIST;
        return -1;
    }
    g_nodes_by_name[node_->mesh_name] = node_;
    g_live_nodes.insert (node_);
    return 0;
}

void unregister_node (mesh_node_t *node_)
{
    std::lock_guard<std::mutex> lock (g_registry_mutex);
    std::map<std::string, mesh_node_t *>::iterator it = g_nodes_by_name.find (node_->mesh_name);
    if (it != g_nodes_by_name.end () && it->second == node_)
        g_nodes_by_name.erase (it);
    g_live_nodes.erase (node_);
}

mesh_node_t *as_mesh_node (void *handle_)
{
    if (!handle_)
        return NULL;
    {
        std::lock_guard<std::mutex> lock (g_registry_mutex);
        if (!g_live_nodes.count (handle_))
            return NULL;
    }
    mesh_node_t *node = static_cast<mesh_node_t *> (handle_);
    return node->check_tag () ? node : NULL;
}

spot_facade_t *as_spot_facade (void *handle_)
{
    if (!handle_)
        return NULL;
    {
        std::lock_guard<std::mutex> lock (g_registry_mutex);
        if (!g_live_facades.count (handle_))
            return NULL;
    }
    spot_facade_t *facade = static_cast<spot_facade_t *> (handle_);
    return facade->check_tag () ? facade : NULL;
}

publisher_t *as_publisher (void *handle_)
{
    if (!handle_)
        return NULL;
    {
        std::lock_guard<std::mutex> lock (g_registry_mutex);
        if (!g_live_publishers.count (handle_))
            return NULL;
    }
    publisher_t *pub = static_cast<publisher_t *> (handle_);
    return pub->check_tag () ? pub : NULL;
}

monitor_state_t *as_monitor (void *handle_)
{
    if (!handle_)
        return NULL;
    {
        std::lock_guard<std::mutex> lock (g_registry_mutex);
        if (!g_live_monitors.count (handle_))
            return NULL;
    }
    monitor_state_t *monitor = static_cast<monitor_state_t *> (handle_);
    return monitor->check_tag () ? monitor : NULL;
}

void track_facade (spot_facade_t *facade_, bool live_)
{
    std::lock_guard<std::mutex> lock (g_registry_mutex);
    if (live_)
        g_live_facades.insert (facade_);
    else
        g_live_facades.erase (facade_);
}

void track_publisher (publisher_t *pub_, bool live_)
{
    std::lock_guard<std::mutex> lock (g_registry_mutex);
    if (live_)
        g_live_publishers.insert (pub_);
    else
        g_live_publishers.erase (pub_);
}

void track_monitor (monitor_state_t *monitor_, bool live_)
{
    std::lock_guard<std::mutex> lock (g_registry_mutex);
    if (live_)
        g_live_monitors.insert (monitor_);
    else
        g_live_monitors.erase (monitor_);
}

ready_batch_t *as_ready_batch (void *handle_)
{
    if (!handle_)
        return NULL;
    ready_batch_t *batch = static_cast<ready_batch_t *> (handle_);
    return batch->check_tag () ? batch : NULL;
}

receive_batch_t *as_receive_batch (void *handle_)
{
    if (!handle_)
        return NULL;
    receive_batch_t *batch = static_cast<receive_batch_t *> (handle_);
    return batch->check_tag () ? batch : NULL;
}

int validate_metadata (const uint8_t *data_, size_t size_)
{
    if (size_ == 0 || size_ > ZLINK_MESH_APPLICATION_METADATA_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (size_ < 2) {
        errno = EINVAL;
        return -1;
    }
    const uint8_t version = data_[0];
    const uint8_t count = data_[1];
    if (version != 1) {
        errno = EINVAL;
        return -1;
    }
    size_t offset = 2;
    std::set<std::string> keys;
    for (uint8_t entry = 0; entry < count; ++entry) {
        if (offset + 1 > size_) {
            errno = EINVAL;
            return -1;
        }
        const uint8_t key_len = data_[offset];
        offset += 1;
        if (key_len == 0 || offset + key_len > size_) {
            errno = EINVAL;
            return -1;
        }
        if (!valid_utf8 (data_ + offset, key_len)) {
            errno = EINVAL;
            return -1;
        }
        std::string key (reinterpret_cast<const char *> (data_ + offset), key_len);
        if (!keys.insert (key).second) {
            errno = EINVAL;
            return -1;
        }
        offset += key_len;
        if (offset + 2 > size_) {
            errno = EINVAL;
            return -1;
        }
        const uint16_t value_len = static_cast<uint16_t> ((data_[offset] << 8) | data_[offset + 1]);
        offset += 2;
        if (offset + value_len > size_) {
            errno = EINVAL;
            return -1;
        }
        if (!valid_utf8 (data_ + offset, value_len)) {
            errno = EINVAL;
            return -1;
        }
        offset += value_len;
    }
    if (offset != size_) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

//  Invokes the ready handler outside the node mutex. The handler only
//  receives the readable domain mask; returned domains are treated as
//  consumer-owned and are not re-notified until new work arrives or a claim
//  release re-arms them.
static void notify_consumer_locked (mesh_node_t *node_, std::unique_lock<std::mutex> &lock_)
{
    node_->cv.notify_all ();
    if (node_->pollin_registered && !node_->pollin_signaled && !node_->ready.empty ()
        && node_->ready_signaler.valid ()) {
        node_->ready_signaler.send ();
        node_->pollin_signaled = true;
    }
    if (!node_->ready_handler || node_->ready_handler_depth > 0)
        return;

    zlink_mesh_ready_domain_mask_t mask = 0;
    for (std::set<std::pair<owner_id_t, int>>::const_iterator it = node_->ready.begin ();
         it != node_->ready.end (); ++it) {
        mask |= it->second == domain_application ? ZLINK_MESH_READY_APPLICATION
                                                 : ZLINK_MESH_READY_INFRASTRUCTURE;
    }
    if (mask == 0)
        return;

    zlink_mesh_ready_handler_fn handler = node_->ready_handler;
    void *userdata = node_->ready_handler_userdata;
    ++node_->ready_handler_depth;
    lock_.unlock ();
    (void) handler (node_, mask, userdata);
    lock_.lock ();
    --node_->ready_handler_depth;
}

void signal_ready (mesh_node_t *node_, const owner_id_t &owner_, domain_t domain_)
{
    std::unique_lock<std::mutex> lock (node_->mutex);
    node_->ready.insert (std::make_pair (owner_, static_cast<int> (domain_)));
    notify_consumer_locked (node_, lock);
}

int admit_record (mesh_node_t *node_,
                  const owner_id_t &owner_,
                  domain_t domain_,
                  std::unique_ptr<queued_record_t> &record_,
                  bool blocking_,
                  uint32_t timeout_ms_)
{
    std::unique_lock<std::mutex> lock (node_->mutex);
    std::map<owner_id_t, owner_state_t>::iterator it = node_->owners.find (owner_);
    if (it == node_->owners.end ()) {
        errno = ENOENT;
        return -1;
    }
    mailbox_t &mailbox = it->second.domains[domain_];

    //  Transfer fence: the frozen application lane accepts no new records
    //  until commit or abort resolves the fence.
    if (domain_ == domain_application && it->second.fenced_transfer_serial != 0) {
        errno = EAGAIN;
        return -1;
    }

    const uint64_t message_budget = node_->effective_message_budget ();
    const uint64_t byte_budget = node_->effective_byte_budget ();
    const uint64_t deadline =
      blocking_ && timeout_ms_ > 0 ? now_ms () + timeout_ms_ : 0;

    //  Infrastructure records (completions, transfer control) are bounded by
    //  the outstanding-operation set, not by the application budget, so they
    //  can always make progress.
    const bool budgeted = domain_ == domain_application;

    while (budgeted
           && (mailbox.pending_messages + 1 > message_budget
               || mailbox.pending_bytes + record_->byte_size > byte_budget)) {
        if (!blocking_ || timeout_ms_ == 0) {
            errno = EAGAIN;
            return -1;
        }
        const uint64_t now = now_ms ();
        if (deadline != 0 && now >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
        node_->cv.wait_for (lock, std::chrono::milliseconds (
                                    deadline != 0 ? deadline - now : 50));
        if (node_->state == ZLINK_MESH_NODE_DRAINING || node_->state == ZLINK_MESH_NODE_STOPPED) {
            errno = ESHUTDOWN;
            return -1;
        }
        it = node_->owners.find (owner_);
        if (it == node_->owners.end ()) {
            errno = ENOENT;
            return -1;
        }
    }

    mailbox.pending_messages += 1;
    mailbox.pending_bytes += record_->byte_size;
    mailbox.records.push_back (std::move (record_));
    node_->ready.insert (std::make_pair (owner_, static_cast<int> (domain_)));
    notify_consumer_locked (node_, lock);
    return 0;
}

void emit_monitor_event (mesh_node_t *node_, zlink_mesh_monitor_event_t &event_)
{
    monitor_state_t *monitor;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        monitor = node_->monitor;
    }
    if (!monitor)
        return;

    event_.struct_size = sizeof (event_);
    event_.version = 1;
    event_.timestamp_ms = now_ms ();

    const uint64_t bit = 1ull << (static_cast<uint64_t> (event_.kind) - 1);
    zlink_mesh_monitor_handler_fn handler = NULL;
    void *userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (monitor->mutex);
        if (monitor->closed)
            return;
        if (monitor->mask != 0 && (monitor->mask & bit) == 0)
            return;
        if (monitor->handler) {
            handler = monitor->handler;
            userdata = monitor->handler_userdata;
        } else {
            //  Bounded queue: aggregate overflow by dropping the oldest
            //  high-frequency event kinds first.
            const size_t queue_limit = 1024;
            if (monitor->events.size () >= queue_limit) {
                for (std::deque<zlink_mesh_monitor_event_t>::iterator it =
                       monitor->events.begin ();
                     it != monitor->events.end (); ++it) {
                    if (it->kind == ZLINK_MESH_MONITOR_MESSAGE_SUBMITTED
                        || it->kind == ZLINK_MESH_MONITOR_BACKPRESSURED) {
                        monitor->events.erase (it);
                        break;
                    }
                }
                if (monitor->events.size () >= queue_limit)
                    monitor->events.pop_front ();
            }
            monitor->events.push_back (event_);
            monitor->cv.notify_all ();
        }
    }
    if (handler)
        handler (&event_, userdata);
}

void recompute_readiness_locked (mesh_node_t *node_)
{
    if (node_->state != ZLINK_MESH_NODE_STARTED && node_->state != ZLINK_MESH_NODE_PARTIAL_READY
        && node_->state != ZLINK_MESH_NODE_READY)
        return;
    bool all_admitted = true;
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        if (node_->peers[i].state == ZLINK_MESH_PEER_CLOSED || node_->peers[i].inbound)
            continue;
        if (node_->peers[i].state != ZLINK_MESH_PEER_ADMITTED)
            all_admitted = false;
    }
    const zlink_mesh_node_state_t next =
      all_admitted ? ZLINK_MESH_NODE_READY : ZLINK_MESH_NODE_PARTIAL_READY;
    if (node_->state != next) {
        node_->state = next;
        node_->last_changed_ms = now_ms ();
    }
}

zlink_submit_result_t submit_errno_result ()
{
    switch (errno) {
        case EAGAIN:
            return ZLINK_SUBMIT_BACKPRESSURED;
        case ETIMEDOUT:
            return ZLINK_SUBMIT_BACKPRESSURED;
        case ENOTCONN:
        case EHOSTUNREACH:
            return ZLINK_SUBMIT_NOT_CONNECTED;
        case ENOENT:
            return ZLINK_SUBMIT_NOT_FOUND;
        case EINVAL:
        case EMSGSIZE:
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        case ESHUTDOWN:
        case EBUSY:
        case ESTALE:
        case EALREADY:
            return ZLINK_SUBMIT_INVALID_STATE;
        case ENOMEM:
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        case EFAULT:
            return ZLINK_SUBMIT_INVALID_HANDLE;
        case ENOTSUP:
            return ZLINK_SUBMIT_NOT_SUPPORTED;
        default:
            return ZLINK_SUBMIT_INTERNAL_ERROR;
    }
}

void complete_operation (mesh_node_t *node_,
                         const pending_operation_t &op_,
                         int32_t terminal_result_,
                         int32_t failure_errno_,
                         std::vector<unsigned char> *kind_data_,
                         std::vector<zlink_msg_t> *reply_parts_)
{
    std::unique_ptr<queued_record_t> record (new queued_record_t ());
    record->kind = ZLINK_MESH_RECORD_COMPLETION;
    record->operation_id = op_.id;
    record->operation_kind = op_.kind;
    record->terminal_result = terminal_result_;
    record->failure_errno = failure_errno_;
    if (kind_data_)
        record->kind_data = std::move (*kind_data_);
    if (reply_parts_) {
        record->parts = std::move (*reply_parts_);
        reply_parts_->clear ();
        for (size_t i = 0; i < record->parts.size (); ++i)
            record->byte_size += zlink_msg_size (&record->parts[i]);
    }
    if (admit_record (node_, op_.requester, domain_infrastructure, record, false, 0) != 0) {
        //  Infrastructure admission cannot fail for live owners; if the
        //  owner is already gone the completion is dropped with the owner.
    }

    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        if (node_->monitor)
            node_->monitor->counters.completed_operations += 1;
    }
    zlink_mesh_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    event.kind = ZLINK_MESH_MONITOR_OPERATION_COMPLETED;
    event.operation_id_high = op_.id.high;
    event.operation_id_low = op_.id.low;
    event.result_code = terminal_result_;
    event.failure_errno = failure_errno_;
    emit_monitor_event (node_, event);
}
}
}
