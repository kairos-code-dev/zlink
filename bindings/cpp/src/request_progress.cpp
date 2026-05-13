/* SPDX-License-Identifier: MPL-2.0 */
#include "zlink/base_socket.hpp"
#include "zlink/services/spot_common.hpp"

extern "C" int zlink_socket_request_progress_internal (void *socket_);
extern "C" int zlink_spot_request_progress_internal (void *spot_);
extern "C" int zlink_spot_request_channel_progress_internal (
  void *spot_,
  const char *channel_name_);

namespace zlink
{
namespace detail
{

void request_progress_socket (void *socket_) noexcept
{
    (void) zlink_socket_request_progress_internal (socket_);
}

} // namespace detail
namespace service
{
namespace detail
{

void request_progress_spot (void *spot_) noexcept
{
    (void) zlink_spot_request_progress_internal (spot_);
}

void request_progress_spot_channel (void *spot_,
                                    const std::string &channel_name_) noexcept
{
    (void) zlink_spot_request_channel_progress_internal (
      spot_, channel_name_.c_str ());
}

} // namespace detail
} // namespace service
} // namespace zlink
