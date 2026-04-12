/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_MESSAGE_HPP_INCLUDED
#define ZLINK_CPP_MESSAGE_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

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
    ~message_t () { close (); }

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

        close ();
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

        if (zlink_msg_init (&_msg) != 0)
            return;

        if (zlink_msg_move (&_msg, &other._msg) == 0) {
            _valid = true;
            other._valid = false;
            return;
        }

        zlink_msg_close (&_msg);
    }

    message_t &operator= (message_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        close ();
        if (!other._valid)
            return *this;

        if (zlink_msg_init (&_msg) != 0)
            return *this;

        if (zlink_msg_move (&_msg, &other._msg) == 0) {
            _valid = true;
            other._valid = false;
            return *this;
        }

        zlink_msg_close (&_msg);
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

    static message_t from_string (const std::string &text_)
    {
        return from_bytes (text_.data (), text_.size ());
    }

    static message_t
    from_external (void *data_,
                   size_t size_,
                   zlink_free_fn *ffn_ = NULL,
                   void *hint_ = NULL)
    {
        message_t msg;
        (void) msg.init (data_, size_, ffn_, hint_);
        return msg;
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
        close ();
        if (zlink_msg_init_size (&_msg, size_) != 0)
            return;
        _valid = true;
    }

    /**
     * @brief Reinitialize with externally provided data ownership.
     * @param data_ Payload buffer.
     * @param size_ Payload size in bytes.
     * @param ffn_ Optional free callback for `data_`.
     * @param hint_ Optional callback context pointer.
     * @return 0 on success, -1 on failure.
     */
    void init (void *data_,
              size_t size_,
              zlink_free_fn *ffn_ = NULL,
              void *hint_ = NULL)
    {
        close ();
        if (zlink_msg_init_data (&_msg, data_, size_, ffn_, hint_) != 0)
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
    const char *get_property (const std::string &property_) const
    {
        if (!_valid || property_.empty ())
            return NULL;
        return zlink_msg_gets (&_msg, property_.c_str ());
    }

    /**
     * @brief Get string property from a message into `std::string`.
     * @param property_ Property name.
     * @param out_ Output string.
     * @return 0 on success, -1 on failure.
     */
    int get_property (const std::string &property_, std::string &out_) const
    {
        const char *value = get_property (property_);
        if (!value)
            return -1;
        out_.assign (value);
        return 0;
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
    void close () noexcept
    {
        if (!_valid)
            return;
        (void) zlink_msg_close (&_msg);
        _valid = false;
    }

    /**
     * @brief Access raw mutable `zlink_msg_t`.
     * @return Message handle pointer.
     */
    zlink_msg_t *handle () noexcept { return &_msg; }
    /**
     * @brief Access raw const `zlink_msg_t`.
     * @return Message handle pointer.
     */
    const zlink_msg_t *handle () const noexcept { return &_msg; }

    /**
     * @brief Adopt ownership from an already initialized native message.
     * @param src_ Source native message that transfers ownership on success.
     * @return 0 on success, -1 on failure.
     */
    void adopt (zlink_msg_t *src_)
    {
        if (!src_)
            return;

        close ();
        if (zlink_msg_init (&_msg) != 0)
            return;

        if (zlink_msg_move (&_msg, src_) == 0) {
            _valid = true;
            return;
        }

        zlink_msg_close (&_msg);
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
        if (zlink_msg_init (dest_) != 0)
            return;

        if (zlink_msg_move (dest_, &_msg) == 0) {
            _valid = false;
            return;
        }

        zlink_msg_close (dest_);
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

  private:
    zlink_msg_t _msg;
    bool _valid;
};

} // namespace zlink

#endif
