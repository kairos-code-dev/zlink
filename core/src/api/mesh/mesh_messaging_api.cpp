/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/mesh/mesh_c_internal.hpp"
#include "services/mesh/mesh_wire.hpp"

#include "api/socket/request_reply_runtime_core.hpp"
#include "api/socket/request_timeout_scheduler_internal.hpp"
#include "utils/err.hpp"
#include "utils/macros.hpp"

#include <string.h>
#include <atomic>
#include <chrono>
#include <thread>

using namespace zlink::mesh;

namespace
{
#ifdef ZLINK_BUILD_TESTS
std::atomic<int> g_publish_pause_after_snapshot_ms (0);
std::atomic<int> g_publish_snapshot_paused (0);
std::atomic<int> g_remote_route_before_commit_pause (0);
std::atomic<int> g_remote_route_before_commit_paused (0);
#endif

//  --- shared submit plumbing --------------------------------------------------

struct operation_timeout_ctx_t
{
    mesh_node_t *node;
    //  Full operation identity: `high` carries the node lifecycle generation,
    //  so a timer that outlives its node can never match an operation of a
    //  recycled node address or serial.
    uint64_t operation_high;
    uint64_t operation_low;
    std::shared_ptr<struct operation_timeout_gate_t> gate;
};

struct operation_timeout_gate_t
{
    enum state_t
    {
        prepared,
        committed,
        canceled
    };

    operation_timeout_gate_t () : state (prepared) {}

    std::mutex mutex;
    std::condition_variable cv;
    state_t state;
};

void on_operation_timeout (void *userdata_)
{
    std::unique_ptr<operation_timeout_ctx_t> ctx (
      static_cast<operation_timeout_ctx_t *> (userdata_));
    if (!ctx.get ())
        return;
    {
        std::unique_lock<std::mutex> lock (ctx->gate->mutex);
        while (ctx->gate->state == operation_timeout_gate_t::prepared)
            ctx->gate->cv.wait (lock);
        if (ctx->gate->state == operation_timeout_gate_t::canceled)
            return;
    }
    mesh_node_pin_t node_pin (ctx->node);
    mesh_node_t *node = node_pin.get ();
    if (!node)
        return;
    zlink_mesh_operation_id_t operation_id;
    operation_id.high = ctx->operation_high;
    operation_id.low = ctx->operation_low;
    (void) complete_pending_operation_by_id (
      node, operation_id, ZLINK_REQUEST_TIMED_OUT, ETIMEDOUT);
}

struct operation_timeout_guard_state_t
{
    std::shared_ptr<operation_timeout_gate_t> gate;
    std::shared_ptr<zlink::request_timeout::task_t> task;
    mesh_node_t *node;
    uint64_t operation_high;
    uint64_t operation_low;
    bool committed;

    operation_timeout_guard_state_t () :
        node (NULL), operation_high (0), operation_low (0), committed (false)
    {
    }
};

}

#ifdef ZLINK_BUILD_TESTS
extern "C" void zlink_test_set_mesh_publish_pause_after_snapshot_ms (int pause_ms_)
{
    g_publish_pause_after_snapshot_ms.store (pause_ms_ < 0 ? 0 : pause_ms_,
                                             std::memory_order_relaxed);
}

extern "C" int zlink_test_mesh_publish_snapshot_paused ()
{
    return g_publish_snapshot_paused.load (std::memory_order_acquire);
}

extern "C" void zlink_test_mesh_pause_remote_route_before_commit (int enabled_)
{
    g_remote_route_before_commit_pause.store (enabled_ != 0 ? 1 : 0,
                                              std::memory_order_release);
}

extern "C" int zlink_test_mesh_remote_route_before_commit_paused ()
{
    return g_remote_route_before_commit_paused.load (std::memory_order_acquire);
}
#endif

namespace zlink
{
namespace mesh
{
operation_timeout_guard_t::operation_timeout_guard_t (
  mesh_node_t *node_,
  const zlink_mesh_operation_id_t &operation_id_,
  uint32_t timeout_ms_) :
    _state (NULL),
    _valid (true)
{
    if (timeout_ms_ == 0)
        return;

#ifdef ZLINK_BUILD_TESTS
    try {
        test_maybe_throw_alloc ();
    }
    catch (const std::bad_alloc &) {
        errno = ENOMEM;
        _valid = false;
        return;
    }
#endif
    std::unique_ptr<operation_timeout_guard_state_t> state (
      new (std::nothrow) operation_timeout_guard_state_t ());
    std::unique_ptr<operation_timeout_ctx_t> ctx (
      new (std::nothrow) operation_timeout_ctx_t ());
    if (!state.get () || !ctx.get ()) {
        errno = ENOMEM;
        _valid = false;
        return;
    }

    try {
        state->gate = std::make_shared<operation_timeout_gate_t> ();
        ctx->node = node_;
        ctx->operation_high = operation_id_.high;
        ctx->operation_low = operation_id_.low;
        ctx->gate = state->gate;
        state->node = node_;
        state->operation_high = operation_id_.high;
        state->operation_low = operation_id_.low;
        state->task = zlink::request_timeout::schedule (
          timeout_ms_, &on_operation_timeout, ctx.get (),
          &zlink::request_reply_runtime::destroy_timeout_callback_ctx<operation_timeout_ctx_t>);
        if (!state->task) {
            errno = ENOMEM;
            _valid = false;
            return;
        }
        ctx.release ();
        _state = state.release ();
    }
    catch (const std::bad_alloc &) {
        errno = ENOMEM;
        _valid = false;
    }
}

operation_timeout_guard_t::~operation_timeout_guard_t ()
{
    operation_timeout_guard_state_t *state =
      static_cast<operation_timeout_guard_state_t *> (_state);
    if (!state)
        return;
    if (!state->committed) {
        {
            std::lock_guard<std::mutex> lock (state->gate->mutex);
            state->gate->state = operation_timeout_gate_t::canceled;
            state->gate->cv.notify_all ();
        }
        zlink::request_timeout::cancel (state->task);
    }
    delete state;
}

bool operation_timeout_guard_t::valid () const
{
    return _valid;
}

void operation_timeout_guard_t::commit ()
{
    operation_timeout_guard_state_t *state =
      static_cast<operation_timeout_guard_state_t *> (_state);
    if (!state || state->committed)
        return;
    //  Hand the task to the pending operation before the gate opens: from
    //  that point terminal completion and node destroy own its cancellation,
    //  so a committed timer cannot outlive the operation it may fire on.
    {
        std::lock_guard<std::mutex> lock (state->node->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator it =
          state->node->operations.find (state->operation_low);
        if (it != state->node->operations.end ()
            && it->second.id.high == state->operation_high)
            it->second.timeout_task = state->task;
    }
    {
        std::lock_guard<std::mutex> lock (state->gate->mutex);
        state->gate->state = operation_timeout_gate_t::committed;
        state->committed = true;
        state->gate->cv.notify_all ();
    }
}
}
}

namespace
{

//  Validates borrowed input parts and copies them (reference counted) into
//  record storage.
int copy_borrowed_parts (const zlink_msg_t *parts_, size_t part_count_, queued_record_t *record_)
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

int check_submit_input (const zlink_mesh_metadata_view_t *metadata_,
                        const zlink_msg_t *parts_,
                        size_t part_count_)
{
    if (!parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (metadata_) {
        if (!metadata_->data || metadata_->size == 0) {
            errno = EINVAL;
            return -1;
        }
        if (validate_metadata (metadata_->data, metadata_->size) != 0)
            return -1;
    }
    return 0;
}

//  Selection: a channel's candidates are the admitted positive-weight remote
//  peers plus the local node when it is a ready member of the channel. Phase
//  A carries no admitted remote pipes, so remote candidates resolve later.
struct channel_target_t
{
    bool is_local;
    size_t peer_index;
    uint32_t weight;
    std::string key;
};

int select_channel_target_locked (mesh_node_t *node_,
                                  const std::string &channel_,
                                  channel_target_t *target_out_)
{
    std::vector<channel_target_t> candidates;
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        const peer_state_t &peer = node_->peers[i];
        if (peer.state != ZLINK_MESH_PEER_ADMITTED)
            continue;
        std::map<std::string, uint32_t>::const_iterator it = peer.channels.find (channel_);
        if (it == peer.channels.end () || it->second == 0)
            continue;
        channel_target_t candidate;
        candidate.is_local = false;
        candidate.peer_index = i;
        candidate.weight = it->second;
        candidate.key.assign (1, 'R');
        candidate.key.append (reinterpret_cast<const char *> (peer.rid.data ()),
                              peer.rid.size ());
        candidates.push_back (candidate);
    }
    std::map<std::string, uint32_t>::const_iterator local_it = node_->channels.find (channel_);
    if (local_it != node_->channels.end () && local_it->second > 0) {
        channel_target_t candidate;
        candidate.is_local = true;
        candidate.peer_index = 0;
        candidate.weight = local_it->second;
        candidate.key.assign (1, 'L');
        candidates.push_back (candidate);
    }
    if (candidates.empty ()) {
        errno = ENOENT;
        return -1;
    }
    int64_t total_weight = 0;
    std::set<std::string> candidate_keys;
    for (size_t i = 0; i < candidates.size (); ++i)
    {
        total_weight += candidates[i].weight;
        candidate_keys.insert (candidates[i].key);
    }

    std::map<std::string, int64_t> &current =
      node_->weighted_rr_current[channel_];
    for (std::map<std::string, int64_t>::iterator it = current.begin ();
         it != current.end ();) {
        if (candidate_keys.find (it->first) == candidate_keys.end ())
            it = current.erase (it);
        else
            ++it;
    }

    size_t selected = 0;
    for (size_t i = 0; i < candidates.size (); ++i) {
        const int64_t updated =
          (current[candidates[i].key] += candidates[i].weight);
        if (i == 0 || updated > current[candidates[selected].key])
            selected = i;
    }
    current[candidates[selected].key] -= total_weight;
    *target_out_ = candidates[selected];
    return 0;
}

//  Common state gate for submits: arguments were already validated.
int check_submit_state_locked (mesh_node_t *node_)
{
    if (node_->state == ZLINK_MESH_NODE_CREATED) {
        errno = EINVAL;
        return -1;
    }
    if (node_->state == ZLINK_MESH_NODE_DRAINING || node_->state == ZLINK_MESH_NODE_STOPPED) {
        errno = ESHUTDOWN;
        return -1;
    }
    return 0;
}

//  Builds and admits one locally-delivered record. Handles request
//  bookkeeping (operation + one-shot reply route) when operation_kind_ is a
//  request kind.
//  Submit observability: one MESSAGE_SUBMITTED per successful application
//  submit and one BACKPRESSURED per admission rejection under pressure. The
//  monitor queue aggregates both kinds under overflow.
void emit_submit_event (mesh_node_t *node_,
                        zlink_mesh_monitor_event_kind_t kind_,
                        zlink_mesh_owner_kind_t owner_kind_,
                        const std::string &channel_,
                        int32_t error_)
{
    zlink_mesh_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    event.kind = kind_;
    event.owner_kind = owner_kind_;
    if (!channel_.empty ())
        snprintf (event.channel_name, sizeof (event.channel_name), "%s", channel_.c_str ());
    event.failure_errno = error_;
    emit_monitor_event (node_, event);
}

zlink_submit_result_t submit_local_record (mesh_node_t *node_,
                                           const owner_id_t &destination_,
                                           const owner_id_t &requester_,
                                           const send_ready_interest_t &interest_,
                                           zlink_mesh_record_kind_t kind_,
                                           const rid_bytes_t &source_spot_rid_,
                                           const std::string &channel_name_,
                                           const zlink_mesh_metadata_view_t *metadata_,
                                           const zlink_msg_t *parts_,
                                           size_t part_count_,
                                           zlink_mesh_operation_id_t *operation_id_out_,
                                           zlink_mesh_operation_kind_t operation_kind_,
                                           zlink_send_flags_t flags_,
                                           uint32_t timeout_ms_)
{
    const bool is_request = operation_id_out_ != NULL;

    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ()) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    try {
#ifdef ZLINK_BUILD_TESTS
        test_maybe_throw_alloc ();
#endif
        record->kind = kind_;
        record->source_node_rid = node_->routing_id;
        record->source_spot_rid = source_spot_rid_;
        record->channel_name = channel_name_;
        if (metadata_) {
            record->has_metadata = true;
            record->application_metadata.assign (metadata_->data,
                                                 metadata_->data + metadata_->size);
            record->byte_size += metadata_->size;
        }
        if (copy_borrowed_parts (parts_, part_count_, record.get ()) != 0)
            return errno == ENOMEM ? ZLINK_SUBMIT_OUT_OF_MEMORY : ZLINK_SUBMIT_INTERNAL_ERROR;
    }
    catch (const std::bad_alloc &) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }

    operation_submission_t submission (
      node_, is_request, operation_kind_, requester_, is_request ? timeout_ms_ : 0);
    if (!submission.valid ())
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    const zlink_mesh_operation_id_t op_id = submission.operation_id ();
    uint64_t reply_serial = 0;
    if (is_request) {
        reply_route_t route;
        route.kind = reply_route_t::kind_generic;
        route.requester = requester_;
        route.operation_kind = operation_kind_;
        memset (&route.join_actor, 0, sizeof (route.join_actor));
        if (!submission.add_reply_route (route, &reply_serial))
            return ZLINK_SUBMIT_OUT_OF_MEMORY;

        record->operation_id = op_id;
        record->operation_kind = operation_kind_;
        record->has_reply_token = true;
        seal_reply_token (node_, reply_serial, &record->reply_token);
    }

