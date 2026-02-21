/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "protocol/raw_encoder.hpp"
#include "core/msg.hpp"
#include "protocol/wire.hpp"

#include <climits>

zlink::raw_encoder_t::raw_encoder_t (size_t bufsize_, bool len32be_) :
    encoder_base_t<raw_encoder_t> (bufsize_),
    _len32be (len32be_)
{
    next_step (NULL, 0, &raw_encoder_t::raw_message_ready, true);
}

zlink::raw_encoder_t::~raw_encoder_t ()
{
}

void zlink::raw_encoder_t::raw_message_ready ()
{
    if (_len32be) {
        const size_t payload_size = in_progress ()->size ();
        zlink_assert (payload_size <= static_cast<size_t> (UINT32_MAX));
        put_uint32 (_header, static_cast<uint32_t> (payload_size));
        next_step (_header, sizeof (_header), &raw_encoder_t::raw_message_body,
                   false);
        return;
    }

    next_step (in_progress ()->data (), in_progress ()->size (),
               &raw_encoder_t::raw_message_ready, true);
}

void zlink::raw_encoder_t::raw_message_body ()
{
    next_step (in_progress ()->data (), in_progress ()->size (),
               &raw_encoder_t::raw_message_ready, true);
}
