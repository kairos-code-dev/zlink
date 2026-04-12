/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_ERROR_HPP_INCLUDED
#define ZLINK_CPP_ERROR_HPP_INCLUDED

#include "types.hpp"

#include <sstream>

namespace zlink
{

class zlink_error_t : public std::runtime_error
{
  public:
    int code () const noexcept { return _code; }
    int internal_errno () const noexcept { return _internal_errno; }

    const char *what () const noexcept override { return _message.c_str (); }

  protected:
    zlink_error_t (int code_, int internal_errno_)
        : std::runtime_error (build_message (code_, internal_errno_)),
          _code (code_),
          _internal_errno (internal_errno_),
          _message (build_message (code_, internal_errno_))
    {
    }

    zlink_error_t (const zlink_error_t &) = default;
    zlink_error_t &operator= (const zlink_error_t &) = default;

  private:
    static std::string build_message (int code_, int internal_errno_)
    {
        std::ostringstream stream;
        stream << zlink_strerror (code_);
        if (internal_errno_ != 0)
            stream << " (errno=" << internal_errno_ << ")";
        return stream.str ();
    }

    int _code;
    int _internal_errno;
    std::string _message;
};

class error_t : public zlink_error_t
{
  public:
    explicit error_t (int code_)
        : error_t (code_, code_)
    {
    }

    error_t (int code_, int internal_errno_)
        : zlink_error_t (code_, internal_errno_)
    {
    }
};

class submit_error_t : public zlink_error_t
{
  public:
    explicit submit_error_t (submit_result_t result_)
        : submit_error_t (result_, zlink_errno ())
    {
    }

    submit_error_t (submit_result_t result_, int internal_errno_)
        : zlink_error_t (static_cast<int> (result_), internal_errno_),
          _result (result_)
    {
    }

    submit_result_t result () const noexcept { return _result; }

  private:
    submit_result_t _result;
};

class request_error_t : public zlink_error_t
{
  public:
    explicit request_error_t (request_result_t result_)
        : request_error_t (result_, zlink_errno ())
    {
    }

    request_error_t (request_result_t result_, int internal_errno_)
        : zlink_error_t (static_cast<int> (result_), internal_errno_),
          _result (result_)
    {
    }

    request_result_t result () const noexcept { return _result; }

  private:
    request_result_t _result;
};

class recv_error_t : public zlink_error_t
{
  public:
    explicit recv_error_t (recv_result_t result_)
        : recv_error_t (result_, zlink_errno ())
    {
    }

    recv_error_t (recv_result_t result_, int internal_errno_)
        : zlink_error_t (static_cast<int> (result_), internal_errno_),
          _result (result_)
    {
    }

    recv_result_t result () const noexcept { return _result; }

  private:
    recv_result_t _result;
};

class handler_error_t : public zlink_error_t
{
  public:
    explicit handler_error_t (handler_result_t result_)
        : handler_error_t (result_, zlink_errno ())
    {
    }

    handler_error_t (handler_result_t result_, int internal_errno_)
        : zlink_error_t (static_cast<int> (result_), internal_errno_),
          _result (result_)
    {
    }

    handler_result_t result () const noexcept { return _result; }

  private:
    handler_result_t _result;
};

class close_error_t : public zlink_error_t
{
  public:
    explicit close_error_t (close_result_t result_)
        : close_error_t (result_, zlink_errno ())
    {
    }

    close_error_t (close_result_t result_, int internal_errno_)
        : zlink_error_t (static_cast<int> (result_), internal_errno_),
          _result (result_)
    {
    }

    close_result_t result () const noexcept { return _result; }

  private:
    close_result_t _result;
};

class bind_error_t : public zlink_error_t
{
  public:
    explicit bind_error_t (bind_result_t result_)
        : bind_error_t (result_, zlink_errno ())
    {
    }

    bind_error_t (bind_result_t result_, int internal_errno_)
        : zlink_error_t (static_cast<int> (result_), internal_errno_),
          _result (result_)
    {
    }

    bind_result_t result () const noexcept { return _result; }

  private:
    bind_result_t _result;
};

class connect_error_t : public zlink_error_t
{
  public:
    explicit connect_error_t (connect_result_t result_)
        : connect_error_t (result_, zlink_errno ())
    {
    }

    connect_error_t (connect_result_t result_, int internal_errno_)
        : zlink_error_t (static_cast<int> (result_), internal_errno_),
          _result (result_)
    {
    }

    connect_result_t result () const noexcept { return _result; }

  private:
    connect_result_t _result;
};

class config_error_t : public zlink_error_t
{
  public:
    explicit config_error_t (config_result_t result_)
        : config_error_t (result_, zlink_errno ())
    {
    }

    config_error_t (config_result_t result_, int internal_errno_)
        : zlink_error_t (static_cast<int> (result_), internal_errno_),
          _result (result_)
    {
    }

    config_result_t result () const noexcept { return _result; }

  private:
    config_result_t _result;
};

namespace detail
{

template<typename ErrorT, typename ResultT>
inline void throw_if_failed (ResultT result_)
{
    if (static_cast<int> (result_) != 0)
        throw ErrorT (result_);
}

template<typename ErrorT, typename ResultT>
inline void throw_if_failed (ResultT result_, int internal_errno_)
{
    if (static_cast<int> (result_) != 0)
        throw ErrorT (result_, internal_errno_);
}

inline void throw_on_error (int rc_)
{
    if (rc_ != 0)
        throw error_t (zlink_errno (), zlink_errno ());
}

inline error_t last_error ()
{
    return error_t (zlink_errno (), zlink_errno ());
}

} // namespace detail

using detail::last_error;
using detail::throw_on_error;

} // namespace zlink

#endif
