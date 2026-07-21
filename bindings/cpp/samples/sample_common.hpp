/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SAMPLES_COMMON_SAMPLE_COMMON_HPP_INCLUDED
#define ZLINK_CPP_SAMPLES_COMMON_SAMPLE_COMMON_HPP_INCLUDED

#include <boost/asio.hpp>
#include <zlink.hpp>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(ZLINK_HAVE_WINDOWS)
#include <process.h>
#endif

namespace detail
{

inline const char *const k_pair_payload = "hello-pair";
inline const char *const k_dealer_router_request = "ping";
inline const char *const k_dealer_router_reply = "pong";
inline const char *const k_stream_payload = "hello-stream";
inline const char *const k_pubsub_topic = "prices";
inline const char *const k_pubsub_payload = "101.25";
inline const char *const k_spot_service = "sample";
inline const char *const k_spot_topic = "room:lobby";
inline const char *const k_spot_payload = "hello-spot";

inline std::string unique_tcp (const char *base_)
{
    boost::asio::io_context io;
    boost::asio::ip::tcp::acceptor acceptor (io);
    boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::make_address ("127.0.0.1"), 0);
    acceptor.open (endpoint.protocol ());
    acceptor.set_option (boost::asio::socket_base::reuse_address (true));
    acceptor.bind (endpoint);
    const unsigned short port = acceptor.local_endpoint ().port ();
    acceptor.close ();

    std::ostringstream stream;
    (void) base_;
    stream << "tcp://127.0.0.1:" << port;
    return stream.str ();
}

inline zlink::message_t make_message (const std::string &text_)
{
    return zlink::message_t::from (text_);
}

inline bool wait_until (std::condition_variable &cv_,
                        std::unique_lock<std::mutex> &lock_,
                        bool &ready_,
                        int timeout_ms_)
{
    return cv_.wait_for (lock_, std::chrono::milliseconds (timeout_ms_),
                         [&ready_] { return ready_; });
}

template <typename MonitorLike>
inline bool wait_for_monitor_readable (MonitorLike &monitor_, int timeout_ms_)
{
    zlink::poller_t poller;
    if (!poller.valid ())
        return false;
    try {
        poller.add (monitor_, zlink::poll_event_flag_t::pollin, 1);
    }
    catch (const zlink::binding_error_t &) {
        return false;
    }

    zlink::poll_event_t event;
    return poller.wait (&event, 1, std::chrono::milliseconds (timeout_ms_)) == 1;
}

inline bool wait_for_socket_monitor_event (zlink::socket_monitor_t &monitor_,
                                           uint64_t event_type_,
                                           int timeout_ms_,
                                           int64_t value_ = -1)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        const std::chrono::steady_clock::duration remaining =
          deadline - std::chrono::steady_clock::now ();
        const int remaining_ms = static_cast<int> (
          std::chrono::duration_cast<std::chrono::milliseconds> (remaining).count ());
        if (!wait_for_monitor_readable (monitor_, remaining_ms))
            continue;

        const std::optional<zlink::monitor_event_t> event =
          monitor_.recv (zlink::recv_flags_t::dontwait);
        if (!event)
            continue;
        if (static_cast<uint64_t> (event->event) != event_type_)
            continue;
        if (value_ >= 0 && static_cast<int64_t> (event->value) != value_)
            continue;
        return true;
    }

    return false;
}

inline bool wait_connected (zlink::socket_monitor_t &server_monitor_,
                            zlink::socket_monitor_t &client_monitor_,
                            int timeout_ms_ = 2000)
{
    return wait_for_socket_monitor_event (
             server_monitor_, static_cast<uint64_t> (zlink::monitor_event::connection_ready),
             timeout_ms_)
           && wait_for_socket_monitor_event (
             client_monitor_, static_cast<uint64_t> (zlink::monitor_event::connection_ready),
             timeout_ms_);
}

inline bool wait_stream_connected (zlink::socket_monitor_t &server_monitor_, int timeout_ms_ = 2000)
{
    return wait_for_socket_monitor_event (
      server_monitor_, static_cast<uint64_t> (zlink::monitor_event::accepted), timeout_ms_);
}

