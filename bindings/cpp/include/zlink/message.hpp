/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_MESSAGE_HPP_INCLUDED
#define ZLINK_CPP_MESSAGE_HPP_INCLUDED

#include "common.hpp"

#include <optional>
#include <span>

namespace zlink
{

class message_t;
class stream_socket_t;

namespace advanced
{
class external_message_t;
}

namespace detail
{
inline zlink_msg_t *native_handle (message_t &message_) noexcept;
inline const zlink_msg_t *native_handle (const message_t &message_) noexcept;
inline void adopt_native_message (message_t &message_, zlink_msg_t *src_);
inline void move_to_native (message_t &message_, zlink_msg_t *dest_);
inline void copy_to_native (const message_t &message_, zlink_msg_t *dest_);
// Mark the message as transferred after a successful zlink_send_part*
// or other zero-copy ownership-transfer call. The native msg is now
// owned by the socket queue; the wrapper only needs to drop its valid
// flag so the destructor does not call zlink_msg_close on dangling state.
inline void mark_sent (message_t &message_) noexcept;
} // namespace detail

/**
 * @brief RAII wrapper for `zlink_msg_t`.
 */
class message_t
{
  public:
    /**
     * @brief Construct an empty message.
     */
    message_t () : _valid (false)
    {
        if (zlink_msg_init (&_msg) == 0)
            _valid = true;
    }

    /**
     * @brief Construct a message with preallocated payload.
     * @param size_ Payload size in bytes.
     */
    explicit message_t (size_t size_) : _valid (false)
    {
        if (zlink_msg_init_size (&_msg, size_) == 0)
            _valid = true;
    }

    /**
     * @brief Close the message if initialized.
     */
    ~message_t () { close_noexcept (); }

    message_t (const message_t &other) : _valid (false)
    {
        if (!other._valid)
            return;

        if (zlink_msg_init (&_msg) != 0)
            return;

        if (zlink_msg_copy (&_msg, const_cast<zlink_msg_t *> (&other._msg)) == 0) {
            _valid = true;
            return;
        }

        zlink_msg_close (&_msg);
    }

    message_t &operator= (const message_t &other)
    {
        if (this == &other)
            return *this;

        close_noexcept ();
        if (!other._valid)
            return *this;

        if (zlink_msg_init (&_msg) != 0)
            return *this;

        if (zlink_msg_copy (&_msg, const_cast<zlink_msg_t *> (&other._msg)) == 0) {
            _valid = true;
            return *this;
        }

        zlink_msg_close (&_msg);
        return *this;
    }

    message_t (message_t &&other) noexcept : _valid (false)
    {
        if (!other._valid)
            return;

        _msg = other._msg;
        _valid = true;
        other._valid = false;
    }

    message_t &operator= (message_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        close_noexcept ();
        if (!other._valid)
            return *this;

        _msg = other._msg;
        _valid = true;
        other._valid = false;
        return *this;
    }

    /**
     * @brief Check whether the wrapper currently owns a valid message.
     * @return `true` when initialized, otherwise `false`.
     */
    bool valid () const noexcept { return _valid; }

    static message_t from_bytes (const void *data_, size_t size_)
    {
        message_t msg (size_);
        if (!msg.valid ())
            return msg;

        if (size_ > 0 && data_)
            std::memcpy (msg.data (), data_, size_);
        return msg;
    }

    static message_t from_bytes (const std::vector<uint8_t> &bytes_)
    {
        return from_bytes (
          bytes_.empty () ? NULL : &bytes_[0], bytes_.size ());
    }

    static message_t from_bytes (std::span<const std::byte> bytes_)
    {
        return from_bytes (bytes_.empty () ? NULL : bytes_.data (),
                           bytes_.size ());
    }

    static message_t from_string (const std::string &text_)
    {
        return from_bytes (text_.data (), text_.size ());
    }

    /**
     * @brief Initialize an empty message if needed.
     * @return 0 on success, -1 on failure.
     */
    void init ()
    {
        if (_valid)
            return;
        if (zlink_msg_init (&_msg) != 0)
            return;
        _valid = true;
    }

    /**
     * @brief Reinitialize message storage with a fixed size.
     * @param size_ Payload size in bytes.
     * @return 0 on success, -1 on failure.
     */
    void init (size_t size_)
    {
        close_noexcept ();
        if (zlink_msg_init_size (&_msg, size_) != 0)
            return;
        _valid = true;
    }

    /**
     * @brief Get mutable payload pointer.
     * @return Payload pointer or `NULL` when invalid.
     */
    void *data () noexcept { return _valid ? zlink_msg_data (&_msg) : NULL; }

    /**
     * @brief Get const payload pointer.
     * @return Payload pointer or `NULL` when invalid.
     */
    const void *data () const noexcept
    {
        return _valid ? zlink_msg_data (const_cast<zlink_msg_t *> (&_msg)) : NULL;
    }

    std::span<std::byte> bytes () noexcept
    {
        return std::span<std::byte> (static_cast<std::byte *> (data ()), size ());
    }

    std::span<const std::byte> bytes () const noexcept
    {
        return std::span<const std::byte> (
          static_cast<const std::byte *> (data ()), size ());
    }

    /**
     * @brief Get payload size.
     * @return Size in bytes, or 0 when invalid.
     */
    size_t size () const noexcept
    {
        return _valid ? zlink_msg_size (&_msg) : 0;
    }

