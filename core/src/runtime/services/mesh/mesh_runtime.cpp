/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/mesh/mesh_runtime.hpp"

#include "core/ctx.hpp"
#include "utils/clock.hpp"
#include "utils/err.hpp"

#include <string.h>
#include <algorithm>
#include <chrono>

namespace
{
//  Handle registry. Intentionally immortal (leaked on exit): detached
//  runtime threads may still validate handles while static destructors run,
//  so the storage must outlive them (same pattern as the request timeout
//  scheduler singleton).
struct handle_registry_t
{
    std::mutex mutex;
    //  Signalled when a node's lifecycle_pins drains to zero so a committed
    //  destroy can proceed to teardown.
    std::condition_variable lifecycle_cv;
    std::map<std::string, zlink::mesh::mesh_node_t *> nodes_by_name;
    std::set<void *> live_nodes;
    std::set<void *> live_facades;
    std::set<void *> live_publishers;
    std::set<void *> live_monitors;
};

#ifdef ZLINK_BUILD_TESTS
std::atomic<int> g_mesh_alloc_faults (0);
#endif

handle_registry_t &registry ()
{
    static handle_registry_t *instance = new handle_registry_t ();
    return *instance;
}

}

#ifdef ZLINK_BUILD_TESTS
//  Test-only hook: throw at the count_-th guarded allocation point in the
//  next mesh operation, exercising rollback at a chosen transaction phase.
extern "C" void zlink_test_set_mesh_alloc_fault (int count_)
{
    g_mesh_alloc_faults.store (count_ < 0 ? 0 : count_, std::memory_order_relaxed);
}
#endif

