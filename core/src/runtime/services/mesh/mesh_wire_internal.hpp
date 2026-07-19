/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_RUNTIME_SERVICES_MESH_WIRE_INTERNAL_HPP_INCLUDED
#define ZLINK_RUNTIME_SERVICES_MESH_WIRE_INTERNAL_HPP_INCLUDED

#include "services/mesh/mesh_wire.hpp"

#include <string>
#include <utility>
#include <vector>

//  Shared declarations of the four wire modules. Each module owns one
//  decision:
//    mesh_wire_codec.cpp     — the envelope/record wire format
//    mesh_wire_admission.cpp — the peer admission state machine
//    mesh_wire_ingress.cpp   — routing received frames into the services
//    mesh_wire.cpp           — transport lifecycle and outbound submits
//  Nothing in this header is visible outside the mesh runtime.
namespace zlink
{
namespace mesh
{
//  --- wire format constants -------------------------------------------------

const unsigned char wire_magic_0 = 'Z';
const unsigned char wire_magic_1 = 'M';
const unsigned char wire_version = 1;
const unsigned char wire_flag_metadata = 0x01;
const int ingress_poll_ms = 20;

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
    //  The bind endpoint this node advertises; lets the receiving side merge
    //  an inbound-observed peer with a manual intent for the same endpoint.
    std::string advertised_endpoint;
    std::vector<std::pair<std::string, uint32_t> > channels;
};

//  --- codec (mesh_wire_codec.cpp) -------------------------------------------

void put_u8 (std::vector<unsigned char> &out_, unsigned char value_);
void put_u16 (std::vector<unsigned char> &out_, uint16_t value_);
void put_u32 (std::vector<unsigned char> &out_, uint32_t value_);
void put_u64 (std::vector<unsigned char> &out_, uint64_t value_);
void put_bytes (std::vector<unsigned char> &out_, const void *data_, size_t size_);
void encode_descriptor_locked (mesh_node_t *node_, std::vector<unsigned char> &out_);
bool decode_descriptor (wire_reader_t &reader_, wire_descriptor_t *out_);
void put_rid (std::vector<unsigned char> &out_, const rid_bytes_t &rid_);
rid_bytes_t read_rid (wire_reader_t &reader_);
void put_actor_ref (std::vector<unsigned char> &out_, const zlink_actor_ref_t &ref_);
zlink_actor_ref_t read_actor_ref (wire_reader_t &reader_, const rid_bytes_t &node_rid_);
void put_blob16 (std::vector<unsigned char> &out_, const std::vector<unsigned char> &blob_);
std::vector<unsigned char> read_blob16 (wire_reader_t &reader_);
void put_record_header (std::vector<unsigned char> &out_,
                        const queued_record_t &record_,
                        uint64_t relay_serial_);
bool read_record_header (wire_reader_t &reader_,
                         queued_record_t *record_,
                         uint64_t *relay_serial_out_);
void put_transfer_id (std::vector<unsigned char> &out_, const zlink_actor_transfer_id_t &id_);
zlink_actor_transfer_id_t read_transfer_id (wire_reader_t &reader_);
std::vector<unsigned char> make_envelope (unsigned char type_, unsigned char flags_);

//  --- transport primitives (mesh_wire.cpp) ----------------------------------

int send_frame (mesh_node_t *node_,
                const zlink_routing_id_t &target_,
                const std::vector<unsigned char> &frame_,
                zlink_part_flag_t part_flag_,
                zlink_send_flags_t flags_);
int send_control (mesh_node_t *node_,
                  const zlink_routing_id_t &target_,
                  const std::vector<unsigned char> &frame_);
zlink_submit_result_t send_data_message (mesh_node_t *node_,
                                         const zlink_routing_id_t &target_,
                                         const std::vector<unsigned char> &envelope_,
                                         const std::vector<unsigned char> *metadata_,
                                         const zlink_msg_t *parts_,
                                         size_t part_count_,
                                         zlink_send_flags_t flags_);
zlink_submit_result_t send_data_message_unlocked (mesh_node_t *node_,
                                                  const zlink_routing_id_t &target_,
                                                  const std::vector<unsigned char> &envelope_,
                                                  const std::vector<unsigned char> *metadata_,
                                                  const zlink_msg_t *parts_,
                                                  size_t part_count_,
                                                  zlink_send_flags_t flags_);

//  --- peer admission state machine (mesh_wire_admission.cpp) ----------------

peer_state_t *find_peer_by_rid_locked (mesh_node_t *node_, const rid_bytes_t &rid_);
peer_state_t *find_intent_by_endpoint_locked (mesh_node_t *node_, const std::string &endpoint_);
bool apply_transport_ready_locked (peer_state_t *peer_, uint64_t connection_id_);
void emit_peer_event (mesh_node_t *node_,
                      zlink_mesh_monitor_event_kind_t kind_,
                      const rid_bytes_t &rid_,
                      int32_t error_);
void handle_hello_or_admit (mesh_node_t *node_,
                            const rid_bytes_t &source_rid_,
                            const wire_descriptor_t &descriptor_,
                            bool is_hello_);
void handle_reject (mesh_node_t *node_, const rid_bytes_t &source_rid_, uint32_t reason_);
void handle_update (mesh_node_t *node_,
                    const rid_bytes_t &source_rid_,
                    const wire_descriptor_t &descriptor_);
void handle_peer_down (mesh_node_t *node_,
                       const rid_bytes_t &rid_,
                       uint64_t connection_id_);

//  --- service ingress router (mesh_wire_ingress.cpp) ------------------------

bool recv_wire_message (mesh_node_t *node_,
                        rid_bytes_t *source_out_,
                        std::vector<zlink_msg_t> *frames_out_);
void close_frames (std::vector<zlink_msg_t> *frames_);
void dispatch_wire_message (mesh_node_t *node_,
                            const rid_bytes_t &source_rid_,
                            std::vector<zlink_msg_t> *frames_);
void drain_monitor (mesh_node_t *node_);
void run_ingress_loop (mesh_node_t *node_);
}
}

#endif
