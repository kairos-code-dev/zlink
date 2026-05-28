/* SPDX-License-Identifier: MPL-2.0 */

#include <Runtime/Native/message_access.hpp>

#include <cstring>
#include <stdexcept>

namespace zlink
{

message_t::message_t () : _storage (), _valid (false)
{
    if (zlink_msg_init (detail::native_handle (*this)) == 0)
        _valid = true;
}

message_t::message_t (size_t size_) : _storage (), _valid (false)
{
    if (zlink_msg_init_size (detail::native_handle (*this), size_) == 0)
        _valid = true;
}

message_t::message_t (no_init_t) noexcept : _storage (), _valid (false) {}

message_t::~message_t () { close_noexcept (); }

message_t::message_t (const message_t &other_) : _storage (), _valid (false)
{
    if (!other_._valid)
        return;
    if (zlink_msg_init (detail::native_handle (*this)) != 0)
        return;
    if (zlink_msg_copy (
          detail::native_handle (*this),
          const_cast<zlink_msg_t *> (detail::native_handle (other_)))
        == 0) {
        _valid = true;
        return;
    }
    zlink_msg_close (detail::native_handle (*this));
}

message_t &message_t::operator= (const message_t &other_)
{
    if (this == &other_)
        return *this;
    close_noexcept ();
    if (!other_._valid)
        return *this;
    if (zlink_msg_init (detail::native_handle (*this)) != 0)
        return *this;
    if (zlink_msg_copy (
          detail::native_handle (*this),
          const_cast<zlink_msg_t *> (detail::native_handle (other_)))
        == 0) {
        _valid = true;
        return *this;
    }
    zlink_msg_close (detail::native_handle (*this));
    return *this;
}

message_t::message_t (message_t &&other_) noexcept
    : _storage (), _valid (false)
{
    if (!other_._valid)
        return;
    *detail::native_handle (*this) = *detail::native_handle (other_);
    _valid = true;
    other_._valid = false;
}

message_t &message_t::operator= (message_t &&other_) noexcept
{
    if (this == &other_)
        return *this;
    close_noexcept ();
    if (!other_._valid)
        return *this;
    *detail::native_handle (*this) = *detail::native_handle (other_);
    _valid = true;
    other_._valid = false;
    return *this;
}

bool message_t::valid () const noexcept { return _valid; }

message_t message_t::allocate (size_t size_) { return message_t (size_); }

message_t message_t::from_bytes (std::span<const std::byte> bytes_)
{
    message_t msg (bytes_.size ());
    if (!msg.valid ())
        return msg;
    if (!bytes_.empty ())
        std::memcpy (msg.data (), bytes_.data (), bytes_.size ());
    return msg;
}

message_t message_t::from_bytes (const std::vector<uint8_t> &bytes_)
{
    return from_bytes (std::span<const uint8_t> (bytes_.data (), bytes_.size ()));
}

message_t message_t::from_bytes (std::span<const uint8_t> bytes_)
{
    return from_bytes (std::as_bytes (bytes_));
}

message_t message_t::from_string (const std::string &text_)
{
    return from_bytes (std::as_bytes (std::span<const char> (text_.data (),
                                                             text_.size ())));
}

void message_t::init ()
{
    if (_valid)
        return;
    if (zlink_msg_init (detail::native_handle (*this)) != 0)
        return;
    _valid = true;
}

void message_t::init (size_t size_)
{
    close_noexcept ();
    if (zlink_msg_init_size (detail::native_handle (*this), size_) != 0)
        return;
    _valid = true;
}

std::byte *message_t::data () noexcept
{
    return _valid ? static_cast<std::byte *> (
                      zlink_msg_data (detail::native_handle (*this)))
                  : nullptr;
}

const std::byte *message_t::data () const noexcept
{
    return _valid
             ? static_cast<const std::byte *> (zlink_msg_data (
                 const_cast<zlink_msg_t *> (detail::native_handle (*this))))
             : nullptr;
}

std::span<std::byte> message_t::bytes () noexcept
{
    return std::span<std::byte> (data (), size ());
}

std::span<const std::byte> message_t::bytes () const noexcept
{
    return std::span<const std::byte> (data (), size ());
}

size_t message_t::size () const noexcept
{
    return _valid ? zlink_msg_size (detail::native_handle (*this)) : 0;
}

bool message_t::is_empty () const noexcept { return size () == 0; }

int message_t::ref_count () const noexcept
{
    return _valid ? zlink_msg_refcnt (detail::native_handle (*this), nullptr)
                  : -1;
}

std::optional<std::string>
message_t::property (const std::string &property_) const
{
    if (!_valid || property_.empty ())
        return std::nullopt;
    const char *value =
      zlink_msg_gets (detail::native_handle (*this), property_.c_str ());
    if (!value)
        return std::nullopt;
    return std::string (value);
}

std::vector<uint8_t> message_t::to_bytes () const
{
    const uint8_t *ptr = reinterpret_cast<const uint8_t *> (data ());
    return ptr ? std::vector<uint8_t> (ptr, ptr + size ())
               : std::vector<uint8_t> ();
}

size_t message_t::copy_to (std::span<std::byte> destination_) const
{
    const size_t payload_size = size ();
    if (payload_size > destination_.size ())
        throw std::invalid_argument ("destination buffer too small");
    if (payload_size == 0)
        return 0;
    std::memcpy (destination_.data (), data (), payload_size);
    return payload_size;
}

size_t message_t::copy_to (std::span<uint8_t> destination_) const
{
    return copy_to (std::as_writable_bytes (destination_));
}

std::string message_t::to_string () const
{
    const char *ptr = reinterpret_cast<const char *> (data ());
    return ptr ? std::string (ptr, ptr + size ()) : std::string ();
}

void message_t::close ()
{
    if (!_valid)
        return;
    (void) zlink_msg_close (detail::native_handle (*this));
    _valid = false;
}

namespace advanced
{

message_t external_message_t::from_bytes (std::span<const std::byte> bytes_)
{
    return message_t::from_bytes (bytes_);
}

message_t external_message_t::from_bytes (std::span<const uint8_t> bytes_)
{
    return message_t::from_bytes (bytes_);
}

} // namespace advanced

void message_t::close_noexcept () noexcept
{
    if (!_valid)
        return;
    (void) zlink_msg_close (detail::native_handle (*this));
    _valid = false;
}

} // namespace zlink