namespace zlink
{
namespace mesh
{
#ifdef ZLINK_BUILD_TESTS
void test_maybe_throw_alloc ()
{
    int remaining = g_mesh_alloc_faults.load (std::memory_order_relaxed);
    while (remaining > 0) {
        if (g_mesh_alloc_faults.compare_exchange_weak (remaining, remaining - 1,
                                                       std::memory_order_relaxed)) {
            if (remaining == 1)
                throw std::bad_alloc ();
            return;
        }
    }
}
#endif

uint64_t now_ms ()
{
    //  clock_t caches its last TSC sample and is intentionally mutable. Mesh
    //  control and ingress threads therefore need independent cache state.
    static thread_local clock_t clock;
    return clock.now_ms ();
}

uint64_t allocate_lifecycle_generation ()
{
    //  The spec orders lifecycles of the same RID by generation, including
    //  across process restarts, so the allocator anchors on epoch wall-clock
    //  MICROseconds — NOT now_ms(), whose clock_t source is boot-relative
    //  CLOCK_MONOTONIC and would move backwards across a reboot — and only
    //  bumps past it to keep same-process allocations strictly increasing.
    //  Core owns no durable state (location authority is the framework's per
    //  the S0 decisions), so cross-process ordering inherits the wall-clock
    //  assumption: a same-microsecond restart of the same RID or a
    //  rolled-back system clock surfaces as the duplicate/stale generation
    //  admission conflict the peer contract already defines (01-mesh-node
    //  §5), not as silent replacement.
    static std::atomic<uint64_t> last_generation (0);
    uint64_t previous = last_generation.load (std::memory_order_relaxed);
    for (;;) {
        uint64_t candidate = static_cast<uint64_t> (
          std::chrono::duration_cast<std::chrono::microseconds> (
            std::chrono::system_clock::now ().time_since_epoch ())
            .count ());
        if (candidate <= previous)
            candidate = previous + 1;
        if (last_generation.compare_exchange_weak (previous, candidate,
                                                   std::memory_order_relaxed))
            return candidate;
    }
}

//  Strict UTF-8 scalar validation (RFC 3629): rejects overlong encodings,
//  UTF-16 surrogates and code points above U+10FFFF. Every public string
//  contract (names, topics, filters, metadata text) shares this one check.
bool valid_utf8 (const unsigned char *data_, size_t size_)
{
    size_t i = 0;
    while (i < size_) {
        const unsigned char c = data_[i];
        if (c < 0x80) {
            i += 1;
            continue;
        }
        size_t extra;
        unsigned char first_min = 0x80;
        unsigned char first_max = 0xBF;
        if (c >= 0xC2 && c <= 0xDF)
            extra = 1;
        else if (c == 0xE0) {
            extra = 2;
            first_min = 0xA0;
        } else if (c >= 0xE1 && c <= 0xEC)
            extra = 2;
        else if (c == 0xED) {
            extra = 2;
            first_max = 0x9F;
        } else if (c >= 0xEE && c <= 0xEF)
            extra = 2;
        else if (c == 0xF0) {
            extra = 3;
            first_min = 0x90;
        } else if (c >= 0xF1 && c <= 0xF3)
            extra = 3;
        else if (c == 0xF4) {
            extra = 3;
            first_max = 0x8F;
        } else
            return false;
        if (i + extra + 1 > size_)
            return false;
        const unsigned char b1 = data_[i + 1];
        if (b1 < first_min || b1 > first_max)
            return false;
        for (size_t j = 2; j <= extra; ++j) {
            if ((data_[i + j] & 0xC0) != 0x80)
                return false;
        }
        i += extra + 1;
    }
    return true;
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

owner_state_t::owner_state_t () :
    draining (false), fenced_transfer_serial (0), timer_turn_active (false)
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
    data_plane_result (ZLINK_REQUEST_OK),
    data_plane_errno (0),
    acked_high_water (0),
    offered_participant_messages (0),
    offered_participant_bytes (0),
    seal_requested (false),
    source_complete (false),
    complete_sent (false)
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
    handler_active (0),
    closed (false)
{
    memset (&counters, 0, sizeof (counters));
    counters.struct_size = sizeof (counters);
    counters.version = 1;
}

publisher_t::publisher_t () : tag (0x4d505542), node (NULL)
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
    lifecycle_generation (allocate_lifecycle_generation ()),
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
    ready_handler (NULL),
    ready_handler_userdata (NULL),
    ready_handler_depth (0),
    pollin_registered (false),
    pollin_signaled (false),
    next_operation_serial (1),
    next_reply_serial (1),
    shutdown_active (false),
    lifecycle_pins (0),
    destroy_claimed (false),
    next_spot_generation (1),
    next_actor_generation (1),
    next_transfer_serial (1),
    publisher_count (0),
    monitor_emit_refs (0),
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
    std::lock_guard<std::mutex> lock (registry ().mutex);
    std::map<std::string, mesh_node_t *>::iterator it = registry ().nodes_by_name.find (name_);
    return it != registry ().nodes_by_name.end () ? it->second : NULL;
}

int register_node (mesh_node_t *node_)
{
    std::lock_guard<std::mutex> lock (registry ().mutex);
    if (registry ().nodes_by_name.count (node_->mesh_name)) {
        errno = EEXIST;
        return -1;
    }
    registry ().nodes_by_name[node_->mesh_name] = node_;
    registry ().live_nodes.insert (node_);
    return 0;
}

void unregister_node (mesh_node_t *node_)
{
    std::lock_guard<std::mutex> lock (registry ().mutex);
    std::map<std::string, mesh_node_t *>::iterator it = registry ().nodes_by_name.find (node_->mesh_name);
    if (it != registry ().nodes_by_name.end () && it->second == node_)
        registry ().nodes_by_name.erase (it);
    registry ().live_nodes.erase (node_);
}

mesh_node_t *pin_node_lifecycle (void *handle_, int *errno_out_)
{
    if (!handle_) {
        *errno_out_ = EFAULT;
        return NULL;
    }
    std::lock_guard<std::mutex> lock (registry ().mutex);
    if (!registry ().live_nodes.count (handle_)) {
        *errno_out_ = EFAULT;
        return NULL;
    }
    mesh_node_t *node = static_cast<mesh_node_t *> (handle_);
    if (!node->check_tag ()) {
        *errno_out_ = EFAULT;
        return NULL;
    }
    if (node->destroy_claimed) {
        //  A destroy already owns this handle's lifecycle: same-handle
        //  re-entry (§11).
        *errno_out_ = EDEADLK;
        return NULL;
    }
    node->lifecycle_pins += 1;
    return node;
}

mesh_node_t *pin_node_data_path (void *handle_)
{
    if (!handle_)
        return NULL;
    std::lock_guard<std::mutex> lock (registry ().mutex);
    if (!registry ().live_nodes.count (handle_))
        return NULL;
    mesh_node_t *node = static_cast<mesh_node_t *> (handle_);
    if (!node->check_tag ())
        return NULL;
    node->lifecycle_pins += 1;
    return node;
}

void unpin_node_lifecycle (mesh_node_t *node_)
{
    std::lock_guard<std::mutex> lock (registry ().mutex);
    node_->lifecycle_pins -= 1;
    if (node_->lifecycle_pins == 0)
        registry ().lifecycle_cv.notify_all ();
}

mesh_node_t *claim_node_destroy (void *handle_)
{
    if (!handle_) {
        errno = EFAULT;
        return NULL;
    }
    std::lock_guard<std::mutex> lock (registry ().mutex);
    if (!registry ().live_nodes.count (handle_)) {
        errno = EFAULT;
        return NULL;
    }
    mesh_node_t *node = static_cast<mesh_node_t *> (handle_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (node->destroy_claimed) {
        errno = ESTALE;
        return NULL;
    }
    node->destroy_claimed = true;
    return node;
}

void release_node_destroy_claim (mesh_node_t *node_)
{
    std::lock_guard<std::mutex> lock (registry ().mutex);
    node_->destroy_claimed = false;
}

void unregister_node_and_wait_lifecycle_quiesced (mesh_node_t *node_)
{
    std::unique_lock<std::mutex> lock (registry ().mutex);
    std::map<std::string, mesh_node_t *>::iterator it =
      registry ().nodes_by_name.find (node_->mesh_name);
    if (it != registry ().nodes_by_name.end () && it->second == node_)
        registry ().nodes_by_name.erase (it);
    registry ().live_nodes.erase (node_);
    //  A pinned shutdown may still be anywhere between its handle check and
    //  its final return; the storage must not go away under it.
    while (node_->lifecycle_pins != 0)
        registry ().lifecycle_cv.wait (lock);
}

mesh_node_t *as_mesh_node (void *handle_)
{
    if (!handle_)
        return NULL;
    //  The tag dereference stays under the registry mutex: a committed
    //  destroy unregisters (needing this mutex) before it deletes, so
    //  membership observed here implies the storage is still alive. The
    //  returned pointer is only lifetime-safe for pinned callers; unpinned
    //  use is limited to membership probes.
    std::lock_guard<std::mutex> lock (registry ().mutex);
    if (!registry ().live_nodes.count (handle_))
        return NULL;
    mesh_node_t *node = static_cast<mesh_node_t *> (handle_);
    return node->check_tag () ? node : NULL;
}

spot_facade_t *as_spot_facade (void *handle_)
{
    if (!handle_)
        return NULL;
    {
        std::lock_guard<std::mutex> lock (registry ().mutex);
        if (!registry ().live_facades.count (handle_))
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
        std::lock_guard<std::mutex> lock (registry ().mutex);
        if (!registry ().live_publishers.count (handle_))
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
        std::lock_guard<std::mutex> lock (registry ().mutex);
        if (!registry ().live_monitors.count (handle_))
            return NULL;
    }
    monitor_state_t *monitor = static_cast<monitor_state_t *> (handle_);
    return monitor->check_tag () ? monitor : NULL;
}

void track_facade (spot_facade_t *facade_, bool live_)
{
    std::lock_guard<std::mutex> lock (registry ().mutex);
    if (live_)
        registry ().live_facades.insert (facade_);
    else
        registry ().live_facades.erase (facade_);
}

void track_publisher (publisher_t *pub_, bool live_)
{
    std::lock_guard<std::mutex> lock (registry ().mutex);
    if (live_)
        registry ().live_publishers.insert (pub_);
    else
        registry ().live_publishers.erase (pub_);
}

void track_monitor (monitor_state_t *monitor_, bool live_)
{
    std::lock_guard<std::mutex> lock (registry ().mutex);
    if (live_)
        registry ().live_monitors.insert (monitor_);
    else
        registry ().live_monitors.erase (monitor_);
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
    node_->ready_handler_thread = std::this_thread::get_id ();
    lock_.unlock ();
    (void) handler (node_, mask, userdata);
    lock_.lock ();
    --node_->ready_handler_depth;
    //  A (de)registration from another thread waits for this return.
    node_->cv.notify_all ();
}

void signal_ready (mesh_node_t *node_, const owner_id_t &owner_, domain_t domain_)
{
    std::unique_lock<std::mutex> lock (node_->mutex);
    node_->ready.insert (std::make_pair (owner_, static_cast<int> (domain_)));
    notify_consumer_locked (node_, lock);
}

//  Emits the BACKPRESSURED observation for a rejected admission after the
//  node mutex is released; keeps errno for the caller.
int admit_backpressured (mesh_node_t *node_,
                         const owner_id_t &owner_,
                         const queued_record_t *record_,
                         std::unique_lock<std::mutex> &lock_,
                         int errno_,
                         bool emit_event_)
{
    if (!emit_event_) {
        errno = errno_;
        return -1;
    }
    zlink_mesh_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    event.kind = ZLINK_MESH_MONITOR_BACKPRESSURED;
    event.owner_kind = static_cast<zlink_mesh_owner_kind_t> (owner_.kind);
    if (record_ && !record_->channel_name.empty ())
        snprintf (event.channel_name, sizeof (event.channel_name), "%s",
                  record_->channel_name.c_str ());
    event.failure_errno = errno_;
    lock_.unlock ();
    emit_monitor_event (node_, event);
    errno = errno_;
    return -1;
}

static int admit_record_impl (mesh_node_t *node_,
                              const owner_id_t &owner_,
                              domain_t domain_,
                              std::unique_ptr<queued_record_t> &record_,
                              bool blocking_,
                              uint32_t timeout_ms_,
                              bool emit_backpressure_event_)
{
    std::unique_lock<std::mutex> lock (node_->mutex);
    //  Application admission and the transition to DRAINING are ordered by
    //  the same mutex. A submit that took its routing snapshot before
    //  shutdown must not enqueue after shutdown has closed admission.
    if (domain_ == domain_application
        && (node_->state == ZLINK_MESH_NODE_DRAINING
            || node_->state == ZLINK_MESH_NODE_STOPPED)) {
        errno = ESHUTDOWN;
        return -1;
    }
    std::map<owner_id_t, owner_state_t>::iterator it = node_->owners.find (owner_);
    if (it == node_->owners.end ()) {
        errno = ENOENT;
        return -1;
    }
    mailbox_t &mailbox = it->second.domains[domain_];

    //  Transfer fence: the frozen application lane accepts no new records
    //  until commit or abort resolves the fence.
    if (domain_ == domain_application && it->second.fenced_transfer_serial != 0)
        return admit_backpressured (node_, owner_, record_.get (), lock, EAGAIN,
                                    emit_backpressure_event_);

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
        if (!blocking_ || timeout_ms_ == 0)
            return admit_backpressured (node_, owner_, record_.get (), lock, EAGAIN,
                                        emit_backpressure_event_);
        const uint64_t now = now_ms ();
        if (deadline != 0 && now >= deadline)
            return admit_backpressured (node_, owner_, record_.get (), lock, ETIMEDOUT,
                                        emit_backpressure_event_);
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

    //  The ready-index node and the deque slot are the only fallible steps
    //  left; take them before the counters so an allocation failure leaves
    //  the mailbox exactly as it was and maps to ENOMEM instead of escaping
    //  the C ABI.
    const uint64_t record_bytes = record_->byte_size;
    const std::pair<owner_id_t, int> ready_key (owner_, static_cast<int> (domain_));
    bool ready_inserted = false;
    try {
        ready_inserted = node_->ready.insert (ready_key).second;
        mailbox.records.push_back (std::move (record_));
    }
    catch (const std::bad_alloc &) {
        if (ready_inserted)
            node_->ready.erase (ready_key);
        errno = ENOMEM;
        return -1;
    }
    mailbox.pending_messages += 1;
    mailbox.pending_bytes += record_bytes;
    notify_consumer_locked (node_, lock);
    return 0;
}

int admit_record (mesh_node_t *node_,
                  const owner_id_t &owner_,
                  domain_t domain_,
                  std::unique_ptr<queued_record_t> &record_,
                  bool blocking_,
                  uint32_t timeout_ms_)
{
    return admit_record_impl (node_, owner_, domain_, record_, blocking_, timeout_ms_, true);
}

int admit_multicast_record (mesh_node_t *node_,
                            const owner_id_t &owner_,
                            std::unique_ptr<queued_record_t> &record_)
{
    return admit_record_impl (
      node_, owner_, domain_application, record_, false, 0, false);
}

void maybe_end_spot_locked (mesh_node_t *node_, const std::string &spot_key_)
{
    std::map<std::string, spot_state_t>::iterator it = node_->spots.find (spot_key_);
    if (it == node_->spots.end ())
        return;
    spot_state_t &spot = it->second;
    //  The entry Spot is referenced by the node for its whole lifetime.
    if (spot.kind == ZLINK_SPOT_KIND_ENTRY)
        return;
    if (spot.facade_count > 0 || spot.timer_count > 0 || spot.active_actor_count > 0)
        return;
    owner_id_t owner;
    owner.kind = owner_spot;
    owner.key = spot_key_;
    owner.generation = spot.generation;
    std::map<owner_id_t, owner_state_t>::iterator owner_it = node_->owners.find (owner);
    if (owner_it != node_->owners.end ()) {
        if (owner_it->second.domains[0].claimed || owner_it->second.domains[1].claimed
            || owner_it->second.timer_turn_active
            || owner_it->second.fenced_transfer_serial != 0)
            return;
        node_->owners.erase (owner_it);
    }
    node_->ready.erase (std::make_pair (owner, static_cast<int> (domain_application)));
    node_->ready.erase (std::make_pair (owner, static_cast<int> (domain_infrastructure)));
    node_->spots.erase (it);
}

void emit_monitor_event (mesh_node_t *node_, zlink_mesh_monitor_event_t &event_)
{
    monitor_state_t *monitor;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        monitor = node_->monitor;
        if (!monitor)
            return;
        //  Pin the monitor: close waits for emit refs to drain before it
        //  deletes the object, so the pointer stays valid past this scope.
        node_->monitor_emit_refs += 1;
    }

    event_.struct_size = sizeof (event_);
    event_.version = 1;
    event_.timestamp_ms = now_ms ();

    const uint64_t bit = 1ull << (static_cast<uint64_t> (event_.kind) - 1);
    zlink_mesh_monitor_handler_fn handler = NULL;
    void *userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (monitor->mutex);
        if (monitor->closed || (monitor->mask != 0 && (monitor->mask & bit) == 0)) {
            //  Filtered or closing: fall through to the ref release below.
        } else if (monitor->handler) {
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
            try {
                monitor->events.push_back (event_);
                monitor->cv.notify_all ();
            }
            catch (const std::bad_alloc &) {
                //  The queue is bounded and lossy by contract; dropping the
                //  event under allocation pressure is the same observable
                //  behavior as an overflow drop.
            }
        }
    }
    if (handler) {
        {
            std::lock_guard<std::mutex> lock (monitor->mutex);
            monitor->handler_active += 1;
        }
        handler (&event_, userdata);
        {
            std::lock_guard<std::mutex> lock (monitor->mutex);
            monitor->handler_active -= 1;
        }
    }
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        node_->monitor_emit_refs -= 1;
        node_->cv.notify_all ();
    }
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
        case ETIMEDOUT:
        case ENOBUFS:
            return ZLINK_SUBMIT_BACKPRESSURED;
        case ENOTCONN:
        case EHOSTUNREACH:
            return ZLINK_SUBMIT_NOT_CONNECTED;
        case ENOENT:
            return ZLINK_SUBMIT_NOT_FOUND;
        case EACCES:
            return ZLINK_SUBMIT_NOT_ADMITTED;
        case ETERM:
            return ZLINK_SUBMIT_TERMINATED;
        case EINVAL:
        case EMSGSIZE:
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        case ESHUTDOWN:
        case EBUSY:
        case ESTALE:
        case EALREADY:
            return ZLINK_SUBMIT_INVALID_STATE;
        case EDEADLK:
        case EPERM:
            return ZLINK_SUBMIT_THREAD_VIOLATION;
        case ENOMEM:
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        case EOVERFLOW:
            return ZLINK_SUBMIT_SEQ_EXHAUSTED;
        case EFAULT:
            return ZLINK_SUBMIT_INVALID_HANDLE;
        case ENOTSUP:
            return ZLINK_SUBMIT_NOT_SUPPORTED;
        default:
            return ZLINK_SUBMIT_INTERNAL_ERROR;
    }
}

