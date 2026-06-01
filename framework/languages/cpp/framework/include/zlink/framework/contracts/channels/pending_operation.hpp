/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

namespace zlink::framework
{

class pending_operation_t
{
public:
  bool valid () const noexcept { return _valid; }

private:
  bool _valid = true;
};

} // namespace zlink::framework
