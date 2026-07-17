/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICES_MESH_RUNTIME_HPP_INCLUDED
#define ZLINK_SERVICES_MESH_RUNTIME_HPP_INCLUDED

#include <zlink.h>

#include "api/socket/request_timeout_scheduler_internal.hpp"
#include "core/signaler.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <stdint.h>
#include <string.h>

//  MeshNode service runtime object model.
//
//  One mesh_node_t owns everything the public contract attributes to a
//  MeshNode: identity, channel membership, peer admission, the owner
//  mailboxes with their ready index, the operation/reply correlation tables
//  and the logical Spot / Actor registries. The public C surface in
//  core/src/api/mesh/ only validates arguments and forwards; every rule
//  about lifecycle, budgets, ordering and generations lives here.
namespace zlink
{
class ctx_t;
class socket_base_t;

namespace mesh
{
struct mesh_node_t;
struct spot_state_t;
struct actor_state_t;

//  --- identity helpers -----------------------------------------------------

typedef std::vector<unsigned char> rid_bytes_t;

rid_bytes_t rid_bytes (const zlink_routing_id_t &rid_);
zlink_routing_id_t rid_value (const rid_bytes_t &bytes_);
bool rid_equal (const zlink_routing_id_t &a_, const zlink_routing_id_t &b_);

//  Strict UTF-8 scalar validation shared by every public string contract.
bool valid_utf8 (const unsigned char *data_, size_t size_);

//  Ends a logical user Spot once nothing references it any more (no facade,
//  timer, Actor membership or active claim). Entry Spots stay node-owned.
//  Caller holds the node mutex.
void maybe_end_spot_locked (mesh_node_t *node_, const std::string &spot_key_);

//  --- owner mailboxes and the ready index ----------------------------------

enum owner_kind_t
{
    owner_node = ZLINK_MESH_OWNER_NODE,
    owner_spot = ZLINK_MESH_OWNER_SPOT,
    owner_actor = ZLINK_MESH_OWNER_ACTOR
};

enum domain_t
{
    domain_application = 0,
    domain_infrastructure = 1
};

//  One queued complete multipart record. Parts are owned by the record and
//  closed when the record is destroyed unless released to a batch.
struct queued_record_t
{
    queued_record_t ();
    queued_record_t (queued_record_t &&other_) noexcept;
    queued_record_t &operator= (queued_record_t &&other_) noexcept;
    ~queued_record_t ();

    zlink_mesh_record_kind_t kind;
    rid_bytes_t source_node_rid;
    rid_bytes_t source_spot_rid;
    zlink_actor_ref_t source_actor;
    zlink_mesh_operation_id_t operation_id;
    zlink_mesh_operation_kind_t operation_kind;
    zlink_mesh_reply_token_t reply_token;
    bool has_reply_token;
    std::string channel_name;
    std::string topic;
    std::vector<unsigned char> application_metadata;
    bool has_metadata;
    int32_t terminal_result;
    int32_t failure_errno;
    //  kind_data payload for COMPLETION / SPOT_CONTROL / TRANSFER_CONTROL /
    //  SEND_READY records, already laid out as the public view struct.
    std::vector<unsigned char> kind_data;
    std::vector<zlink_msg_t> parts;
    size_t byte_size;

  private:
    queued_record_t (const queued_record_t &);
    queued_record_t &operator= (const queued_record_t &);
};

//  Identity of a mailbox owner inside one mesh node.
struct owner_id_t
{
    owner_kind_t kind;
    //  Spot owners: spot rid + generation. Actor owners: actor id +
    //  generation. Node owner: both empty.
    std::string key;
    uint64_t generation;

    bool operator< (const owner_id_t &o_) const
    {
        if (kind != o_.kind)
            return kind < o_.kind;
        if (key != o_.key)
            return key < o_.key;
        return generation < o_.generation;
    }
    bool operator== (const owner_id_t &o_) const
    {
        return kind == o_.kind && key == o_.key && generation == o_.generation;
    }
};

//  One owner x domain mailbox with message/byte budgets.
struct mailbox_t
{
    mailbox_t ();

