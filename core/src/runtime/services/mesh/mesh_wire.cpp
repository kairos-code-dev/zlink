/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "services/mesh/mesh_wire.hpp"
#include "api/mesh/mesh_c_internal.hpp"
#include "api/socket/socket_api_internal.hpp"
#include "utils/macros.hpp"

namespace zlink
{
namespace mesh
{
namespace
{
const unsigned char wire_magic_0 = 'Z';
const unsigned char wire_magic_1 = 'M';
const unsigned char wire_version = 1;
const unsigned char wire_flag_metadata = 0x01;
const int ingress_poll_ms = 20;

//  --- envelope codec ---------------------------------------------------------

void put_u8 (std::vector<unsigned char> &out_, unsigned char value_)
{
    out_.push_back (value_);
}

void put_u16 (std::vector<unsigned char> &out_, uint16_t value_)
{
    out_.push_back (static_cast<unsigned char> (value_ >> 8));
    out_.push_back (static_cast<unsigned char> (value_));
}

void put_u32 (std::vector<unsigned char> &out_, uint32_t value_)
{
    put_u16 (out_, static_cast<uint16_t> (value_ >> 16));
    put_u16 (out_, static_cast<uint16_t> (value_));
}

void put_u64 (std::vector<unsigned char> &out_, uint64_t value_)
{
    put_u32 (out_, static_cast<uint32_t> (value_ >> 32));
    put_u32 (out_, static_cast<uint32_t> (value_));
}

void put_bytes (std::vector<unsigned char> &out_, const void *data_, size_t size_)
{
    const unsigned char *bytes = static_cast<const unsigned char *> (data_);
    out_.insert (out_.end (), bytes, bytes + size_);
}

//  Bounds-checked big-endian reader over one received frame.
struct wire_reader_t
{
    wire_reader_t (const unsigned char *data_, size_t size_) :
        data (data_), size (size_), pos (0), failed (false)
    {
    }

    bool need (size_t bytes_)
    {
        if (pos + bytes_ > size) {
            failed = true;
            return false;
        }
        return true;
    }

    uint8_t u8 ()
    {
        if (!need (1))
            return 0;
        return data[pos++];
    }

    uint16_t u16 ()
    {
        const uint16_t high = u8 ();
        return static_cast<uint16_t> (high << 8 | u8 ());
    }

    uint32_t u32 ()
    {
        const uint32_t high = u16 ();
        return high << 16 | u16 ();
    }

    uint64_t u64 ()
    {
        const uint64_t high = u32 ();
        return high << 32 | u32 ();
    }

    std::string bytes (size_t count_)
    {
        if (!need (count_))
            return std::string ();
        const std::string out (reinterpret_cast<const char *> (data + pos), count_);
        pos += count_;
        return out;
    }

    const unsigned char *data;
    size_t size;
    size_t pos;
    bool failed;
};

//  Descriptor advertised in HELLO/ADMIT/UPDATE frames.
struct wire_descriptor_t
{
    std::string mesh_name;
    std::string trust_profile;
    uint64_t lifecycle_generation;
    uint64_t descriptor_revision;
    std::vector<std::pair<std::string, uint32_t> > channels;
};

void encode_descriptor_locked (mesh_node_t *node_, std::vector<unsigned char> &out_)
{
    put_u8 (out_, static_cast<unsigned char> (node_->mesh_name.size ()));
    put_bytes (out_, node_->mesh_name.data (), node_->mesh_name.size ());
    put_u8 (out_, static_cast<unsigned char> (node_->trust_profile.size ()));
    put_bytes (out_, node_->trust_profile.data (), node_->trust_profile.size ());
    put_u64 (out_, node_->lifecycle_generation);
    put_u64 (out_, node_->descriptor_revision);
    put_u16 (out_, static_cast<uint16_t> (node_->channels.size ()));
    for (std::map<std::string, uint32_t>::const_iterator it = node_->channels.begin ();
         it != node_->channels.end (); ++it) {
        put_u8 (out_, static_cast<unsigned char> (it->first.size ()));
        put_bytes (out_, it->first.data (), it->first.size ());
        put_u32 (out_, it->second);
    }
}

bool decode_descriptor (wire_reader_t &reader_, wire_descriptor_t *out_)
{
    const size_t name_len = reader_.u8 ();
    out_->mesh_name = reader_.bytes (name_len);
    const size_t trust_len = reader_.u8 ();
    out_->trust_profile = reader_.bytes (trust_len);
    out_->lifecycle_generation = reader_.u64 ();
    out_->descriptor_revision = reader_.u64 ();
    const size_t channel_count = reader_.u16 ();
    out_->channels.clear ();
    for (size_t i = 0; i < channel_count && !reader_.failed; ++i) {
        const size_t len = reader_.u8 ();
        const std::string name = reader_.bytes (len);
        const uint32_t weight = reader_.u32 ();
        out_->channels.push_back (std::make_pair (name, weight));
    }
    return !reader_.failed && !out_->mesh_name.empty ();
}

void put_rid (std::vector<unsigned char> &out_, const rid_bytes_t &rid_)
{
    put_u8 (out_, static_cast<unsigned char> (rid_.size ()));
    if (!rid_.empty ())
        put_bytes (out_, rid_.data (), rid_.size ());
}

rid_bytes_t read_rid (wire_reader_t &reader_)
{
    const size_t len = reader_.u8 ();
    const std::string bytes = reader_.bytes (len);
    return rid_bytes_t (bytes.begin (), bytes.end ());
}

void put_actor_ref (std::vector<unsigned char> &out_, const zlink_actor_ref_t &ref_)
{
    const size_t id_len = strnlen (ref_.actor_id, sizeof (ref_.actor_id));
    put_u8 (out_, static_cast<unsigned char> (id_len));
    put_bytes (out_, ref_.actor_id, id_len);
    put_u64 (out_, ref_.generation);
}

//  Reads an actor ref advertised by source_rid_'s node.
zlink_actor_ref_t read_actor_ref (wire_reader_t &reader_, const rid_bytes_t &node_rid_)
{
    zlink_actor_ref_t ref;
    memset (&ref, 0, sizeof (ref));
    const size_t id_len = reader_.u8 ();
    const std::string id = reader_.bytes (id_len);
    snprintf (ref.actor_id, sizeof (ref.actor_id), "%.*s", (int) id.size (), id.data ());
    ref.generation = reader_.u64 ();
    ref.node_rid = rid_value (node_rid_);
    return ref;
}

void put_blob16 (std::vector<unsigned char> &out_, const std::vector<unsigned char> &blob_)
{
    put_u16 (out_, static_cast<uint16_t> (blob_.size ()));
    if (!blob_.empty ())
        put_bytes (out_, blob_.data (), blob_.size ());
}

std::vector<unsigned char> read_blob16 (wire_reader_t &reader_)
{
    const size_t len = reader_.u16 ();
    const std::string bytes = reader_.bytes (len);
    return std::vector<unsigned char> (bytes.begin (), bytes.end ());
}

//  Serializes one frozen record for TRANSFER_DATA (parts travel as payload
//  frames). relay_serial_ replaces a source-sealed reply token with the
//  source-route relay key; 0 means the record carries no reply route.
void put_record_header (std::vector<unsigned char> &out_,
                        const queued_record_t &record_,
                        uint64_t relay_serial_)
{
    put_u8 (out_, static_cast<unsigned char> (record_.kind));
    put_rid (out_, record_.source_node_rid);
    put_rid (out_, record_.source_spot_rid);
    put_actor_ref (out_, record_.source_actor);
    put_u8 (out_, static_cast<unsigned char> (record_.channel_name.size ()));
    put_bytes (out_, record_.channel_name.data (), record_.channel_name.size ());
    put_u8 (out_, static_cast<unsigned char> (record_.topic.size ()));
    put_bytes (out_, record_.topic.data (), record_.topic.size ());
    put_u8 (out_, record_.has_metadata ? 1 : 0);
    if (record_.has_metadata)
        put_blob16 (out_, record_.application_metadata);
    put_u64 (out_, record_.operation_id.high);
    put_u64 (out_, record_.operation_id.low);
    put_u32 (out_, static_cast<uint32_t> (record_.operation_kind));
    put_u8 (out_, relay_serial_ != 0 ? 1 : 0);
    if (relay_serial_ != 0)
        put_u64 (out_, relay_serial_);
    put_blob16 (out_, record_.kind_data);
    put_u32 (out_, static_cast<uint32_t> (record_.terminal_result));
    put_u32 (out_, static_cast<uint32_t> (record_.failure_errno));
}

bool read_record_header (wire_reader_t &reader_,
                         queued_record_t *record_,
                         uint64_t *relay_serial_out_)
{
    record_->kind = static_cast<zlink_mesh_record_kind_t> (reader_.u8 ());
    record_->source_node_rid = read_rid (reader_);
    record_->source_spot_rid = read_rid (reader_);
    {
        const size_t id_len = reader_.u8 ();
        const std::string id = reader_.bytes (id_len);
        memset (&record_->source_actor, 0, sizeof (record_->source_actor));
        snprintf (record_->source_actor.actor_id, sizeof (record_->source_actor.actor_id), "%.*s",
                  (int) id.size (), id.data ());
        record_->source_actor.generation = reader_.u64 ();
    }
    {
        const size_t len = reader_.u8 ();
        record_->channel_name = reader_.bytes (len);
    }
    {
        const size_t len = reader_.u8 ();
        record_->topic = reader_.bytes (len);
    }
    record_->has_metadata = reader_.u8 () != 0;
    if (record_->has_metadata) {
        record_->application_metadata = read_blob16 (reader_);
        record_->byte_size += record_->application_metadata.size ();
    }
    record_->operation_id.high = reader_.u64 ();
    record_->operation_id.low = reader_.u64 ();
    record_->operation_kind = static_cast<zlink_mesh_operation_kind_t> (reader_.u32 ());
    *relay_serial_out_ = 0;
    if (reader_.u8 () != 0)
        *relay_serial_out_ = reader_.u64 ();
    record_->kind_data = read_blob16 (reader_);
    record_->terminal_result = static_cast<int32_t> (reader_.u32 ());
    record_->failure_errno = static_cast<int32_t> (reader_.u32 ());
    return !reader_.failed;
}

void put_transfer_id (std::vector<unsigned char> &out_, const zlink_actor_transfer_id_t &id_)
{
    put_u64 (out_, id_.high);
    put_u64 (out_, id_.low);
}

zlink_actor_transfer_id_t read_transfer_id (wire_reader_t &reader_)
{
    zlink_actor_transfer_id_t id;
    id.high = reader_.u64 ();
    id.low = reader_.u64 ();
    return id;
}

std::vector<unsigned char> make_envelope (unsigned char type_, unsigned char flags_)
{
    std::vector<unsigned char> frame;
    frame.reserve (64);
    put_u8 (frame, wire_magic_0);
    put_u8 (frame, wire_magic_1);
    put_u8 (frame, wire_version);
    put_u8 (frame, type_);
    put_u8 (frame, flags_);
    return frame;
}

//  --- frame transmission -------------------------------------------------------

int send_frame (mesh_node_t *node_,
                const zlink_routing_id_t &target_,
                const std::vector<unsigned char> &bytes_,
                zlink_part_flag_t part_flag_,
                zlink_send_flags_t flags_)
{
    zlink_msg_t frame;
    if (zlink_msg_init_size (&frame, bytes_.size ()) != 0)
        return -1;
    memcpy (zlink_msg_data (&frame), bytes_.data (), bytes_.size ());
    const zlink_submit_result_t rc =
      zlink_send_part_rid (node_->router_socket, &target_, &frame, flags_, part_flag_);
    if (rc != ZLINK_SUBMIT_OK) {
        const int saved_errno = errno;
        zlink_msg_close (&frame);
        errno = saved_errno;
        return -1;
    }
    return 0;
}

//  Sends one control message (single frame) to target_ without blocking.
int send_control (mesh_node_t *node_,
                  const zlink_routing_id_t &target_,
                  const std::vector<unsigned char> &frame_)
{
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    return send_frame (node_, target_, frame_, ZLINK_PART_FINAL, ZLINK_SEND_FLAGS_DONTWAIT);
}

//  send_data_message body for callers that already hold wire_send_mutex.
zlink_submit_result_t send_data_message_unlocked (mesh_node_t *node_,
                                                  const zlink_routing_id_t &target_,
                                                  const std::vector<unsigned char> &envelope_,
                                                  const std::vector<unsigned char> *metadata_,
                                                  const zlink_msg_t *parts_,
                                                  size_t part_count_,
                                                  zlink_send_flags_t flags_);

//  Sends envelope [+ metadata] + payload parts. Payload parts are borrowed
//  from the caller and copied (reference counted) per part.
zlink_submit_result_t send_data_message (mesh_node_t *node_,
                                         const zlink_routing_id_t &target_,
                                         const std::vector<unsigned char> &envelope_,
                                         const std::vector<unsigned char> *metadata_,
                                         const zlink_msg_t *parts_,
                                         size_t part_count_,
                                         zlink_send_flags_t flags_)
{
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    return send_data_message_unlocked (node_, target_, envelope_, metadata_, parts_, part_count_,
                                       flags_);
}

zlink_submit_result_t send_data_message_unlocked (mesh_node_t *node_,
                                                  const zlink_routing_id_t &target_,
                                                  const std::vector<unsigned char> &envelope_,
                                                  const std::vector<unsigned char> *metadata_,
                                                  const zlink_msg_t *parts_,
                                                  size_t part_count_,
                                                  zlink_send_flags_t flags_)
{
    const bool has_payload = part_count_ > 0;
    if (send_frame (node_, target_, envelope_,
                    (metadata_ || has_payload) ? ZLINK_PART_MORE : ZLINK_PART_FINAL, flags_)
        != 0)
        return submit_errno_result ();
    if (metadata_) {
        if (send_frame (node_, target_, *metadata_,
                        has_payload ? ZLINK_PART_MORE : ZLINK_PART_FINAL, flags_)
            != 0)
            return submit_errno_result ();
    }
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_t copy;
        if (zlink_msg_init (&copy) != 0)
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        if (zlink_msg_copy (&copy, const_cast<zlink_msg_t *> (&parts_[i])) != 0) {
            zlink_msg_close (&copy);
            errno = EFAULT;
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        }
        const zlink_part_flag_t part_flag =
          (i + 1 < part_count_) ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        const zlink_submit_result_t rc =
          zlink_send_part_rid (node_->router_socket, &target_, &copy, flags_, part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            const int saved_errno = errno;
            zlink_msg_close (&copy);
            errno = saved_errno;
            return submit_errno_result ();
        }
    }
    return ZLINK_SUBMIT_OK;
}

//  --- peer bookkeeping ----------------------------------------------------------

peer_state_t *find_peer_by_rid_locked (mesh_node_t *node_, const rid_bytes_t &rid_)
{
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        if (node_->peers[i].rid == rid_ && node_->peers[i].state != ZLINK_MESH_PEER_CLOSED)
            return &node_->peers[i];
    }
    return NULL;
}

peer_state_t *find_intent_by_endpoint_locked (mesh_node_t *node_, const std::string &endpoint_)
{
    //  A re-established transport re-runs the handshake, so intents in the
    //  ERROR or ADMITTED state are eligible again alongside CONNECTING.
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        peer_state_t &peer = node_->peers[i];
        if (peer.endpoint != endpoint_ || peer.inbound)
            continue;
        if (peer.state == ZLINK_MESH_PEER_CONNECTING || peer.state == ZLINK_MESH_PEER_ERROR
            || peer.state == ZLINK_MESH_PEER_ADMITTED)
            return &peer;
    }
    return NULL;
}