template <typename T> inline T wait_future (std::future<T> &future_, int timeout_ms_)
{
    assert (future_.wait_for (std::chrono::milliseconds (timeout_ms_))
            == std::future_status::ready);
    return future_.get ();
}

template <typename T, typename ProgressFn>
inline T wait_future_with_progress (std::future<T> &future_, int timeout_ms_, ProgressFn progress_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        if (future_.wait_for (std::chrono::milliseconds (0)) == std::future_status::ready)
            return future_.get ();

        progress_ ();
        if (future_.wait_for (std::chrono::milliseconds (1)) == std::future_status::ready)
            return future_.get ();
    }

    assert (false && "future did not become ready before timeout");
    return future_.get ();
}

inline bool
parse_tcp_endpoint (const std::string &endpoint_, std::string &host_, std::string &port_)
{
    char proto[8] = {0};
    char host[64] = {0};
    int port = 0;
    if (std::sscanf (endpoint_.c_str (), "%7[^:]://%63[^:]:%d", proto, host, &port) != 3)
        return false;

    if (std::strcmp (proto, "tcp") != 0 || port <= 0 || port > 65535)
        return false;

    host_ = host;
    std::ostringstream stream;
    stream << port;
    port_ = stream.str ();
    return true;
}

class raw_tcp_client_t
{
  public:
    explicit raw_tcp_client_t (const std::string &endpoint_) : _socket (_io)
    {
        std::string host;
        std::string port;
        assert (parse_tcp_endpoint (endpoint_, host, port));

        boost::asio::ip::tcp::resolver resolver (_io);
        boost::system::error_code ec;
        const boost::asio::ip::tcp::resolver::results_type endpoints =
          resolver.resolve (host, port, ec);
        assert (!ec);
        boost::asio::connect (_socket, endpoints, ec);
        assert (!ec);
    }

    ~raw_tcp_client_t ()
    {
        boost::system::error_code ec;
        _socket.close (ec);
    }

    void send_all (const char *data_, size_t size_)
    {
        boost::system::error_code ec;
        const size_t written = boost::asio::write (_socket, boost::asio::buffer (data_, size_), ec);
        assert (!ec);
        assert (written == size_);
    }

    int recv_exact (char *buffer_, size_t size_)
    {
        boost::system::error_code ec;
        const size_t received =
          boost::asio::read (_socket, boost::asio::buffer (buffer_, size_), ec);
        assert (!ec);
        assert (received == size_);
        return static_cast<int> (received);
    }

    void close ()
    {
        boost::system::error_code ec;
        _socket.close (ec);
    }

  private:
    boost::asio::io_context _io;
    boost::asio::ip::tcp::socket _socket;
};

//  --- RouteMesh 10.0.0 pull-dispatch helpers ---------------------------------
//
//  In the 10.0.0 model completions and inbound records are not pushed to a
//  callback: the application drives a pull loop
//    set_ready_handler → drain_ready(domains, ready_batch) → take_claim(i)
//    → claim.recv_batch(receive_batch) → receive_batch.at(i)/retain_message(i)
//  and replies to a request through the sealed reply token it carried.
//
//  These helpers wrap that loop so the guide samples can stay focused on the
//  concept each one demonstrates.

//  One record drained from a mesh node: its metadata (kind, source, reply
//  token, topic/channel, …) plus an owned copy of its payload parts.
struct mesh_record_t
{
    zlink::service::receive_record_t record;
    std::vector<zlink::message_t> parts;
    zlink::service::claim_t claim;

    std::string first_text () const
    {
        return parts.empty () ? std::string () : parts.front ().to_string ();
    }
};

