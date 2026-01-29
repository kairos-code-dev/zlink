/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_I_ENCODER_HPP_INCLUDED__
#define __ZLINK_I_ENCODER_HPP_INCLUDED__

#include "utils/macros.hpp"
#include "utils/stdint.hpp"

namespace zlink
{
//  Forward declaration
class msg_t;

//  Interface to be implemented by message encoder.

struct i_encoder
{
    virtual ~i_encoder () ZLINK_DEFAULT;

    //  The function returns a batch of binary data. The data
    //  are filled to a supplied buffer. If no buffer is supplied (data_
    //  is NULL) encoder will provide buffer of its own.
    //  Function returns 0 when a new message is required.
    virtual size_t encode (unsigned char **data_, size_t size_) = 0;

    //  Load a new message into encoder.
    virtual void load_msg (msg_t *msg_) = 0;
};
}

#endif
