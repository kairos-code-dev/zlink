/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_API_MESH_STREAM_SESSION_INTERNAL_HPP_INCLUDED
#define ZLINK_API_MESH_STREAM_SESSION_INTERNAL_HPP_INCLUDED

namespace zlink
{
namespace mesh
{
//  True while a STREAM session service retains the raw socket handle.
bool stream_session_owns_socket (void *socket_);
}
}

#endif
