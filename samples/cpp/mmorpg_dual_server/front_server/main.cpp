#include "front_server/front_server.hpp"
#include <cstdlib>
#include <ctime>
#include <unistd.h>

int main(int argc, char* argv[]) {
    // Seed rand() uniquely per process so that zlink internal random IDs
    // (e.g., spot_node routing IDs) don't collide across instances.
    std::srand(static_cast<unsigned>(std::time(nullptr)) * 100u
               + static_cast<unsigned>(getpid()));

    sample::FrontServer server;
    return server.run(argc, argv);
}
