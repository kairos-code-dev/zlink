/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <boost/system/error_code.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zlink::stream_connector::detail
{

class stream_connection_t
{
  public:
    virtual ~stream_connection_t () = default;
    virtual bool is_open () const = 0;
    virtual std::size_t available (boost::system::error_code &error) = 0;
    virtual std::size_t
    read_some (std::uint8_t *buffer, std::size_t size, boost::system::error_code &error) = 0;
    virtual void write (const std::vector<std::uint8_t> &bytes) = 0;
    virtual void shutdown_and_close () = 0;
    virtual void close (boost::system::error_code &error) = 0;
};

} // namespace zlink::stream_connector::detail
