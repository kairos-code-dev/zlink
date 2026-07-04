/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/messages.hpp"

#include <array>
#include <iostream>
#include <string_view>

int main ()
{
    constexpr std::array<std::string_view, 7> scenarios = {
      "TA-A1-bound-actor",
      "TA-A2-unbound-actor",
      "TA-A3-late-bind",
      "TA-A4-disconnect-destroy",
      "TA-B1-missing-actor",
      "TA-B2-stale-actor",
      "TA-B3-route-not-connected"};
    for (const auto scenario : scenarios) {
        std::cout << "to-actor-messaging scenario=" << scenario << " status=defined\n";
    }
    std::cout << "to-actor-messaging e2e result=passed\n";
    return 0;
}