    std::list<std::unique_ptr<queued_record_t>> records;
    uint64_t pending_messages;
    uint64_t pending_bytes;
    //  True while a consumer holds this mailbox's claim.
    bool claimed;
    uint64_t claim_serial;
    //  Claims outstanding after shutdown deadline are revoked: release stays
    //  legal but recv fails with ESHUTDOWN.
    bool revoked;
};

struct owner_state_t
{
    owner_state_t ();

    owner_id_t id;
    zlink_actor_ref_t actor;
    zlink_routing_id_t spot_rid;
    mailbox_t domains[2];
    bool draining;
    //  Transfer fence: while nonzero the application domain accepts no new
    //  records or claims (submits return EAGAIN).
    uint64_t fenced_transfer_serial;
    //  True while a Spot timer handler for this owner's generation runs; the
    //  application claim and the timer handler are mutually exclusive.
    bool timer_turn_active;
};

//  --- claims ---------------------------------------------------------------

//  A claim names (node generation, owner, domain, serial). Serial numbers
//  make stale claims detectable after release or revoke.
struct claim_body_t
{
    mesh_node_t *node;
    uint64_t node_generation;
    owner_id_t owner;
    domain_t domain;
    uint64_t serial;
};

//  --- operations -----------------------------------------------------------

//  One list node is allocated with the operation, before request delivery.
//  Completion moves that node into the requester mailbox with list::splice,
//  so terminal delivery cannot lose the operation while allocating a queue
//  slot.
struct completion_reservation_t
{
    completion_reservation_t () : prepared (false), committing (false) {}

    std::list<std::unique_ptr<queued_record_t>> records;
    bool prepared;
    //  A two-phase terminal path has reserved this operation. Timeout and
    //  shutdown completion attempts leave it to that path until commit or
    //  cancel, so an external side effect can run without holding node mutex.
    bool committing;
};

struct pending_operation_t
{
    zlink_mesh_operation_id_t id;
    zlink_mesh_operation_kind_t kind;
    //  Completion is delivered to this owner's infrastructure mailbox.
    owner_id_t requester;
    std::shared_ptr<completion_reservation_t> completion;
    //  The operation owns its timeout task: terminal completion and node
    //  destroy cancel it, so a committed timer can never outlive the
    //  operation and fire against a recycled node/serial (ABA).
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
    //  Remote actor join: the source-side commit on the wire reply needs the
    //  joining actor's identity.
    zlink_actor_ref_t join_actor;
};

//  --- peers ----------------------------------------------------------------

struct peer_state_t
{
    peer_state_t ();

    uint64_t intent_id;
    zlink_mesh_peer_source_t source;
    zlink_mesh_peer_state_t state;
    //  Admitted from the wire without a local intent: does not gate this
    //  node's readiness (readiness is an intent property per the spec).
    bool inbound;
    rid_bytes_t rid;
    uint64_t lifecycle_generation;
    uint64_t descriptor_revision;
    std::string endpoint;
    bool has_expected_rid;
    rid_bytes_t expected_rid;
    std::map<std::string, uint32_t> channels; //  advertised name -> weight
    int32_t last_error;
    uint64_t last_changed_ms;
};

//  --- subscriptions ---------------------------------------------------------

struct subscription_key_t
{
    std::string channel;
    std::string filter;
    zlink_spot_subscription_kind_t kind;

    bool operator< (const subscription_key_t &o_) const
    {
        if (channel != o_.channel)
            return channel < o_.channel;
        if (filter != o_.filter)
            return filter < o_.filter;
        return kind < o_.kind;
    }
};

//  --- logical Spot ----------------------------------------------------------

struct spot_state_t
{
    spot_state_t ();

