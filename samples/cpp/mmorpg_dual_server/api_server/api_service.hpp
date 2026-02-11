/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include <string>

namespace sample {

class ApiService {
  public:
    explicit ApiService(const std::string &server_id);

    std::string handle_request(const std::string &op,
                               const std::string &req_id,
                               const std::string &payload);

  private:
    std::string server_id_;
};

} // namespace sample
