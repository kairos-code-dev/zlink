/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "zmp_encoder.hpp"
#include "msg.hpp"
#include "wire.hpp"

zmq::zmp_encoder_t::zmp_encoder_t (size_t bufsize_) :
    encoder_base_t<zmp_encoder_t> (bufsize_)
{
    next_step (NULL, 0, &zmp_encoder_t::header_ready, true);
}

zmq::zmp_encoder_t::~zmp_encoder_t ()
{
}

void zmq::zmp_encoder_t::header_ready ()
{
    const msg_t *msg = in_progress ();
    const size_t size = msg->size ();

    unsigned char flags = 0;
    if (msg->flags () & msg_t::more)
        flags |= zmp_flag_more;
    if (msg->flags () & msg_t::command)
        flags |= zmp_flag_control;
    if (msg->flags () & msg_t::routing_id)
        flags |= zmp_flag_identity;
    if (msg->is_subscribe ())
        flags |= zmp_flag_subscribe;
    if (msg->is_cancel ())
        flags |= zmp_flag_cancel;

    _tmp_buf[0] = zmp_magic;
    _tmp_buf[1] = zmp_version;
    _tmp_buf[2] = flags;
    _tmp_buf[3] = 0;
    put_uint32 (_tmp_buf + 4, static_cast<uint32_t> (size));

    next_step (_tmp_buf, zmp_header_size, &zmp_encoder_t::body_ready, false);
}

void zmq::zmp_encoder_t::body_ready ()
{
    next_step (in_progress ()->data (), in_progress ()->size (),
               &zmp_encoder_t::header_ready, true);
}
