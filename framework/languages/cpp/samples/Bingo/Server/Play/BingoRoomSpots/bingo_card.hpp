/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Contracts/messages.hpp"

#include <array>
#include <algorithm>

namespace zlink::samples::bingo
{

struct bingo_card_t
{
  std::array<int, 25> numbers {};

  bool contains (int number) const
  {
    return std::find (numbers.begin (), numbers.end (), number) !=
           numbers.end ();
  }
};

} // namespace zlink::samples::bingo
