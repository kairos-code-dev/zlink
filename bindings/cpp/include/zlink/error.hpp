/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_ERROR_HPP_INCLUDED
#define ZLINK_CPP_ERROR_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

class error_t
#if defined(ZLINK_CPP_EXCEPTIONS)
  : public std::exception
#endif
{
  public:
    explicit error_t (int code_) : _code (code_) {}
    int code () const noexcept { return _code; }

    const char *what () const noexcept
#if defined(ZLINK_CPP_EXCEPTIONS)
      override
#endif
    {
        return zlink_strerror (_code);
    }

  private:
    int _code;
};

inline error_t last_error () { return error_t (zlink_errno ()); }

#if defined(ZLINK_CPP_EXCEPTIONS)
inline void throw_on_error (int rc)
{
    if (rc < 0)
        throw std::runtime_error (zlink_strerror (zlink_errno ()));
}
#endif

} // namespace zlink

#endif
