/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_OPTIONS_OWNER_HPP_INCLUDED__
#define __ZLINK_OPTIONS_OWNER_HPP_INCLUDED__

namespace zlink
{
// Owner map for the shared options_t storage bag.
// This keeps the storage layout stable while making the validation/apply
// owner explicit in code. Service-local options still live behind service
// seams and should not be routed through the central options_t bag.
enum options_owner_t
{
    options_owner_unknown = 0,
    options_owner_core_socket,
    options_owner_transport_network,
    options_owner_protocol_metadata,
    options_owner_service_specific
};

options_owner_t option_owner_of (int option_);
const char *option_owner_name (options_owner_t owner_);
}

#endif
