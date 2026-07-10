/* SPDX-License-Identifier: MPL-2.0 */
/* Plain-TCP swarm client for the raw STREAM scenario.
 * Usage: cli_tcp_swarm <host> <port> <count>
 * Opens <count> plain TCP connections and writes one short payload on each so
 * the server-side STREAM socket registers the session and allocates its
 * per-connection state. Prints DONE, then waits for quit. */

#include "mem_common.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <vector>

int main (int argc, char **argv)
{
    if (argc < 4) {
        fprintf (stderr, "usage: %s <host> <port> <count>\n", argv[0]);
        return 1;
    }
    study::raise_nofile_limit ();
    const char *host = argv[1];
    const int port = atoi (argv[2]);
    const int count = atoi (argv[3]);

    struct sockaddr_in addr;
    memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons (static_cast<uint16_t> (port));
    if (inet_pton (AF_INET, host, &addr.sin_addr) != 1) {
        fprintf (stderr, "bad host: %s\n", host);
        return 1;
    }

    std::vector<int> fds;
    fds.reserve (count);
    for (int i = 0; i < count; ++i) {
        const int fd = socket (AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            perror ("socket");
            return 1;
        }
        if (connect (fd, reinterpret_cast<struct sockaddr *> (&addr), sizeof (addr)) != 0) {
            perror ("connect");
            return 1;
        }
        /* stream packet framing: u16 header_len (BE) + u32 body_len (BE) + payload */
        const unsigned char frame[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 'h', 'i'};
        if (write (fd, frame, sizeof (frame)) < 0)
            perror ("write");
        fds.push_back (fd);
        if ((i + 1) % 1000 == 0) {
            printf ("PROGRESS %d\n", i + 1);
            fflush (stdout);
        }
    }

    printf ("DONE %d\n", count);
    fflush (stdout);

    study::run_command_loop ({
      {"burst",
       [&] (const std::string &arg_) {
           int n = 1;
           int bytes = 64;
           sscanf (arg_.c_str (), "%d %d", &n, &bytes);
           std::vector<unsigned char> frame (6 + bytes, 'x');
           frame[0] = frame[1] = 0; /* header_len = 0 */
           frame[2] = static_cast<unsigned char> ((bytes >> 24) & 0xff);
           frame[3] = static_cast<unsigned char> ((bytes >> 16) & 0xff);
           frame[4] = static_cast<unsigned char> ((bytes >> 8) & 0xff);
           frame[5] = static_cast<unsigned char> (bytes & 0xff);
           for (int round = 0; round < n; ++round)
               for (const int fd : fds) {
                   size_t off = 0;
                   while (off < frame.size ()) {
                       const ssize_t written = write (fd, frame.data () + off, frame.size () - off);
                       if (written <= 0)
                           break;
                       off += static_cast<size_t> (written);
                   }
               }
           printf ("BURST_DONE %d %d\n", n, bytes);
           fflush (stdout);
       }},
    });

    for (const int fd : fds)
        close (fd);
    return 0;
}
