/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/http_client/contracts/client.hpp>

#include <chrono>
#include <map>
#include <optional>
#include <string>

namespace zlink::http_client::detail
{

struct http_client_options_t
{
    std::string base_url;
    bool json = false;
    std::chrono::milliseconds timeout{3000};
    std::map<std::string, std::string> headers;
    std::optional<std::string> trust_certificate_file;
};

struct http_request_t
{
    http_method_t method;
    std::string path;
    std::optional<std::string> body;
    std::map<std::string, std::string> headers;
};

class http_client_runtime_t
{
  public:
    explicit http_client_runtime_t (http_client_options_t options);

    zlink::framework::result_t<raw_http_response_t> execute (const http_request_t &request) const;

  private:
    http_client_options_t _options;
};

} // namespace zlink::http_client::detail