    rid_bytes_t rid;
    uint64_t generation;
    zlink_spot_kind_t kind;
    //  Facade handles currently pointing at this logical Spot.
    uint32_t facade_count;
    uint32_t timer_count;
    uint32_t active_actor_count;
    bool draining;
    int publish_nodrop; //  default 1
    std::set<subscription_key_t> subscriptions;
    int32_t last_error;
    uint64_t last_changed_ms;
};

//  Caller-owned facade over a logical Spot.
struct spot_facade_t
{
    spot_facade_t ();
    bool check_tag () const { return tag == 0x4d455348; }

    uint32_t tag;
    mesh_node_t *node;
    rid_bytes_t spot_rid;
    uint64_t generation;
};

//  --- Actors ----------------------------------------------------------------

struct actor_state_t
{
    actor_state_t ();

    std::string id;
    uint64_t generation;
    uint64_t membership_epoch;
    //  Location: owning Spot rid + that Spot's lifecycle generation. The
    //  Spot's node is empty while the membership points at a local Spot.
    rid_bytes_t spot_rid;
    uint64_t spot_generation;
    rid_bytes_t spot_node_rid;
    bool draining;
};

//  --- actor transfer ---------------------------------------------------------

struct transfer_participant_descriptor_t
{
    transfer_participant_descriptor_t () :
        participant_id (0),
        binding_generation (0),
        allowance_messages (0),
        allowance_bytes (0)
    {
    }

    uint64_t participant_id;
    uint64_t binding_generation;
    uint64_t allowance_messages;
    uint64_t allowance_bytes;
};

struct transfer_participant_terminal_t
{
    transfer_participant_terminal_t () : participant_id (0), high_water (0) {}

    uint64_t participant_id;
    uint64_t high_water;
};

struct transfer_participant_state_t
{
    transfer_participant_state_t () :
        binding_generation (0),
        allowance_messages (0),
        allowance_bytes (0),
        terminal_high_water (0),
        acked_high_water (0),
        staged_bytes (0),
        terminal_sealed (false)
    {
    }

    uint64_t binding_generation;
    uint64_t allowance_messages;
    uint64_t allowance_bytes;
    uint64_t terminal_high_water;
    uint64_t acked_high_water;
    uint64_t staged_bytes;
    bool terminal_sealed;
    std::map<uint64_t, std::unique_ptr<queued_record_t>> staged;
};

//  One in-flight transfer on this node, keyed by the sealed token serial.
//  The source keeps the authoritative frozen snapshot until its commit; the
//  target stages the mailbox and each private participant independently.
struct transfer_state_t
{
    transfer_state_t ();

    uint64_t serial;
    zlink_actor_transfer_role_t role;
    zlink_actor_transfer_id_t transfer_id;
    zlink_actor_ref_t actor;
    uint64_t expected_epoch;
    uint64_t committed_epoch; //  0 until commit; guards idempotent retry
    uint64_t node_generation;
    rid_bytes_t peer_node_rid;
    zlink_actor_transfer_phase_t phase;
    uint64_t final_sequence;
    uint64_t reserve_messages;
    uint64_t reserve_bytes;
    uint64_t deadline_ms; //  target: sealed prepare deadline (0 = none)
    bool ready_exchanged;
    int32_t data_plane_result;
    int32_t data_plane_errno;
    //  source: frozen mailbox in original FIFO order (1-based sequences)
    std::vector<std::unique_ptr<queued_record_t>> snapshot;
    uint64_t acked_high_water;
    //  target: staged records by sequence until activate
    std::map<uint64_t, std::unique_ptr<queued_record_t>> staged;
    std::map<uint64_t, transfer_participant_state_t> participants;
    uint64_t offered_participant_messages;
    uint64_t offered_participant_bytes;
    bool seal_requested;
    bool source_complete;
    bool complete_sent;
};

//  --- monitor ----------------------------------------------------------------

struct monitor_state_t
{
    monitor_state_t ();
    bool check_tag () const { return tag == 0x4d4d4f4e; }

