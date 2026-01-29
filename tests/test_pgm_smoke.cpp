/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <stdlib.h>
#include <string.h>
#include <vector>

#if !defined(ZMQ_HAVE_WINDOWS)
#include <ifaddrs.h>
#include <net/if.h>
#endif

void setUp ()
{
}

void tearDown ()
{
}

static bool has_prefix (const char *str_, const char *prefix_)
{
    return str_ && prefix_ && strncmp (str_, prefix_, strlen (prefix_)) == 0;
}

static void add_unique_addr (std::vector<std::string> &addrs_,
                             const char *addr_)
{
    for (size_t i = 0; i < addrs_.size (); ++i) {
        if (addrs_[i] == addr_)
            return;
    }
    addrs_.push_back (addr_);
}

static void collect_multicast_addrs (std::vector<std::string> &addrs_)
{
#if !defined(ZMQ_HAVE_WINDOWS)
    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs (&ifaddr) != 0)
        return;

    for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;
        if (!(ifa->ifa_flags & IFF_UP))
            continue;
        if (!(ifa->ifa_flags & IFF_MULTICAST))
            continue;
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        char addr[INET_ADDRSTRLEN];
        const struct sockaddr_in *sa =
          reinterpret_cast<struct sockaddr_in *> (ifa->ifa_addr);
        if (!inet_ntop (AF_INET, &sa->sin_addr, addr, sizeof (addr)))
            continue;

        add_unique_addr (addrs_, addr);
    }

    freeifaddrs (ifaddr);
#else
    (void) addrs_;
#endif
}

static bool try_pgm_endpoint (const char *endpoint_, bool *bound_out_)
{
    *bound_out_ = false;

    void *ctx = zmq_ctx_new ();
    if (!ctx)
        return false;

    void *pub = zmq_socket (ctx, ZMQ_PUB);
    if (!pub) {
        zmq_ctx_term (ctx);
        return false;
    }
    void *sub = zmq_socket (ctx, ZMQ_SUB);
    if (!sub) {
        close_zero_linger (pub);
        zmq_ctx_term (ctx);
        return false;
    }

    const int hwm = 10;
    zmq_setsockopt (pub, ZMQ_SNDHWM, &hwm, sizeof (hwm));
    zmq_setsockopt (sub, ZMQ_RCVHWM, &hwm, sizeof (hwm));
    zmq_setsockopt (sub, ZMQ_SUBSCRIBE, "", 0);

    if (zmq_bind (pub, endpoint_) != 0) {
        close_zero_linger (pub);
        close_zero_linger (sub);
        zmq_ctx_term (ctx);
        return false;
    }

    if (zmq_connect (sub, endpoint_) != 0) {
        close_zero_linger (pub);
        close_zero_linger (sub);
        zmq_ctx_term (ctx);
        return false;
    }
    *bound_out_ = true;

    msleep (SETTLE_TIME * 5);

    const char *payload = "PGM_SMOKE";
    const size_t payload_size = strlen (payload);
    bool received = false;

    for (int attempt = 0; attempt < 25 && !received; ++attempt) {
        const int rc = zmq_send (pub, payload, payload_size, ZMQ_DONTWAIT);
        if (rc < 0 && zmq_errno () != EAGAIN)
            break;

        zmq_pollitem_t items[] = {{sub, 0, ZMQ_POLLIN, 0}};
        const int poll_rc = zmq_poll (items, 1, 200);
        if (poll_rc < 0)
            break;

        if (items[0].revents & ZMQ_POLLIN) {
            char buffer[32];
            const int recv_rc = zmq_recv (sub, buffer, sizeof (buffer), 0);
            if (recv_rc >= 0) {
                if ((size_t) recv_rc == payload_size
                    && memcmp (buffer, payload, payload_size) == 0)
                    received = true;
            }
        }

        if (!received)
            msleep (50);
    }

    close_zero_linger (pub);
    close_zero_linger (sub);
    zmq_ctx_term (ctx);
    return received;
}

static void test_pgm_smoke_pub_sub ()
{
#if defined(ZMQ_HAVE_OPENPGM)
    std::vector<std::string> endpoints;
    const char *env_endpoint = getenv ("ZMQ_PGM_SMOKE_ENDPOINT");
    if (env_endpoint && env_endpoint[0] != '\0') {
        if (!has_prefix (env_endpoint, "pgm://")
            && !has_prefix (env_endpoint, "epgm://")) {
            TEST_FAIL_MESSAGE (
              "ZMQ_PGM_SMOKE_ENDPOINT must start with pgm:// or epgm://");
        }
        endpoints.push_back (env_endpoint);
    } else {
        std::vector<std::string> addrs;
        collect_multicast_addrs (addrs);
        for (size_t i = 0; i < addrs.size (); ++i) {
            endpoints.push_back (
              std::string ("epgm://") + addrs[i] + ";239.192.1.1:5555");
            endpoints.push_back (
              std::string ("pgm://") + addrs[i] + ";239.192.1.1:5555");
        }
    }

    bool any_bound = false;
    bool success = false;
    for (size_t i = 0; i < endpoints.size () && !success; ++i) {
        bool bound = false;
        const bool ok = try_pgm_endpoint (endpoints[i].c_str (), &bound);
        if (ok)
            success = true;
        if (bound)
            any_bound = true;
    }

    if (success)
        return;

    if (any_bound)
        TEST_FAIL_MESSAGE ("pgm message not received");

    TEST_IGNORE_MESSAGE ("no usable multicast interface for pgm test");
#else
    TEST_IGNORE_MESSAGE ("OpenPGM not enabled");
#endif
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_pgm_smoke_pub_sub);
    return UNITY_END ();
}