void emit_peer_event (mesh_node_t *node_,
                      zlink_mesh_monitor_event_kind_t kind_,
                      const rid_bytes_t &rid_,
                      int32_t error_)
{
    zlink_mesh_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    event.kind = kind_;
    if (!rid_.empty ()) {
        event.peer_rid.size = static_cast<uint8_t> (rid_.size ());
        memcpy (event.peer_rid.data, rid_.data (), rid_.size ());
    }
    event.failure_errno = error_;
    emit_monitor_event (node_, event);
}

//  Applies an admitted descriptor to a peer entry (mutex held).
void apply_descriptor_locked (peer_state_t *peer_, const wire_descriptor_t &descriptor_)
{
    //  Same-generation updates only move forward; a higher revision replaces
    //  the channel index atomically under the node mutex.
    if (peer_->descriptor_revision >= descriptor_.descriptor_revision
        && peer_->lifecycle_generation == descriptor_.lifecycle_generation)
        return;
    peer_->lifecycle_generation = descriptor_.lifecycle_generation;
    peer_->descriptor_revision = descriptor_.descriptor_revision;
    peer_->channels.clear ();
    for (size_t i = 0; i < descriptor_.channels.size (); ++i)
        peer_->channels[descriptor_.channels[i].first] = descriptor_.channels[i].second;
    peer_->last_changed_ms = now_ms ();
}

//  Validates an inbound descriptor against local admission rules. Returns 0
//  or -1 with errno describing the conflict class.
int validate_admission_locked (mesh_node_t *node_,
                               const rid_bytes_t &source_rid_,
                               const wire_descriptor_t &descriptor_,
                               const peer_state_t *intent_)
{
    if (descriptor_.mesh_name != node_->mesh_name) {
        errno = EEXIST;
        return -1;
    }
    if (descriptor_.trust_profile != node_->trust_profile) {
        errno = EACCES;
        return -1;
    }
    if (intent_ && intent_->has_expected_rid) {
        const rid_bytes_t expected = intent_->expected_rid;
        if (expected != source_rid_) {
            errno = ESTALE;
            return -1;
        }
    }
    const peer_state_t *existing = find_peer_by_rid_locked (node_, source_rid_);
    if (existing && existing->state == ZLINK_MESH_PEER_ADMITTED
        && existing->lifecycle_generation > descriptor_.lifecycle_generation) {
        //  A stale generation cannot replace a newer admitted lifetime.
        errno = ESTALE;
        return -1;
    }
    return 0;
}

//  --- ingress: control ------------------------------------------------------------

