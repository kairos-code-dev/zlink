/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/stateful/maintenance_runtime.hpp"

#include <zlink/framework/contracts/locations/maintenance_stores.hpp>

#include <memory>
#include <stdexcept>

namespace zlink::framework::runtime::stateful
{

class public_relocation_store_adapter_t final :
    public relocation_store_port_t
{
  public:
    explicit public_relocation_store_adapter_t (
      std::shared_ptr<zlink::framework::relocation_store_t> store) :
        _owner (std::move (store)), _store (_owner.get ())
    {
        if (!_store)
            throw std::invalid_argument (
              "relocation store must not be null");
    }

    explicit public_relocation_store_adapter_t (
      zlink::framework::relocation_store_t &store) noexcept :
        _store (&store)
    {
    }

    relocation_stored_t put (
      const std::vector<std::uint8_t> &payload,
      std::chrono::hours retention) override
    {
        std::vector<std::byte> public_payload;
        public_payload.reserve (payload.size ());
        for (const auto value : payload)
            public_payload.push_back (static_cast<std::byte> (value));
        const auto stored =
          _store
            ->put_relocation (
              std::move (public_payload), retention)
            .result ()
            .value ();
        return {stored.reference, stored.checksum_crc32c};
    }

    std::optional<std::vector<std::uint8_t>>
    get (const std::string &reference) override
    {
        const auto read =
          _store->get_relocation (reference).result ().value ();
        const auto *found =
          std::get_if<zlink::framework::relocation_found_t> (&read);
        if (!found)
            return std::nullopt;
        std::vector<std::uint8_t> payload;
        payload.reserve (found->payload.size ());
        for (const auto value : found->payload)
            payload.push_back (std::to_integer<std::uint8_t> (value));
        return payload;
    }

    void remove (const std::string &reference) override
    {
        (void) _store->delete_relocation (reference).result ().value ();
    }

  private:
    std::shared_ptr<zlink::framework::relocation_store_t> _owner;
    zlink::framework::relocation_store_t *_store;
};

} // namespace zlink::framework::runtime::stateful