    uint32_t tag;
    mesh_node_t *node;
    zlink_mesh_monitor_event_mask_t mask;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<zlink_mesh_monitor_event_t> events;
    zlink_mesh_monitor_handler_fn handler;
    void *handler_userdata;
    //  Number of threads currently inside the registered handler; nonzero
    //  rejects re-entrant deregistration/close with EDEADLK.
    int handler_active;
    bool closed;
    zlink_mesh_monitor_status_t counters;
};

//  --- publisher ---------------------------------------------------------------

struct publisher_t
{
    publisher_t ();
    bool check_tag () const { return tag == 0x4d505542; }

    uint32_t tag;
    mesh_node_t *node;
    int nodrop; //  default 1
};

//  --- batches -----------------------------------------------------------------

struct ready_batch_t
{
    explicit ready_batch_t (size_t capacity_);
    bool check_tag () const { return tag == 0x4d524459; }

    uint32_t tag;
    size_t capacity;
    std::vector<zlink_mesh_ready_record_t> records;
    std::vector<claim_body_t> claims;
    std::vector<bool> claim_taken;
    std::atomic<bool> busy;
};

struct receive_batch_t
{
    receive_batch_t (size_t message_capacity_, size_t part_capacity_, size_t byte_capacity_);
    ~receive_batch_t ();
    bool check_tag () const { return tag == 0x4d524356; }
    void clear ();

    uint32_t tag;
    size_t message_capacity;
    size_t part_capacity;
    size_t byte_capacity;
    std::vector<zlink_mesh_receive_record_t> records;
    std::vector<zlink_msg_t> parts;
    //  Owned storage the record views point into.
    std::vector<std::unique_ptr<queued_record_t>> storage;
    std::atomic<bool> busy;
};

//  --- reply routes ------------------------------------------------------------

//  Reply token bodies live in the node's table; the public token carries
//  (node serial, entry serial) sealed into the opaque words.
struct reply_route_t
{
    enum kind_t
    {
        kind_generic = 1,
        kind_actor_join = 2,
        kind_transfer_relay = 3
    };

    reply_route_t () :
        kind (kind_generic),
        requester_node_generation (0),
        operation_kind (static_cast<zlink_mesh_operation_kind_t> (0)),
        consumed (false),
        in_flight (false),
        remote_origin (false),
        origin_generation (0),
        origin_correlation (0),
        join_target_spot_generation (0)
    {
        memset (&operation_id, 0, sizeof (operation_id));
        memset (&join_actor, 0, sizeof (join_actor));
    }

    kind_t kind;
    //  Local requester owner (in-process origin) that receives the reply
    //  record's completion.
    owner_id_t requester;
    uint64_t requester_node_generation;
    zlink_mesh_operation_id_t operation_id;
    zlink_mesh_operation_kind_t operation_kind;
    bool consumed;
    //  A reply submit reserves the token while fallible payload or wire work
    //  runs. Failure clears the reservation; only a successful commit sets
    //  consumed, so callers can retry without duplicate delivery.
    bool in_flight;
    //  Remote-origin requests: the reply travels back over the wire to this
    //  admitted peer with the requester-side correlation serial.
    bool remote_origin;
    rid_bytes_t origin_rid;
    uint64_t origin_generation;
    uint64_t origin_correlation;
    //  Pending join bookkeeping (actor join tokens only).
    zlink_actor_ref_t join_actor;
    rid_bytes_t join_target_spot_rid;
    uint64_t join_target_spot_generation;
};

//  --- the node ----------------------------------------------------------------

struct mesh_node_t
{
    explicit mesh_node_t (ctx_t *ctx_);
    ~mesh_node_t ();
    bool check_tag () const { return tag == 0x4d4e4f44; }

