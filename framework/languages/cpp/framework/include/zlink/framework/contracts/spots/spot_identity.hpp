/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>
#include <string_view>

namespace zlink::framework
{

class node_rid_t
{
  public:
    node_rid_t () = default;
    explicit node_rid_t (std::string value);

    static node_rid_t from_string (std::string value);
    std::string_view value () const noexcept;
    bool empty () const noexcept;

  private:
    std::string _value;
};

class spot_rid_t
{
  public:
    spot_rid_t () = default;
    explicit spot_rid_t (std::string value);

    static spot_rid_t from_string (std::string value);
    std::string_view value () const noexcept;
    bool empty () const noexcept;

  private:
    std::string _value;
};

} // namespace zlink::framework
