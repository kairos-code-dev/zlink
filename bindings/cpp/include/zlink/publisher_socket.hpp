/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_PUBLISHER_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_PUBLISHER_SOCKET_HPP_INCLUDED

#include "base_socket.hpp"
#include "error.hpp"

namespace zlink
{

class publisher_socket_t : public base_socket_t
{
  protected:
    publisher_socket_t (context_t &ctx_, socket_type type_) : base_socket_t (ctx_, type_) {}
};

} // namespace zlink

#endif
