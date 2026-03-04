#include "test_helpers.hpp"

#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>

namespace {

void free_msg_buf (void *data, void *)
{
    std::free (data);
}

void free_msg_buf_counted (void *data, void *hint)
{
    std::free (data);
    int *count = static_cast<int *> (hint);
    if (count)
        ++(*count);
}

static_assert (
  std::is_same<decltype (&zlink::timers_t::add),
               int (zlink::timers_t::*) (size_t, zlink_timer_fn, void *)>::value,
  "zlink::timers_t::add signature mismatch");

static_assert (
  std::is_same<decltype (&zlink::timers_t::cancel),
               int (zlink::timers_t::*) (int)>::value,
  "zlink::timers_t::cancel signature mismatch");

static_assert (
  std::is_same<decltype (&zlink::timers_t::set_interval),
               int (zlink::timers_t::*) (int, size_t)>::value,
  "zlink::timers_t::set_interval signature mismatch");

static_assert (
  std::is_same<decltype (&zlink::timers_t::reset),
               int (zlink::timers_t::*) (int)>::value,
  "zlink::timers_t::reset signature mismatch");

static_assert (
  std::is_same<decltype (&zlink::timers_t::timeout),
               long (zlink::timers_t::*) () const>::value,
  "zlink::timers_t::timeout signature mismatch");

static_assert (
  std::is_same<decltype (&zlink::timers_t::execute),
               int (zlink::timers_t::*) ()>::value,
  "zlink::timers_t::execute signature mismatch");

} // namespace