    const bool blocking = (flags_ & ZLINK_SEND_FLAGS_DONTWAIT) == 0;
    const uint32_t admit_timeout =
      blocking ? static_cast<uint32_t> (node_->sndtimeo_ms < 0 ? 0 : node_->sndtimeo_ms) : 0;
    if (admit_record (node_, destination_, domain_application, record, blocking, admit_timeout)
        != 0) {
        const int reason = errno;
        if (reason == EAGAIN)
            register_local_send_ready_interest (
              node_, interest_, destination_, record->byte_size);
        errno = reason;
        return submit_errno_result ();
    }

    if (is_request) {
        submission.commit ();
        *operation_id_out_ = op_id;
    }

    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        node_->monitor_counters.submitted_messages += 1;
    }
    emit_submit_event (node_, ZLINK_MESH_MONITOR_MESSAGE_SUBMITTED,
                       static_cast<zlink_mesh_owner_kind_t> (destination_.kind), channel_name_,
                       0);
    return ZLINK_SUBMIT_OK;
}

//  Remote submit to an admitted peer: request bookkeeping mirrors the local
//  path but the reply returns over the wire keyed by the operation serial.
zlink_submit_result_t submit_remote (mesh_node_t *node_,
                                     const rid_bytes_t &peer_rid_,
                                     const owner_id_t &requester_,
                                     bool channel_kind_,
                                     const std::string &channel_,
                                     const zlink_mesh_metadata_view_t *metadata_,
                                     const zlink_msg_t *parts_,
                                     size_t part_count_,
                                     zlink_mesh_operation_id_t *operation_id_out_,
                                     zlink_mesh_operation_kind_t operation_kind_,
                                     zlink_send_flags_t flags_,
                                     uint32_t timeout_ms_)
{
    const bool is_request = operation_id_out_ != NULL;
    const send_ready_interest_t interest = channel_kind_
      ? make_channel_send_ready_interest (requester_, channel_)
      : make_node_send_ready_interest (requester_, peer_rid_);
    if (!is_request) {
        const wire_type_t type = channel_kind_ ? wire_channel_send : wire_node_send;
        const zlink_submit_result_t rc = wire_submit_data (
          node_, peer_rid_, type, 0, channel_, metadata_, parts_, part_count_,
          flags_, &interest);
        if (rc == ZLINK_SUBMIT_OK) {
            {
                std::lock_guard<std::mutex> lock (node_->mutex);
                node_->monitor_counters.submitted_messages += 1;
            }
            emit_submit_event (node_, ZLINK_MESH_MONITOR_MESSAGE_SUBMITTED,
                               ZLINK_MESH_OWNER_NODE, channel_, 0);
        } else if (errno == EAGAIN || errno == ETIMEDOUT) {
            const int reason = errno;
            emit_submit_event (node_, ZLINK_MESH_MONITOR_BACKPRESSURED, ZLINK_MESH_OWNER_NODE,
                               channel_, reason);
            errno = reason;
        }
        return rc;
    }

    operation_submission_t submission (
      node_, true, operation_kind_, requester_, timeout_ms_);
    if (!submission.valid ())
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    const zlink_mesh_operation_id_t op_id = submission.operation_id ();
    const wire_type_t type = channel_kind_ ? wire_channel_request : wire_node_request;
    const zlink_submit_result_t rc = wire_submit_data (
      node_, peer_rid_, type, op_id.low, channel_, metadata_, parts_, part_count_,
      flags_, &interest);
    if (rc != ZLINK_SUBMIT_OK) {
        const int reason = errno;
        if (reason == EAGAIN || reason == ETIMEDOUT)
            emit_submit_event (node_, ZLINK_MESH_MONITOR_BACKPRESSURED, ZLINK_MESH_OWNER_NODE,
                               channel_, reason);
        errno = reason;
        return rc;
    }
    submission.commit ();
    *operation_id_out_ = op_id;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        node_->monitor_counters.submitted_messages += 1;
    }
    emit_submit_event (node_, ZLINK_MESH_MONITOR_MESSAGE_SUBMITTED, ZLINK_MESH_OWNER_NODE,
                       channel_, 0);
    return ZLINK_SUBMIT_OK;
}

//  Shared body of the four node/channel submit entry points.
zlink_submit_result_t node_channel_submit (void *mesh_node_,
                                           const zlink_routing_id_t *target_rid_,
                                           const char *channel_name_,
                                           const owner_id_t *requester_,
                                           const rid_bytes_t *source_spot_rid_,
                                           const zlink_mesh_metadata_view_t *metadata_,
                                           const zlink_msg_t *parts_,
                                           size_t part_count_,
                                           zlink_mesh_operation_id_t *operation_id_out_,
                                           zlink_mesh_operation_kind_t operation_kind_,
                                           zlink_send_flags_t flags_,
                                           uint32_t timeout_ms_)
{
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (check_submit_input (metadata_, parts_, part_count_) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if (operation_id_out_)
        memset (operation_id_out_, 0, sizeof (*operation_id_out_));

    const owner_id_t requester = requester_ ? *requester_ : node_owner ();
    const rid_bytes_t source_spot = source_spot_rid_ ? *source_spot_rid_ : rid_bytes_t ();

    if (target_rid_) {
        //  Direct node submit: needs an admitted pipe for the target RID.
        if (target_rid_->size == 0) {
            errno = EINVAL;
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        }
        bool admitted = false;
        rid_bytes_t rid;
        {
            std::unique_lock<std::mutex> lock (node->mutex);
            if (check_submit_state_locked (node) != 0)
                return submit_errno_result ();
            rid = rid_bytes (*target_rid_);
            for (size_t i = 0; i < node->peers.size (); ++i) {
                if (node->peers[i].state == ZLINK_MESH_PEER_ADMITTED
                    && node->peers[i].rid == rid) {
                    admitted = true;
                    break;
                }
            }
        }
        if (!admitted) {
            errno = ENOTCONN;
            return ZLINK_SUBMIT_NOT_CONNECTED;
        }
        return submit_remote (node, rid, requester, false, std::string (), metadata_, parts_,
                              part_count_, operation_id_out_, operation_kind_, flags_,
                              timeout_ms_);
    }

    std::string channel;
    if (check_name (channel_name_, ZLINK_CHANNEL_NAME_MAX, &channel) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;

    channel_target_t target;
    rid_bytes_t remote_rid;
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        if (check_submit_state_locked (node) != 0)
            return submit_errno_result ();
        if (select_channel_target_locked (node, channel, &target) != 0)
            return ZLINK_SUBMIT_NOT_FOUND;
        if (!target.is_local)
            remote_rid = node->peers[target.peer_index].rid;
    }

    if (!target.is_local) {
        return submit_remote (node, remote_rid, requester, true, channel, metadata_, parts_,
                              part_count_, operation_id_out_,
                              operation_id_out_ ? ZLINK_MESH_OPERATION_CHANNEL_REQUEST
                                                : operation_kind_,
                              flags_, timeout_ms_);
    }

    const zlink_mesh_record_kind_t kind = operation_id_out_
                                            ? ZLINK_MESH_RECORD_CHANNEL_REQUEST
                                            : ZLINK_MESH_RECORD_CHANNEL_SEND;
    const send_ready_interest_t interest =
      make_channel_send_ready_interest (requester, channel);
    return submit_local_record (node, node_owner (), requester, interest,
                                kind, source_spot, channel, metadata_, parts_,
                                part_count_, operation_id_out_, operation_kind_,
                                flags_, timeout_ms_);
}
}

//  --- MeshNode node/channel messaging ------------------------------------------

zlink_submit_result_t zlink_mesh_node_send_to_node (void *mesh_node_,
                                                    const zlink_routing_id_t *target_rid_,
                                                    const zlink_mesh_metadata_view_t *metadata_,
                                                    const zlink_msg_t *parts_,
                                                    size_t part_count_,
                                                    zlink_send_flags_t flags_)
