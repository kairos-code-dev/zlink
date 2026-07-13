/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "session_host_factory.hpp"

int main (int argc, char **argv)
{
    auto app = zlink::framework::e2e::automatic_turn_dispatch::server::session::create_session_host ();
    return app.run (argc, argv);
}
