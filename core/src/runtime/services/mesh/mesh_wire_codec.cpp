/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "services/mesh/mesh_wire_internal.hpp"
#include "api/mesh/mesh_c_internal.hpp"
#include "api/socket/socket_api_internal.hpp"
#include "utils/macros.hpp"

//  Wire format codec: envelope primitives, descriptor and record codecs.
//  This module owns the byte layout decision of the mesh wire protocol.
namespace zlink
{
namespace mesh
{
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

void encode_descriptor_locked (mesh_node_t *node_, std::vector<unsigned char> &out_)
{
    put_u8 (out_, static_cast<unsigned char> (node_->mesh_name.size ()));
    put_bytes (out_, node_->mesh_name.data (), node_->mesh_name.size ());
    put_u8 (out_, static_cast<unsigned char> (node_->trust_profile.size ()));
    put_bytes (out_, node_->trust_profile.data (), node_->trust_profile.size ());
    put_u64 (out_, node_->lifecycle_generation);
    put_u64 (out_, node_->descriptor_revision);
    put_u16 (out_, static_cast<uint16_t> (node_->bind_endpoint.size ()));
    put_bytes (out_, node_->bind_endpoint.data (), node_->bind_endpoint.size ());
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
    const size_t endpoint_len = reader_.u16 ();
    out_->advertised_endpoint = reader_.bytes (endpoint_len);
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
}
}