void handle_hello_or_admit (mesh_node_t *node_,
                            const rid_bytes_t &source_rid_,
                            const wire_descriptor_t &descriptor_,
                            bool is_hello_)
{
    bool send_admit = false;
    bool send_reject = false;
    int reject_errno = 0;
    zlink_routing_id_t target = rid_value (source_rid_);

    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        if (node_->state != ZLINK_MESH_NODE_STARTED && node_->state != ZLINK_MESH_NODE_PARTIAL_READY
            && node_->state != ZLINK_MESH_NODE_READY)
            return;

        peer_state_t *intent = NULL;
        if (!is_hello_) {
            //  ADMIT answers our HELLO: it must match a connecting (or
            //  re-connecting) intent by the advertised rid, or any
            //  connecting intent still awaiting its rid.
            intent = find_peer_by_rid_locked (node_, source_rid_);
            if (intent
                && (intent->inbound
                    || (intent->state != ZLINK_MESH_PEER_CONNECTING
                        && intent->state != ZLINK_MESH_PEER_ERROR
                        && intent->state != ZLINK_MESH_PEER_ADMITTED)))
                intent = NULL;
            if (!intent) {
                for (size_t i = 0; i < node_->peers.size () && !intent; ++i) {
                    if (node_->peers[i].state == ZLINK_MESH_PEER_CONNECTING
                        && node_->peers[i].rid.empty () && !node_->peers[i].inbound)
                        intent = &node_->peers[i];
                }
            }
            if (!intent)
                return;
        }

        if (validate_admission_locked (node_, source_rid_, descriptor_, intent) != 0) {
            const int reason = errno;
            if (is_hello_) {
                send_reject = true;
                reject_errno = reason;
            } else if (intent) {
                intent->state = ZLINK_MESH_PEER_ERROR;
                intent->last_error = reason;
                intent->last_changed_ms = now_ms ();
                recompute_readiness_locked (node_);
            }
        } else {
            peer_state_t *peer = intent;
            if (!peer)
                peer = find_peer_by_rid_locked (node_, source_rid_);
            if (!peer) {
                //  Inbound admitted peer without a local intent.
                peer_state_t fresh;
                fresh.intent_id = node_->next_intent_id++;
                fresh.source = ZLINK_MESH_PEER_MANUAL;
                fresh.inbound = true;
                node_->peers.push_back (fresh);
                peer = &node_->peers.back ();
            }
            peer->rid = source_rid_;
            peer->state = ZLINK_MESH_PEER_ADMITTED;
            peer->last_error = 0;
            apply_descriptor_locked (peer, descriptor_);
            recompute_readiness_locked (node_);
            if (is_hello_)
                send_admit = true;
        }
    }

    if (send_admit) {
        std::vector<unsigned char> frame = make_envelope (wire_admit, 0);
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            encode_descriptor_locked (node_, frame);
        }
        send_control (node_, target, frame);
        emit_peer_event (node_, ZLINK_MESH_MONITOR_PEER_ADMITTED, source_rid_, 0);
    } else if (send_reject) {
        std::vector<unsigned char> frame = make_envelope (wire_reject, 0);
        put_u32 (frame, static_cast<uint32_t> (reject_errno));
        send_control (node_, target, frame);
    } else if (!is_hello_) {
        emit_peer_event (node_, ZLINK_MESH_MONITOR_PEER_ADMITTED, source_rid_, 0);
    }
}

void handle_reject (mesh_node_t *node_, const rid_bytes_t &source_rid_, uint32_t reason_)
{
    std::lock_guard<std::mutex> lock (node_->mutex);
    //  The rejecting side answered our HELLO; the matching intent is either
    //  keyed by rid (expected) or the connecting intent awaiting its rid.
    peer_state_t *intent = find_peer_by_rid_locked (node_, source_rid_);
    if (!intent || intent->state != ZLINK_MESH_PEER_CONNECTING) {
        intent = NULL;
        for (size_t i = 0; i < node_->peers.size () && !intent; ++i) {
            if (node_->peers[i].state == ZLINK_MESH_PEER_CONNECTING)
                intent = &node_->peers[i];
        }
    }
    if (!intent)
        return;
    intent->rid = source_rid_;
    intent->state = ZLINK_MESH_PEER_ERROR;
    intent->last_error = static_cast<int32_t> (reason_);
    intent->last_changed_ms = now_ms ();
    recompute_readiness_locked (node_);
}

void handle_update (mesh_node_t *node_,
                    const rid_bytes_t &source_rid_,
                    const wire_descriptor_t &descriptor_)
{
    std::lock_guard<std::mutex> lock (node_->mutex);
    peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
    if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED)
        return;
    if (peer->lifecycle_generation != descriptor_.lifecycle_generation)
        return;
    apply_descriptor_locked (peer, descriptor_);
}

void handle_peer_down (mesh_node_t *node_, const rid_bytes_t &rid_)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer = find_peer_by_rid_locked (node_, rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED)
            return;
        peer->state = ZLINK_MESH_PEER_ERROR;
        peer->last_error = ENOTCONN;
        peer->last_changed_ms = now_ms ();
        recompute_readiness_locked (node_);
        changed = true;
    }
    if (changed)
        emit_peer_event (node_, ZLINK_MESH_MONITOR_PEER_CLOSED, rid_, ENOTCONN);
}

//  --- ingress: data -----------------------------------------------------------------

//  Admits one wire data record into the Node application mailbox. Requests
//  install a remote-origin reply route first so the receiver can answer
//  through the regular one-shot token.
void handle_data (mesh_node_t *node_,
                  const rid_bytes_t &source_rid_,
                  unsigned char type_,
                  uint64_t correlation_,
                  const std::string &channel_,
                  std::vector<unsigned char> *metadata_,
                  std::vector<zlink_msg_t> *parts_)
{
    const bool is_request = type_ == wire_node_request || type_ == wire_channel_request;

    uint64_t origin_generation = 0;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED)
            return; //  Unadmitted traffic is dropped by contract.
        origin_generation = peer->lifecycle_generation;
    }

    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ())
        return;
    switch (type_) {
        case wire_node_send:
            record->kind = ZLINK_MESH_RECORD_NODE_SEND;
            break;
        case wire_node_request:
            record->kind = ZLINK_MESH_RECORD_NODE_REQUEST;
            record->operation_kind = ZLINK_MESH_OPERATION_NODE_REQUEST;
            break;
        case wire_channel_send:
            record->kind = ZLINK_MESH_RECORD_CHANNEL_SEND;
            break;
        case wire_channel_request:
        default:
            record->kind = ZLINK_MESH_RECORD_CHANNEL_REQUEST;
            record->operation_kind = ZLINK_MESH_OPERATION_CHANNEL_REQUEST;
            break;
    }
    record->source_node_rid = source_rid_;
    record->channel_name = channel_;
    if (metadata_ && !metadata_->empty ()) {
        record->has_metadata = true;
        record->application_metadata = std::move (*metadata_);
        record->byte_size += record->application_metadata.size ();
    }
    record->parts = std::move (*parts_);
    parts_->clear ();
    for (size_t i = 0; i < record->parts.size (); ++i)
        record->byte_size += zlink_msg_size (&record->parts[i]);

    uint64_t reply_serial = 0;
    if (is_request) {
        std::lock_guard<std::mutex> lock (node_->mutex);
        reply_serial = node_->next_reply_serial++;
        reply_route_t route;
        route.kind = reply_route_t::kind_generic;
        route.requester = node_owner ();
        route.requester_node_generation = node_->lifecycle_generation;
        memset (&route.operation_id, 0, sizeof (route.operation_id));
        route.operation_kind = record->operation_kind;
        route.consumed = false;
        route.remote_origin = true;
        route.origin_rid = source_rid_;
        route.origin_generation = origin_generation;
        route.origin_correlation = correlation_;
        memset (&route.join_actor, 0, sizeof (route.join_actor));
        route.join_target_spot_generation = 0;
        node_->reply_routes[reply_serial] = route;
        record->has_reply_token = true;
        seal_reply_token (node_, reply_serial, &record->reply_token);
    }

    if (admit_record (node_, node_owner (), domain_application, record, false, 0) != 0) {
        //  Route failure at admission: requests answer with a terminal
        //  failure so the requester completes exactly once.
        const int reason = errno;
        if (is_request) {
            {
                std::lock_guard<std::mutex> lock (node_->mutex);
                node_->reply_routes.erase (reply_serial);
            }
            wire_submit_reply (node_, source_rid_, correlation_,
                               reason == EAGAIN ? ZLINK_REQUEST_BACKPRESSURED
                                                : ZLINK_REQUEST_INTERNAL_ERROR,
                               reason, NULL, 0);
        }
    }
}

void close_frames (std::vector<zlink_msg_t> *frames_);

//  Spot direct ingress: locate the target Spot by rid + generation. Requests
//  answer absence/conflict with a terminal completion for exactly-once.
void handle_spot_data (mesh_node_t *node_,
                       const rid_bytes_t &source_rid_,
                       bool is_request_,
                       uint64_t correlation_,
                       const rid_bytes_t &source_spot_rid_,
                       const rid_bytes_t &target_spot_rid_,
                       uint64_t target_spot_generation_,
                       std::vector<unsigned char> *metadata_,
                       std::vector<zlink_msg_t> *parts_)
{
    uint64_t origin_generation = 0;
    bool admitted_peer = false;
    owner_id_t destination;
    bool target_found = false;
    bool generation_conflict = false;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
        if (peer && peer->state == ZLINK_MESH_PEER_ADMITTED) {
            admitted_peer = true;
            origin_generation = peer->lifecycle_generation;
        }
        const std::string key (target_spot_rid_.begin (), target_spot_rid_.end ());
        std::map<std::string, spot_state_t>::iterator it = node_->spots.find (key);
        if (it != node_->spots.end () && !it->second.draining) {
            if (it->second.generation == target_spot_generation_) {
                target_found = true;
                destination = spot_owner (target_spot_rid_, it->second.generation);
            } else {
                generation_conflict = true;
            }
        }
    }
    if (!admitted_peer) {
        close_frames (parts_);
        return;
    }
    if (!target_found) {
        close_frames (parts_);
        if (is_request_) {
            wire_submit_reply (node_, source_rid_, correlation_,
                               generation_conflict ? ZLINK_REQUEST_CONFLICT
                                                   : ZLINK_REQUEST_NOT_FOUND,
                               generation_conflict ? ESTALE : ENOENT, NULL, 0);
        }
        return;
    }

    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ()) {
        close_frames (parts_);
        return;
    }
    record->kind = is_request_ ? ZLINK_MESH_RECORD_SPOT_REQUEST : ZLINK_MESH_RECORD_SPOT_SEND;
    record->source_node_rid = source_rid_;
    record->source_spot_rid = source_spot_rid_;
    if (metadata_ && !metadata_->empty ()) {
        record->has_metadata = true;
        record->application_metadata = std::move (*metadata_);
        record->byte_size += record->application_metadata.size ();
    }
    record->parts = std::move (*parts_);
    parts_->clear ();
    for (size_t i = 0; i < record->parts.size (); ++i)
        record->byte_size += zlink_msg_size (&record->parts[i]);

    uint64_t reply_serial = 0;
    if (is_request_) {
        std::lock_guard<std::mutex> lock (node_->mutex);
        reply_serial = node_->next_reply_serial++;
        reply_route_t route;
        route.requester = node_owner ();
        route.requester_node_generation = node_->lifecycle_generation;
        route.operation_kind = ZLINK_MESH_OPERATION_SPOT_REQUEST;
        route.remote_origin = true;
        route.origin_rid = source_rid_;
        route.origin_generation = origin_generation;
        route.origin_correlation = correlation_;
        node_->reply_routes[reply_serial] = route;
        record->operation_kind = ZLINK_MESH_OPERATION_SPOT_REQUEST;
        record->has_reply_token = true;
        seal_reply_token (node_, reply_serial, &record->reply_token);
    }

    if (admit_record (node_, destination, domain_application, record, false, 0) != 0) {
        const int reason = errno;
        if (is_request_) {
            {
                std::lock_guard<std::mutex> lock (node_->mutex);
                node_->reply_routes.erase (reply_serial);
            }
            wire_submit_reply (node_, source_rid_, correlation_,
                               reason == EAGAIN ? ZLINK_REQUEST_BACKPRESSURED
                                                : ZLINK_REQUEST_INTERNAL_ERROR,
                               reason, NULL, 0);
        }
    }
}