    /**
     * @brief Get the storage reference count.
     * @return Reference count, or -1 when invalid.
     */
    int ref_count () const noexcept
    {
        return _valid ? zlink_msg_refcnt (&_msg, nullptr) : -1;
    }

    /**
     * @brief Get string property from a message.
     * @param property_ Property name.
     * @return Property string or `NULL` on failure.
     */
    std::optional<std::string> property (const std::string &property_) const
    {
        const char *value = get_property (property_);
        if (!value)
            return std::nullopt;
        return std::optional<std::string> (std::string (value));
    }

    // -- Conversions ----------------------------------------------------------

    std::vector<uint8_t> to_bytes () const
    {
        const uint8_t *ptr = static_cast<const uint8_t *> (data ());
        return ptr ? std::vector<uint8_t> (ptr, ptr + size ())
                   : std::vector<uint8_t> ();
    }

    std::string to_string () const
    {
        const char *ptr = static_cast<const char *> (data ());
        return ptr ? std::string (ptr, ptr + size ()) : std::string ();
    }

    /**
     * @brief Close and invalidate the message.
     * @return 0 on success, -1 on failure.
     */
    void close ()
    {
        if (!_valid)
            return;
        (void) zlink_msg_close (&_msg);
        _valid = false;
    }

  private:
    struct no_init_t
    {
    };

    explicit message_t (no_init_t) noexcept : _valid (false) {}

    friend class advanced::external_message_t;
    friend class stream_socket_t;
    friend zlink_msg_t *detail::native_handle (message_t &message_) noexcept;
    friend const zlink_msg_t *
    detail::native_handle (const message_t &message_) noexcept;
    friend void detail::adopt_native_message (message_t &message_,
                                              zlink_msg_t *src_);
    friend void detail::move_to_native (message_t &message_,
                                        zlink_msg_t *dest_);
    friend void detail::copy_to_native (const message_t &message_,
                                        zlink_msg_t *dest_);
    friend void detail::mark_sent (message_t &message_) noexcept;

    const char *get_property (const std::string &property_) const
    {
        if (!_valid || property_.empty ())
            return NULL;
        return zlink_msg_gets (&_msg, property_.c_str ());
    }

    static message_t
    from_external (void *data_,
                   size_t size_,
                   zlink_free_fn *ffn_ = NULL,
                   void *hint_ = NULL)
    {
        message_t msg {no_init_t ()};
        (void) msg.init (data_, size_, ffn_, hint_);
        return msg;
    }

    void init (void *data_,
              size_t size_,
              zlink_free_fn *ffn_ = NULL,
              void *hint_ = NULL)
    {
        close_noexcept ();
        if (zlink_msg_init_data (&_msg, data_, size_, ffn_, hint_) != 0)
            return;
        _valid = true;
    }

    zlink_msg_t *handle () noexcept { return &_msg; }
    const zlink_msg_t *handle () const noexcept { return &_msg; }

    void close_noexcept () noexcept
    {
        if (!_valid)
            return;
        (void) zlink_msg_close (&_msg);
        _valid = false;
    }

    /**
     * @brief Adopt ownership from an already initialized native message.
     * @param src_ Source native message that transfers ownership on success.
     * @return 0 on success, -1 on failure.
     */
    void adopt (zlink_msg_t *src_)
    {
        if (!src_)
            return;

        close_noexcept ();
        _msg = *src_;
        if (zlink_msg_init (src_) != 0)
            return;
        _valid = true;
    }

    /**
     * @brief Move message ownership into another native message.
     * @param dest_ Destination native message.
     * @return 0 on success, -1 on failure.
     */
    void move_to (zlink_msg_t *dest_)
    {
        if (!dest_ || !_valid)
            return;

        *dest_ = _msg;
        _valid = false;
    }

    /**
     * @brief Deep-copy this message into another native message.
     * @param dest_ Destination native message.
     * @return 0 on success, -1 on failure.
     */
    void copy_to (zlink_msg_t *dest_) const
    {
        if (!dest_ || !_valid)
            return;
        if (zlink_msg_init (dest_) != 0)
            return;
        if (zlink_msg_copy (dest_, const_cast<zlink_msg_t *> (&_msg)) == 0)
            return;

        zlink_msg_close (dest_);
    }

    zlink_msg_t _msg;
    bool _valid;
};

namespace advanced
{
class external_message_t
{
  public:
    static message_t adopt (void *data_,
                            size_t size_,
                            zlink_free_fn *release_,
                            void *hint_ = NULL)
    {
        return message_t::from_external (data_, size_, release_, hint_);
    }
};
} // namespace advanced

namespace detail
{
inline zlink_msg_t *native_handle (message_t &message_) noexcept
{
    return message_.handle ();
}

inline const zlink_msg_t *native_handle (const message_t &message_) noexcept
{
    return message_.handle ();
}

inline void adopt_native_message (message_t &message_, zlink_msg_t *src_)
{
    message_.adopt (src_);
}

inline void move_to_native (message_t &message_, zlink_msg_t *dest_)
{
    message_.move_to (dest_);
}

inline void copy_to_native (const message_t &message_, zlink_msg_t *dest_)
{
    message_.copy_to (dest_);
}

inline void mark_sent (message_t &message_) noexcept
{
    // After zlink_send_* succeeds, the native msg state is empty (zlink
    // moved its data into the socket queue). The wrapper just needs to
    // drop its valid flag so the destructor will not invoke zlink_msg_close
    // on a moved-from native handle.
    message_._valid = false;
}
} // namespace detail

} // namespace zlink

#endif