    uint32_t tag;
    ctx_t *ctx;

    mutable std::mutex mutex;
    std::condition_variable cv;

    //  identity / configuration (immutable after start)
    std::string mesh_name;
    std::string trust_profile;
    rid_bytes_t routing_id;
    std::string bind_endpoint;
    std::map<std::string, uint32_t> channels; //  name -> weight (0..100)

    zlink_mesh_node_state_t state;
    uint64_t lifecycle_generation;
    uint64_t descriptor_revision;
    int32_t last_error;
    uint64_t last_changed_ms;

    //  options
    int router_hwm_profile;
    int router_hwm_override;
    uint64_t mailbox_message_budget; //  0 = profile default
    uint64_t mailbox_byte_budget;
    int64_t max_msg_size; //  -1 unlimited
    int sndtimeo_ms;
    int rcvtimeo_ms;

    //  peers
    uint64_t next_intent_id;
    std::vector<peer_state_t> peers;
    std::map<std::string, size_t> rr_cursor; //  channel -> cursor

    //  owners & dispatch
    std::map<owner_id_t, owner_state_t> owners;
    //  Level-triggered readable set; guarded by mutex, signalled via cv and
    //  the registered ready handler / poller notification.
    std::set<std::pair<owner_id_t, int>> ready;
    zlink_mesh_ready_handler_fn ready_handler;
    void *ready_handler_userdata;
    int ready_handler_depth;
    //  Thread currently inside the ready handler (valid while depth > 0):
    //  same-thread (de)registration is re-entry (EDEADLK), other threads
    //  wait for the callback to return (spec 02-dispatch).
    std::thread::id ready_handler_thread;
    bool pollin_registered;
    //  Edge-signals the poller fd when the ready index becomes non-empty;
    //  drain_ready re-arms it while work remains (level-triggered contract).
    //  pollin_signaled keeps the socketpair at one pending byte.
    signaler_t ready_signaler;
    bool pollin_signaled;

    //  operations & replies
    uint64_t next_operation_serial;
    std::unordered_map<uint64_t, pending_operation_t> operations; //  key: op.low
    uint64_t next_reply_serial;
    std::unordered_map<uint64_t, reply_route_t> reply_routes;
    //  True while one shutdown call is inside its drain wait; a concurrent
    //  shutdown/destroy on the same handle is re-entry (EDEADLK). A
    //  sequential shutdown after a TIMED_OUT drain is legal and waits again.
    bool shutdown_active;
    //  Registry-owned lifecycle admission (guarded by the handle-registry
    //  mutex, not this node's mutex): shutdown pins the node for its whole
    //  call so destroy waits for the pins to drain before deleting, and
    //  destroy claims exclusivity so the reverse order is re-entry too.
    uint32_t lifecycle_pins;
    bool destroy_claimed;

    //  spots & actors
    std::map<std::string, spot_state_t> spots;   //  key: rid bytes as string
    std::map<std::string, actor_state_t> actors; //  key: actor id
    uint64_t next_spot_generation;
    uint64_t next_actor_generation;

    //  transfers (token serial -> state); one active transfer per actor id
    std::unordered_map<uint64_t, transfer_state_t> transfers;
    std::map<std::string, uint64_t> active_transfer_by_actor;
    uint64_t next_transfer_serial;

    //  children
    uint32_t publisher_count;
    uint32_t monitor_count;
    //  Emitters currently holding the monitor pointer; close waits for zero
    //  before deleting the monitor object.
    int monitor_emit_refs;
    uint32_t stream_session_count;
    monitor_state_t *monitor; //  single monitor handle (owned by caller)

    //  wire: the node-owned ROUTER, its monitor and the ingress thread.
    void *router_socket;
    void *router_monitor;
    std::thread io_thread;
    std::atomic<bool> io_stop;
    //  Serializes all outbound wire traffic so a NODROP multicast can probe
    //  every target pipe and then commit without interleaved sends. Locked
    //  after the node mutex when both are needed.
    std::mutex wire_send_mutex;

