/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <boost/system/error_code.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
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
    virtual void async_read_some (
      std::size_t max_size,
      std::function<void (boost::system::error_code, std::vector<std::uint8_t>)> completion) = 0;
    virtual bool
    wait_readable_until (std::chrono::steady_clock::time_point deadline,
                         boost::system::error_code &error)
    {
        (void) deadline;
        error.clear ();
        return true;
    }
    virtual void write (const std::vector<std::uint8_t> &bytes) = 0;
    virtual void async_write (std::vector<std::uint8_t> bytes,
                              std::function<void (boost::system::error_code)> completion) = 0;
    virtual void shutdown_and_close () = 0;
    virtual void close (boost::system::error_code &error) = 0;
};

} // namespace zlink::stream_connector::detail
