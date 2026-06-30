/* SPDX-License-Identifier: MPL-2.0 */

#include "session_host_factory.hpp"

int main (int argc, char **argv)
{
    auto app = zlink::framework::e2e::yield_dispatch::server::session::create_session_host ();
    return app.run (argc, argv);
}
