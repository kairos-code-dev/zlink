/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_QUERY_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_QUERY_HPP_INCLUDED

#include "../context.hpp"
#include "../types.hpp"

#include <cerrno>

namespace zlink
{
namespace service
{

class registry_query_client_t
{
  public:
    explicit registry_query_client_t (context_t &ctx_)
        : _client (zlink_registry_query_client_new (ctx_.handle ())),
          _last_error (0)
    {
        if (!_client)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    ~registry_query_client_t () { (void) close (); }

    registry_query_client_t (registry_query_client_t &&other) noexcept
        : _client (other._client), _last_error (other._last_error)
    {
        other._client = NULL;
        other._last_error = 0;
    }

    registry_query_client_t &operator= (registry_query_client_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        (void) close ();
        _client = other._client;
        _last_error = other._last_error;
        other._client = NULL;
        other._last_error = 0;
        return *this;
    }

    registry_query_client_t (const registry_query_client_t &) = delete;
    registry_query_client_t &
    operator= (const registry_query_client_t &) = delete;

    bool valid () const noexcept { return _client != NULL; }

    int last_error () const noexcept { return _last_error; }

    ZLINK_CPP_NODISCARD int connect (const std::string &endpoint_)
    {
        validate_bounded_c_string (endpoint_, 255u, "endpoint");
        return zlink_registry_query_client_connect (_client, endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int
    snapshot (zlink_registry_topology_entry_t *entries_,
              size_t *count_,
              const zlink_registry_topology_filter_t *filter_ = NULL) const
    {
        return zlink_registry_query_snapshot (_client, filter_, entries_, count_);
    }

    ZLINK_CPP_NODISCARD int close ()
    {
        if (!_client)
            return 0;

        void *tmp = _client;
        const int rc = zlink_registry_query_destroy (&tmp);
        if (rc == 0)
            _client = NULL;
        return rc;
    }

    void *handle () const { return _client; }

  private:
    void *_client;
    int _last_error;
};

} // namespace service
} // namespace zlink

#endif
