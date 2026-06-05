/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/http_client.hpp>

#include "runtime/http_client_runtime.hpp"

#include <algorithm>
#include <stdexcept>

namespace zlink::http_client
{
namespace
{

bool is_blank (const std::string &value)
{
    return value.empty () || std::all_of (value.begin (), value.end (), [] (char ch) {
               return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
           });
}

void require_non_blank (const std::string &value, const char *message)
{
    if (is_blank (value)) {
        throw zlink::framework::framework_exception_t (zlink::framework::framework_error_kind_t::request_protocol_error,
                                                       message);
    }
}

void require_positive_timeout (std::chrono::milliseconds value)
{
    if (value <= std::chrono::milliseconds::zero ()) {
        throw zlink::framework::framework_exception_t (zlink::framework::framework_error_kind_t::request_protocol_error,
                                                       "HTTP client timeout must be greater than zero");
    }
}

} // namespace

client_builder_t client_t::create ()
{
    return {};
}

client_t::client_t (std::shared_ptr<detail::http_client_runtime_t> runtime) : _runtime (std::move (runtime))
{
}

request_builder_t client_t::get (std::string path) const
{
    return request_builder_t (*this, http_method_t::get, std::move (path));
}

request_builder_t client_t::post (std::string path) const
{
    return request_builder_t (*this, http_method_t::post, std::move (path));
}

request_builder_t client_t::put (std::string path) const
{
    return request_builder_t (*this, http_method_t::put, std::move (path));
}

request_builder_t client_t::delete_ (std::string path) const
{
    return request_builder_t (*this, http_method_t::delete_, std::move (path));
}

client_builder_t &client_builder_t::base_url (std::string value)
{
    require_non_blank (value, "HTTP client base_url is required");
    _base_url = std::move (value);
    return *this;
}

client_builder_t &client_builder_t::json ()
{
    _json = true;
    _headers.try_emplace ("content-type", "application/json");
    return *this;
}

client_builder_t &client_builder_t::timeout (std::chrono::milliseconds value)
{
    require_positive_timeout (value);
    _timeout = value;
    return *this;
}

client_builder_t &client_builder_t::default_header (std::string name, std::string value)
{
    require_non_blank (name, "HTTP client default header name is required");
    _headers[std::move (name)] = std::move (value);
    return *this;
}

client_builder_t &client_builder_t::trust_certificate_file (std::string path)
{
    require_non_blank (path, "HTTP client trust certificate file is required");
    _trust_certificate_file = std::move (path);
    return *this;
}

client_t client_builder_t::build () const
{
    require_non_blank (_base_url, "HTTP client base_url is required");
    require_positive_timeout (_timeout);
    detail::http_client_options_t options{.base_url = _base_url,
                                          .json = _json,
                                          .timeout = _timeout,
                                          .headers = _headers,
                                          .trust_certificate_file = _trust_certificate_file};
    try {
        return client_t (std::make_shared<detail::http_client_runtime_t> (std::move (options)));
    }
    catch (const std::invalid_argument &error) {
        throw zlink::framework::framework_exception_t (zlink::framework::framework_error_kind_t::request_protocol_error,
                                                       error.what ());
    }
}

request_builder_t::request_builder_t (const client_t &client, http_method_t method, std::string path) :
    _client (&client), _method (method), _path (std::move (path))
{
    if (_path.empty () || _path.front () != '/') {
        throw zlink::framework::framework_exception_t (zlink::framework::framework_error_kind_t::request_protocol_error,
                                                       "HTTP request path must start with /");
    }
}

request_builder_t &request_builder_t::header (std::string name, std::string value)
{
    require_non_blank (name, "HTTP request header name is required");
    _headers[std::move (name)] = std::move (value);
    return *this;
}

zlink::framework::task_t<raw_http_response_t> request_builder_t::submit_raw () const
{
    if (!_client || !_client->_runtime) {
        return zlink::framework::task_t<raw_http_response_t> (zlink::framework::result_t<raw_http_response_t>::failure (
          zlink::framework::framework_error_kind_t::closed, "HTTP client is not initialized"));
    }

    detail::http_request_t request{.method = _method, .path = _path, .body = _body, .headers = _headers};
    return zlink::framework::task_t<raw_http_response_t> (_client->_runtime->execute (request));
}

} // namespace zlink::http_client