//  One dispatch turn over a single claimed ready owner. It drains the *whole*
//  receive batch — every record, not just record 0 — retaining an owned copy of
//  each record's parts while the claim is still held so reply tokens stay valid,
//  The selected record retains the claim until its caller has handled any
//  reply token. This keeps the token lifetime rule in one place.
inline bool mesh_drain_claim (zlink::service::claim_t &claim_,
                              zlink::service::receive_batch_t &batch_,
                              std::vector<mesh_record_t> &out_records_)
{
    using namespace zlink::service;
    if (!claim_.valid ())
        return false;
    batch_.reset ();
    receive_requirements_t required;
    const int brc = claim_.recv_batch (batch_, required, zlink::recv_flags_t::none);
    if (brc != static_cast<int> (zlink::recv_result_t::ok))
        return false;
    const size_t count = batch_.count ();
    for (size_t i = 0; i < count; ++i) {
        mesh_record_t rec;
        rec.record = batch_.at (i);
        rec.parts = batch_.retain_message (i);
        out_records_.push_back (std::move (rec));
    }
    return true;
}

//  Drives the pull loop until a record whose owner is @p want_owner and whose
//  domain intersects @p want_domain arrives, then returns the first such record.
//  Every record in a claimed batch is drained and preserved during the turn (so
//  the whole batch is removed from the claim); the selected record retains the
//  claim until the caller finishes handling it. Returns false on
//  timeout. Application traffic (requests, sends, multicast) lands on the
//  application domain; completions of the caller's own requests land on the
//  infrastructure domain.
inline bool mesh_pull_one (zlink::service::mesh_node_t &node_,
                           zlink::service::owner_kind_t want_owner_,
                           zlink::service::ready_domain_t want_domain_,
                           mesh_record_t &out_,
                           int timeout_ms_ = 3000,
                           std::function<bool (const mesh_record_t &)> accept_ = {})
{
    using namespace zlink::service;
    ready_batch_t ready (16);
    receive_batch_t batch (32, 128, 1u << 20);
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        ready.reset ();
        bool has_residue = false;
        const int rc = node_.drain_ready (ready_domain_t::all, ready, has_residue,
                                          zlink::recv_flags_t::dontwait);
        if (rc != static_cast<int> (zlink::recv_result_t::ok)) {
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
            continue;
        }
        const size_t count = ready.count ();
        for (size_t i = 0; i < count; ++i) {
            const ready_record_t record = ready.at (i);
            if (record.owner_kind != want_owner_)
                continue;
            if ((record.domain & want_domain_) == ready_domain_t::none)
                continue;
            claim_t claim = ready.take_claim (i);
            std::vector<mesh_record_t> drained;
            const bool ok = mesh_drain_claim (claim, batch, drained);
            if (!ok || drained.empty ())
                continue;
            auto selected = accept_
                              ? std::find_if (drained.begin (), drained.end (), accept_)
                              : drained.begin ();
            if (selected == drained.end ())
                continue;
            // A reply token remains valid only while its claim is retained.
            // Transfer the claim with the selected record so a caller can
            // submit its reply before the record goes out of scope.
            selected->claim = std::move (claim);
            out_ = std::move (*selected);
            return true;
        }
    }
    return false;
}

//  Convenience: wrap a single text payload as a one-part message vector.
inline std::vector<zlink::message_t> make_parts (const std::string &text_)
{
    std::vector<zlink::message_t> parts;
    parts.push_back (zlink::message_t::from (text_));
    return parts;
}

//  Starts a single-node mesh with one channel and returns the started node's
//  own routing id (used for local self-addressing in the guide samples). Core
//  requires a routing id, a bind endpoint and at least one channel before
//  start(), so a unique per-node routing id is assigned here first.
inline zlink::routing_id_t
mesh_start_single_node (zlink::service::mesh_node_t &node_, const std::string &channel_name_)
{
    static std::atomic<uint64_t> counter{0};
    std::ostringstream rid;
    rid << "node-" << channel_name_ << '-' << counter.fetch_add (1u, std::memory_order_relaxed);
    node_.set_routing_id (zlink::routing_id_t::from (rid.str ()));
    node_.set_bind (unique_tcp ("mesh-node"));
    node_.add_channel_name (channel_name_);
    node_.start ();
    return node_.status ().routing_id ();
}

} // namespace detail

#endif