//  Multicast ingress: fan out to local subscription matches when this node
//  is a member of the channel. Local budget misses drop that Spot only.
void handle_multicast (mesh_node_t *node_,
                       const rid_bytes_t &source_rid_,
                       const std::string &channel_,
                       const std::string &topic_,
                       const rid_bytes_t &source_spot_rid_,
                       std::vector<unsigned char> *metadata_,
                       std::vector<zlink_msg_t> *parts_)
{
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED) {
            close_frames (parts_);
            return;
        }
    }

    size_t payload_bytes = 0;
    for (size_t i = 0; i < parts_->size (); ++i)
        payload_bytes += zlink_msg_size (&(*parts_)[i]);

    uint32_t dropped = 0;
    std::vector<owner_id_t> delivered_to;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        if (node_->channels.count (channel_) == 0) {
            //  Not a member: the sender snapshot was stale; drop silently.
        } else {
            const uint64_t message_budget = node_->effective_message_budget ();
            const uint64_t byte_budget = node_->effective_byte_budget ();
            for (std::map<std::string, spot_state_t>::iterator it = node_->spots.begin ();
                 it != node_->spots.end (); ++it) {
                spot_state_t &spot = it->second;
                if (spot.draining)
                    continue;
                bool match = false;
                for (std::set<subscription_key_t>::const_iterator sub =
                       spot.subscriptions.begin ();
                     sub != spot.subscriptions.end () && !match; ++sub) {
                    if (sub->channel != channel_)
                        continue;
                    if (sub->kind == ZLINK_SPOT_SUBSCRIPTION_EXACT)
                        match = sub->filter == topic_;
                    else
                        match = topic_.compare (0, sub->filter.size (), sub->filter) == 0;
                }
                if (!match)
                    continue;
                const owner_id_t owner = spot_owner (spot.rid, spot.generation);
                std::map<owner_id_t, owner_state_t>::iterator owner_it =
                  node_->owners.find (owner);
                if (owner_it == node_->owners.end ()) {
                    ++dropped;
                    continue;
                }
                mailbox_t &mailbox = owner_it->second.domains[domain_application];
                if (mailbox.pending_messages + 1 > message_budget
                    || mailbox.pending_bytes + payload_bytes > byte_budget) {
                    ++dropped;
                    continue;
                }
                std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
                if (!record.get ()) {
                    ++dropped;
                    continue;
                }
                record->kind = ZLINK_MESH_RECORD_SPOT_MULTICAST;
                record->source_node_rid = source_rid_;
                record->source_spot_rid = source_spot_rid_;
                record->channel_name = channel_;
                record->topic = topic_;
                if (metadata_ && !metadata_->empty ()) {
                    record->has_metadata = true;
                    record->application_metadata = *metadata_;
                    record->byte_size += record->application_metadata.size ();
                }
                record->parts.resize (parts_->size ());
                bool copy_failed = false;
                for (size_t i = 0; i < parts_->size (); ++i) {
                    zlink_msg_init (&record->parts[i]);
                    if (zlink_msg_copy (&record->parts[i], &(*parts_)[i]) != 0)
                        copy_failed = true;
                }
                if (copy_failed) {
                    ++dropped;
                    continue;
                }
                record->byte_size += payload_bytes;
                mailbox.pending_messages += 1;
                mailbox.pending_bytes += record->byte_size;
                mailbox.records.push_back (std::move (record));
                node_->ready.insert (
                  std::make_pair (owner, static_cast<int> (domain_application)));
                delivered_to.push_back (owner);
            }
            node_->cv.notify_all ();
        }
    }
    close_frames (parts_);
    if (!delivered_to.empty ())
        signal_ready (node_, delivered_to[0], domain_application);
    if (dropped > 0) {
        zlink_mesh_monitor_event_t event;
        memset (&event, 0, sizeof (event));
        event.kind = ZLINK_MESH_MONITOR_MULTICAST_DROPPED;
        event.dropped_local_spot_count = dropped;
        snprintf (event.channel_name, sizeof (event.channel_name), "%s", channel_.c_str ());
        emit_monitor_event (node_, event);
    }
}

//  Actor data ingress: the destination validates the generation and admits
//  into the actor's application mailbox; requests install a remote-origin
//  generic reply route.
void handle_actor_data (mesh_node_t *node_,
                        const rid_bytes_t &source_rid_,
                        bool is_request_,
                        uint64_t correlation_,
                        const zlink_actor_ref_t &source_actor_,
                        bool has_source_actor_,
                        const zlink_actor_ref_t &target_actor_,
                        std::vector<unsigned char> *metadata_,
                        std::vector<zlink_msg_t> *parts_)
{
    uint64_t origin_generation = 0;
    owner_id_t destination;
    int fail_errno = 0;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED) {
            close_frames (parts_);
            return;
        }
        origin_generation = peer->lifecycle_generation;
        const std::string id (target_actor_.actor_id);
        std::map<std::string, actor_state_t>::iterator it = node_->actors.find (id);
        if (it == node_->actors.end ())
            fail_errno = ENOENT;
        else if (it->second.generation != target_actor_.generation || it->second.draining)
            fail_errno = ESTALE;
        else
            destination = actor_owner (id, it->second.generation);
    }
    if (fail_errno != 0) {
        close_frames (parts_);
        if (is_request_) {
            wire_submit_reply (node_, source_rid_, correlation_,
                               fail_errno == ENOENT ? ZLINK_REQUEST_NOT_FOUND
                                                    : ZLINK_REQUEST_CONFLICT,
                               fail_errno, NULL, 0);
        }
        return;
    }

    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ()) {
        close_frames (parts_);
        return;
    }
    record->kind = is_request_ ? ZLINK_MESH_RECORD_ACTOR_REQUEST : ZLINK_MESH_RECORD_ACTOR_SEND;
    record->source_node_rid = source_rid_;
    if (has_source_actor_)
        record->source_actor = source_actor_;
    if (metadata_ && !metadata_->empty ()) {
        record->has_metadata = true;
        record->application_metadata = std::move (*metadata_);
        record->byte_size += record->application_metadata.size ();
    }
    record->parts = std::move (*parts_);
    parts_->clear ();
    for (size_t i = 0; i < record->parts.size (); ++i)
        record->byte_size += zlink_msg_size (&record->parts[i]);

    uint64_t reply_serial = 0;
    if (is_request_) {
        std::lock_guard<std::mutex> lock (node_->mutex);
        reply_serial = node_->next_reply_serial++;
        reply_route_t route;
        route.requester = node_owner ();
        route.requester_node_generation = node_->lifecycle_generation;
        route.operation_kind = ZLINK_MESH_OPERATION_ACTOR_REQUEST;
        route.remote_origin = true;
        route.origin_rid = source_rid_;
        route.origin_generation = origin_generation;
        route.origin_correlation = correlation_;
        node_->reply_routes[reply_serial] = route;
        record->operation_kind = ZLINK_MESH_OPERATION_ACTOR_REQUEST;
        record->has_reply_token = true;
        seal_reply_token (node_, reply_serial, &record->reply_token);
    }

    if (admit_record (node_, destination, domain_application, record, false, 0) != 0) {
        const int reason = errno;
        if (is_request_) {
            {
                std::lock_guard<std::mutex> lock (node_->mutex);
                node_->reply_routes.erase (reply_serial);
            }
            wire_submit_reply (node_, source_rid_, correlation_,
                               reason == EAGAIN ? ZLINK_REQUEST_BACKPRESSURED
                                                : ZLINK_REQUEST_INTERNAL_ERROR,
                               reason, NULL, 0);
        }
    }
}

void handle_actor_lookup (mesh_node_t *node_,
                          const rid_bytes_t &source_rid_,
                          uint64_t correlation_,
                          const std::string &actor_id_)
{
    zlink_actor_ref_t ref;
    rid_bytes_t spot_rid;
    uint64_t spot_generation = 0;
    uint64_t membership_epoch = 0;
    if (actor_lookup_local (node_, actor_id_, &ref, &spot_rid, &spot_generation,
                            &membership_epoch)
        == 0) {
        wire_submit_lookup_reply (node_, source_rid_, correlation_, ref, spot_rid,
                                  spot_generation, membership_epoch);
    } else {
        wire_submit_reply (node_, source_rid_, correlation_, ZLINK_REQUEST_NOT_FOUND, ENOENT,
                           NULL, 0);
    }
}

void handle_actor_destroy (mesh_node_t *node_,
                           const rid_bytes_t &source_rid_,
                           uint64_t correlation_,
                           const zlink_actor_ref_t &actor_)
{
    if (actor_destroy_local (node_, &actor_) == 0) {
        wire_submit_reply (node_, source_rid_, correlation_, ZLINK_REQUEST_OK, 0, NULL, 0);
    } else {
        const int reason = errno;
        wire_submit_reply (node_, source_rid_, correlation_,
                           reason == ENOENT ? ZLINK_REQUEST_NOT_FOUND : ZLINK_REQUEST_CONFLICT,
                           reason, NULL, 0);
    }
}

