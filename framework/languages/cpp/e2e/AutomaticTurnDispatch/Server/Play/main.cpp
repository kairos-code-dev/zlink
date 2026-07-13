/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "play_host_factory.hpp"

int main (int argc, char **argv)
{
    auto app = zlink::framework::e2e::automatic_turn_dispatch::server::play::create_play_host ();
    return app.run (argc, argv);
}
