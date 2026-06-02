/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/detail/call_facade.hpp>

#include <chrono>
#include <functional>
#include <utility>

namespace zlink::framework
{

template<typename TReply>
class request_call_t : private detail::call_facade_t<request_call_t<TReply>,
                                                     TReply>
{
private:
  using base_t = detail::call_facade_t<request_call_t<TReply>, TReply>;

public:
  explicit request_call_t (result_t<TReply> result)
    : base_t (std::move (result))
  {
  }

  using base_t::submit;
  using base_t::timeout;
};

class send_call_t : private detail::call_facade_t<send_call_t, void>
{
private:
  using base_t = detail::call_facade_t<send_call_t, void>;

public:
  explicit send_call_t (result_t<void> result) : base_t (std::move (result)) {}

  using base_t::submit;
  using base_t::timeout;
};

class relay_call_t : private detail::call_facade_t<relay_call_t, void>
{
private:
  using base_t = detail::call_facade_t<relay_call_t, void>;

public:
  explicit relay_call_t (result_t<void> result) : base_t (std::move (result)) {}

  using base_t::submit;
  using base_t::timeout;
};

class stream_write_call_t
  : private detail::call_facade_t<stream_write_call_t, void>
{
private:
  using base_t = detail::call_facade_t<stream_write_call_t, void>;

public:
  explicit stream_write_call_t (result_t<void> result)
    : base_t (std::move (result))
  {
  }

  using base_t::submit;
  using base_t::timeout;
};

} // namespace zlink::framework