zlink_submit_result_t submit_out_of_memory_result ()
{
    errno = ENOMEM;
    return ZLINK_SUBMIT_OUT_OF_MEMORY;
}

std::shared_ptr<zlink::request_timeout::task_t> detach_pending_operation_locked (
  mesh_node_t *node_, std::unordered_map<uint64_t, pending_operation_t>::iterator op_it_)
{
    std::shared_ptr<zlink::request_timeout::task_t> task =
      op_it_->second.timeout_task;
    node_->operations.erase (op_it_);
    return task;
}

void observe_operation_completed (mesh_node_t *node_,
                                  const zlink_mesh_operation_id_t &operation_id_,
                                  int32_t terminal_result_,
                                  int32_t failure_errno_)
{
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        if (node_->monitor)
            node_->monitor->counters.completed_operations += 1;
    }
    zlink_mesh_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    event.kind = ZLINK_MESH_MONITOR_OPERATION_COMPLETED;
    event.operation_id_high = operation_id_.high;
    event.operation_id_low = operation_id_.low;
    event.result_code = terminal_result_;
    event.failure_errno = failure_errno_;
    emit_monitor_event (node_, event);
}

int complete_pending_operation (mesh_node_t *node_,
                                const pending_operation_t &op_,
                                int32_t terminal_result_,
                                int32_t failure_errno_,
                                std::vector<unsigned char> *kind_data_,
                                std::vector<zlink_msg_t> *reply_parts_)
{
    return complete_pending_operation_with_commit (
      node_, op_, terminal_result_, failure_errno_, kind_data_, reply_parts_,
      NULL, NULL);
}

