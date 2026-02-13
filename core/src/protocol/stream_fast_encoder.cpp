/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "protocol/stream_fast_encoder.hpp"
#include "core/msg.hpp"
#include "protocol/wire.hpp"
#include "protocol/stream_fast_protocol.hpp"

zlink::stream_fast_encoder_t::stream_fast_encoder_t (size_t bufsize_) :
    encoder_base_t<stream_fast_encoder_t> (bufsize_)
{
    next_step (NULL, 0, &stream_fast_encoder_t::header_ready, true);
}

zlink::stream_fast_encoder_t::~stream_fast_encoder_t ()
{
}

void zlink::stream_fast_encoder_t::header_ready ()
{
    const msg_t *msg = in_progress ();
    const size_t payload_size = msg->size ();
    const uint32_t routing_id = msg->get_routing_id ();

    put_uint32 (_tmp_buf,
                static_cast<uint32_t> (stream_fast_protocol::header_size
                                       + payload_size));
    _tmp_buf[4] = stream_fast_protocol::version;
    _tmp_buf[5] = stream_fast_protocol::type_data;
    _tmp_buf[6] = stream_fast_protocol::magic0;
    _tmp_buf[7] = stream_fast_protocol::magic1;
    put_uint32 (_tmp_buf + 8, routing_id);

    next_step (_tmp_buf, sizeof (_tmp_buf), &stream_fast_encoder_t::body_ready,
               false);
}

void zlink::stream_fast_encoder_t::body_ready ()
{
    next_step (in_progress ()->data (), in_progress ()->size (),
               &stream_fast_encoder_t::header_ready, true);
}
