/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ASIO_IPC_LISTENER_HPP_INCLUDED__
#define __ZMQ_ASIO_IPC_LISTENER_HPP_INCLUDED__

#include "core/poller.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_IPC

#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <string>

#include "utils/fd.hpp"
#include "core/own.hpp"
#include "core/io_object.hpp"

namespace zmq
{
class io_thread_t;
class socket_base_t;

//  ASIO-based IPC listener using local stream sockets.
class asio_ipc_listener_t ZMQ_FINAL : public own_t, public io_object_t
{
  public:
    asio_ipc_listener_t (zmq::io_thread_t *io_thread_,
                         zmq::socket_base_t *socket_,
                         const options_t &options_);
    ~asio_ipc_listener_t ();

    //  Set address to listen on.
    int set_local_address (const char *addr_);

    //  Get last bound endpoint.
    int get_local_address (std::string &addr_) const;

  private:
    void process_plug () ZMQ_FINAL;
    void process_term (int linger_) ZMQ_OVERRIDE;

    void start_accept ();
    void on_accept (const boost::system::error_code &ec);

    void create_engine (fd_t fd_);
    void close ();

    bool apply_accept_filters (fd_t fd_);

#if defined ZMQ_HAVE_SO_PEERCRED || defined ZMQ_HAVE_LOCAL_PEERCRED
    bool filter (fd_t sock_);
#endif

    boost::asio::io_context &_io_context;
    boost::asio::local::stream_protocol::acceptor _acceptor;
    boost::asio::local::stream_protocol::socket _accept_socket;

    zmq::socket_base_t *const _socket;

    std::string _endpoint;
    bool _accepting;
    bool _terminating;
    int _linger;

    bool _has_file;
    std::string _tmp_socket_dirname;
    std::string _filename;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (asio_ipc_listener_t)
};
}  // namespace zmq

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_IPC

#endif  // __ZMQ_ASIO_IPC_LISTENER_HPP_INCLUDED__
