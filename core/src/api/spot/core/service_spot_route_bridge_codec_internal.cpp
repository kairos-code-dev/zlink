/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/spot/core/service_spot_route_bridge_codec_internal.hpp"

#include <string.h>

namespace
{
const char spot_route_bridge_send_packet_name[] = "__zlink.routed_spot.egress.send";
const char spot_route_bridge_request_packet_name[] = "__zlink.routed_spot.egress.request";

bool has_valid_routing_id (const zlink_routing_id_t *rid_)
{
    return rid_ && rid_->size > 0 && rid_->size <= sizeof (rid_->data);
}

bool init_msg_from_buffer (zlink_msg_t *part_, const void *data_, size_t size_)
{
    if (!part_) {
        errno = EFAULT;
        return false;
    }
    zlink_msg_init (part_);
    if (zlink_msg_init_size (part_, size_) != 0) {
        zlink_msg_close (part_);
        return false;
    }
    if (size_ > 0)
        memcpy (zlink_msg_data (part_), data_, size_);
    return true;
}

bool init_msg_from_text (zlink_msg_t *part_, const char *text_)
{
    return init_msg_from_buffer (part_, text_, strlen (text_));
}

bool msg_equals_text (zlink_msg_t *part_, const char *text_)
{
    if (!part_ || !text_)
        return false;
    const size_t expected_size = strlen (text_);
    return zlink_msg_size (part_) == expected_size
           && memcmp (zlink_msg_data (part_), text_, expected_size) == 0;
}

bool build_relay_parts (const char *packet_name_,
                        const zlink_routing_id_t *target_spot_rid_,
                        zlink_msg_t *parts_,
                        size_t part_count_,
                        std::vector<zlink_msg_t> *out_)
{
    if (!packet_name_ || !has_valid_routing_id (target_spot_rid_) || !out_
        || (!parts_ && part_count_ != 0)) {
        errno = EINVAL;
        return false;
    }

    out_->clear ();
    out_->resize (part_count_ + 2);
    size_t initialized = 0;
    if (!init_msg_from_text (&(*out_)[initialized++], packet_name_))
        goto fail;
    if (!init_msg_from_buffer (&(*out_)[initialized++], target_spot_rid_->data,
                               target_spot_rid_->size))
        goto fail;
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_init (&(*out_)[initialized]);
        if (zlink_msg_move (&(*out_)[initialized], &parts_[i]) != 0) {
            zlink_msg_close (&(*out_)[initialized]);
            goto fail;
        }
        ++initialized;
    }
    return true;

fail:
    for (size_t i = 0; i < initialized; ++i)
        zlink_msg_close (&(*out_)[i]);
    out_->clear ();
    return false;
}

bool decode_target_spot_rid (zlink_msg_t *part_, zlink_routing_id_t *out_)
{
    if (!part_ || !out_) {
        errno = EFAULT;
        return false;
    }
    const size_t size = zlink_msg_size (part_);
    if (size == 0 || size > sizeof (out_->data)) {
        errno = EPROTO;
        return false;
    }
    memset (out_, 0, sizeof (*out_));
    out_->size = static_cast<uint8_t> (size);
    memcpy (out_->data, zlink_msg_data (part_), size);
    return true;
}
}

namespace zlink
{
namespace spot_route_bridge_codec_internal
{

namespace
{
packet_kind_t decode_packet_kind (zlink_msg_t *part_)
{
    if (msg_equals_text (part_, spot_route_bridge_send_packet_name))
        return packet_kind_send;
    if (msg_equals_text (part_, spot_route_bridge_request_packet_name))
        return packet_kind_request;
    return packet_kind_none;
}
}

packet_kind_t decode_relay_kind (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_ || part_count_ == 0)
        return packet_kind_none;
    return decode_packet_kind (&parts_[0]);
}

bool decode_relay_parts (zlink_msg_t *parts_, size_t part_count_, decoded_relay_t *out_)
{
    if (!parts_ || !out_) {
        errno = EFAULT;
        return false;
    }

    memset (out_, 0, sizeof (*out_));
    out_->kind = decode_relay_kind (parts_, part_count_);
    if (out_->kind == packet_kind_none) {
        errno = EPROTO;
        return false;
    }
    if (part_count_ < 3) {
        errno = EPROTO;
        return false;
    }
    if (!decode_target_spot_rid (&parts_[1], &out_->target_spot_rid))
        return false;

    out_->payload_parts = parts_ + 2;
    out_->payload_part_count = part_count_ - 2;
    out_->envelope_parts = parts_;
    out_->envelope_part_count = part_count_;
    return true;
}

bool build_send_relay_parts (const zlink_routing_id_t *target_spot_rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             std::vector<zlink_msg_t> *out_)
{
    return build_relay_parts (spot_route_bridge_send_packet_name, target_spot_rid_, parts_,
                              part_count_, out_);
}

bool build_request_relay_parts (const zlink_routing_id_t *target_spot_rid_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                std::vector<zlink_msg_t> *out_)
{
    return build_relay_parts (spot_route_bridge_request_packet_name, target_spot_rid_, parts_,
                              part_count_, out_);
}

void close_decoded_relay_envelope (decoded_relay_t *relay_)
{
    if (!relay_ || !relay_->envelope_parts || relay_->envelope_part_count < 2)
        return;
    zlink_msg_close (&relay_->envelope_parts[0]);
    zlink_msg_close (&relay_->envelope_parts[1]);
    relay_->envelope_parts = NULL;
    relay_->envelope_part_count = 0;
    relay_->payload_parts = NULL;
    relay_->payload_part_count = 0;
}

void close_decoded_relay_parts (decoded_relay_t *relay_)
{
    if (!relay_ || !relay_->envelope_parts)
        return;
    for (size_t i = 0; i < relay_->envelope_part_count; ++i)
        zlink_msg_close (&relay_->envelope_parts[i]);
    relay_->envelope_parts = NULL;
    relay_->envelope_part_count = 0;
    relay_->payload_parts = NULL;
    relay_->payload_part_count = 0;
}

void close_relay_parts (std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        zlink_msg_close (&(*parts_)[i]);
    parts_->clear ();
}

}
}
