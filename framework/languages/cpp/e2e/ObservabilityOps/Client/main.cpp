/* SPDX-License-Identifier: MPL-2.0 */

#include "Scenarios/obs_a1_scenario.hpp"
#include "Support/client_runner.hpp"

int main (int argc, char **argv)
{
    return zlink::framework::e2e::observability_ops::client::run (argc, argv);
}