try {
    if (!target_rid_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    return node_channel_submit (mesh_node_, target_rid_, NULL, NULL, NULL, metadata_, parts_,
                                part_count_, NULL, static_cast<zlink_mesh_operation_kind_t> (0),
                                flags_, 0);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_mesh_node_request_to_node (void *mesh_node_,
                                                       const zlink_routing_id_t *target_rid_,
                                                       const zlink_mesh_metadata_view_t *metadata_,
                                                       const zlink_msg_t *parts_,
                                                       size_t part_count_,
                                                       zlink_mesh_operation_id_t *operation_id_out_,
                                                       zlink_send_flags_t flags_,
                                                       uint32_t timeout_ms_)
try {
    if (!target_rid_ || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    return node_channel_submit (mesh_node_, target_rid_, NULL, NULL, NULL, metadata_, parts_,
                                part_count_, operation_id_out_, ZLINK_MESH_OPERATION_NODE_REQUEST,
                                flags_, timeout_ms_);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_mesh_node_send_to_channel (void *mesh_node_,
                                                       const char *channel_name_,
                                                       const zlink_mesh_metadata_view_t *metadata_,
                                                       const zlink_msg_t *parts_,
                                                       size_t part_count_,
                                                       zlink_send_flags_t flags_)
try {
    return node_channel_submit (mesh_node_, NULL, channel_name_, NULL, NULL, metadata_, parts_,
                                part_count_, NULL, static_cast<zlink_mesh_operation_kind_t> (0),
                                flags_, 0);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t
zlink_mesh_node_request_to_channel (void *mesh_node_,
                                    const char *channel_name_,
                                    const zlink_mesh_metadata_view_t *metadata_,
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
    return node_channel_submit (mesh_node_, NULL, channel_name_, NULL, NULL, metadata_, parts_,
                                part_count_, operation_id_out_,
                                ZLINK_MESH_OPERATION_CHANNEL_REQUEST, flags_, timeout_ms_);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

//  --- Spot messaging -------------------------------------------------------------

namespace
{
//  Resolves a live facade and its owner id, or fails with the right errno.
int resolve_facade (void *spot_,
                    spot_facade_t **facade_out_,
                    mesh_node_t **node_out_,
                    owner_id_t *owner_out_)
{
    spot_facade_t *facade = as_spot_facade (spot_);
    if (!facade) {
        errno = EFAULT;
        return -1;
    }
    *facade_out_ = facade;
    *node_out_ = facade->node;
    if (owner_out_)
        *owner_out_ = spot_owner (facade->spot_rid, facade->generation);
    return 0;
}
}

zlink_submit_result_t zlink_spot_send_to_channel (void *spot_,
                                                  const char *channel_name_,
                                                  const zlink_mesh_metadata_view_t *metadata_,
                                                  const zlink_msg_t *parts_,
                                                  size_t part_count_,
                                                  zlink_send_flags_t flags_)
try {
    spot_facade_t *facade;
    mesh_node_t *node;
    owner_id_t owner;
    if (resolve_facade (spot_, &facade, &node, &owner) != 0)
        return ZLINK_SUBMIT_INVALID_HANDLE;
    return node_channel_submit (node, NULL, channel_name_, &owner, &facade->spot_rid, metadata_,
                                parts_, part_count_, NULL,
                                static_cast<zlink_mesh_operation_kind_t> (0), flags_, 0);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_spot_request_to_channel (void *spot_,
                                                     const char *channel_name_,
                                                     const zlink_mesh_metadata_view_t *metadata_,
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
    spot_facade_t *facade;
    mesh_node_t *node;
    owner_id_t owner;
    if (resolve_facade (spot_, &facade, &node, &owner) != 0)
        return ZLINK_SUBMIT_INVALID_HANDLE;
    return node_channel_submit (node, NULL, channel_name_, &owner, &facade->spot_rid, metadata_,
                                parts_, part_count_, operation_id_out_,
                                ZLINK_MESH_OPERATION_SPOT_REQUEST, flags_, timeout_ms_);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

namespace
{
zlink_submit_result_t spot_direct_submit (void *spot_,
                                          const zlink_routing_id_t *target_node_rid_,
                                          const zlink_routing_id_t *target_spot_rid_,
                                          uint64_t target_spot_generation_,
                                          const zlink_mesh_metadata_view_t *metadata_,
                                          const zlink_msg_t *parts_,
                                          size_t part_count_,
                                          zlink_mesh_operation_id_t *operation_id_out_,
                                          zlink_send_flags_t flags_,
                                          uint32_t timeout_ms_)
{
    spot_facade_t *facade;
    mesh_node_t *node;
    owner_id_t requester;
    if (resolve_facade (spot_, &facade, &node, &requester) != 0)
        return ZLINK_SUBMIT_INVALID_HANDLE;
    if (!target_node_rid_ || target_node_rid_->size == 0 || !target_spot_rid_
        || target_spot_rid_->size == 0 || target_spot_generation_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (check_submit_input (metadata_, parts_, part_count_) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if (operation_id_out_)
        memset (operation_id_out_, 0, sizeof (*operation_id_out_));

    const rid_bytes_t target_node = rid_bytes (*target_node_rid_);
    const rid_bytes_t target_spot = rid_bytes (*target_spot_rid_);

    owner_id_t destination;
    bool local_target_found = false;
    bool generation_conflict = false;
    bool instance_busy = false;
    bool remote_target = false;
    uint64_t remote_target_generation = 0;
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        if (check_submit_state_locked (node) != 0)
            return submit_errno_result ();
        if (target_node != node->routing_id) {
            //  Remote node targets require an admitted pipe.
            bool admitted = false;
            for (size_t i = 0; i < node->peers.size (); ++i) {
                if (node->peers[i].state == ZLINK_MESH_PEER_ADMITTED
                    && node->peers[i].rid == target_node) {
                    admitted = true;
                    remote_target_generation =
                      node->peers[i].lifecycle_generation;
                    break;
                }
            }
            if (!admitted) {
                errno = ENOTCONN;
                return ZLINK_SUBMIT_NOT_CONNECTED;
            }
            remote_target = true;
        }
        if (!remote_target) {
            const std::string key (target_spot.begin (), target_spot.end ());
            std::map<std::string, spot_state_t>::iterator it = node->spots.find (key);
            if (it != node->spots.end ()) {
                if (it->second.generation == target_spot_generation_) {
                    if (it->second.kind == ZLINK_SPOT_KIND_INSTANCE) {
                        std::map<std::string, instance_activation_state_t>::iterator
                          activation = node->instance_activations.find (key);
                        const uint64_t now_ns =
                          zlink::request_timeout::monotonic_now_ns ();
                        if (activation == node->instance_activations.end ()
                            || activation->second.spot_generation
                                 != it->second.generation
                            || activation->second.state
                                 != ZLINK_SPOT_ACTIVATION_READY
                            || activation->second.owner_deadline_ns == 0
                            || now_ns
                                 >= activation->second.owner_deadline_ns) {
                            instance_busy = true;
                            if (activation != node->instance_activations.end ()
                                && activation->second.state
                                     == ZLINK_SPOT_ACTIVATION_READY
                                && activation->second.owner_deadline_ns != 0
                                && now_ns
                                     >= activation->second.owner_deadline_ns) {
                                activation->second.state =
                                  ZLINK_SPOT_ACTIVATION_CLOSING;
                                it->second.activation_state =
                                  ZLINK_SPOT_ACTIVATION_CLOSING;
                                it->second.draining = true;
                            }
                        } else {
                            local_target_found = true;
                            destination = spot_owner (
                              target_spot, it->second.generation);
                        }
                    } else {
                        local_target_found = true;
                        destination = spot_owner (
                          target_spot, it->second.generation);
                    }
                } else {
                    generation_conflict = true;
                }
            }
        }
    }

    const bool is_request = operation_id_out_ != NULL;
    if (remote_target) {
        remote_route_flight_guard_t route_flight (node, is_request);
        if (!route_flight.valid ())
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        uint64_t expected_connection_id = 0;
        if (!validate_remote_route_flight (
              node, target_node, remote_target_generation,
              &expected_connection_id))
            return ZLINK_SUBMIT_NOT_CONNECTED;
        if (!is_request) {
            const send_ready_interest_t interest =
              make_spot_send_ready_interest (
                requester, target_node, target_spot);
            const zlink_submit_result_t rc =
              wire_submit_spot (node, target_node, false, 0, facade->spot_rid, target_spot,
                                target_spot_generation_, metadata_, parts_, part_count_, flags_,
                                &interest, expected_connection_id);
            return rc;
        }
        operation_submission_t submission (
          node, true, ZLINK_MESH_OPERATION_SPOT_REQUEST, requester, timeout_ms_);
        if (!submission.valid ())
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        const zlink_mesh_operation_id_t op_id = submission.operation_id ();
        const send_ready_interest_t interest =
          make_spot_send_ready_interest (requester, target_node, target_spot);
        const zlink_submit_result_t rc =
          wire_submit_spot (node, target_node, true, op_id.low, facade->spot_rid, target_spot,
                            target_spot_generation_, metadata_, parts_, part_count_, flags_,
                            &interest, expected_connection_id);
        if (rc != ZLINK_SUBMIT_OK) {
            return rc;
        }
        submission.commit ();
        *operation_id_out_ = op_id;
        return ZLINK_SUBMIT_OK;
    }
    if (!local_target_found) {
        if (!is_request) {
            //  One-way send: submit succeeds, remote/local absence is not
            //  reported back per the Spot contract.
            return ZLINK_SUBMIT_OK;
        }
        //  Request: admission succeeded, the terminal completion carries the
        //  lookup failure.
        operation_submission_t submission (
          node, true, ZLINK_MESH_OPERATION_SPOT_REQUEST, requester, 0);
        if (!submission.valid ())
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        const zlink_mesh_operation_id_t op_id = submission.operation_id ();
        pending_operation_t op;
        {
            std::lock_guard<std::mutex> lock (node->mutex);
            std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
              node->operations.find (op_id.low);
            op = op_it->second;
        }
        const int completion_rc =
          instance_busy
            ? complete_pending_operation (
                node, op, ZLINK_REQUEST_BUSY, EBUSY, NULL, NULL)
          : generation_conflict
            ? complete_pending_operation (
                node, op, ZLINK_REQUEST_CONFLICT, ESTALE, NULL, NULL)
            : complete_pending_operation (
                node, op, ZLINK_REQUEST_NOT_FOUND, ENOENT, NULL, NULL);
        if (completion_rc < 0)
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        submission.commit ();
        *operation_id_out_ = op_id;
        return ZLINK_SUBMIT_OK;
    }

    const send_ready_interest_t interest = make_spot_send_ready_interest (
      requester, node->routing_id, target_spot);
    return submit_local_record (node, destination, requester, interest,
                                is_request ? ZLINK_MESH_RECORD_SPOT_REQUEST
                                           : ZLINK_MESH_RECORD_SPOT_SEND,
                                facade->spot_rid, std::string (), metadata_, parts_, part_count_,
                                operation_id_out_, ZLINK_MESH_OPERATION_SPOT_REQUEST, flags_,
                                timeout_ms_);
}
}

zlink_submit_result_t zlink_spot_send_to_spot (void *spot_,
                                               const zlink_routing_id_t *target_node_rid_,
                                               const zlink_routing_id_t *target_spot_rid_,
                                               uint64_t target_spot_generation_,
                                               const zlink_mesh_metadata_view_t *metadata_,
                                               const zlink_msg_t *parts_,
                                               size_t part_count_,
                                               zlink_send_flags_t flags_)
try {
    return spot_direct_submit (spot_, target_node_rid_, target_spot_rid_, target_spot_generation_,
                               metadata_, parts_, part_count_, NULL, flags_, 0);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_spot_request_to_spot (void *spot_,
                                                  const zlink_routing_id_t *target_node_rid_,
                                                  const zlink_routing_id_t *target_spot_rid_,
                                                  uint64_t target_spot_generation_,
                                                  const zlink_mesh_metadata_view_t *metadata_,
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
    return spot_direct_submit (spot_, target_node_rid_, target_spot_rid_, target_spot_generation_,
                               metadata_, parts_, part_count_, operation_id_out_, flags_,
                               timeout_ms_);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

namespace
{
struct instance_watchdog_ctx_t
{
    mesh_node_t *node;
    uint64_t node_generation;
    uint64_t token_serial;
};

struct instance_deadline_ctx_t
{
    mesh_node_t *node;
    uint64_t node_generation;
    uint64_t activation_token_serial;
    rid_bytes_t source_node_rid;
    zlink_mesh_operation_id_t operation_id;
};

bool copy_instance_string (const char *data_,
                           size_t size_,
                           size_t max_,
                           bool allow_empty_,
                           std::string *out_)
{
    if ((!data_ && size_ != 0) || (!allow_empty_ && size_ == 0)
        || size_ > max_ || (size_ > 0 && memchr (data_, 0, size_))) {
        errno = EINVAL;
        return false;
    }
    if (size_ > 0
        && !valid_utf8 (reinterpret_cast<const unsigned char *> (data_), size_)) {
        errno = EINVAL;
        return false;
    }
    out_->assign (data_ ? data_ : "", size_);
    return true;
}

bool copy_instance_placement (const zlink_instance_spot_placement_t *target_,
                           instance_placement_value_t *out_)
{
    if (!target_ || target_->node_rid.size == 0 || target_->spot_rid.size == 0
        || target_->node_generation == 0) {
        errno = EINVAL;
        return false;
    }
    instance_placement_value_t value;
    value.node_rid = rid_bytes (target_->node_rid);
    value.node_generation = target_->node_generation;
    value.spot_rid = rid_bytes (target_->spot_rid);
    if (!copy_instance_string (
          target_->instance_spot_type, target_->instance_spot_type_size,
          ZLINK_INSTANCE_SPOT_TYPE_MAX, false, &value.instance_spot_type)
        || !copy_instance_string (
          target_->message_contract_id, target_->message_contract_id_size,
          ZLINK_INSTANCE_SPOT_CONTRACT_ID_MAX, false,
          &value.message_contract_id))
        return false;
    *out_ = value;
    return true;
}

void finish_instance_record (
  std::unique_ptr<queued_record_t> record,
  zlink_request_result_t terminal_result_,
  int failure_errno_)
{
    if (!record.get ())
        return;
    if (record->instance_deadline_task) {
        zlink::request_timeout::cancel (record->instance_deadline_task);
        record->instance_deadline_task.reset ();
    }
    if (record->has_reply_token) {
        mesh_node_t *route_node = NULL;
        uint64_t route_serial = 0;
        std::vector<zlink_msg_t> empty;
        if (unseal_reply_token (&record->reply_token, &route_node,
                                &route_serial)
              == 0) {
            try {
                (void) deliver_reply_via_route (
                  route_node, route_serial, terminal_result_, failure_errno_,
                  &empty);
            }
            catch (const std::bad_alloc &) {
                std::lock_guard<std::mutex> lock (route_node->mutex);
                std::unordered_map<uint64_t, reply_route_t>::iterator route =
                  route_node->reply_routes.find (route_serial);
                if (route != route_node->reply_routes.end ()
                    && !route->second.consumed) {
                    route->second.in_flight = false;
                    route->second.force_terminal_pending = true;
                    route->second.force_terminal_result = terminal_result_;
                    route->second.force_terminal_errno = failure_errno_;
                }
            }
        }
    }
}

void finish_instance_records (
  std::list<std::unique_ptr<queued_record_t> > *records_,
  zlink_request_result_t terminal_result_,
  int failure_errno_)
{
    while (!records_->empty ()) {
        std::unique_ptr<queued_record_t> record =
          std::move (records_->front ());
        records_->pop_front ();
        finish_instance_record (
          std::move (record), terminal_result_, failure_errno_);
    }
}

bool same_instance_operation (const queued_record_t &record_,
                              const rid_bytes_t &source_node_rid_,
                              const zlink_mesh_operation_id_t &operation_id_)
{
    return record_.has_reply_token
           && record_.source_node_rid == source_node_rid_
           && record_.operation_id.high == operation_id_.high
           && record_.operation_id.low == operation_id_.low;
}

void insert_instance_pending_ordered (
  instance_activation_state_t *group_,
  std::unique_ptr<queued_record_t> &record_)
{
    const uint64_t sequence = record_->instance_admission_sequence;
    std::list<std::unique_ptr<queued_record_t> >::iterator position =
      group_->pending.begin ();
    while (position != group_->pending.end ()
           && (*position)->instance_admission_sequence < sequence)
        ++position;
    group_->pending.insert (position, std::move (record_));
}

bool remove_instance_deadline_record_locked (
  mesh_node_t *node_,
  const rid_bytes_t &source_node_rid_,
  const zlink_mesh_operation_id_t &operation_id_,
  std::list<std::unique_ptr<queued_record_t> > *expired_out_,
  std::shared_ptr<zlink::request_timeout::task_t> *watchdog_out_)
{
    for (std::unordered_map<uint64_t, instance_token_state_t>::iterator it =
           node_->instance_tokens.begin ();
         it != node_->instance_tokens.end (); ++it) {
        if (it->second.phase != instance_token_placement
            || !it->second.pending_record
            || !same_instance_operation (*it->second.pending_record,
                                         source_node_rid_, operation_id_))
            continue;
        if (it->second.watchdog)
            *watchdog_out_ = it->second.watchdog;
        expired_out_->push_back (std::move (it->second.pending_record));
        node_->instance_tokens.erase (it);
        node_->cv.notify_all ();
        return true;
    }
    for (std::map<std::string, instance_activation_state_t>::iterator group =
           node_->instance_activations.begin ();
         group != node_->instance_activations.end (); ++group) {
        for (std::list<std::unique_ptr<queued_record_t> >::iterator it =
               group->second.pending.begin ();
             it != group->second.pending.end (); ++it) {
            if (!same_instance_operation (**it, source_node_rid_, operation_id_))
                continue;
            group->second.pending_messages -= 1;
            group->second.pending_bytes -= (*it)->byte_size;
            expired_out_->splice (expired_out_->end (),
                                  group->second.pending, it);
            node_->cv.notify_all ();
            return true;
        }
    }
    return false;
}

void on_instance_record_deadline (void *userdata_)
{
    std::unique_ptr<instance_deadline_ctx_t> ctx (
      static_cast<instance_deadline_ctx_t *> (userdata_));
    if (!ctx.get ())
        return;
    mesh_node_pin_t node_pin (ctx->node);
    mesh_node_t *node = node_pin.get ();
    if (!node || node->lifecycle_generation != ctx->node_generation)
        return;
    std::list<std::unique_ptr<queued_record_t> > expired;
    std::shared_ptr<zlink::request_timeout::task_t> watchdog;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        if (ctx->activation_token_serial != 0) {
            std::unordered_map<uint64_t, instance_token_state_t>::iterator token =
              node->instance_tokens.find (ctx->activation_token_serial);
            if (token != node->instance_tokens.end ()
                && token->second.phase == instance_token_redirecting) {
                token->second.request_deadline_expired = true;
                node->cv.notify_all ();
                return;
            }
        }
        (void) remove_instance_deadline_record_locked (
          node, ctx->source_node_rid, ctx->operation_id, &expired, &watchdog);
    }
    if (watchdog)
        zlink::request_timeout::cancel (watchdog);
    finish_instance_records (&expired, ZLINK_REQUEST_TIMED_OUT, ETIMEDOUT);
}

void remove_instance_activation_locked (
  mesh_node_t *node_,
  const std::string &key_,
  std::list<std::unique_ptr<queued_record_t> > *pending_out_)
{
    std::map<std::string, instance_activation_state_t>::iterator activation =
      node_->instance_activations.find (key_);
    if (activation == node_->instance_activations.end ())
        return;
    pending_out_->splice (pending_out_->end (), activation->second.pending);
    activation->second.pending_messages = 0;
    activation->second.pending_bytes = 0;
    std::map<std::string, spot_state_t>::iterator spot =
      node_->spots.find (key_);
    if (spot != node_->spots.end ()) {
        spot->second.activation_state = ZLINK_SPOT_ACTIVATION_CLOSING;
        spot->second.draining = true;
        maybe_end_spot_locked (node_, key_);
    }
    node_->cv.notify_all ();
}

void on_instance_watchdog (void *userdata_)
{
    std::unique_ptr<instance_watchdog_ctx_t> ctx (
      static_cast<instance_watchdog_ctx_t *> (userdata_));
    if (!ctx.get ())
        return;
    mesh_node_pin_t node_pin (ctx->node);
    mesh_node_t *node = node_pin.get ();
    if (!node || node->lifecycle_generation != ctx->node_generation)
        return;
    std::list<std::unique_ptr<queued_record_t> > pending;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, instance_token_state_t>::iterator token =
          node->instance_tokens.find (ctx->token_serial);
        if (token == node->instance_tokens.end ())
            return;
        if (token->second.phase == instance_token_redirecting) {
            token->second.watchdog_expired = true;
            return;
        } else if (token->second.phase == instance_token_placement) {
            if (token->second.pending_record)
                pending.push_back (std::move (token->second.pending_record));
        } else {
            const std::string key (token->second.target.spot_rid.begin (),
                                   token->second.target.spot_rid.end ());
            remove_instance_activation_locked (node, key, &pending);
        }
        node->instance_tokens.erase (token);
        node->cv.notify_all ();
    }
    finish_instance_records (&pending, ZLINK_REQUEST_TIMED_OUT, ETIMEDOUT);
}

std::shared_ptr<zlink::request_timeout::task_t>
schedule_instance_watchdog (mesh_node_t *node_,
                            uint64_t serial_,
                            uint32_t timeout_ms_)
{
    std::unique_ptr<instance_watchdog_ctx_t> ctx (
      new (std::nothrow) instance_watchdog_ctx_t ());
    if (!ctx.get ()) {
        errno = ENOMEM;
        return std::shared_ptr<zlink::request_timeout::task_t> ();
    }
    ctx->node = node_;
    ctx->node_generation = node_->lifecycle_generation;
    ctx->token_serial = serial_;
    std::shared_ptr<zlink::request_timeout::task_t> task =
      zlink::request_timeout::schedule (
        timeout_ms_,
        &on_instance_watchdog, ctx.get (),
        &zlink::request_reply_runtime::destroy_timeout_callback_ctx<
          instance_watchdog_ctx_t>);
    if (task)
        ctx.release ();
    else
        errno = ENOMEM;
    return task;
}

uint32_t remaining_instance_timeout_ms (uint64_t deadline_ns_)
{
    const uint64_t now_ns = zlink::request_timeout::monotonic_now_ns ();
    if (deadline_ns_ == 0 || now_ns >= deadline_ns_)
        return 0;
    const uint64_t remaining_ns = deadline_ns_ - now_ns;
    const uint64_t remaining_ms =
      (remaining_ns + static_cast<uint64_t> (999999))
      / static_cast<uint64_t> (1000000);
    return static_cast<uint32_t> (
      std::min<uint64_t> (remaining_ms, UINT32_MAX));
}

zlink_request_result_t instance_failure_result (int error_)
{
    switch (error_) {
        case ETIMEDOUT:
            return ZLINK_REQUEST_TIMED_OUT;
        case EAGAIN:
        case ENOBUFS:
            return ZLINK_REQUEST_BACKPRESSURED;
        case EEXIST:
        case ESTALE:
            return ZLINK_REQUEST_CONFLICT;
        case EBUSY:
        case ESHUTDOWN:
            return ZLINK_REQUEST_BUSY;
        case ENOENT:
            return ZLINK_REQUEST_NOT_FOUND;
        default:
            return ZLINK_REQUEST_INTERNAL_ERROR;
    }
}

bool valid_instance_abort_result (zlink_request_result_t result_)
{
    return result_ != ZLINK_REQUEST_OK
           && result_ >= ZLINK_REQUEST_TIMED_OUT
           && result_ <= ZLINK_REQUEST_BACKPRESSURED;
}
}

namespace zlink
{
namespace mesh
{
int arm_instance_record_deadline (
  mesh_node_t *node_,
  const rid_bytes_t &source_node_rid_,
  const zlink_mesh_operation_id_t &operation_id_)
{
    if (!node_ || operation_id_.high == 0 || operation_id_.low == 0) {
        errno = EINVAL;
        return -1;
    }
    std::list<std::unique_ptr<queued_record_t> > terminal;
    std::shared_ptr<zlink::request_timeout::task_t> activation_watchdog;
    int terminal_errno = 0;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        queued_record_t *record = NULL;
        uint64_t activation_token_serial = 0;
        for (std::unordered_map<uint64_t, instance_token_state_t>::iterator it =
               node_->instance_tokens.begin ();
             it != node_->instance_tokens.end () && !record; ++it) {
            if (it->second.phase == instance_token_placement
                && it->second.pending_record
                && same_instance_operation (*it->second.pending_record,
                                            source_node_rid_, operation_id_))
            {
                record = it->second.pending_record.get ();
                activation_token_serial = it->first;
            }
        }
        for (std::map<std::string, instance_activation_state_t>::iterator group =
               node_->instance_activations.begin ();
             group != node_->instance_activations.end () && !record; ++group) {
            for (std::list<std::unique_ptr<queued_record_t> >::iterator it =
                   group->second.pending.begin ();
                 it != group->second.pending.end (); ++it) {
                if (same_instance_operation (**it, source_node_rid_,
                                             operation_id_)) {
                    record = it->get ();
                    break;
                }
            }
        }
        //  An owner in Ready state admits directly to its application
        //  mailbox. The source request timer owns completion from that point.
        if (!record || record->deadline_ns == 0
            || record->instance_deadline_task)
            return 0;

        const uint64_t now_ns = zlink::request_timeout::monotonic_now_ns ();
        if (now_ns >= record->deadline_ns) {
            (void) remove_instance_deadline_record_locked (
              node_, source_node_rid_, operation_id_, &terminal,
              &activation_watchdog);
            terminal_errno = ETIMEDOUT;
        } else {
            const uint64_t remaining_ns = record->deadline_ns - now_ns;
            uint64_t remaining_ms =
              (remaining_ns + static_cast<uint64_t> (999999))
              / static_cast<uint64_t> (1000000);
            if (remaining_ms > UINT32_MAX)
                remaining_ms = UINT32_MAX;
            std::unique_ptr<instance_deadline_ctx_t> ctx (
              new (std::nothrow) instance_deadline_ctx_t ());
            if (ctx.get ()) {
                ctx->node = node_;
                ctx->node_generation = node_->lifecycle_generation;
                ctx->activation_token_serial = activation_token_serial;
                ctx->source_node_rid = source_node_rid_;
                ctx->operation_id = operation_id_;
                std::shared_ptr<zlink::request_timeout::task_t> task =
                  zlink::request_timeout::schedule (
                    static_cast<uint32_t> (remaining_ms),
                    &on_instance_record_deadline, ctx.get (),
                    &zlink::request_reply_runtime::destroy_timeout_callback_ctx<
                      instance_deadline_ctx_t>);
                if (task) {
                    ctx.release ();
                    record->instance_deadline_task = task;
                }
            }
            if (!record->instance_deadline_task) {
                (void) remove_instance_deadline_record_locked (
                  node_, source_node_rid_, operation_id_, &terminal,
                  &activation_watchdog);
                terminal_errno = ENOMEM;
            }
        }
    }
    if (activation_watchdog)
        zlink::request_timeout::cancel (activation_watchdog);
    if (terminal_errno != 0)
        finish_instance_records (
          &terminal,
          terminal_errno == ETIMEDOUT ? ZLINK_REQUEST_TIMED_OUT
                                      : ZLINK_REQUEST_INTERNAL_ERROR,
          terminal_errno);
    return 0;
}

int admit_instance_record (mesh_node_t *node_,
                           const instance_placement_value_t &target_,
                           std::unique_ptr<queued_record_t> &record_)
{
    if (!node_ || !record_.get ()) {
        errno = EFAULT;
        return -1;
    }
    const std::string key (target_.spot_rid.begin (), target_.spot_rid.end ());
    {
        uint64_t serial = 0;
        uint64_t activation_deadline_ns = 0;
        std::unique_ptr<queued_record_t> activation_record (
          new (std::nothrow) queued_record_t ());
        if (!activation_record.get ()) {
            errno = ENOMEM;
            return -1;
        }
        instance_token_state_t token;
        zlink_instance_spot_activation_data_t data;
        memset (&data, 0, sizeof (data));
        data.spot_rid = rid_value (target_.spot_rid);
        data.operation_kind = record_->has_reply_token
                                ? ZLINK_INSTANCE_SPOT_OPERATION_REQUEST
                                : ZLINK_INSTANCE_SPOT_OPERATION_SEND;
        snprintf (data.instance_spot_type, sizeof (data.instance_spot_type),
                  "%s", target_.instance_spot_type.c_str ());
        snprintf (data.message_contract_id,
                  sizeof (data.message_contract_id), "%s",
                  target_.message_contract_id.c_str ());
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            if (node_->state == ZLINK_MESH_NODE_DRAINING
                || node_->state == ZLINK_MESH_NODE_STOPPED) {
                errno = ESHUTDOWN;
                return -1;
            }
            if (node_->state == ZLINK_MESH_NODE_CREATED
                || target_.node_rid != node_->routing_id
                || target_.node_generation != node_->lifecycle_generation) {
                errno = target_.node_generation != node_->lifecycle_generation
                          ? ESTALE
                          : EINVAL;
                return -1;
            }
            const uint64_t message_limit = std::min (
              node_->effective_instance_activation_message_budget (),
              node_->effective_message_budget ());
            const uint64_t byte_limit = std::min (
              node_->effective_instance_activation_byte_budget (),
              node_->effective_byte_budget ());
            uint64_t provisional_messages = 0;
            uint64_t provisional_bytes = 0;
            for (std::unordered_map<uint64_t, instance_token_state_t>::const_iterator it =
                   node_->instance_tokens.begin ();
                 it != node_->instance_tokens.end (); ++it) {
                if (it->second.phase != instance_token_placement
                    || it->second.target.spot_rid != target_.spot_rid
                    || it->second.target.instance_spot_type
                         != target_.instance_spot_type
                    || !it->second.pending_record)
                    continue;
                provisional_messages += 1;
                provisional_bytes += it->second.pending_record->byte_size;
            }
            std::map<std::string, instance_activation_state_t>::const_iterator
              active = node_->instance_activations.find (key);
            if (active != node_->instance_activations.end ()
                && active->second.state == ZLINK_SPOT_ACTIVATION_ACTIVATING
                && active->second.instance_spot_type
                     == target_.instance_spot_type) {
                provisional_messages += active->second.pending_messages;
                provisional_bytes += active->second.pending_bytes;
            }
            if (provisional_messages >= message_limit
                || record_->byte_size > byte_limit
                || provisional_bytes > byte_limit - record_->byte_size) {
                errno = EAGAIN;
                return -1;
            }
            serial = node_->next_instance_token_serial++;
            if (serial == 0) {
                errno = EOVERFLOW;
                return -1;
            }
            token.serial = serial;
            token.target = target_;
            const uint32_t activation_timeout_ms =
              node_->effective_instance_activation_timeout_ms ();
            token.activation_deadline_ns =
              zlink::request_timeout::deadline_after_ms (
                activation_timeout_ms);
            activation_deadline_ns = token.activation_deadline_ns;
            if (record_->instance_admission_sequence == 0) {
                record_->instance_admission_sequence =
                  node_->next_instance_admission_sequence++;
                if (record_->instance_admission_sequence == 0) {
                    errno = EOVERFLOW;
                    return -1;
                }
            }
            token.pending_record = std::move (record_);
            seal_instance_token (node_, serial, &data.token);
            activation_record->kind =
              ZLINK_MESH_RECORD_INSTANCE_SPOT_ACTIVATION;
            activation_record->source_node_rid =
              token.pending_record->source_node_rid;
            activation_record->source_spot_rid =
              token.pending_record->source_spot_rid;
            activation_record->has_metadata =
              token.pending_record->has_metadata;
            activation_record->application_metadata =
              token.pending_record->application_metadata;
            activation_record->kind_data.assign (
              reinterpret_cast<unsigned char *> (&data),
              reinterpret_cast<unsigned char *> (&data) + sizeof (data));
            activation_record->byte_size =
              sizeof (data) + activation_record->application_metadata.size ();
            try {
                node_->instance_tokens[serial] = std::move (token);
            }
            catch (const std::bad_alloc &) {
                record_ = std::move (token.pending_record);
                errno = ENOMEM;
                return -1;
            }
        }
        if (admit_record (node_, node_owner (), domain_infrastructure,
                          activation_record, false, 0)
            != 0) {
            std::lock_guard<std::mutex> lock (node_->mutex);
            std::unordered_map<uint64_t, instance_token_state_t>::iterator it =
              node_->instance_tokens.find (serial);
            if (it != node_->instance_tokens.end ()) {
                record_ = std::move (it->second.pending_record);
                node_->instance_tokens.erase (it);
                node_->cv.notify_all ();
            }
            return -1;
        }
        const uint32_t activation_timeout_ms =
          remaining_instance_timeout_ms (activation_deadline_ns);
        if (activation_timeout_ms == 0) {
            std::list<std::unique_ptr<queued_record_t> > pending;
            {
                std::lock_guard<std::mutex> lock (node_->mutex);
                std::unordered_map<uint64_t, instance_token_state_t>::iterator it =
                  node_->instance_tokens.find (serial);
                if (it != node_->instance_tokens.end ()) {
                    if (it->second.phase == instance_token_placement
                        && it->second.pending_record)
                        pending.push_back (
                          std::move (it->second.pending_record));
                    else if (it->second.phase
                             == instance_token_authorized_leader) {
                        const std::string key (
                          it->second.target.spot_rid.begin (),
                          it->second.target.spot_rid.end ());
                        remove_instance_activation_locked (
                          node_, key, &pending);
                    }
                    node_->instance_tokens.erase (it);
                    node_->cv.notify_all ();
                }
            }
            finish_instance_records (
              &pending, ZLINK_REQUEST_TIMED_OUT, ETIMEDOUT);
            return 0;
        }
        std::shared_ptr<zlink::request_timeout::task_t> watchdog =
          schedule_instance_watchdog (
            node_, serial, activation_timeout_ms);
        if (!watchdog) {
            std::list<std::unique_ptr<queued_record_t> > pending;
            {
                std::lock_guard<std::mutex> lock (node_->mutex);
                std::unordered_map<uint64_t, instance_token_state_t>::iterator it =
                  node_->instance_tokens.find (serial);
                if (it != node_->instance_tokens.end ()) {
                    if (it->second.phase == instance_token_placement
                        && it->second.pending_record)
                        pending.push_back (std::move (it->second.pending_record));
                    else if (it->second.phase
                             == instance_token_authorized_leader) {
                        const std::string key (
                          it->second.target.spot_rid.begin (),
                          it->second.target.spot_rid.end ());
                        remove_instance_activation_locked (
                          node_, key, &pending);
                    }
                    node_->instance_tokens.erase (it);
                    node_->cv.notify_all ();
                }
            }
            finish_instance_records (&pending, ZLINK_REQUEST_INTERNAL_ERROR,
                                     ENOMEM);
            return 0;
        }
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            std::unordered_map<uint64_t, instance_token_state_t>::iterator it =
              node_->instance_tokens.find (serial);
            if (it != node_->instance_tokens.end ()
                && it->second.phase == instance_token_placement) {
                it->second.watchdog = watchdog;
                watchdog.reset ();
            } else if (it != node_->instance_tokens.end ()
                       && it->second.phase
                            == instance_token_authorized_leader) {
                const std::string key (it->second.target.spot_rid.begin (),
                                       it->second.target.spot_rid.end ());
                std::map<std::string, instance_activation_state_t>::iterator group =
                  node_->instance_activations.find (key);
                if (group != node_->instance_activations.end ()
                    && group->second.leader_token_serial == serial
                    && group->second.state
                         == ZLINK_SPOT_ACTIVATION_ACTIVATING) {
                    it->second.watchdog = watchdog;
                    group->second.watchdog = watchdog;
                    watchdog.reset ();
                }
            }
        }
        //  Shutdown, a fast consumer or redirect may have consumed or moved
        //  the token before this task can be stored. Cancel that orphan now
        //  instead of retaining its callback context until the deadline.
        if (watchdog)
            zlink::request_timeout::cancel (watchdog);
        return 0;
    }

}

int admit_instance_direct_record (
  mesh_node_t *node_,
  const rid_bytes_t &target_spot_rid_,
  uint64_t target_spot_generation_,
  std::unique_ptr<queued_record_t> &record_)
{
    if (!node_ || !record_.get () || target_spot_rid_.empty ()
        || target_spot_generation_ == 0) {
        errno = EINVAL;
        return -1;
    }
    owner_id_t destination;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        const std::string key (target_spot_rid_.begin (),
                               target_spot_rid_.end ());
        std::map<std::string, spot_state_t>::iterator spot =
          node_->spots.find (key);
        if (spot == node_->spots.end ()) {
            errno = ENOENT;
            return -1;
        }
        if (spot->second.generation != target_spot_generation_) {
            errno = ESTALE;
            return -1;
        }
        std::map<std::string, instance_activation_state_t>::iterator activation =
          node_->instance_activations.find (key);
        const uint64_t now_ns = zlink::request_timeout::monotonic_now_ns ();
        if (spot->second.kind != ZLINK_SPOT_KIND_INSTANCE
            || activation == node_->instance_activations.end ()
            || activation->second.spot_generation != spot->second.generation) {
            errno = EEXIST;
            return -1;
        }
        if (activation->second.state != ZLINK_SPOT_ACTIVATION_READY
            || activation->second.owner_deadline_ns == 0
            || now_ns >= activation->second.owner_deadline_ns) {
            if (activation->second.state == ZLINK_SPOT_ACTIVATION_READY
                && activation->second.owner_deadline_ns != 0
                && now_ns >= activation->second.owner_deadline_ns) {
                activation->second.state = ZLINK_SPOT_ACTIVATION_CLOSING;
                spot->second.activation_state = ZLINK_SPOT_ACTIVATION_CLOSING;
                spot->second.draining = true;
            }
            errno = EBUSY;
            return -1;
        }
        destination = spot_owner (target_spot_rid_, spot->second.generation);
    }
    return admit_record (
      node_, destination, domain_application, record_, false, 0);
}

void terminate_instance_activations (
  mesh_node_t *node_,
  zlink_request_result_t terminal_result_,
  int failure_errno_)
{
    std::list<std::unique_ptr<queued_record_t> > pending;
    while (true) {
        std::shared_ptr<zlink::request_timeout::task_t> watchdog;
        std::unique_ptr<queued_record_t> record;
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            if (node_->instance_tokens.empty ())
                break;
            std::unordered_map<uint64_t, instance_token_state_t>::iterator it =
              node_->instance_tokens.begin ();
            watchdog = it->second.watchdog;
            if (it->second.phase == instance_token_placement)
                record = std::move (it->second.pending_record);
            node_->instance_tokens.erase (it);
            node_->cv.notify_all ();
        }
        if (watchdog)
            zlink::request_timeout::cancel (watchdog);
        finish_instance_record (
          std::move (record), terminal_result_, failure_errno_);
    }

    while (true) {
        std::shared_ptr<zlink::request_timeout::task_t> watchdog;
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            for (std::map<std::string, instance_activation_state_t>::iterator it =
                   node_->instance_activations.begin ();
                 it != node_->instance_activations.end (); ++it) {
                if (it->second.watchdog) {
                    watchdog = it->second.watchdog;
                    it->second.watchdog.reset ();
                    break;
                }
            }
        }
        if (!watchdog)
            break;
        zlink::request_timeout::cancel (watchdog);
    }

    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        for (std::map<std::string, instance_activation_state_t>::iterator it =
               node_->instance_activations.begin ();
             it != node_->instance_activations.end (); ++it) {
            pending.splice (pending.end (), it->second.pending);
            it->second.pending_messages = 0;
            it->second.pending_bytes = 0;
            it->second.state = ZLINK_SPOT_ACTIVATION_CLOSING;
            it->second.owner_deadline_ns = 0;
            std::map<std::string, spot_state_t>::iterator spot =
              node_->spots.find (it->first);
            if (spot != node_->spots.end ()) {
                spot->second.activation_state =
                  ZLINK_SPOT_ACTIVATION_CLOSING;
                spot->second.draining = true;
            }
            std::map<owner_id_t, owner_state_t>::iterator owner_it =
              node_->owners.begin ();
            for (; owner_it != node_->owners.end (); ++owner_it) {
                if (owner_it->first.kind == owner_spot
                    && owner_it->first.generation
                         == it->second.spot_generation
                    && owner_it->first.key.size ()
                         == it->second.spot_rid.size ()
                    && memcmp (owner_it->first.key.data (),
                               it->second.spot_rid.data (),
                               it->second.spot_rid.size ()) == 0)
                    break;
            }
            if (owner_it != node_->owners.end ()) {
                mailbox_t &mailbox =
                  owner_it->second.domains[domain_application];
                pending.splice (pending.end (), mailbox.records);
                mailbox.pending_messages = 0;
                mailbox.pending_bytes = 0;
                for (std::set<std::pair<owner_id_t, int> >::iterator ready =
                       node_->ready.begin ();
                     ready != node_->ready.end ();) {
                    if (ready->second == static_cast<int> (domain_application)
                        && ready->first == owner_it->first)
                        ready = node_->ready.erase (ready);
                    else
                        ++ready;
                }
            }
        }
        node_->cv.notify_all ();
    }

    while (true) {
        uint64_t reply_serial = 0;
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            for (std::unordered_map<uint64_t, reply_route_t>::iterator it =
                   node_->reply_routes.begin ();
                 it != node_->reply_routes.end (); ++it) {
                if (it->second.instance_request && !it->second.consumed
                    && !it->second.force_terminal_pending) {
                    reply_serial = it->first;
                    break;
                }
            }
        }
        if (reply_serial == 0)
            break;
        try {
            (void) force_reply_via_route (
              node_, reply_serial, terminal_result_, failure_errno_);
        }
        catch (const std::bad_alloc &) {
            std::lock_guard<std::mutex> lock (node_->mutex);
            std::unordered_map<uint64_t, reply_route_t>::iterator route =
              node_->reply_routes.find (reply_serial);
            if (route != node_->reply_routes.end ()
                && !route->second.consumed) {
                route->second.in_flight = false;
                route->second.force_terminal_pending = true;
                route->second.force_terminal_result = terminal_result_;
                route->second.force_terminal_errno = failure_errno_;
            }
        }
    }
    finish_instance_records (&pending, terminal_result_, failure_errno_);
    while (true) {
        std::lock_guard<std::mutex> lock (node_->mutex);
        if (node_->instance_activations.empty ())
            break;
        const size_t before = node_->instance_activations.size ();
        maybe_end_spot_locked (
          node_, node_->instance_activations.begin ()->first);
        if (node_->instance_activations.size () == before)
            break;
    }
}
}
}

namespace
{
zlink_submit_result_t instance_submit (
  void *spot_,
  const zlink_instance_spot_placement_t *target_,
  const zlink_mesh_metadata_view_t *metadata_,
  const zlink_msg_t *parts_,
  size_t part_count_,
  zlink_mesh_operation_id_t *operation_id_out_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    spot_facade_t *facade = as_spot_facade (spot_);
    if (!facade) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (check_submit_input (metadata_, parts_, part_count_) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    instance_placement_value_t target;
    try {
        if (!copy_instance_placement (target_, &target))
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    catch (const std::bad_alloc &) {
        return submit_out_of_memory_result ();
    }
    mesh_node_t *node = facade->node;
    const bool is_request = operation_id_out_ != NULL;
    owner_id_t requester;
    bool local_target = false;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        const std::string source_key (facade->spot_rid.begin (),
                                      facade->spot_rid.end ());
        std::map<std::string, spot_state_t>::iterator source =
          node->spots.find (source_key);
        if (source == node->spots.end ()
            || source->second.generation != facade->generation
            || source->second.kind != ZLINK_SPOT_KIND_ENTRY) {
            errno = source == node->spots.end () ? ESTALE : EINVAL;
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        }
        if (check_submit_state_locked (node) != 0)
            return submit_errno_result ();
        requester = spot_owner (facade->spot_rid, facade->generation);
        local_target = target.node_rid == node->routing_id;
        if (!local_target) {
            bool admitted = false;
            for (size_t i = 0; i < node->peers.size (); ++i) {
                if (node->peers[i].state == ZLINK_MESH_PEER_ADMITTED
                    && node->peers[i].rid == target.node_rid
                    && node->peers[i].lifecycle_generation
                         == target.node_generation) {
                    admitted = true;
                    break;
                }
            }
            if (!admitted) {
                errno = ENOTCONN;
                return ZLINK_SUBMIT_NOT_CONNECTED;
            }
        }
    }

    std::unique_ptr<queued_record_t> record (
      new (std::nothrow) queued_record_t ());
    if (!record.get ())
        return submit_out_of_memory_result ();
    record->kind = is_request ? ZLINK_MESH_RECORD_SPOT_REQUEST
                              : ZLINK_MESH_RECORD_SPOT_SEND;
    record->source_node_rid = node->routing_id;
    record->source_spot_rid = facade->spot_rid;
    if (is_request && timeout_ms_ > 0)
        record->deadline_ns =
          zlink::request_timeout::deadline_after_ms (timeout_ms_);
    try {
        if (metadata_) {
            record->has_metadata = true;
            record->application_metadata.assign (
              metadata_->data, metadata_->data + metadata_->size);
            record->byte_size += metadata_->size;
        }
        if (copy_borrowed_parts (parts_, part_count_, record.get ()) != 0)
            return errno == ENOMEM ? ZLINK_SUBMIT_OUT_OF_MEMORY
                                   : ZLINK_SUBMIT_INTERNAL_ERROR;
    }
    catch (const std::bad_alloc &) {
        return submit_out_of_memory_result ();
    }

    operation_submission_t submission (
      node, is_request, ZLINK_MESH_OPERATION_SPOT_REQUEST, requester,
      is_request ? timeout_ms_ : 0);
    if (!submission.valid ())
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    const zlink_mesh_operation_id_t operation_id = submission.operation_id ();
    if (is_request && local_target) {
        reply_route_t route;
        route.kind = reply_route_t::kind_generic;
        route.instance_request = true;
        route.requester = requester;
        route.operation_kind = ZLINK_MESH_OPERATION_SPOT_REQUEST;
        uint64_t reply_serial = 0;
        if (!submission.add_reply_route (route, &reply_serial))
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        record->operation_id = operation_id;
        record->operation_kind = ZLINK_MESH_OPERATION_SPOT_REQUEST;
        record->has_reply_token = true;
        seal_reply_token (node, reply_serial, &record->reply_token);
    }

    if (!local_target) {
        if (is_request
            && !submission.set_instance_remote_route (
              target.node_rid))
            return errno == ENOMEM ? ZLINK_SUBMIT_OUT_OF_MEMORY
                                   : ZLINK_SUBMIT_INTERNAL_ERROR;
        const send_ready_interest_t interest =
          make_spot_send_ready_interest (
            requester, target.node_rid, target.spot_rid);
        remote_route_flight_guard_t route_flight (node, is_request);
        if (!route_flight.valid ())
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        uint64_t expected_connection_id = 0;
        if (!validate_remote_route_flight (
              node, target.node_rid, target.node_generation,
              &expected_connection_id))
            return ZLINK_SUBMIT_NOT_CONNECTED;
        uint64_t connection_id = 0;
        const zlink_submit_result_t result = wire_submit_instance (
          node, target, is_request, operation_id.low, timeout_ms_, facade->spot_rid,
          metadata_, parts_, part_count_, flags_, &interest,
          is_request ? &connection_id : NULL, expected_connection_id);
        if (result != ZLINK_SUBMIT_OK)
            return result;
        if (is_request) {
#ifdef ZLINK_BUILD_TESTS
            g_remote_route_before_commit_paused.store (1,
                                                       std::memory_order_release);
            while (g_remote_route_before_commit_pause.load (
              std::memory_order_acquire))
                std::this_thread::yield ();
            g_remote_route_before_commit_paused.store (0,
                                                       std::memory_order_release);
#endif
            submission.commit_instance_remote_route (connection_id);
            route_flight.release ();
            *operation_id_out_ = operation_id;
        }
        return ZLINK_SUBMIT_OK;
    }

    if (admit_instance_record (node, target, record) != 0) {
        const int reason = errno;
        if (!is_request)
            return ZLINK_SUBMIT_OK;
        submission.commit ();
        *operation_id_out_ = operation_id;
        if (record.get () && record->has_reply_token) {
            mesh_node_t *route_node = NULL;
            uint64_t route_serial = 0;
            std::vector<zlink_msg_t> empty;
            if (unseal_reply_token (&record->reply_token, &route_node,
                                    &route_serial)
                  == 0)
                (void) deliver_reply_via_route (
                  route_node, route_serial, instance_failure_result (reason),
                  reason, &empty);
        }
        return ZLINK_SUBMIT_OK;
    }
    if (is_request)
        (void) arm_instance_record_deadline (
          node, node->routing_id, operation_id);
    if (is_request) {
        submission.commit ();
        *operation_id_out_ = operation_id;
    }
    (void) flags_;
    return ZLINK_SUBMIT_OK;
}
}

zlink_submit_result_t zlink_spot_send_to_instance_placement (
  void *spot_,
  const zlink_instance_spot_placement_t *target_,
  const zlink_mesh_metadata_view_t *metadata_,
  const zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_)
try {
    return instance_submit (spot_, target_, metadata_, parts_, part_count_, NULL,
                            flags_, 0);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_spot_request_to_instance_placement (
  void *spot_,
  const zlink_instance_spot_placement_t *target_,
  const zlink_mesh_metadata_view_t *metadata_,
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
    return instance_submit (spot_, target_, metadata_, parts_, part_count_,
                            operation_id_out_, flags_, timeout_ms_);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_config_result_t zlink_instance_spot_activation_claim_owner (
  zlink_instance_spot_activation_token_t *token_,
  const char *location_owner_id_,
  size_t location_owner_id_size_,
  zlink_instance_spot_claim_result_t *result_out_)
try {
    if (!result_out_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::string owner_id;
    if (!copy_instance_string (
          location_owner_id_, location_owner_id_size_,
          ZLINK_INSTANCE_SPOT_OWNER_ID_MAX, false, &owner_id))
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    mesh_node_t *token_node = NULL;
    uint64_t serial = 0;
    if (unseal_instance_token (token_, &token_node, &serial) != 0)
        return errno == EINVAL ? ZLINK_CONFIG_INVALID_ARGUMENT
                               : ZLINK_CONFIG_INVALID_STATE;
    mesh_node_pin_t node_pin (token_node);
    mesh_node_t *node = node_pin.get ();
    if (!node || token_->opaque[1] != node->lifecycle_generation) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }

    zlink_instance_spot_claim_role_t role =
      ZLINK_INSTANCE_SPOT_CLAIM_INVALID;
    spot_facade_t *borrowed = NULL;
    uint64_t generation = 0;
    std::shared_ptr<zlink::request_timeout::task_t> canceled_watchdog;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        if (node->state == ZLINK_MESH_NODE_STOPPED) {
            errno = ESHUTDOWN;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        std::unordered_map<uint64_t, instance_token_state_t>::iterator token =
          node->instance_tokens.find (serial);
        if (token == node->instance_tokens.end ()
            || token->second.phase != instance_token_placement) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        const std::string key (token->second.target.spot_rid.begin (),
                               token->second.target.spot_rid.end ());
        std::map<std::string, spot_state_t>::iterator existing =
          node->spots.find (key);
        if (existing != node->spots.end ()) {
            std::map<std::string, instance_activation_state_t>::iterator activation =
              node->instance_activations.find (key);
            if (existing->second.kind != ZLINK_SPOT_KIND_INSTANCE
                || activation == node->instance_activations.end ()
                || activation->second.instance_spot_type
                     != token->second.target.instance_spot_type
                || activation->second.owner_id != owner_id
                || activation->second.state != ZLINK_SPOT_ACTIVATION_ACTIVATING) {
                errno = EEXIST;
                return ZLINK_CONFIG_CONFLICT;
            }
            instance_activation_state_t &group = activation->second;
            const uint64_t message_limit = std::min (
              node->effective_instance_activation_message_budget (),
              node->effective_message_budget ());
            const uint64_t byte_limit = std::min (
              node->effective_instance_activation_byte_budget (),
              node->effective_byte_budget ());
            const size_t bytes = token->second.pending_record
                                   ? token->second.pending_record->byte_size
                                   : 0;
            if (group.pending_messages >= message_limit || bytes > byte_limit
                || group.pending_bytes > byte_limit - bytes) {
                errno = EBUSY;
                return ZLINK_CONFIG_BUSY;
            }
            if (token->second.pending_record) {
                insert_instance_pending_ordered (
                  &group, token->second.pending_record);
                group.pending_messages += 1;
                group.pending_bytes += bytes;
            }
            borrowed = group.borrowed_facade;
            generation = group.spot_generation;
            canceled_watchdog = token->second.watchdog;
            node->instance_tokens.erase (token);
            node->cv.notify_all ();
            role = ZLINK_INSTANCE_SPOT_CLAIM_FOLLOWER;
        } else {
            if (node->instance_activations.count (key) != 0) {
                errno = EEXIST;
                return ZLINK_CONFIG_CONFLICT;
            }
            spot_facade_t *new_borrowed =
              new (std::nothrow) spot_facade_t ();
            if (!new_borrowed) {
                errno = ENOMEM;
                return ZLINK_CONFIG_INTERNAL_ERROR;
            }
            spot_state_t spot;
            spot.rid = token->second.target.spot_rid;
            spot.generation = node->next_spot_generation++;
            spot.kind = ZLINK_SPOT_KIND_INSTANCE;
            spot.facade_count = 1;
            spot.activation_state = ZLINK_SPOT_ACTIVATION_ACTIVATING;
            spot.last_changed_ms = now_ms ();

            owner_state_t owner;
            owner.id = spot_owner (spot.rid, spot.generation);
            owner.spot_rid = rid_value (spot.rid);

            instance_activation_state_t group;
            group.instance_spot_type =
              token->second.target.instance_spot_type;
            group.spot_rid = spot.rid;
            group.spot_generation = spot.generation;
            group.owner_id = owner_id;
            group.state = ZLINK_SPOT_ACTIVATION_ACTIVATING;
            group.leader_token_serial = serial;
            group.borrowed_facade = new_borrowed;
            group.watchdog = token->second.watchdog;
            if (token->second.pending_record) {
                group.pending_bytes = token->second.pending_record->byte_size;
                group.pending_messages = 1;
                group.pending.push_back (
                  std::move (token->second.pending_record));
            }
            new_borrowed->node = node;
            new_borrowed->spot_rid = spot.rid;
            new_borrowed->generation = spot.generation;
            new_borrowed->caller_owned = false;
            try {
                track_facade (new_borrowed, true);
                const std::pair<std::map<std::string, spot_state_t>::iterator,
                                bool> spot_inserted =
                  node->spots.insert (std::make_pair (key, spot));
                if (!spot_inserted.second)
                    throw std::bad_alloc ();
                owner_state_t &inserted_owner = node->owners[owner.id];
                inserted_owner.id = owner.id;
                inserted_owner.spot_rid = owner.spot_rid;
                const std::pair<
                  std::map<std::string, instance_activation_state_t>::iterator,
                  bool> activation_inserted =
                  node->instance_activations.insert (
                    std::make_pair (key, std::move (group)));
                if (!activation_inserted.second)
                    throw std::bad_alloc ();
            }
            catch (const std::bad_alloc &) {
                node->spots.erase (key);
                node->owners.erase (owner.id);
                node->instance_activations.erase (key);
                track_facade (new_borrowed, false);
                delete new_borrowed;
                node->next_spot_generation -= 1;
                errno = ENOMEM;
                return ZLINK_CONFIG_INTERNAL_ERROR;
            }
            token->second.phase = instance_token_authorized_leader;
            borrowed = new_borrowed;
            generation = spot.generation;
            role = ZLINK_INSTANCE_SPOT_CLAIM_LEADER;
        }
    }
    if (canceled_watchdog)
        zlink::request_timeout::cancel (canceled_watchdog);
    zlink_instance_spot_claim_result_t result;
    result.role = role;
    result.leader_spot = role == ZLINK_INSTANCE_SPOT_CLAIM_LEADER
                           ? borrowed
                           : NULL;
    result.leader_spot_generation =
      role == ZLINK_INSTANCE_SPOT_CLAIM_LEADER ? generation : 0;
    *result_out_ = result;
    return ZLINK_CONFIG_OK;
}
catch (const std::bad_alloc &) {
    errno = ENOMEM;
    return ZLINK_CONFIG_INTERNAL_ERROR;
}

zlink_config_result_t zlink_instance_spot_activation_mark_ready (
  zlink_instance_spot_activation_token_t *token_,
  uint32_t owner_lease_valid_for_ms_)
try {
    if (owner_lease_valid_for_ms_ == 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    mesh_node_t *token_node = NULL;
    uint64_t serial = 0;
    if (unseal_instance_token (token_, &token_node, &serial) != 0)
        return errno == EINVAL ? ZLINK_CONFIG_INVALID_ARGUMENT
                               : ZLINK_CONFIG_INVALID_STATE;
    mesh_node_pin_t node_pin (token_node);
    mesh_node_t *node = node_pin.get ();
    if (!node || token_->opaque[1] != node->lifecycle_generation) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    owner_id_t ready_owner;
    bool application_ready = false;
    std::list<std::unique_ptr<queued_record_t> > expired;
    std::shared_ptr<zlink::request_timeout::task_t> watchdog;
    std::vector<std::shared_ptr<zlink::request_timeout::task_t> >
      admitted_deadlines;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, instance_token_state_t>::iterator token =
          node->instance_tokens.find (serial);
        if (token == node->instance_tokens.end ()
            || token->second.phase != instance_token_authorized_leader) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        const std::string key (token->second.target.spot_rid.begin (),
                               token->second.target.spot_rid.end ());
        std::map<std::string, instance_activation_state_t>::iterator activation =
          node->instance_activations.find (key);
        if (activation == node->instance_activations.end ()
            || activation->second.leader_token_serial != serial
            || activation->second.state != ZLINK_SPOT_ACTIVATION_ACTIVATING) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        instance_activation_state_t &group = activation->second;
        const uint64_t now_ns =
          zlink::request_timeout::monotonic_now_ns ();
        for (std::list<std::unique_ptr<queued_record_t> >::iterator it =
               group.pending.begin ();
             it != group.pending.end ();) {
            if ((*it)->deadline_ns == 0 || now_ns < (*it)->deadline_ns) {
                ++it;
                continue;
            }
            group.pending_messages -= 1;
            group.pending_bytes -= (*it)->byte_size;
            std::list<std::unique_ptr<queued_record_t> >::iterator current =
              it++;
            expired.splice (expired.end (), group.pending, current);
        }
        ready_owner = spot_owner (group.spot_rid, group.spot_generation);
        std::map<owner_id_t, owner_state_t>::iterator owner =
          node->owners.find (ready_owner);
        if (owner == node->owners.end ()) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        mailbox_t &mailbox = owner->second.domains[domain_application];
        for (std::list<std::unique_ptr<queued_record_t> >::iterator it =
               group.pending.begin ();
             it != group.pending.end (); ++it) {
            if ((*it)->instance_deadline_task) {
                admitted_deadlines.push_back (
                  (*it)->instance_deadline_task);
                (*it)->instance_deadline_task.reset ();
            }
        }
        if (!group.pending.empty ()) {
            const std::pair<owner_id_t, int> ready_key =
              std::make_pair (ready_owner,
                              static_cast<int> (domain_application));
            try {
                node->ready.insert (ready_key);
            }
            catch (const std::bad_alloc &) {
                errno = ENOMEM;
                return ZLINK_CONFIG_INTERNAL_ERROR;
            }
            application_ready = true;
        }
        mailbox.records.splice (mailbox.records.end (), group.pending);
        mailbox.pending_messages += group.pending_messages;
        mailbox.pending_bytes += group.pending_bytes;
        group.pending_messages = 0;
        group.pending_bytes = 0;
        group.owner_deadline_ns =
          zlink::request_timeout::deadline_after_ms (
            owner_lease_valid_for_ms_);
        group.state = ZLINK_SPOT_ACTIVATION_READY;
        std::map<std::string, spot_state_t>::iterator spot =
          node->spots.find (key);
        if (spot != node->spots.end ()) {
            spot->second.activation_state = ZLINK_SPOT_ACTIVATION_READY;
            spot->second.last_changed_ms = now_ms ();
        }
        watchdog = group.watchdog;
        group.watchdog.reset ();
        node->instance_tokens.erase (token);
        node->cv.notify_all ();
    }
    if (watchdog)
        zlink::request_timeout::cancel (watchdog);
    for (size_t i = 0; i < admitted_deadlines.size (); ++i)
        zlink::request_timeout::cancel (admitted_deadlines[i]);
    finish_instance_records (
      &expired, ZLINK_REQUEST_TIMED_OUT, ETIMEDOUT);
    if (application_ready)
        signal_ready (node, ready_owner, domain_application);
    return ZLINK_CONFIG_OK;
}
catch (const std::bad_alloc &) {
    errno = ENOMEM;
    return ZLINK_CONFIG_INTERNAL_ERROR;
}

zlink_config_result_t zlink_instance_spot_activation_abort (
  zlink_instance_spot_activation_token_t *token_,
  zlink_request_result_t terminal_result_,
  int32_t failure_errno_)
try {
    if (!valid_instance_abort_result (terminal_result_)
        || failure_errno_ == 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    mesh_node_t *token_node = NULL;
    uint64_t serial = 0;
    if (unseal_instance_token (token_, &token_node, &serial) != 0)
        return errno == EINVAL ? ZLINK_CONFIG_INVALID_ARGUMENT
                               : ZLINK_CONFIG_INVALID_STATE;
    mesh_node_pin_t node_pin (token_node);
    mesh_node_t *node = node_pin.get ();
    if (!node || token_->opaque[1] != node->lifecycle_generation) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    std::list<std::unique_ptr<queued_record_t> > pending;
    std::shared_ptr<zlink::request_timeout::task_t> watchdog;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, instance_token_state_t>::iterator token =
          node->instance_tokens.find (serial);
        if (token == node->instance_tokens.end ()) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        watchdog = token->second.watchdog;
        if (token->second.phase == instance_token_placement) {
            if (token->second.pending_record)
                pending.push_back (std::move (token->second.pending_record));
        } else if (token->second.phase
                   == instance_token_authorized_leader) {
            const std::string key (token->second.target.spot_rid.begin (),
                                   token->second.target.spot_rid.end ());
            remove_instance_activation_locked (node, key, &pending);
        } else {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        node->instance_tokens.erase (token);
        node->cv.notify_all ();
    }
    if (watchdog)
        zlink::request_timeout::cancel (watchdog);
    finish_instance_records (&pending, terminal_result_, failure_errno_);
    return ZLINK_CONFIG_OK;
}
catch (const std::bad_alloc &) {
    errno = ENOMEM;
    return ZLINK_CONFIG_INTERNAL_ERROR;
}

zlink_config_result_t zlink_instance_spot_renew_owner_admission (
  void *spot_,
  uint32_t owner_lease_valid_for_ms_)
{
    if (owner_lease_valid_for_ms_ == 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    spot_facade_t *facade = as_spot_facade (spot_);
    if (!facade) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    mesh_node_pin_t node_pin (facade->node);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    const std::string key (facade->spot_rid.begin (), facade->spot_rid.end ());
    std::map<std::string, instance_activation_state_t>::iterator activation =
      node->instance_activations.find (key);
    if (activation == node->instance_activations.end ()
        || activation->second.spot_generation != facade->generation) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    if (activation->second.state != ZLINK_SPOT_ACTIVATION_READY) {
        errno = activation->second.state == ZLINK_SPOT_ACTIVATION_CLOSING
                  ? ESTALE
                  : EBUSY;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    activation->second.owner_deadline_ns =
      zlink::request_timeout::deadline_after_ms (owner_lease_valid_for_ms_);
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_instance_spot_begin_close (void *spot_)
{
    spot_facade_t *facade = as_spot_facade (spot_);
    if (!facade) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    mesh_node_pin_t node_pin (facade->node);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    std::shared_ptr<zlink::request_timeout::task_t> watchdog;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        const std::string key (facade->spot_rid.begin (),
                               facade->spot_rid.end ());
        std::map<std::string, instance_activation_state_t>::iterator activation =
          node->instance_activations.find (key);
        if (activation == node->instance_activations.end ()
            || activation->second.spot_generation != facade->generation) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        if (activation->second.state == ZLINK_SPOT_ACTIVATION_CLOSING)
            return ZLINK_CONFIG_OK;
        activation->second.state = ZLINK_SPOT_ACTIVATION_CLOSING;
        activation->second.owner_deadline_ns = 0;
        watchdog = activation->second.watchdog;
        activation->second.watchdog.reset ();
        std::map<std::string, spot_state_t>::iterator current =
          node->spots.find (key);
        if (current != node->spots.end ()) {
            current->second.activation_state =
              ZLINK_SPOT_ACTIVATION_CLOSING;
            current->second.draining = true;
            current->second.last_changed_ms = now_ms ();
        }
        maybe_end_spot_locked (node, key);
        node->cv.notify_all ();
    }
    if (watchdog)
        zlink::request_timeout::cancel (watchdog);
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_instance_spot_activation_redirect (
  zlink_instance_spot_activation_token_t *token_,
  const zlink_routing_id_t *target_node_rid_,
  const zlink_routing_id_t *target_spot_rid_,
  uint64_t target_spot_generation_)
try {
    if (!target_node_rid_ || target_node_rid_->size == 0
        || !target_spot_rid_ || target_spot_rid_->size == 0
        || target_spot_generation_ == 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    const rid_bytes_t target_node_rid = rid_bytes (*target_node_rid_);
    const rid_bytes_t target_spot_rid = rid_bytes (*target_spot_rid_);
    instance_placement_value_t target;
    mesh_node_t *token_node = NULL;
    uint64_t serial = 0;
    if (unseal_instance_token (token_, &token_node, &serial) != 0)
        return errno == EINVAL ? ZLINK_CONFIG_INVALID_ARGUMENT
                               : ZLINK_CONFIG_INVALID_STATE;
    mesh_node_pin_t node_pin (token_node);
    mesh_node_t *node = node_pin.get ();
    if (!node || token_->opaque[1] != node->lifecycle_generation) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    std::unique_ptr<queued_record_t> record;
    std::shared_ptr<zlink::request_timeout::task_t> watchdog;
    std::list<std::unique_ptr<queued_record_t> > timed_out;
    rid_bytes_t request_source_node_rid;
    zlink_mesh_operation_id_t request_operation_id;
    memset (&request_operation_id, 0, sizeof (request_operation_id));
    bool has_request_deadline = false;
    bool remote_connected = true;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, instance_token_state_t>::iterator token =
          node->instance_tokens.find (serial);
        if (token == node->instance_tokens.end ()
            || token->second.phase != instance_token_placement) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        target = token->second.target;
        target.node_rid = target_node_rid;
        target.spot_rid = target_spot_rid;
        if (target_node_rid == node->routing_id) {
            target.node_generation = node->lifecycle_generation;
        } else {
            remote_connected = false;
            for (size_t i = 0; i < node->peers.size (); ++i) {
                if (node->peers[i].state == ZLINK_MESH_PEER_ADMITTED
                    && node->peers[i].rid == target_node_rid) {
                    target.node_generation =
                      node->peers[i].lifecycle_generation;
                    remote_connected = true;
                    break;
                }
            }
        }
        watchdog = token->second.watchdog;
        record = std::move (token->second.pending_record);
        if (record && record->deadline_ns != 0) {
            request_source_node_rid = record->source_node_rid;
            request_operation_id = record->operation_id;
            has_request_deadline = true;
        }
        token->second.phase = instance_token_redirecting;
    }
    //  Redirect owns the placement token's deadline decision. Cancel waits
    //  for a concurrently firing watchdog; a watchdog that won first records
    //  watchdog_expired on the redirecting token.
    if (watchdog)
        zlink::request_timeout::cancel (watchdog);
    bool activation_expired = false;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, instance_token_state_t>::iterator token =
          node->instance_tokens.find (serial);
        if (token == node->instance_tokens.end ()) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        token->second.watchdog.reset ();
        activation_expired = token->second.watchdog_expired
                             || remaining_instance_timeout_ms (
                                  token->second.activation_deadline_ns)
                                  == 0;
        if (activation_expired) {
            if (record)
                timed_out.push_back (std::move (record));
            node->instance_tokens.erase (token);
            node->cv.notify_all ();
        }
    }
    if (activation_expired) {
        finish_instance_records (
          &timed_out, ZLINK_REQUEST_TIMED_OUT, ETIMEDOUT);
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }

    zlink_submit_result_t redirect_result = ZLINK_SUBMIT_INTERNAL_ERROR;
    if (target.node_rid == node->routing_id) {
        redirect_result = admit_instance_direct_record (
                            node, target.spot_rid,
                            target_spot_generation_, record)
                              == 0
                            ? ZLINK_SUBMIT_OK
                            : submit_errno_result ();
    } else if (!remote_connected) {
        errno = ENOTCONN;
        redirect_result = ZLINK_SUBMIT_NOT_CONNECTED;
    } else {
        redirect_result = wire_redirect_instance (
          node, target, target_spot_generation_, record);
    }
    const int redirect_errno =
      redirect_result == ZLINK_SUBMIT_OK ? 0 : errno;
    bool expired = false;
    uint32_t rearm_timeout_ms = 0;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, instance_token_state_t>::iterator token =
          node->instance_tokens.find (serial);
        if (token == node->instance_tokens.end ()) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        if (redirect_result == ZLINK_SUBMIT_OK) {
            node->instance_tokens.erase (token);
            node->cv.notify_all ();
        } else {
            token->second.pending_record = std::move (record);
            const uint64_t now_ns =
              zlink::request_timeout::monotonic_now_ns ();
            expired = token->second.request_deadline_expired
                      || (token->second.pending_record
                          && token->second.pending_record->deadline_ns != 0
                          && now_ns
                               >= token->second.pending_record->deadline_ns)
                      || now_ns >= token->second.activation_deadline_ns;
            if (expired) {
                if (token->second.pending_record)
                    timed_out.push_back (
                      std::move (token->second.pending_record));
                node->instance_tokens.erase (token);
                node->cv.notify_all ();
            } else {
                token->second.phase = instance_token_placement;
                rearm_timeout_ms = remaining_instance_timeout_ms (
                  token->second.activation_deadline_ns);
            }
        }
    }
    if (redirect_result == ZLINK_SUBMIT_OK) {
        return ZLINK_CONFIG_OK;
    }
    if (expired) {
        finish_instance_records (
          &timed_out, ZLINK_REQUEST_TIMED_OUT, ETIMEDOUT);
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    if (rearm_timeout_ms != 0) {
        std::shared_ptr<zlink::request_timeout::task_t> rearmed =
          schedule_instance_watchdog (
            node, serial, rearm_timeout_ms);
        if (!rearmed) {
            std::list<std::unique_ptr<queued_record_t> > failed;
            {
                std::lock_guard<std::mutex> lock (node->mutex);
                std::unordered_map<uint64_t, instance_token_state_t>::iterator token =
                  node->instance_tokens.find (serial);
                if (token != node->instance_tokens.end ()) {
                    if (token->second.pending_record)
                        failed.push_back (
                          std::move (token->second.pending_record));
                    node->instance_tokens.erase (token);
                    node->cv.notify_all ();
                }
            }
            finish_instance_records (
              &failed, ZLINK_REQUEST_INTERNAL_ERROR, ENOMEM);
            errno = ENOMEM;
            return ZLINK_CONFIG_INTERNAL_ERROR;
        }
        if (rearmed) {
            std::lock_guard<std::mutex> lock (node->mutex);
            std::unordered_map<uint64_t, instance_token_state_t>::iterator token =
              node->instance_tokens.find (serial);
            if (token != node->instance_tokens.end ()
                && token->second.phase == instance_token_placement) {
                token->second.watchdog = rearmed;
                rearmed.reset ();
            }
        }
        if (rearmed)
            zlink::request_timeout::cancel (rearmed);
    }
    if (has_request_deadline)
        (void) arm_instance_record_deadline (
          node, request_source_node_rid, request_operation_id);
    errno = redirect_errno;
    if (redirect_errno == EEXIST)
        return ZLINK_CONFIG_CONFLICT;
    if (redirect_errno == EINVAL)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    return ZLINK_CONFIG_INVALID_STATE;
}
catch (const std::bad_alloc &) {
    errno = ENOMEM;
    return ZLINK_CONFIG_INTERNAL_ERROR;
}

//  --- Logical Multicast ------------------------------------------------------------

namespace
{
bool subscription_matches (const spot_state_t &spot_,
                           const std::string &channel_,
                           const std::string &topic_)
{
    for (std::set<subscription_key_t>::const_iterator it = spot_.subscriptions.begin ();
         it != spot_.subscriptions.end (); ++it) {
        if (it->channel != channel_)
            continue;
        if (it->kind == ZLINK_SPOT_SUBSCRIPTION_EXACT) {
            if (it->filter == topic_)
                return true;
        } else {
            if (topic_.compare (0, it->filter.size (), it->filter) == 0)
                return true;
        }
    }
    return false;
}

zlink_submit_result_t publish_common (mesh_node_t *node_,
                                      const rid_bytes_t *source_spot_rid_,
                                      const char *channel_name_,
                                      const char *topic_,
                                      const zlink_mesh_metadata_view_t *metadata_,
                                      const zlink_msg_t *parts_,
                                      size_t part_count_,
                                      zlink_mesh_publish_detail_t *detail_out_,
                                      zlink_send_flags_t flags_)
{
    std::string channel;
    std::string topic;
    try {
        if (check_name (channel_name_, ZLINK_CHANNEL_NAME_MAX, &channel) != 0)
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        if (check_name (topic_, ZLINK_MESH_TOPIC_MAX, &topic) != 0)
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    catch (const std::bad_alloc &) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    if (!valid_utf8 (reinterpret_cast<const unsigned char *> (topic.data ()), topic.size ()))
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if (check_submit_input (metadata_, parts_, part_count_) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if (detail_out_ && check_versioned (detail_out_) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;

    //  Snapshot local Spot matches and admitted remote channel members.
    std::vector<owner_id_t> local_targets;
    std::vector<rid_bytes_t> remote_targets;
    try {
        std::unique_lock<std::mutex> lock (node_->mutex);
        if (check_submit_state_locked (node_) != 0)
            return submit_errno_result ();
        const bool local_member = node_->channels.count (channel) > 0;
        if (local_member) {
            for (std::map<std::string, spot_state_t>::iterator it = node_->spots.begin ();
                 it != node_->spots.end (); ++it) {
                if (!it->second.draining && subscription_matches (it->second, channel, topic))
                    local_targets.push_back (spot_owner (it->second.rid, it->second.generation));
            }
        }
        for (size_t i = 0; i < node_->peers.size (); ++i) {
            const peer_state_t &peer = node_->peers[i];
            if (peer.state != ZLINK_MESH_PEER_ADMITTED)
                continue;
            std::map<std::string, uint32_t>::const_iterator ch = peer.channels.find (channel);
            if (ch != peer.channels.end () && ch->second > 0)
                remote_targets.push_back (peer.rid);
        }
    }
    catch (const std::bad_alloc &) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    const uint32_t snapshot_remote = static_cast<uint32_t> (remote_targets.size ());
    const uint32_t snapshot_local = static_cast<uint32_t> (local_targets.size ());
#ifdef ZLINK_BUILD_TESTS
    const int pause_ms =
      g_publish_pause_after_snapshot_ms.exchange (0, std::memory_order_relaxed);
    if (pause_ms > 0) {
        g_publish_snapshot_paused.store (1, std::memory_order_release);
        std::this_thread::sleep_for (std::chrono::milliseconds (pause_ms));
        g_publish_snapshot_paused.store (0, std::memory_order_release);
    }
#endif
    if (snapshot_remote + snapshot_local == 0) {
        errno = ENOENT;
        return ZLINK_SUBMIT_NOT_FOUND;
    }

    //  Each local mailbox and remote ROUTER target admits independently.
    //  There is no cross-target capacity reservation or rollback.
    uint32_t admitted_local = 0;
    uint32_t dropped_local = 0;
    uint32_t admitted_remote = 0;
    uint32_t dropped_remote = 0;
    uint32_t unreachable_remote = 0;
    zlink_submit_result_t aggregate_rc = ZLINK_SUBMIT_OK;
    int aggregate_errno = 0;

    //  Local multicast uses the normal mailbox admission owner. A full
    //  mailbox drops only that target and never makes another target wait.
    for (size_t t = 0; t < local_targets.size (); ++t) {
        std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
        if (!record.get ()) {
            aggregate_rc = ZLINK_SUBMIT_OUT_OF_MEMORY;
            aggregate_errno = ENOMEM;
            dropped_local += static_cast<uint32_t> (local_targets.size () - t);
            break;
        }
        try {
#ifdef ZLINK_BUILD_TESTS
            test_maybe_throw_alloc ();
#endif
            record->kind = ZLINK_MESH_RECORD_SPOT_MULTICAST;
            record->source_node_rid = node_->routing_id;
            if (source_spot_rid_)
                record->source_spot_rid = *source_spot_rid_;
            record->channel_name = channel;
            record->topic = topic;
            if (metadata_) {
                record->has_metadata = true;
                record->application_metadata.assign (metadata_->data,
                                                     metadata_->data + metadata_->size);
                record->byte_size += metadata_->size;
            }
            if (copy_borrowed_parts (parts_, part_count_, record.get ()) != 0) {
                aggregate_errno = errno;
                aggregate_rc = errno == ENOMEM ? ZLINK_SUBMIT_OUT_OF_MEMORY
                                                : ZLINK_SUBMIT_INTERNAL_ERROR;
                dropped_local += static_cast<uint32_t> (local_targets.size () - t);
                break;
            }
        }
        catch (const std::bad_alloc &) {
            aggregate_rc = ZLINK_SUBMIT_OUT_OF_MEMORY;
            aggregate_errno = ENOMEM;
            dropped_local += static_cast<uint32_t> (local_targets.size () - t);
            break;
        }

        if (admit_multicast_record (node_, local_targets[t], record) == 0) {
            ++admitted_local;
            continue;
        }
        const int admission_errno = errno;
        ++dropped_local;
        if (admission_errno == EAGAIN || admission_errno == ENOENT)
            continue;
        aggregate_rc = submit_errno_result ();
        aggregate_errno = admission_errno;
        dropped_local += static_cast<uint32_t> (local_targets.size () - t - 1);
        break;
    }

    //  A hard local failure stops this submit. Otherwise the ROUTER leg runs
    //  without the node mutex, so a blocking target does not stall MeshNode
    //  lifecycle, receive, or claim progress.
    if (aggregate_rc == ZLINK_SUBMIT_OK) {
        try {
            aggregate_rc = wire_publish_remote (
              node_, remote_targets, channel, topic,
              source_spot_rid_ ? *source_spot_rid_ : rid_bytes_t (), metadata_, parts_,
              part_count_, flags_, &admitted_remote, &dropped_remote, &unreachable_remote);
            if (aggregate_rc != ZLINK_SUBMIT_OK)
                aggregate_errno = errno;
        }
        catch (const std::bad_alloc &) {
            aggregate_rc = ZLINK_SUBMIT_OUT_OF_MEMORY;
            aggregate_errno = ENOMEM;
            const uint32_t completed_remote = admitted_remote + unreachable_remote;
            dropped_remote =
              completed_remote < snapshot_remote ? snapshot_remote - completed_remote : 0;
        }
    } else {
        dropped_remote = snapshot_remote;
    }
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        node_->monitor_counters.multicast_messages += 1;
        node_->monitor_counters.multicast_dropped_targets +=
          dropped_local + dropped_remote;
    }

    if (detail_out_) {
        init_versioned (detail_out_);
        detail_out_->snapshot_remote_target_count = snapshot_remote;
        detail_out_->admitted_remote_target_count = admitted_remote;
        detail_out_->dropped_remote_target_count = dropped_remote;
        detail_out_->unreachable_remote_target_count = unreachable_remote;
        detail_out_->snapshot_local_spot_count = snapshot_local;
        detail_out_->admitted_local_spot_count = admitted_local;
        detail_out_->dropped_local_spot_count = dropped_local;
    }

    zlink_mesh_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    event.snapshot_remote_target_count = snapshot_remote;
    event.admitted_remote_target_count = admitted_remote;
    event.dropped_remote_target_count = dropped_remote;
    event.unreachable_remote_target_count = unreachable_remote;
    event.snapshot_local_spot_count = snapshot_local;
    event.admitted_local_spot_count = admitted_local;
    event.dropped_local_spot_count = dropped_local;
    snprintf (event.channel_name, sizeof (event.channel_name), "%s", channel.c_str ());

    const bool remote_backpressured =
      aggregate_rc == ZLINK_SUBMIT_BACKPRESSURED && dropped_remote > 0;
    if (remote_backpressured) {
        event.kind = ZLINK_MESH_MONITOR_BACKPRESSURED;
        event.owner_kind = source_spot_rid_ ? ZLINK_MESH_OWNER_SPOT
                                            : ZLINK_MESH_OWNER_NODE;
        if (source_spot_rid_)
            event.spot_rid = rid_value (*source_spot_rid_);
        event.result_code = ZLINK_SUBMIT_BACKPRESSURED;
        event.failure_errno = aggregate_errno;
        emit_monitor_event (node_, event);
    }

    if (dropped_local > 0
        || (dropped_remote > 0 && !remote_backpressured)) {
        event.kind = ZLINK_MESH_MONITOR_MULTICAST_DROPPED;
        event.result_code = aggregate_rc;
        event.failure_errno = aggregate_errno;
        emit_monitor_event (node_, event);
    } else if (!remote_backpressured) {
        event.kind = ZLINK_MESH_MONITOR_MULTICAST_COMMITTED;
        event.result_code = aggregate_rc;
        emit_monitor_event (node_, event);
    }
    if (aggregate_rc != ZLINK_SUBMIT_OK)
        errno = aggregate_errno;
    return aggregate_rc;
}
}

zlink_submit_result_t zlink_mesh_node_publisher_publish (void *publisher_,
                                                         const char *channel_name_,
                                                         const char *topic_,
                                                         const zlink_mesh_metadata_view_t *metadata_,
                                                         const zlink_msg_t *parts_,
                                                         size_t part_count_,
                                                         zlink_mesh_publish_detail_t *detail_out_,
                                                         zlink_send_flags_t flags_)
try {
    publisher_t *pub = as_publisher (publisher_);
    if (!pub) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    return publish_common (pub->node, NULL, channel_name_, topic_, metadata_, parts_,
                           part_count_, detail_out_, flags_);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_spot_publish (void *spot_,
                                          const char *channel_name_,
                                          const char *topic_,
                                          const zlink_mesh_metadata_view_t *metadata_,
                                          const zlink_msg_t *parts_,
                                          size_t part_count_,
                                          zlink_mesh_publish_detail_t *detail_out_,
                                          zlink_send_flags_t flags_)
try {
    spot_facade_t *facade;
    mesh_node_t *node;
    if (resolve_facade (spot_, &facade, &node, NULL) != 0)
        return ZLINK_SUBMIT_INVALID_HANDLE;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        const std::string key (facade->spot_rid.begin (), facade->spot_rid.end ());
        std::map<std::string, spot_state_t>::iterator it = node->spots.find (key);
        if (it == node->spots.end () || it->second.generation != facade->generation) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
    }
    return publish_common (node, &facade->spot_rid, channel_name_, topic_, metadata_, parts_,
                           part_count_, detail_out_, flags_);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

//  --- local subscription --------------------------------------------------------

namespace
{
zlink_config_result_t subscription_common (void *spot_,
                                           const char *channel_name_,
                                           const char *topic_filter_,
                                           zlink_spot_subscription_kind_t kind_,
                                           bool set_)
{
    spot_facade_t *facade = as_spot_facade (spot_);
    if (!facade) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (kind_ != ZLINK_SPOT_SUBSCRIPTION_EXACT && kind_ != ZLINK_SPOT_SUBSCRIPTION_PREFIX) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::string channel;
    if (check_name (channel_name_, ZLINK_CHANNEL_NAME_MAX, &channel) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    if (!topic_filter_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    const std::string filter (topic_filter_);
    if (filter.size () > ZLINK_MESH_TOPIC_MAX) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (!filter.empty ()
        && !valid_utf8 (reinterpret_cast<const unsigned char *> (filter.data ()),
                        filter.size ())) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    mesh_node_t *node = facade->node;
    std::lock_guard<std::mutex> lock (node->mutex);
    const std::string key (facade->spot_rid.begin (), facade->spot_rid.end ());
    std::map<std::string, spot_state_t>::iterator it = node->spots.find (key);
    if (it == node->spots.end () || it->second.generation != facade->generation) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    subscription_key_t sub;
    sub.channel = channel;
    sub.filter = filter;
    sub.kind = kind_;
    if (set_)
        it->second.subscriptions.insert (sub);
    else
        it->second.subscriptions.erase (sub);
    return ZLINK_CONFIG_OK;
}
}

zlink_config_result_t zlink_spot_set_subscription (void *spot_,
                                                   const char *channel_name_,
                                                   const char *topic_filter_,
                                                   zlink_spot_subscription_kind_t kind_)
{
    return subscription_common (spot_, channel_name_, topic_filter_, kind_, true);
}

zlink_config_result_t zlink_spot_unset_subscription (void *spot_,
                                                     const char *channel_name_,
                                                     const char *topic_filter_,
                                                     zlink_spot_subscription_kind_t kind_)
{
    return subscription_common (spot_, channel_name_, topic_filter_, kind_, false);
}