int main ()
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    zlink::version (&major, &minor, &patch);
    const zlink::version_info_t ver = zlink::version ();
    assert (ver.major == major);
    assert (ver.minor == minor);
    assert (ver.patch == patch);

    zlink::sleep (0);

    zlink::context_t ctx (1);
    int io_threads = 0;
    assert (ctx.get (zlink::context_option::io_threads, &io_threads) == 0);
    assert (io_threads >= 1);
    int thread_priority = 0;
    assert (ctx.get (zlink::context_option::thread_priority, &thread_priority)
            == 0);
    assert (thread_priority == ZLINK_THREAD_PRIORITY_DFLT);
    assert (ctx.shutdown () == 0);

    zlink::message_t msg;
    assert (msg.valid ());
    (void) msg.get (ZLINK_MORE);
    (void) msg.set (ZLINK_MORE, 0);
    (void) msg.gets ("Socket-Type");

    char *payload = static_cast<char *> (std::malloc (4));
    assert (payload != NULL);
    std::memcpy (payload, "ping", 4);

    zlink::message_t msg_data;
    assert (msg_data.init_data (payload, 4, &free_msg_buf, NULL) == 0);
    assert (msg_data.size () == 4);

    typedef zlink::socket_t socket_type;
    int (socket_type::*peer_info_fn) (const zlink_routing_id_t &,
                                      zlink_peer_info_t *) const =
      &socket_type::peer_info;
    int (socket_type::*peer_info_bytes_fn) (const void *,
                                            size_t,
                                            zlink_peer_info_t *) const =
      &socket_type::peer_info;
    int (socket_type::*peer_info_string_fn) (const std::string &,
                                             zlink_peer_info_t *) const =
      &socket_type::peer_info;
    int (socket_type::*peer_routing_id_fn) (int, std::string &) const =
      &socket_type::peer_routing_id;
    int (socket_type::*stream_peer_routing_id_fn) (int, uint32_t *) =
      &socket_type::stream_peer_routing_id;
    int (socket_type::*peer_count_fn) () const = &socket_type::peer_count;
    int (socket_type::*peers_fn) (zlink_peer_info_t *, size_t *) const =
      &socket_type::peers;
    int (socket_type::*stream_send_fn_u32) (uint32_t,
                                            const void *,
                                            size_t,
                                            zlink::send_flag) =
      &socket_type::stream_send;
    int (socket_type::*stream_send_msg_fn_u32) (uint32_t,
                                                zlink::message_t &,
                                                zlink::send_flag) =
      &socket_type::stream_send_msg;
    int (socket_type::*stream_send_fn_string_rid) (const std::string &,
                                                   const void *,
                                                   size_t,
                                                   zlink::send_flag) =
      &socket_type::stream_send;
    int (socket_type::*stream_send_msg_fn_string_rid) (const std::string &,
                                                       zlink::message_t &,
                                                       zlink::send_flag) =
      &socket_type::stream_send_msg;
    int (socket_type::*send_zero_fn) (void *,
                                      size_t,
                                      zlink_free_fn *,
                                      void *,
                                      zlink::send_flag) = &socket_type::send_zero;
    int (zlink::gateway_t::*send_zero_fn_gateway) (const char *,
                                                   void *,
                                                   size_t,
                                                   zlink_free_fn *,
                                                   void *,
                                                   zlink::send_flag) =
      &zlink::gateway_t::send_zero;
    int (zlink::gateway_t::*send_rid_zero_fn_gateway) (
      const char *,
      const zlink_routing_id_t &,
      void *,
      size_t,
      zlink_free_fn *,
      void *,
      zlink::send_flag) = &zlink::gateway_t::send_rid_zero;
    int (zlink::gateway_t::*send_rid_zero_fn_gateway_string) (
      const char *,
      const std::string &,
      void *,
      size_t,
      zlink_free_fn *,
      void *,
      zlink::send_flag) = &zlink::gateway_t::send_rid_zero;
    int (zlink::spot_t::*publish_zero_fn_spot) (const char *,
                                                void *,
                                                size_t,
                                                zlink_free_fn *,
                                                void *,
                                                zlink::send_flag) =
      &zlink::spot_t::publish_zero;
    int (zlink::gateway_t::*send_rid_fn_string) (const char *,
                                                 const std::string &,
                                                 std::vector<zlink::message_t> &,
                                                 zlink::send_flag) =
      &zlink::gateway_t::send_rid;
    int (zlink::gateway_t::*send_rid_bytes_fn_string) (
      const char *,
      const std::string &,
      const void *,
      size_t,
      zlink::send_flag) = &zlink::gateway_t::send_rid_bytes;
    (void) peer_info_fn;
    (void) peer_info_bytes_fn;
    (void) peer_info_string_fn;
    (void) peer_routing_id_fn;
    (void) stream_peer_routing_id_fn;
    (void) peer_count_fn;
    (void) peers_fn;
    (void) stream_send_fn_u32;
    (void) stream_send_msg_fn_u32;
    (void) stream_send_fn_string_rid;
    (void) stream_send_msg_fn_string_rid;
    (void) send_zero_fn;
    (void) send_zero_fn_gateway;
    (void) send_rid_zero_fn_gateway;
    (void) send_rid_zero_fn_gateway_string;
    (void) publish_zero_fn_spot;
    (void) send_rid_fn_string;
    (void) send_rid_bytes_fn_string;

    {
        zlink_routing_id_t rid;
        const std::string rid_bytes ("\x10\x00\x7f", 3);
        assert (zlink::routing_id_from_string (rid_bytes, &rid) == 0);
        assert (rid.size == rid_bytes.size ());
        assert (std::memcmp (rid.data, rid_bytes.data (), rid.size) == 0);
        assert (zlink::routing_id_to_string (rid) == rid_bytes);
        assert (zlink::routing_id_from_bytes (NULL, 1, &rid) == -1);
    }

    {
        zlink::socket_t invalid_socket;
        zlink::message_t retry_msg (4);
        std::memcpy (retry_msg.data (), "ping", 4);
        assert (invalid_socket.send (retry_msg, zlink::send_flag::dontwait)
                == -1);
        assert (retry_msg.valid ());
        assert (retry_msg.size () == 4);
    }

    {
        zlink::socket_t invalid_socket;
        int freed = 0;
        char *owned = static_cast<char *> (std::malloc (4));
        assert (owned != NULL);
        std::memcpy (owned, "pong", 4);
        assert (invalid_socket.send_zero (owned,
                                          4,
                                          &free_msg_buf_counted,
                                          &freed,
                                          zlink::send_flag::dontwait)
                == -1);
        assert (freed == 1);
    }

    {
        zlink::context_t ctx_local;
        zlink::discovery_t disc_local (ctx_local, zlink::service_type::gateway);
        zlink::gateway_t gateway_local (ctx_local, disc_local);
        zlink::gateway_t moved_gateway (std::move (gateway_local));
        (void) moved_gateway;

        int freed = 0;
        char *owned = static_cast<char *> (std::malloc (4));
        assert (owned != NULL);
        std::memcpy (owned, "gw00", 4);

        assert (gateway_local.send_zero ("svc",
                                         owned,
                                         4,
                                         &free_msg_buf_counted,
                                         &freed,
                                         zlink::send_flag::dontwait)
                == -1);
        assert (freed == 1);
    }

    {
        zlink::context_t ctx_local;
        zlink::spot_node_t node (ctx_local);
        zlink::spot_t spot (node);
        assert (spot.valid ());

        int freed = 0;
        char *owned = static_cast<char *> (std::malloc (4));
        assert (owned != NULL);
        std::memcpy (owned, "sp00", 4);

        assert (spot.publish_zero ("",
                                   owned,
                                   4,
                                   &free_msg_buf_counted,
                                   &freed,
                                   zlink::send_flag::none)
                == -1);
        assert (freed == 1);
    }

    zlink::stopwatch_t watch;
    (void) watch.intermediate ();
    (void) watch.stop ();
    zlink::stopwatch_t moved_watch;
    moved_watch = std::move (watch);

    return 0;
}
