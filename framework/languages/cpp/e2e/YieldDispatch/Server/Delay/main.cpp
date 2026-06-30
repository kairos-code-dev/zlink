/* SPDX-License-Identifier: MPL-2.0 */

#include "delay_host_factory.hpp"

int main (int argc, char **argv)
{
    auto app = zlink::framework::e2e::yield_dispatch::server::delay::create_delay_host ();
    return app.run (argc, argv);
}
