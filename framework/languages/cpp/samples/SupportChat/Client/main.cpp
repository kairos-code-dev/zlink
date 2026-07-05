/* SPDX-License-Identifier: MPL-2.0 */

#include "supportchat_client_scenario.hpp"

#include <exception>
#include <iostream>

int main ()
{
    try {
        zlink::samples::supportchat::supportchat_client_scenario_t scenario;
        scenario.run ();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "supportchat client failed: " << error.what () << std::endl;
        return 1;
    }
}
