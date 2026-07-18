/* SPDX-License-Identifier: MPL-2.0 */
#include <zlink/Contracts/Service/dispatch.hpp>

#include "dispatch_access.hpp"
#include "detail.hpp"

#include <zlink.h>

#include <utility>
#include <vector>

namespace zlink
{
namespace service
{

// --- claim_t ---

claim_t::claim_t () noexcept : _opaque{}, _valid (false) {}

claim_t::~claim_t () { release (); }

claim_t::claim_t (claim_t &&other_) noexcept : _opaque (other_._opaque), _valid (other_._valid)
{
    other_._valid = false;
}

claim_t &claim_t::operator= (claim_t &&other_) noexcept
{
    if (this != &other_) {
        release ();
        _opaque = other_._opaque;
        _valid = other_._valid;
        other_._valid = false;
    }
    return *this;
}

void claim_t::release () noexcept
{
    if (!_valid)
        return;
    zlink_mesh_claim_t native = detail::dispatch_access_t::native_claim (*this);
    const zlink_close_result_t rc = zlink_mesh_claim_release (&native);
    detail::dispatch_access_t::store_claim (*this, native);
    // The claim (and any reply tokens drained under it) stays valid until the
    // release actually succeeds; releasing rearms the owner's remaining mailbox
    // work. On failure the claim is kept so it can be released again.
    if (rc == ZLINK_CLOSE_OK)
        _valid = false;
}

int claim_t::recv_batch (receive_batch_t &batch_,
                         receive_requirements_t &required_out_,
                         recv_flags_t flags_)
{
    if (!_valid)
        return static_cast<int> (recv_result_t::invalid_handle);

    zlink_mesh_claim_t native = detail::dispatch_access_t::native_claim (*this);
    zlink_mesh_receive_requirements_t required;
    std::memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = ZLINK_MESH_DISPATCH_ABI_VERSION;

    const zlink_recv_result_t rc = zlink_mesh_claim_recv_batch (
      &native, detail::dispatch_access_t::receive_handle (batch_), &required,
      static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));

    detail::dispatch_access_t::store_claim (*this, native);
    required_out_.message_count = required.message_count;
    required_out_.part_count = required.part_count;
    required_out_.byte_count = required.byte_count;

    // A successful recv does NOT consume the claim: reply tokens drained here
    // remain valid, and the owner's mailbox is rearmed only by an actual
    // release(). The claim stays valid until release() succeeds.
    return static_cast<int> (rc);
}

// --- ready_batch_t ---

ready_batch_t::ready_batch_t (size_t record_capacity_) :
    _handle (zlink_mesh_ready_batch_new (record_capacity_))
{
}

ready_batch_t::~ready_batch_t ()
{
    if (_handle)
        (void) zlink_mesh_ready_batch_destroy (&_handle);
}

ready_batch_t::ready_batch_t (ready_batch_t &&other_) noexcept : _handle (other_._handle)
{
    other_._handle = nullptr;
}

ready_batch_t &ready_batch_t::operator= (ready_batch_t &&other_) noexcept
{
    if (this != &other_) {
        if (_handle)
            (void) zlink_mesh_ready_batch_destroy (&_handle);
        _handle = other_._handle;
        other_._handle = nullptr;
    }
    return *this;
}

void ready_batch_t::reset ()
{
    if (_handle)
        (void) zlink_mesh_ready_batch_reset (_handle);
}

size_t ready_batch_t::count () const noexcept
{
    return _handle ? zlink_mesh_ready_batch_count (_handle) : 0u;
}

ready_record_t ready_batch_t::at (size_t index_) const
{
    const zlink_mesh_ready_record_t *data = zlink_mesh_ready_batch_data (_handle);
    if (!data || index_ >= zlink_mesh_ready_batch_count (_handle))
        return ready_record_t ();
    return detail::dispatch_access_t::ready_from_native (data[index_]);
}

claim_t ready_batch_t::take_claim (size_t index_)
{
    claim_t claim;
    zlink_mesh_claim_t native;
    std::memset (&native, 0, sizeof (native));
    const zlink_config_result_t rc =
      zlink_mesh_ready_batch_take_claim (_handle, index_, &native);
    if (rc == ZLINK_CONFIG_OK)
        detail::dispatch_access_t::set_claim (claim, native);
    return claim;
}

// --- receive_batch_t ---

receive_batch_t::receive_batch_t (size_t message_capacity_,
                                  size_t part_capacity_,
                                  size_t byte_capacity_) :
    _handle (zlink_mesh_receive_batch_new (message_capacity_, part_capacity_, byte_capacity_))
{
}

receive_batch_t::~receive_batch_t ()
{
    if (_handle)
        (void) zlink_mesh_receive_batch_destroy (&_handle);
}

receive_batch_t::receive_batch_t (receive_batch_t &&other_) noexcept : _handle (other_._handle)
{
    other_._handle = nullptr;
}

receive_batch_t &receive_batch_t::operator= (receive_batch_t &&other_) noexcept
{
    if (this != &other_) {
        if (_handle)
            (void) zlink_mesh_receive_batch_destroy (&_handle);
        _handle = other_._handle;
        other_._handle = nullptr;
    }
    return *this;
}

void receive_batch_t::reset ()
{
    if (_handle)
        (void) zlink_mesh_receive_batch_reset (_handle);
}

size_t receive_batch_t::count () const noexcept
{
    return _handle ? zlink_mesh_receive_batch_count (_handle) : 0u;
}

receive_record_t receive_batch_t::at (size_t index_) const
{
    const zlink_mesh_receive_record_t *data = zlink_mesh_receive_batch_data (_handle);
    if (!data || index_ >= zlink_mesh_receive_batch_count (_handle))
        return receive_record_t ();
    return detail::dispatch_access_t::receive_from_native (data[index_]);
}

std::vector<message_t> receive_batch_t::retain_message (size_t index_) const
{
    const zlink_mesh_receive_record_t *data = zlink_mesh_receive_batch_data (_handle);
    if (!data || index_ >= zlink_mesh_receive_batch_count (_handle))
        return {};
    size_t part_count = data[index_].part_count;
    if (part_count == 0)
        return {};
    std::vector<zlink_msg_t> native (part_count);
    size_t count_inout = part_count;
    const zlink_config_result_t rc = zlink_mesh_receive_batch_retain_message (
      _handle, index_, native.data (), &count_inout);
    if (rc != ZLINK_CONFIG_OK)
        return {};
    return zlink::detail::take_parts_from_native (native.data (), count_inout);
}

// --- reply ---

submit_result_t reply (const reply_token_t &token_,
                       const std::vector<message_t> &parts_,
                       send_flags_t flags_)
{
    const zlink_mesh_reply_token_t native_token =
      detail::dispatch_access_t::native_token (token_);
    const int rc = zlink::detail::submit_borrowed_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t part_count_) {
          return zlink_mesh_reply (&native_token, native_, part_count_,
                                   static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    if (rc == -1)
        return submit_result_t::invalid_argument;
    return static_cast<submit_result_t> (rc);
}

} // namespace service
} // namespace zlink
