#include "test_helpers.hpp"

#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace {

void free_msg_buf (void *data, void *)
{
    std::free (data);
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
    int (socket_type::*peer_count_fn) () const = &socket_type::peer_count;
    int (socket_type::*peers_fn) (zlink_peer_info_t *, size_t *) const =
      &socket_type::peers;
    int (socket_type::*stream_send_msg_fn) (const zlink_routing_id_t &,
                                            zlink::message_t &,
                                            zlink::send_flag) =
      &socket_type::stream_send_msg;
    (void) peer_info_fn;
    (void) peer_count_fn;
    (void) peers_fn;
    (void) stream_send_msg_fn;

    return 0;
}
