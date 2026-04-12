/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <condition_variable>
#include <mutex>
#include <type_traits>
#include <utility>

#include <future>

namespace {

static_assert (!std::is_constructible<zlink::monitor_handle_t, void *>::value,
               "monitor_handle_t must not expose a raw void* constructor");
static_assert (!std::is_constructible<zlink::service_monitor_handle_t, void *>::value,
               "service_monitor_handle_t must not expose a raw void* constructor");
static_assert (!std::is_constructible<zlink::monitor_handle_t, const zlink::base_socket_t &, zlink::monitor_event>::value,
               "monitor_handle_t must not expose a public direct constructor");

template<typename T> class has_open_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (U::open (std::declval<const zlink::base_socket_t &> (),
                            zlink::monitor_event::all),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

static_assert (has_open_t<zlink::monitor_handle_t>::value,
               "monitor_handle_t must expose open");

template<typename T> class has_on_event_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().on_event (
                      static_cast<zlink::monitor_event_handler_fn> (NULL),
                      static_cast<void *> (NULL)),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

static_assert (has_on_event_t<zlink::monitor_handle_t>::value,
               "monitor_handle_t must expose on_event");

struct monitor_callback_state_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false;
    zlink::monitor_event_t event;
};

void socket_monitor_callback (const zlink::monitor_event_t *event_,
                              void *userdata_)
{
    auto *state = static_cast<monitor_callback_state_t *> (userdata_);
    assert (state != NULL);
    assert (event_ != NULL);
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        state->event = *event_;
        state->ready = true;
    }
    state->cv.notify_one ();
}

bool wait_for_any_socket_monitor_event (zlink::monitor_handle_t &monitor_,
                                       int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        const std::chrono::steady_clock::duration remaining =
          deadline - std::chrono::steady_clock::now ();
        const int remaining_ms = static_cast<int> (
          std::chrono::duration_cast<std::chrono::milliseconds> (remaining)
            .count ());
        if (!zlink_cpp_contract::wait_for_monitor_readable (
              monitor_.handle (), remaining_ms)) {
            continue;
        }

        if (monitor_.recv (zlink::non_blocking_t {}))
            return true;
    }

    return false;
}

void test_socket_monitor_open_recv_snapshot ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);

    zlink::monitor_handle_t monitor = server.monitor_handle ();
    assert (monitor.valid ());

    assert (server.bind ("tcp://127.0.0.1:*") == 0);
    std::string endpoint;
    assert (server.get_option (zlink::socket_options::last_endpoint, endpoint)
            == 0);
    assert (!endpoint.empty ());
    assert (client.connect (endpoint) == 0);

    (void) monitor.recv (zlink::non_blocking_t {});
    assert (wait_for_any_socket_monitor_event (monitor, 2000));
    const zlink::monitor_snapshot_t snapshot = monitor.snapshot ();
    assert (snapshot.ready () || snapshot.closed ());
}

void test_socket_monitor_on_event_callback ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);

    zlink::monitor_handle_t monitor = server.monitor_handle ();
    assert (monitor.valid ());

    monitor_callback_state_t callback_state;
    monitor.on_event (&socket_monitor_callback, &callback_state);

    assert (server.bind ("tcp://127.0.0.1:*") == 0);
    std::string endpoint;
    assert (server.get_option (zlink::socket_options::last_endpoint, endpoint)
            == 0);
    assert (!endpoint.empty ());
    assert (client.connect (endpoint) == 0);

    {
        std::unique_lock<std::mutex> lock (callback_state.mutex);
        assert (callback_state.cv.wait_for (
          lock, std::chrono::seconds (2), [&callback_state] {
              return callback_state.ready;
          }));
    }
    assert (static_cast<uint64_t> (callback_state.event.event)
            == static_cast<uint64_t> (
              zlink::monitor_event::connection_ready_changed));

    const zlink::monitor_snapshot_t snapshot = monitor.snapshot ();
    assert (snapshot.ready () || snapshot.closed ());
}

void test_discovery_service_monitor_open_recv ()
{
    zlink::context_t ctx;
    zlink::service::discovery_t discovery (
      ctx, zlink::service_type::spot, "monitor-contract");
    assert (discovery.valid ());

    zlink::service_monitor_handle_t monitor = discovery.monitor_open (
      zlink::service_monitor_event::discovery_service_up);
    assert (monitor.valid ());
    monitor.on_event (static_cast<zlink::service_event_handler_fn> (NULL), NULL);
    (void) monitor.recv (zlink::non_blocking_t {});
}

} // namespace

int main ()
{
    test_socket_monitor_open_recv_snapshot ();
    test_socket_monitor_on_event_callback ();
    test_discovery_service_monitor_open_recv ();
    return 0;
}