void handle_actor_join (mesh_node_t *node_,
                        const rid_bytes_t &source_rid_,
                        uint64_t correlation_,
                        const zlink_actor_ref_t &actor_,
                        bool entry_,
                        const rid_bytes_t &spot_rid_,
                        uint64_t spot_generation_,
                        std::vector<zlink_msg_t> *parts_)
{
    uint64_t origin_generation = 0;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED) {
            close_frames (parts_);
            return;
        }
        origin_generation = peer->lifecycle_generation;
    }
    if (actor_admit_remote_join (node_, source_rid_, origin_generation, correlation_, actor_,
                                 entry_, spot_rid_, spot_generation_, parts_)
        != 0) {
        const int reason = errno;
        close_frames (parts_);
        wire_submit_reply (node_, source_rid_, correlation_,
                           reason == ESTALE  ? ZLINK_REQUEST_CONFLICT
                           : reason == EAGAIN ? ZLINK_REQUEST_BACKPRESSURED
                                              : ZLINK_REQUEST_INTERNAL_ERROR,
                           reason, NULL, 0);
    }
}

//  The previous Spot's node observes the departure as a LEFT control record.
void handle_actor_left (mesh_node_t *node_,
                        const rid_bytes_t &source_rid_,
                        const zlink_actor_ref_t &actor_,
                        const rid_bytes_t &previous_spot_rid_,
                        uint64_t previous_spot_generation_,
                        uint64_t previous_membership_epoch_,
                        uint64_t current_membership_epoch_)
{
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED)
            return;
    }
    owner_id_t spot_owner_id;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        const std::string key (previous_spot_rid_.begin (), previous_spot_rid_.end ());
        std::map<std::string, spot_state_t>::iterator it = node_->spots.find (key);
        if (it == node_->spots.end () || it->second.generation != previous_spot_generation_)
            return;
        if (it->second.active_actor_count > 0)
            it->second.active_actor_count -= 1;
        spot_owner_id = spot_owner (previous_spot_rid_, previous_spot_generation_);
    }

    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ())
        return;
    record->kind = ZLINK_MESH_RECORD_SPOT_CONTROL;
    record->source_node_rid = source_rid_;
    zlink_actor_control_record_t data;
    memset (&data, 0, sizeof (data));
    data.struct_size = sizeof (data);
    data.version = 1;
    data.kind = ZLINK_ACTOR_LIFECYCLE_LEFT;
    data.previous_actor = actor_;
    data.current_actor = actor_;
    data.previous_spot_rid = rid_value (previous_spot_rid_);
    data.previous_spot_generation = previous_spot_generation_;
    data.previous_membership_epoch = previous_membership_epoch_;
    data.current_membership_epoch = current_membership_epoch_;
    data.result_code = ZLINK_REQUEST_OK;
    record->kind_data.assign (reinterpret_cast<unsigned char *> (&data),
                              reinterpret_cast<unsigned char *> (&data) + sizeof (data));
    (void) admit_record (node_, spot_owner_id, domain_infrastructure, record, false, 0);
}

void handle_reply (mesh_node_t *node_,
                   const rid_bytes_t &source_rid_,
                   uint64_t correlation_,
                   int32_t terminal_result_,
                   int32_t failure_errno_,
                   wire_reader_t &tail_,
                   std::vector<zlink_msg_t> *parts_)
{
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED)
            return;
    }
    pending_operation_t op;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator it =
          node_->operations.find (correlation_);
        if (it == node_->operations.end ())
            return; //  Already completed (timeout/shutdown): exactly-once.
        op = it->second;
        node_->operations.erase (it);
    }

    //  Operation-specific reply tails.
    if (op.kind == ZLINK_MESH_OPERATION_ACTOR_JOIN) {
        uint32_t join_result = ZLINK_ACTOR_JOIN_REJECTED;
        rid_bytes_t spot_rid;
        uint64_t spot_generation = 0;
        if (terminal_result_ == ZLINK_REQUEST_OK || terminal_result_ == ZLINK_REQUEST_REJECTED) {
            join_result = tail_.u32 ();
            spot_rid = read_rid (tail_);
            spot_generation = tail_.u64 ();
            if (tail_.failed) {
                complete_operation (node_, op, ZLINK_REQUEST_PROTOCOL_ERROR, EPROTO, NULL, NULL);
                return;
            }
            actor_apply_remote_join_reply (node_, op, join_result, source_rid_, spot_rid,
                                           spot_generation);
            return;
        }
        complete_operation (node_, op, terminal_result_, failure_errno_, NULL, NULL);
        return;
    }
    if (op.kind == ZLINK_MESH_OPERATION_ACTOR_LOOKUP && terminal_result_ == ZLINK_REQUEST_OK) {
        zlink_actor_location_t location;
        memset (&location, 0, sizeof (location));
        location.struct_size = sizeof (location);
        location.version = 1;
        const size_t id_len = tail_.u8 ();
        const std::string id = tail_.bytes (id_len);
        snprintf (location.actor.actor_id, sizeof (location.actor.actor_id), "%.*s",
                  (int) id.size (), id.data ());
        location.actor.generation = tail_.u64 ();
        location.actor.node_rid = rid_value (source_rid_);
        const rid_bytes_t spot_rid = read_rid (tail_);
        location.spot_rid = rid_value (spot_rid);
        location.spot_generation = tail_.u64 ();
        location.membership_epoch = tail_.u64 ();
        if (tail_.failed) {
            complete_operation (node_, op, ZLINK_REQUEST_PROTOCOL_ERROR, EPROTO, NULL, NULL);
            return;
        }
        std::vector<unsigned char> kind_data (
          reinterpret_cast<unsigned char *> (&location),
          reinterpret_cast<unsigned char *> (&location) + sizeof (location));
        complete_operation (node_, op, terminal_result_, failure_errno_, &kind_data, NULL);
        return;
    }

    complete_operation (node_, op, terminal_result_, failure_errno_,
                        NULL, parts_->empty () ? NULL : parts_);
}

//  --- ingress: message pump -----------------------------------------------------------

//  Receives every frame of one wire message. Returns false when no message
//  is available.
bool recv_wire_message (mesh_node_t *node_,
                        rid_bytes_t *source_out_,
                        std::vector<zlink_msg_t> *frames_out_)
{
    const zlink_routing_id_t *source_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t part;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    if (zlink_msg_init (&part) != 0)
        return false;
    if (zlink_router_recv_part (node_->router_socket, &source_rid, &request_seq, &part, &has_more,
                                ZLINK_RECV_FLAGS_DONTWAIT)
        != 0) {
        zlink_msg_close (&part);
        return false;
    }
    *source_out_ = source_rid && source_rid->size > 0 ? rid_bytes (*source_rid) : rid_bytes_t ();
    frames_out_->push_back (part);
    while (has_more == ZLINK_PART_MORE) {
        zlink_msg_t next;
        if (zlink_msg_init (&next) != 0)
            break;
        const zlink_routing_id_t *next_rid = NULL;
        uint64_t next_seq = 0;
        if (zlink_router_recv_part (node_->router_socket, &next_rid, &next_seq, &next, &has_more,
                                    ZLINK_RECV_FLAGS_NONE)
            != 0) {
            zlink_msg_close (&next);
            break;
        }
        frames_out_->push_back (next);
    }
    return true;
}

void close_frames (std::vector<zlink_msg_t> *frames_)
{
    for (size_t i = 0; i < frames_->size (); ++i)
        zlink_msg_close (&(*frames_)[i]);
    frames_->clear ();
}

