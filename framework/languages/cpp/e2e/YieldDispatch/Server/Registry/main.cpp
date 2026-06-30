/* SPDX-License-Identifier: MPL-2.0 */

#include "registry_host_factory.hpp"

int main (int argc, char **argv)
{
    auto app = zlink::framework::e2e::yield_dispatch::server::registry::create_registry_host ();
    return app.run (argc, argv);
}