int complete_pending_operation_with_commit (
  mesh_node_t *node_,
  const pending_operation_t &op_,
  int32_t terminal_result_,
  int32_t failure_errno_,
  std::vector<unsigned char> *kind_data_,
  std::vector<zlink_msg_t> *reply_parts_,
  pending_operation_commit_fn commit_locked_,
  void *commit_userdata_)
{
    owner_id_t ready_owner;
    std::pair<owner_id_t, int> ready_key;
    try {
        ready_owner = op_.requester;
        ready_key =
          std::make_pair (ready_owner, static_cast<int> (domain_infrastructure));
    }
    catch (const std::bad_alloc &) {
        errno = ENOMEM;
        return -1;
    }
    const std::shared_ptr<completion_reservation_t> reservation = op_.completion;
    if (!reservation || reservation->records.empty ()) {
        errno = EFAULT;
        return -1;
    }

    size_t record_bytes = 0;
    int32_t observed_terminal_result = terminal_result_;
    int32_t observed_failure_errno = failure_errno_;
    //  Cancelled outside the node mutex: cancel() waits for a firing timeout
    //  handler, and that handler takes the node mutex to look the operation
    //  up. The scheduler recognizes a self-cancel from its own handler.
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
    bool owner_missing = false;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
          node_->operations.find (op_.id.low);
        if (op_it == node_->operations.end ())
            return 0;
        if (op_it->second.completion != reservation
            || reservation->records.empty ()) {
            errno = EFAULT;
            return -1;
        }
        if (reservation->committing)
            return 0;
        std::map<owner_id_t, owner_state_t>::iterator owner_it =
          node_->owners.find (op_it->second.requester);
        if (owner_it == node_->owners.end ()) {
            timeout_task = detach_pending_operation_locked (node_, op_it);
            owner_missing = true;
        } else {
            queued_record_t &record = *reservation->records.front ();
            if (!reservation->prepared) {
                record.kind = ZLINK_MESH_RECORD_COMPLETION;
                record.operation_id = op_.id;
                record.operation_kind = op_.kind;
                record.terminal_result = terminal_result_;
                record.failure_errno = failure_errno_;
                if (kind_data_)
                    record.kind_data.swap (*kind_data_);
                if (reply_parts_)
                    record.parts.swap (*reply_parts_);
                for (size_t i = 0; i < record.parts.size (); ++i)
                    record.byte_size += zlink_msg_size (&record.parts[i]);
                reservation->prepared = true;
            }
            record_bytes = record.byte_size;
            observed_terminal_result = record.terminal_result;
            observed_failure_errno = record.failure_errno;
            mailbox_t &mailbox = owner_it->second.domains[domain_infrastructure];
            bool ready_inserted = false;
            try {
#ifdef ZLINK_BUILD_TESTS
                test_maybe_throw_alloc ();
#endif
                ready_inserted = node_->ready.insert (ready_key).second;
            }
            catch (const std::bad_alloc &) {
                if (ready_inserted)
                    node_->ready.erase (ready_key);
                errno = ENOMEM;
                return -1;
            }
            if (commit_locked_
                && !commit_locked_ (node_, commit_userdata_)) {
                if (ready_inserted)
                    node_->ready.erase (ready_key);
                return 0;
            }
            mailbox.records.splice (mailbox.records.end (), reservation->records);
            mailbox.pending_messages += 1;
            mailbox.pending_bytes += record_bytes;
            timeout_task = detach_pending_operation_locked (node_, op_it);
        }
    }
    if (timeout_task)
        zlink::request_timeout::cancel (timeout_task);
    if (owner_missing)
        return 0;
    signal_ready (node_, ready_owner, domain_infrastructure);
    observe_operation_completed (
      node_, op_.id, observed_terminal_result, observed_failure_errno);
    return 1;
}

