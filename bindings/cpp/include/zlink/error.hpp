/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_ERROR_HPP_INCLUDED
#define ZLINK_CPP_ERROR_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

/**
 * @brief Lightweight error object that stores a zlink error code.
 */
class error_t
  : public std::exception
{
  public:
    /**
     * @brief Create an error wrapper from a numeric code.
     * @param code_ Error code.
     */
    explicit error_t (int code_) : _code (code_) {}
    /**
     * @brief Get the stored error code.
     * @return Error code.
     */
    int code () const noexcept { return _code; }

    /**
     * @brief Get a human-readable error message.
     * @return Message string managed by zlink.
     */
    const char *what () const noexcept
      override
    {
        return zlink_strerror (_code);
    }

  private:
    int _code;
};

/**
 * @brief Fetch the last error from the zlink thread-local error state.
 * @return Error wrapper with current `zlink_errno()`.
 */
inline error_t last_error () { return error_t (zlink_errno ()); }

/**
 * @brief Throw `std::runtime_error` when a zlink return code indicates failure.
 * @param rc Return code to check.
 */
inline void throw_on_error (int rc)
{
    if (rc < 0)
        throw std::runtime_error (zlink_strerror (zlink_errno ()));
}

} // namespace zlink

#endif