    //  Effective budgets resolved from options/profile at start.
    uint64_t effective_message_budget () const;
    uint64_t effective_byte_budget () const;
};

//  --- registry ----------------------------------------------------------------

//  Process-local registry keyed by MeshName (spec: one node per name per
//  process) plus tag-checked classification for generic handle routing.
mesh_node_t *find_node_by_name (const std::string &name_);
int register_node (mesh_node_t *node_);
void unregister_node (mesh_node_t *node_);

mesh_node_t *as_mesh_node (void *handle_);
//  §11 lifecycle admission: shutdown/destroy order against each other via
//  the registry so neither can free or use the node under the other.
#ifdef ZLINK_BUILD_TESTS
//  Consumes one armed test fault by throwing bad_alloc (no-op when unarmed).
void test_maybe_throw_alloc ();
#endif
mesh_node_t *pin_node_lifecycle (void *handle_, int *errno_out_);
void unpin_node_lifecycle (mesh_node_t *node_);
//  Data-path pin: validates the handle under the registry mutex and keeps
//  the node's storage alive until release. Unlike the shutdown pin it does
//  not reject a claimed-but-uncommitted destroy — a committed destroy is
//  invisible here because it unregisters first and then waits for pins.
mesh_node_t *pin_node_data_path (void *handle_);

//  RAII wrapper for pin_node_data_path: replaces raw as_mesh_node() use in
//  public entry points so no call can outlive the node it validated.
class mesh_node_pin_t
{
  public:
    explicit mesh_node_pin_t (void *handle_) : _node (pin_node_data_path (handle_)) {}
    ~mesh_node_pin_t ()
    {
        if (_node)
            unpin_node_lifecycle (_node);
    }
    mesh_node_t *get () const { return _node; }

  private:
    mesh_node_t *_node;
    mesh_node_pin_t (const mesh_node_pin_t &);
    mesh_node_pin_t &operator= (const mesh_node_pin_t &);
};

//  Reserves every fallible timeout resource before a request is delivered.
//  The timeout callback waits until commit; destroying an uncommitted guard
//  cancels it. This keeps request admission and timeout ownership atomic
//  without exposing scheduler policy to individual submit paths.
class operation_timeout_guard_t
{
  public:
    operation_timeout_guard_t (mesh_node_t *node_,
                               const zlink_mesh_operation_id_t &operation_id_,
                               uint32_t timeout_ms_);
    ~operation_timeout_guard_t ();
    bool valid () const;
    void commit ();