int prepare_pending_operation_completion (
  mesh_node_t *node_,
  const zlink_mesh_operation_id_t &operation_id_,
  pending_operation_completion_t *completion_)
{
    if (!node_ || !completion_ || completion_->active) {
        errno = EINVAL;
        return -1;
    }
    try {
        std::lock_guard<std::mutex> lock (node_->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
          node_->operations.find (operation_id_.low);
        if (op_it == node_->operations.end ())
            return 0;
        const std::shared_ptr<completion_reservation_t> reservation =
          op_it->second.completion;
        if (!reservation || reservation->records.empty ()) {
            errno = EFAULT;
            return -1;
        }
        if (reservation->committing)
            return 0;
        if (node_->owners.find (op_it->second.requester)
            == node_->owners.end ())
            return 0;
#ifdef ZLINK_BUILD_TESTS
        test_maybe_throw_alloc ();
#endif
        completion_->operation = op_it->second;
        completion_->ready_owner = op_it->second.requester;
        completion_->ready_node.insert (std::make_pair (
          completion_->ready_owner,
          static_cast<int> (domain_infrastructure)));
        reservation->committing = true;
        completion_->active = true;
        return 1;
    }
    catch (const std::bad_alloc &) {
        completion_->ready_node.clear ();
        errno = ENOMEM;
        return -1;
    }
}

void cancel_pending_operation_completion (
  mesh_node_t *node_,
  pending_operation_completion_t *completion_)
{
    if (!node_ || !completion_ || !completion_->active)
        return;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
          node_->operations.find (completion_->operation.id.low);
        if (op_it != node_->operations.end ()
            && op_it->second.completion
                 == completion_->operation.completion)
            op_it->second.completion->committing = false;
    }
    completion_->ready_node.clear ();
    completion_->active = false;
}

