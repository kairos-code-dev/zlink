/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_RUNTIME_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_RUNTIME_INTERNAL_HPP_INCLUDED__

#include "services/spot/spot_runtime.hpp"

namespace zlink
{
struct runtime_socket_slot_ref_t
{
    socket_base_t **slot;
    const std::string *endpoint;
    bool close_directly;
};

void preserve_first_error_local (int rc_, int *first_error_);
int send_ascii_frame_local (socket_base_t *socket_,
                            const std::string &value_,
                            int flags_);
void close_socket_ptr_local (socket_base_t **socket_p_);
bool spot_shutdown_debug_enabled_local ();
size_t fill_runtime_socket_slot_refs (spot_runtime_t *runtime_,
                                      runtime_socket_slot_ref_t *out_);
}

#endif