void dispatch_wire_message (mesh_node_t *node_,
                            const rid_bytes_t &source_rid_,
                            std::vector<zlink_msg_t> *frames_)
{
    if (source_rid_.empty () || frames_->empty ()) {
        close_frames (frames_);
        return;
    }
    zlink_msg_t &head = (*frames_)[0];
    wire_reader_t reader (static_cast<const unsigned char *> (zlink_msg_data (&head)),
                          zlink_msg_size (&head));
    if (reader.u8 () != wire_magic_0 || reader.u8 () != wire_magic_1
        || reader.u8 () != wire_version) {
        close_frames (frames_);
        return;
    }
    const unsigned char type = reader.u8 ();
    const unsigned char flags = reader.u8 ();

    switch (type) {
        case wire_hello:
        case wire_admit:
        case wire_update: {
            wire_descriptor_t descriptor;
            if (decode_descriptor (reader, &descriptor)) {
                if (type == wire_update)
                    handle_update (node_, source_rid_, descriptor);
                else
                    handle_hello_or_admit (node_, source_rid_, descriptor, type == wire_hello);
            }
            close_frames (frames_);
            return;
        }
        case wire_reject: {
            const uint32_t reason = reader.u32 ();
            if (!reader.failed)
                handle_reject (node_, source_rid_, reason);
            close_frames (frames_);
            return;
        }
        default:
            break;
    }

    //  Data messages: correlation for request/reply, channel name for
    //  channel kinds, spot addressing for spot kinds, then optional metadata
    //  frame and payload parts.
    uint64_t correlation = 0;
    int32_t terminal_result = 0;
    int32_t failure_errno = 0;
    std::string channel;
    std::string topic;
    rid_bytes_t source_spot_rid;
    rid_bytes_t target_spot_rid;
    uint64_t target_spot_generation = 0;
    if (type == wire_node_request || type == wire_channel_request || type == wire_spot_request
        || type == wire_reply)
        correlation = reader.u64 ();
    if (type == wire_reply) {
        terminal_result = static_cast<int32_t> (reader.u32 ());
        failure_errno = static_cast<int32_t> (reader.u32 ());
    }
    if (type == wire_channel_send || type == wire_channel_request) {
        const size_t len = reader.u8 ();
        channel = reader.bytes (len);
    }
    if (type == wire_spot_send || type == wire_spot_request) {
        source_spot_rid = read_rid (reader);
        target_spot_rid = read_rid (reader);
        target_spot_generation = reader.u64 ();
    }
    if (type == wire_multicast) {
        const size_t channel_len = reader.u8 ();
        channel = reader.bytes (channel_len);
        const size_t topic_len = reader.u8 ();
        topic = reader.bytes (topic_len);
        source_spot_rid = read_rid (reader);
    }
    zlink_actor_ref_t source_actor;
    zlink_actor_ref_t target_actor;
    memset (&source_actor, 0, sizeof (source_actor));
    memset (&target_actor, 0, sizeof (target_actor));
    bool has_source_actor = false;
    bool join_entry = false;
    std::string actor_id;
    uint64_t left_prev_epoch = 0;
    uint64_t left_new_epoch = 0;
    if (type == wire_actor_send || type == wire_actor_request) {
        if (type == wire_actor_request)
            correlation = reader.u64 ();
        const size_t source_len = reader.u8 ();
        if (source_len > 0) {
            const std::string id = reader.bytes (source_len);
            snprintf (source_actor.actor_id, sizeof (source_actor.actor_id), "%.*s",
                      (int) id.size (), id.data ());
            source_actor.generation = reader.u64 ();
            source_actor.node_rid = rid_value (source_rid_);
            has_source_actor = true;
        }
        target_actor = read_actor_ref (reader, node_->routing_id);
    }
    if (type == wire_actor_lookup) {
        correlation = reader.u64 ();
        const size_t len = reader.u8 ();
        actor_id = reader.bytes (len);
    }
    if (type == wire_actor_destroy) {
        correlation = reader.u64 ();
        target_actor = read_actor_ref (reader, node_->routing_id);
    }
    if (type == wire_actor_join) {
        correlation = reader.u64 ();
        target_actor = read_actor_ref (reader, source_rid_);
        join_entry = reader.u8 () != 0;
        target_spot_rid = read_rid (reader);
        target_spot_generation = reader.u64 ();
    }
    if (type == wire_actor_left) {
        target_actor = read_actor_ref (reader, source_rid_);
        target_spot_rid = read_rid (reader);
        target_spot_generation = reader.u64 ();
        left_prev_epoch = reader.u64 ();
        left_new_epoch = reader.u64 ();
    }
    zlink_actor_transfer_id_t transfer_id;
    memset (&transfer_id, 0, sizeof (transfer_id));
    uint64_t transfer_sequence = 0;
    uint64_t transfer_participant_id = 0;
    uint64_t transfer_final_sequence = 0;
    uint8_t transfer_role = 0;
    uint64_t transfer_offered_messages = 0;
    uint64_t transfer_offered_bytes = 0;
    bool transfer_seal_response = false;
    std::vector<transfer_participant_descriptor_t> transfer_participants;
    std::vector<transfer_participant_terminal_t> transfer_terminals;
    uint64_t relay_serial = 0;
    std::unique_ptr<queued_record_t> transfer_record;
    if (type == wire_transfer_ready) {
        transfer_id = read_transfer_id (reader);
        target_actor = read_actor_ref (reader, source_rid_);
        left_prev_epoch = reader.u64 (); //  expected epoch slot
        transfer_final_sequence = reader.u64 ();
        transfer_role = reader.u8 ();
        transfer_offered_messages = reader.u64 ();
        transfer_offered_bytes = reader.u64 ();
        const uint32_t participant_count = reader.u32 ();
        transfer_participants.reserve (participant_count);
        for (uint32_t i = 0; i < participant_count; ++i) {
            transfer_participant_descriptor_t participant;
            participant.participant_id = reader.u64 ();
            participant.binding_generation = reader.u64 ();
            participant.allowance_messages = reader.u64 ();
            participant.allowance_bytes = reader.u64 ();
            transfer_participants.push_back (participant);
        }
    }
    if (type == wire_transfer_data) {
        transfer_id = read_transfer_id (reader);
        transfer_participant_id = reader.u64 ();
        transfer_sequence = reader.u64 ();
        transfer_record.reset (new (std::nothrow) queued_record_t ());
        if (!transfer_record.get () || !read_record_header (reader, transfer_record.get (),
                                                            &relay_serial)) {
            close_frames (frames_);
            return;
        }
    }
    if (type == wire_transfer_ack) {
        transfer_id = read_transfer_id (reader);
        transfer_participant_id = reader.u64 ();
        transfer_sequence = reader.u64 ();
    }
    if (type == wire_transfer_seal) {
        transfer_id = read_transfer_id (reader);
        transfer_seal_response = reader.u8 () != 0;
        const uint32_t terminal_count = reader.u32 ();
        transfer_terminals.reserve (terminal_count);
        for (uint32_t i = 0; i < terminal_count; ++i) {
            transfer_participant_terminal_t terminal;
            terminal.participant_id = reader.u64 ();
            terminal.high_water = reader.u64 ();
            transfer_terminals.push_back (terminal);
        }
    }
    if (type == wire_transfer_complete)
        transfer_id = read_transfer_id (reader);
    if (type == wire_reply_relay) {
        relay_serial = reader.u64 ();
        terminal_result = static_cast<int32_t> (reader.u32 ());
        failure_errno = static_cast<int32_t> (reader.u32 ());
    }
    if (reader.failed
        || (type != wire_node_send && type != wire_node_request && type != wire_channel_send
            && type != wire_channel_request && type != wire_reply && type != wire_spot_send
            && type != wire_spot_request && type != wire_multicast && type != wire_actor_send
            && type != wire_actor_request && type != wire_actor_lookup
            && type != wire_actor_destroy && type != wire_actor_join && type != wire_actor_left
            && type != wire_transfer_ready && type != wire_transfer_data
            && type != wire_transfer_ack && type != wire_reply_relay
            && type != wire_transfer_seal && type != wire_transfer_complete)) {
        close_frames (frames_);
        return;
    }

    //  The reply tail is parsed after the envelope frame is closed below, so
    //  snapshot the remaining bytes while the frame storage is still alive.
    std::vector<unsigned char> reply_tail;
    if (type == wire_reply && reader.pos < reader.size)
        reply_tail.assign (reader.data + reader.pos, reader.data + reader.size);

    size_t next_frame = 1;
    std::vector<unsigned char> metadata;
    if (flags & wire_flag_metadata) {
        if (frames_->size () < 2) {
            close_frames (frames_);
            return;
        }
        zlink_msg_t &meta_frame = (*frames_)[1];
        const unsigned char *data =
          static_cast<const unsigned char *> (zlink_msg_data (&meta_frame));
        const size_t size = zlink_msg_size (&meta_frame);
        //  Invalid ingress metadata rejects the complete message before any
        //  mailbox admission.
        if (validate_metadata (data, size) != 0) {
            close_frames (frames_);
            return;
        }
        metadata.assign (data, data + size);
        next_frame = 2;
    }

    std::vector<zlink_msg_t> parts;
    for (size_t i = next_frame; i < frames_->size (); ++i)
        parts.push_back ((*frames_)[i]);
    //  Envelope/metadata frames are closed here; payload part ownership
    //  moved into parts.
    for (size_t i = 0; i < next_frame; ++i)
        zlink_msg_close (&(*frames_)[i]);
    frames_->clear ();

    if (type == wire_reply) {
        wire_reader_t tail_reader (reply_tail.empty () ? NULL : reply_tail.data (),
                                   reply_tail.size ());
        handle_reply (node_, source_rid_, correlation, terminal_result, failure_errno,
                      tail_reader, &parts);
        for (size_t i = 0; i < parts.size (); ++i)
            zlink_msg_close (&parts[i]);
        return;
    }
    if (type == wire_actor_lookup) {
        close_frames (&parts);
        handle_actor_lookup (node_, source_rid_, correlation, actor_id);
        return;
    }
    if (type == wire_actor_destroy) {
        close_frames (&parts);
        handle_actor_destroy (node_, source_rid_, correlation, target_actor);
        return;
    }
    if (type == wire_actor_left) {
        close_frames (&parts);
        handle_actor_left (node_, source_rid_, target_actor, target_spot_rid,
                           target_spot_generation, left_prev_epoch, left_new_epoch);
        return;
    }
    if (type == wire_actor_join) {
        //  creation parts are optional for joins.
        handle_actor_join (node_, source_rid_, correlation, target_actor, join_entry,
                           target_spot_rid, target_spot_generation, &parts);
        return;
    }
    if (type == wire_transfer_ready) {
        close_frames (&parts);
        transfer_handle_ready (node_, source_rid_, transfer_id, target_actor, left_prev_epoch,
                               transfer_final_sequence, transfer_role,
                               transfer_offered_messages, transfer_offered_bytes,
                               transfer_participants);
        return;
    }
    if (type == wire_transfer_data) {
        transfer_record->parts = std::move (parts);
        for (size_t i = 0; i < transfer_record->parts.size (); ++i)
            transfer_record->byte_size += zlink_msg_size (&transfer_record->parts[i]);
        transfer_handle_data (node_, source_rid_, transfer_id, transfer_participant_id,
                              transfer_sequence, relay_serial, &transfer_record);
        return;
    }
    if (type == wire_transfer_ack) {
        close_frames (&parts);
        transfer_handle_ack (node_, source_rid_, transfer_id, transfer_participant_id,
                             transfer_sequence);
        return;
    }
    if (type == wire_transfer_seal) {
        close_frames (&parts);
        transfer_handle_seal (node_, source_rid_, transfer_id, transfer_seal_response,
                              transfer_terminals);
        return;
    }
    if (type == wire_transfer_complete) {
        close_frames (&parts);
        transfer_handle_complete (node_, source_rid_, transfer_id);
        return;
    }
    if (type == wire_reply_relay) {
        transfer_handle_reply_relay (node_, source_rid_, relay_serial, terminal_result,
                                     failure_errno, &parts);
        for (size_t i = 0; i < parts.size (); ++i)
            zlink_msg_close (&parts[i]);
        return;
    }
    if (parts.empty ()) {
        //  Data messages carry at least one payload part.
        return;
    }
    if (type == wire_actor_send || type == wire_actor_request) {
        handle_actor_data (node_, source_rid_, type == wire_actor_request, correlation,
                           source_actor, has_source_actor, target_actor, &metadata, &parts);
        return;
    }
    if (type == wire_spot_send || type == wire_spot_request) {
        handle_spot_data (node_, source_rid_, type == wire_spot_request, correlation,
                          source_spot_rid, target_spot_rid, target_spot_generation, &metadata,
                          &parts);
        return;
    }
    if (type == wire_multicast) {
        handle_multicast (node_, source_rid_, channel, topic, source_spot_rid, &metadata, &parts);
        return;
    }
    handle_data (node_, source_rid_, type, correlation, channel, &metadata, &parts);
}