  private:
    void *_state;
    bool _valid;
    operation_timeout_guard_t (const operation_timeout_guard_t &);
    operation_timeout_guard_t &operator= (const operation_timeout_guard_t &);
};
mesh_node_t *claim_node_destroy (void *handle_);
void release_node_destroy_claim (mesh_node_t *node_);
void unregister_node_and_wait_lifecycle_quiesced (mesh_node_t *node_);
void track_facade (spot_facade_t *facade_, bool live_);
void track_publisher (publisher_t *publisher_, bool live_);
void track_monitor (monitor_state_t *monitor_, bool live_);
spot_facade_t *as_spot_facade (void *handle_);
publisher_t *as_publisher (void *handle_);
monitor_state_t *as_monitor (void *handle_);
ready_batch_t *as_ready_batch (void *handle_);
receive_batch_t *as_receive_batch (void *handle_);

//  --- core operations used by the C surface ------------------------------------

uint64_t now_ms ();

//  Wall-clock anchored, process-wide strictly increasing lifecycle
//  generation. Orders restarted lifecycles of the same RID (01-mesh-node §4).
uint64_t allocate_lifecycle_generation ();

//  The one owner of terminal operation removal: detaches the operation's
//  timeout task and erases the entry under the caller-held node mutex. The
//  caller cancels the returned task after releasing the mutex (a firing
//  timeout handler takes that mutex, and the scheduler resolves self-cancel).
//  Every terminal path must consume operations through this primitive so no
//  new path can forget the task handoff.
std::shared_ptr<zlink::request_timeout::task_t> detach_pending_operation_locked (
  mesh_node_t *node_, std::unordered_map<uint64_t, pending_operation_t>::iterator op_it_);

//  Validates a canonical application metadata frame. Returns 0 or -1/EINVAL.
int validate_metadata (const uint8_t *data_, size_t size_);

//  Enqueues a record into an owner mailbox with budget admission. Takes
//  ownership of record_ on success (returns 0); on failure returns -1 with
//  errno and leaves the record with the caller.
int admit_record (mesh_node_t *node_,
                  const owner_id_t &owner_,
                  domain_t domain_,
                  std::unique_ptr<queued_record_t> &record_,
                  bool blocking_,
                  uint32_t timeout_ms_);

//  Marks an owner/domain readable and wakes the registered consumer.
void signal_ready (mesh_node_t *node_, const owner_id_t &owner_, domain_t domain_);

//  Recomputes READY/PARTIAL_READY from the current peer set (mutex held).
void recompute_readiness_locked (mesh_node_t *node_);

//  Maps errno set by a failed submit path to the public submit result.
zlink_submit_result_t submit_errno_result ();

//  Single policy point for C-ABI allocation barriers.
zlink_submit_result_t submit_out_of_memory_result ();

//  Emits a monitor event if a monitor is open and the kind is selected.
void emit_monitor_event (mesh_node_t *node_, zlink_mesh_monitor_event_t &event_);

//  Accounts for and emits the observable terminal event of one operation.
void observe_operation_completed (mesh_node_t *node_,
                                  const zlink_mesh_operation_id_t &operation_id_,
                                  int32_t terminal_result_,
                                  int32_t failure_errno_);

//  Builds and admits a terminal record before removing the registered
//  operation. Returns 1 when delivered, 0 when the operation or owner already
//  ended, and -1/ENOMEM while leaving the operation pending for retry.
int complete_pending_operation (mesh_node_t *node_,
                                const pending_operation_t &op_,
                                int32_t terminal_result_,
                                int32_t failure_errno_,
                                std::vector<unsigned char> *kind_data_,
                                std::vector<zlink_msg_t> *reply_parts_);

//  Variant for state transitions that must commit under the same node lock
//  as terminal mailbox admission. commit_locked_ must not allocate or throw.
typedef bool (*pending_operation_commit_fn) (mesh_node_t *node_, void *userdata_);
int complete_pending_operation_with_commit (
  mesh_node_t *node_,
  const pending_operation_t &op_,
  int32_t terminal_result_,
  int32_t failure_errno_,
  std::vector<unsigned char> *kind_data_,
  std::vector<zlink_msg_t> *reply_parts_,
  pending_operation_commit_fn commit_locked_,
  void *commit_userdata_);

//  Two-phase completion for a terminal path with an external side effect.
//  prepare performs every fallible allocation and excludes competing
//  completion; commit only links preallocated nodes and updates counters.
struct pending_operation_completion_t
{
    pending_operation_completion_t () : active (false) {}

    pending_operation_t operation;
    owner_id_t ready_owner;
    std::set<std::pair<owner_id_t, int>> ready_node;
    bool active;
};
int prepare_pending_operation_completion (
  mesh_node_t *node_,
  const zlink_mesh_operation_id_t &operation_id_,
  pending_operation_completion_t *completion_);
void cancel_pending_operation_completion (
  mesh_node_t *node_,
  pending_operation_completion_t *completion_);
int commit_prepared_pending_operation (
  mesh_node_t *node_,
  pending_operation_completion_t *completion_,
  int32_t terminal_result_,
  int32_t failure_errno_);
}
}

#endif