int commit_prepared_pending_operation (
  mesh_node_t *node_,
  pending_operation_completion_t *completion_,
  int32_t terminal_result_,
  int32_t failure_errno_)
{
    if (!node_ || !completion_ || !completion_->active) {
        errno = EINVAL;
        return -1;
    }
    const std::shared_ptr<completion_reservation_t> reservation =
      completion_->operation.completion;
    size_t record_bytes = 0;
    //  Cancelled outside the node mutex for the same reason as
    //  complete_pending_operation_with_commit.
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
          node_->operations.find (completion_->operation.id.low);
        if (op_it == node_->operations.end ()
            || op_it->second.completion != reservation
            || !reservation || !reservation->committing
            || reservation->records.empty ()) {
            completion_->active = false;
            return 0;
        }
        std::map<owner_id_t, owner_state_t>::iterator owner_it =
          node_->owners.find (op_it->second.requester);
        if (owner_it == node_->owners.end ()) {
            reservation->committing = false;
            completion_->active = false;
            return 0;
        }
        queued_record_t &record = *reservation->records.front ();
        record.kind = ZLINK_MESH_RECORD_COMPLETION;
        record.operation_id = completion_->operation.id;
        record.operation_kind = completion_->operation.kind;
        record.terminal_result = terminal_result_;
        record.failure_errno = failure_errno_;
        reservation->prepared = true;
        record_bytes = record.byte_size;

        //  C++17 set::merge transfers the node allocated by prepare without
        //  allocating. A duplicate key stays in ready_node and is discarded
        //  with the completed reservation.
        node_->ready.merge (completion_->ready_node);
        mailbox_t &mailbox =
          owner_it->second.domains[domain_infrastructure];
        mailbox.records.splice (
          mailbox.records.end (), reservation->records);
        mailbox.pending_messages += 1;
        mailbox.pending_bytes += record_bytes;
        timeout_task = detach_pending_operation_locked (node_, op_it);
        completion_->active = false;
    }
    if (timeout_task)
        zlink::request_timeout::cancel (timeout_task);
    signal_ready (
      node_, completion_->ready_owner, domain_infrastructure);
    observe_operation_completed (
      node_, completion_->operation.id, terminal_result_, failure_errno_);
    return 1;
}
}
}