void drain_monitor (mesh_node_t *node_)
{
    if (!node_->router_monitor)
        return;
    for (;;) {
        zlink_socket_monitor_event_t event;
        memset (&event, 0, sizeof (event));
        if (zlink_socket_monitor_recv (node_->router_monitor, &event, ZLINK_RECV_FLAGS_DONTWAIT)
            != ZLINK_RECV_OK)
            return;
        if (event.event == ZLINK_EVENT_CONNECTION_READY) {
            //  Outbound connects match a connecting intent by endpoint; the
            //  handshake starts with our HELLO to the revealed peer rid.
            bool ours = false;
            {
                std::lock_guard<std::mutex> lock (node_->mutex);
                peer_state_t *intent =
                  find_intent_by_endpoint_locked (node_, event.remote_addr);
                if (intent && event.routing_id.size > 0) {
                    intent->rid = rid_bytes (event.routing_id);
                    if (intent->state == ZLINK_MESH_PEER_ERROR) {
                        intent->state = ZLINK_MESH_PEER_CONNECTING;
                        intent->last_error = 0;
                        intent->last_changed_ms = now_ms ();
                        recompute_readiness_locked (node_);
                    }
                    ours = true;
                }
            }
            if (ours) {
                std::vector<unsigned char> frame = make_envelope (wire_hello, 0);
                {
                    std::lock_guard<std::mutex> lock (node_->mutex);
                    encode_descriptor_locked (node_, frame);
                }
                send_control (node_, event.routing_id, frame);
            }
        } else if (event.event == ZLINK_EVENT_DISCONNECTED) {
            if (event.routing_id.size > 0)
                handle_peer_down (node_, rid_bytes (event.routing_id));
        }
    }
}

void run_ingress_loop (mesh_node_t *node_)
{
    while (!node_->io_stop.load (std::memory_order_acquire)) {
        drain_monitor (node_);

        zlink_pollitem_t item;
        memset (&item, 0, sizeof (item));
        item.socket = node_->router_socket;
        item.events = ZLINK_POLLIN;
        const int rc = zlink_poll (&item, 1, ingress_poll_ms, NULL);
        if (rc < 0) {
            //  Timeout surfaces as EAGAIN from the poller; both it and EINTR
            //  just re-arm the loop.
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return;
        }
        if (rc == 0)
            continue;
        for (;;) {
            rid_bytes_t source;
            std::vector<zlink_msg_t> frames;
            if (!recv_wire_message (node_, &source, &frames))
                break;
            dispatch_wire_message (node_, source, &frames);
        }
    }
}
} // namespace

//  --- lifecycle ---------------------------------------------------------------------

int wire_start (mesh_node_t *node_)
{
    void *router = zlink_socket (node_->ctx, ZLINK_SOCKET_ROUTER);
    if (!router)
        return -1;

    zlink_routing_id_t rid = rid_value (node_->routing_id);
    if (zlink_set_routing_id (router, reinterpret_cast<const char *> (rid.data), rid.size)
        != ZLINK_CONFIG_OK) {
        zlink_close (router);
        return -1;
    }
    if (node_->router_hwm_override > 0) {
        const int hwm = node_->router_hwm_override;
        (void) zlink_set_option (router, ZLINK_OPT_SNDHWM, &hwm, sizeof (hwm));
        (void) zlink_set_option (router, ZLINK_OPT_RCVHWM, &hwm, sizeof (hwm));
    }
    if (node_->sndtimeo_ms >= 0) {
        (void) zlink_set_option (router, ZLINK_OPT_SNDTIMEO, &node_->sndtimeo_ms,
                                 sizeof (node_->sndtimeo_ms));
    }
    if (zlink_bind (router, node_->bind_endpoint.c_str ()) != ZLINK_BIND_OK) {
        const int saved_errno = errno;
        zlink_close (router);
        errno = saved_errno;
        return -1;
    }
    char resolved[512] = "";
    size_t resolved_size = sizeof (resolved);
    if (zlink_get_option (router, ZLINK_OPT_LAST_ENDPOINT, resolved, &resolved_size)
          == ZLINK_CONFIG_OK
        && resolved[0] != '\0')
        node_->bind_endpoint = resolved;

    zlink_socket_monitor_open_options_t monitor_options;
    memset (&monitor_options, 0, sizeof (monitor_options));
    monitor_options.events =
      ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED;
    node_->router_monitor = zlink_socket_monitor_open (router, &monitor_options);

    node_->router_socket = router;
    node_->io_stop.store (false, std::memory_order_release);
    try {
        node_->io_thread = std::thread (run_ingress_loop, node_);
    } catch (...) {
        if (node_->router_monitor)
            zlink_monitor_close (&node_->router_monitor);
        zlink_close (router);
        node_->router_socket = NULL;
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void wire_stop (mesh_node_t *node_)
{
    node_->io_stop.store (true, std::memory_order_release);
    if (node_->io_thread.joinable ())
        node_->io_thread.join ();
    if (node_->router_monitor)
        zlink_monitor_close (&node_->router_monitor);
    if (node_->router_socket) {
        zlink_close (node_->router_socket);
        node_->router_socket = NULL;
    }
}

int wire_connect_endpoint (mesh_node_t *node_, const std::string &endpoint_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return -1;
    }
    if (zlink_connect (node_->router_socket, endpoint_.c_str ()) != ZLINK_CONNECT_OK)
        return -1;
    return 0;
}

void wire_broadcast_update_locked (mesh_node_t *node_)
{
    if (!node_->router_socket)
        return;
    std::vector<unsigned char> frame = make_envelope (wire_update, 0);
    encode_descriptor_locked (node_, frame);
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        if (node_->peers[i].state != ZLINK_MESH_PEER_ADMITTED)
            continue;
        const zlink_routing_id_t target = rid_value (node_->peers[i].rid);
        send_control (node_, target, frame);
    }
}

zlink_submit_result_t wire_submit_data (mesh_node_t *node_,
                                        const rid_bytes_t &peer_rid_,
                                        wire_type_t type_,
                                        uint64_t correlation_,
                                        const std::string &channel_,
                                        const zlink_mesh_metadata_view_t *metadata_,
                                        const zlink_msg_t *parts_,
                                        size_t part_count_,
                                        zlink_send_flags_t flags_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope =
      make_envelope (static_cast<unsigned char> (type_), metadata_ ? wire_flag_metadata : 0);
    if (type_ == wire_node_request || type_ == wire_channel_request)
        put_u64 (envelope, correlation_);
    if (type_ == wire_channel_send || type_ == wire_channel_request) {
        put_u8 (envelope, static_cast<unsigned char> (channel_.size ()));
        put_bytes (envelope, channel_.data (), channel_.size ());
    }
    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);

    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, metadata_ ? &metadata : NULL, parts_,
                              part_count_, flags_);
}

zlink_submit_result_t wire_submit_spot (mesh_node_t *node_,
                                        const rid_bytes_t &peer_rid_,
                                        bool is_request_,
                                        uint64_t correlation_,
                                        const rid_bytes_t &source_spot_rid_,
                                        const rid_bytes_t &target_spot_rid_,
                                        uint64_t target_spot_generation_,
                                        const zlink_mesh_metadata_view_t *metadata_,
                                        const zlink_msg_t *parts_,
                                        size_t part_count_,
                                        zlink_send_flags_t flags_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope =
      make_envelope (static_cast<unsigned char> (is_request_ ? wire_spot_request : wire_spot_send),
                     metadata_ ? wire_flag_metadata : 0);
    if (is_request_)
        put_u64 (envelope, correlation_);
    put_rid (envelope, source_spot_rid_);
    put_rid (envelope, target_spot_rid_);
    put_u64 (envelope, target_spot_generation_);
    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);

    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, metadata_ ? &metadata : NULL, parts_,
                              part_count_, flags_);
}

