/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/errors/error.hpp>

#include <optional>
#include <utility>

namespace zlink::framework
{

template <typename T> class result_t
{
  public:
    static result_t success (T value) { return result_t (std::move (value)); }

    static result_t
    failure (framework_error_kind_t kind, std::string message, bool retriable = false)
    {
        return result_t (framework_exception_t (kind, std::move (message), retriable));
    }

    bool has_value () const noexcept { return _value.has_value (); }

    explicit operator bool () const noexcept { return has_value (); }

    const T &value () const
    {
        if (!_value) {
            throw *_error;
        }
        return *_value;
    }

    T &value ()
    {
        if (!_value) {
            throw *_error;
        }
        return *_value;
    }

    const framework_exception_t *error () const noexcept { return _error ? &*_error : nullptr; }

    framework_error_kind_t error_kind () const
    {
        if (!_error) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "successful result has no error");
        }
        return _error->kind ();
    }

  private:
    explicit result_t (T value) : _value (std::move (value)) {}
    explicit result_t (framework_exception_t error) : _error (std::move (error)) {}

    std::optional<T> _value;
    std::optional<framework_exception_t> _error;
};

template <> class result_t<void>
{
  public:
    static result_t success () { return result_t (); }

    static result_t
    failure (framework_error_kind_t kind, std::string message, bool retriable = false)
    {
        return result_t (framework_exception_t (kind, std::move (message), retriable));
    }

    bool has_value () const noexcept { return !_error.has_value (); }

    explicit operator bool () const noexcept { return has_value (); }

    void value () const
    {
        if (_error) {
            throw *_error;
        }
    }

    const framework_exception_t *error () const noexcept { return _error ? &*_error : nullptr; }

    framework_error_kind_t error_kind () const
    {
        if (!_error) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "successful result has no error");
        }
        return _error->kind ();
    }

  private:
    result_t () = default;
    explicit result_t (framework_exception_t error) : _error (std::move (error)) {}

    std::optional<framework_exception_t> _error;
};

} // namespace zlink::framework