zlink_submit_result_t wire_publish_remote_locked (mesh_node_t *node_,
                                                  const std::vector<rid_bytes_t> &targets_,
                                                  const std::string &channel_,
                                                  const std::string &topic_,
                                                  const rid_bytes_t &source_spot_rid_,
                                                  int nodrop_,
                                                  const zlink_mesh_metadata_view_t *metadata_,
                                                  const zlink_msg_t *parts_,
                                                  size_t part_count_,
                                                  uint32_t *admitted_out_,
                                                  uint32_t *dropped_out_)
{
    *admitted_out_ = 0;
    *dropped_out_ = 0;
    if (targets_.empty ())
        return ZLINK_SUBMIT_OK;
    if (!node_->router_socket) {
        *dropped_out_ = static_cast<uint32_t> (targets_.size ());
        if (nodrop_) {
            errno = EAGAIN;
            return ZLINK_SUBMIT_BACKPRESSURED;
        }
        return ZLINK_SUBMIT_OK;
    }

    std::vector<unsigned char> envelope =
      make_envelope (wire_multicast, metadata_ ? wire_flag_metadata : 0);
    put_u8 (envelope, static_cast<unsigned char> (channel_.size ()));
    put_bytes (envelope, channel_.data (), channel_.size ());
    put_u8 (envelope, static_cast<unsigned char> (topic_.size ()));
    put_bytes (envelope, topic_.data (), topic_.size ());
    put_rid (envelope, source_spot_rid_);
    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);

    //  All outbound wire traffic serializes on wire_send_mutex, so this
    //  commit is atomic against other submits from this node. NODROP uses
    //  full (SNDTIMEO-bounded) sends per admitted pipe; a mid-set failure is
    //  reported as backpressure without retrying already-delivered targets.
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    for (size_t i = 0; i < targets_.size (); ++i) {
        const zlink_routing_id_t target = rid_value (targets_[i]);
        const zlink_submit_result_t rc = send_data_message_unlocked (
          node_, target, envelope, metadata_ ? &metadata : NULL, parts_, part_count_,
          nodrop_ ? ZLINK_SEND_FLAGS_NONE : ZLINK_SEND_FLAGS_DONTWAIT);
        if (rc == ZLINK_SUBMIT_OK)
            *admitted_out_ += 1;
        else
            *dropped_out_ += 1;
    }
    if (nodrop_ && *dropped_out_ > 0) {
        errno = EAGAIN;
        return ZLINK_SUBMIT_BACKPRESSURED;
    }
    return ZLINK_SUBMIT_OK;
}

zlink_submit_result_t wire_submit_actor_data (mesh_node_t *node_,
                                              const rid_bytes_t &peer_rid_,
                                              bool is_request_,
                                              uint64_t correlation_,
                                              const zlink_actor_ref_t *source_actor_,
                                              const zlink_actor_ref_t &target_actor_,
                                              const zlink_mesh_metadata_view_t *metadata_,
                                              const zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_send_flags_t flags_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (
      static_cast<unsigned char> (is_request_ ? wire_actor_request : wire_actor_send),
      metadata_ ? wire_flag_metadata : 0);
    if (is_request_)
        put_u64 (envelope, correlation_);
    if (source_actor_) {
        put_actor_ref (envelope, *source_actor_);
    } else {
        put_u8 (envelope, 0);
    }
    put_actor_ref (envelope, target_actor_);
    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);

    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, metadata_ ? &metadata : NULL, parts_,
                              part_count_, flags_);
}

zlink_submit_result_t wire_submit_actor_lookup (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                uint64_t correlation_,
                                                const std::string &actor_id_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_actor_lookup, 0);
    put_u64 (envelope, correlation_);
    put_u8 (envelope, static_cast<unsigned char> (actor_id_.size ()));
    put_bytes (envelope, actor_id_.data (), actor_id_.size ());
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_actor_destroy (mesh_node_t *node_,
                                                 const rid_bytes_t &peer_rid_,
                                                 uint64_t correlation_,
                                                 const zlink_actor_ref_t &actor_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_actor_destroy, 0);
    put_u64 (envelope, correlation_);
    put_actor_ref (envelope, actor_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_actor_join (mesh_node_t *node_,
                                              const rid_bytes_t &peer_rid_,
                                              uint64_t correlation_,
                                              const zlink_actor_ref_t &actor_,
                                              bool entry_,
                                              const rid_bytes_t &target_spot_rid_,
                                              uint64_t target_spot_generation_,
                                              const zlink_msg_t *creation_parts_,
                                              size_t creation_part_count_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_actor_join, 0);
    put_u64 (envelope, correlation_);
    put_actor_ref (envelope, actor_);
    put_u8 (envelope, entry_ ? 1 : 0);
    put_rid (envelope, target_spot_rid_);
    put_u64 (envelope, target_spot_generation_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, creation_parts_,
                              creation_part_count_, ZLINK_SEND_FLAGS_NONE);
}

void wire_notify_actor_left (mesh_node_t *node_,
                             const rid_bytes_t &peer_rid_,
                             const zlink_actor_ref_t &actor_,
                             const rid_bytes_t &previous_spot_rid_,
                             uint64_t previous_spot_generation_,
                             uint64_t previous_membership_epoch_,
                             uint64_t current_membership_epoch_)
{
    if (!node_->router_socket)
        return;
    std::vector<unsigned char> envelope = make_envelope (wire_actor_left, 0);
    put_actor_ref (envelope, actor_);
    put_rid (envelope, previous_spot_rid_);
    put_u64 (envelope, previous_spot_generation_);
    put_u64 (envelope, previous_membership_epoch_);
    put_u64 (envelope, current_membership_epoch_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    send_control (node_, target, envelope);
}

zlink_submit_result_t wire_submit_join_reply (mesh_node_t *node_,
                                              const rid_bytes_t &peer_rid_,
                                              uint64_t correlation_,
                                              uint32_t join_result_,
                                              const rid_bytes_t &spot_rid_,
                                              uint64_t spot_generation_,
                                              const zlink_msg_t *parts_,
                                              size_t part_count_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_reply, 0);
    put_u64 (envelope, correlation_);
    put_u32 (envelope, static_cast<uint32_t> (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED
                                                ? ZLINK_REQUEST_OK
                                                : ZLINK_REQUEST_REJECTED));
    put_u32 (envelope, join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED ? 0
                                                                 : static_cast<uint32_t> (EACCES));
    put_u32 (envelope, join_result_);
    put_rid (envelope, spot_rid_);
    put_u64 (envelope, spot_generation_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, parts_, part_count_,
                              ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_lookup_reply (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                uint64_t correlation_,
                                                const zlink_actor_ref_t &ref_,
                                                const rid_bytes_t &spot_rid_,
                                                uint64_t spot_generation_,
                                                uint64_t membership_epoch_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_reply, 0);
    put_u64 (envelope, correlation_);
    put_u32 (envelope, static_cast<uint32_t> (ZLINK_REQUEST_OK));
    put_u32 (envelope, 0);
    put_actor_ref (envelope, ref_);
    put_rid (envelope, spot_rid_);
    put_u64 (envelope, spot_generation_);
    put_u64 (envelope, membership_epoch_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_ready (mesh_node_t *node_,
                                                  const rid_bytes_t &peer_rid_,
                                                  const zlink_actor_transfer_id_t &transfer_id_,
                                                  const zlink_actor_ref_t &actor_,
                                                  uint64_t expected_epoch_,
                                                  uint64_t final_sequence_,
                                                  uint8_t role_,
                                                  uint64_t offered_messages_,
                                                  uint64_t offered_bytes_,
                                                  const std::vector<transfer_participant_descriptor_t>
                                                    &participants_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_ready, 0);
    put_transfer_id (envelope, transfer_id_);
    put_actor_ref (envelope, actor_);
    put_u64 (envelope, expected_epoch_);
    put_u64 (envelope, final_sequence_);
    put_u8 (envelope, role_);
    put_u64 (envelope, offered_messages_);
    put_u64 (envelope, offered_bytes_);
    put_u32 (envelope, static_cast<uint32_t> (participants_.size ()));
    for (size_t i = 0; i < participants_.size (); ++i) {
        put_u64 (envelope, participants_[i].participant_id);
        put_u64 (envelope, participants_[i].binding_generation);
        put_u64 (envelope, participants_[i].allowance_messages);
        put_u64 (envelope, participants_[i].allowance_bytes);
    }
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_data (mesh_node_t *node_,
                                                 const rid_bytes_t &peer_rid_,
                                                 const zlink_actor_transfer_id_t &transfer_id_,
                                                 uint64_t participant_id_,
                                                 uint64_t sequence_,
                                                 const queued_record_t &record_,
                                                 uint64_t relay_serial_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_data, 0);
    put_transfer_id (envelope, transfer_id_);
    put_u64 (envelope, participant_id_);
    put_u64 (envelope, sequence_);
    put_record_header (envelope, record_, relay_serial_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL,
                              record_.parts.empty () ? NULL : &record_.parts[0],
                              record_.parts.size (), ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_ack (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                const zlink_actor_transfer_id_t &transfer_id_,
                                                uint64_t participant_id_,
                                                uint64_t high_water_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_ack, 0);
    put_transfer_id (envelope, transfer_id_);
    put_u64 (envelope, participant_id_);
    put_u64 (envelope, high_water_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_seal (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  const zlink_actor_transfer_id_t &transfer_id_,
  bool response_,
  const std::vector<transfer_participant_terminal_t> &terminals_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_seal, 0);
    put_transfer_id (envelope, transfer_id_);
    put_u8 (envelope, response_ ? 1 : 0);
    put_u32 (envelope, static_cast<uint32_t> (terminals_.size ()));
    for (size_t i = 0; i < terminals_.size (); ++i) {
        put_u64 (envelope, terminals_[i].participant_id);
        put_u64 (envelope, terminals_[i].high_water);
    }
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_complete (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  const zlink_actor_transfer_id_t &transfer_id_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_complete, 0);
    put_transfer_id (envelope, transfer_id_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_reply_relay (mesh_node_t *node_,
                                               const rid_bytes_t &peer_rid_,
                                               uint64_t relay_serial_,
                                               int32_t terminal_result_,
                                               int32_t failure_errno_,
                                               const zlink_msg_t *parts_,
                                               size_t part_count_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_reply_relay, 0);
    put_u64 (envelope, relay_serial_);
    put_u32 (envelope, static_cast<uint32_t> (terminal_result_));
    put_u32 (envelope, static_cast<uint32_t> (failure_errno_));
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, parts_, part_count_,
                              ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_reply (mesh_node_t *node_,
                                         const rid_bytes_t &peer_rid_,
                                         uint64_t correlation_,
                                         int32_t terminal_result_,
                                         int32_t failure_errno_,
                                         const zlink_msg_t *parts_,
                                         size_t part_count_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_reply, 0);
    put_u64 (envelope, correlation_);
    put_u32 (envelope, static_cast<uint32_t> (terminal_result_));
    put_u32 (envelope, static_cast<uint32_t> (failure_errno_));

    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, parts_, part_count_,
                              ZLINK_SEND_FLAGS_NONE);
}
}
}
