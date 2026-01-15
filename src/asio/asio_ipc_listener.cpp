/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_IPC

#include "asio_ipc_listener.hpp"
#include "asio_poller.hpp"
#include "asio_zmtp_engine.hpp"
#include "ipc_transport.hpp"
#include "../address.hpp"
#include "../err.hpp"
#include "../io_thread.hpp"
#include "../ipc_address.hpp"
#include "../ip.hpp"
#include "../session_base.hpp"
#include "../socket_base.hpp"

#include <string.h>
#include <memory>

#ifndef ZMQ_HAVE_WINDOWS
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>
#endif

#ifdef ZMQ_HAVE_LOCAL_PEERCRED
#include <sys/types.h>
#include <sys/ucred.h>
#endif
#ifdef ZMQ_HAVE_SO_PEERCRED
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#if defined ZMQ_HAVE_OPENBSD
#define ucred sockpeercred
#endif
#endif

// Debug logging for ASIO IPC listener - set to 1 to enable
#define ASIO_IPC_LISTENER_DEBUG 0

#if ASIO_IPC_LISTENER_DEBUG
#include <cstdio>
#define IPC_LISTENER_DBG(fmt, ...)                                            \
    fprintf (stderr, "[ASIO_IPC_LISTENER] " fmt "\n", ##__VA_ARGS__)
#else
#define IPC_LISTENER_DBG(fmt, ...)
#endif

namespace
{
void cleanup_tmp_dir (std::string &tmp_dir_)
{
    if (tmp_dir_.empty ())
        return;

    ::rmdir (tmp_dir_.c_str ());
    tmp_dir_.clear ();
}

boost::asio::local::stream_protocol::endpoint
make_ipc_endpoint (const zmq::ipc_address_t &addr_)
{
    boost::asio::local::stream_protocol::endpoint endpoint;
    memcpy (endpoint.data (), addr_.addr (), addr_.addrlen ());
    endpoint.resize (addr_.addrlen ());
    return endpoint;
}
}

zmq::asio_ipc_listener_t::asio_ipc_listener_t (io_thread_t *io_thread_,
                                               socket_base_t *socket_,
                                               const options_t &options_) :
    own_t (io_thread_, options_),
    io_object_t (io_thread_),
    _io_context (io_thread_->get_io_context ()),
    _acceptor (_io_context),
    _accept_socket (_io_context),
    _socket (socket_),
    _accepting (false),
    _terminating (false),
    _linger (0),
    _has_file (false)
{
    IPC_LISTENER_DBG ("Constructor called, this=%p",
                      static_cast<void *> (this));
}

zmq::asio_ipc_listener_t::~asio_ipc_listener_t ()
{
    IPC_LISTENER_DBG ("Destructor called, this=%p",
                      static_cast<void *> (this));
}

int zmq::asio_ipc_listener_t::set_local_address (const char *addr_)
{
    IPC_LISTENER_DBG ("set_local_address: addr=%s", addr_);

    std::string addr (addr_);

    if (options.use_fd == -1 && !addr.empty () && addr[0] == '*') {
        if (create_ipc_wildcard_address (_tmp_socket_dirname, addr) < 0)
            return -1;
    }

    if (options.use_fd == -1) {
        ::unlink (addr.c_str ());
    }
    _filename.clear ();

    ipc_address_t address;
    int rc = address.resolve (addr.c_str ());
    if (rc != 0) {
        const int tmp_errno = errno;
        cleanup_tmp_dir (_tmp_socket_dirname);
        errno = tmp_errno;
        return -1;
    }

    std::string resolved_endpoint;
    address.to_string (resolved_endpoint);

    boost::system::error_code ec;
    if (options.use_fd != -1) {
        _acceptor.assign (boost::asio::local::stream_protocol (),
                          options.use_fd, ec);
        if (ec) {
            const int tmp_errno = ec.value ();
            cleanup_tmp_dir (_tmp_socket_dirname);
            errno = tmp_errno;
            return -1;
        }
    } else {
        _acceptor.open (boost::asio::local::stream_protocol (), ec);
        if (ec) {
            const int tmp_errno = ec.value ();
            cleanup_tmp_dir (_tmp_socket_dirname);
            errno = tmp_errno;
            return -1;
        }

        boost::asio::local::stream_protocol::endpoint bind_endpoint =
          make_ipc_endpoint (address);

        _acceptor.bind (bind_endpoint, ec);
        if (ec) {
            const int tmp_errno = ec.value ();
            _acceptor.close (ec);
            cleanup_tmp_dir (_tmp_socket_dirname);
            errno = tmp_errno;
            return -1;
        }

        _filename = addr;
        _has_file = true;

        _acceptor.listen (options.backlog, ec);
        if (ec) {
            const int tmp_errno = ec.value ();
            close ();
            errno = tmp_errno;
            return -1;
        }
    }

    if (_filename.empty ()) {
        _filename = addr;
        _has_file = true;
    }

    _endpoint =
      get_socket_name<ipc_address_t> (_acceptor.native_handle (),
                                      socket_end_local);
    if (_endpoint.empty ())
        _endpoint = resolved_endpoint;

    _socket->event_listening (make_unconnected_bind_endpoint_pair (_endpoint),
                              _acceptor.native_handle ());

    return 0;
}

int zmq::asio_ipc_listener_t::get_local_address (std::string &addr_) const
{
    addr_ = _endpoint;
    return addr_.empty () ? -1 : 0;
}

void zmq::asio_ipc_listener_t::process_plug ()
{
    IPC_LISTENER_DBG ("process_plug called");
    start_accept ();
}

void zmq::asio_ipc_listener_t::process_term (int linger_)
{
    IPC_LISTENER_DBG ("process_term called, linger=%d, accepting=%d", linger_,
                      _accepting);

    _terminating = true;
    _linger = linger_;

    close ();

    if (_accepting) {
        _io_context.poll ();
    }

    own_t::process_term (linger_);
}

void zmq::asio_ipc_listener_t::start_accept ()
{
    if (_accepting || !_acceptor.is_open ())
        return;

    _accepting = true;
    IPC_LISTENER_DBG ("start_accept: starting async_accept");

    _acceptor.async_accept (
      _accept_socket,
      [this] (const boost::system::error_code &ec) { on_accept (ec); });
}

void zmq::asio_ipc_listener_t::on_accept (const boost::system::error_code &ec)
{
    _accepting = false;
    IPC_LISTENER_DBG ("on_accept: ec=%s, terminating=%d", ec.message ().c_str (),
                      _terminating);

    if (_terminating) {
        if (!ec) {
            _accept_socket.close ();
        }
        return;
    }

    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;
        }

        _socket->event_accept_failed (
          make_unconnected_bind_endpoint_pair (_endpoint), ec.value ());

        _accept_socket =
          boost::asio::local::stream_protocol::socket (_io_context);
        start_accept ();
        return;
    }

    const fd_t fd = _accept_socket.native_handle ();

    if (!apply_accept_filters (fd)) {
        _accept_socket.close ();
        _accept_socket =
          boost::asio::local::stream_protocol::socket (_io_context);
        start_accept ();
        return;
    }

    _accept_socket.release ();

    create_engine (fd);

    _accept_socket =
      boost::asio::local::stream_protocol::socket (_io_context);
    start_accept ();
}

void zmq::asio_ipc_listener_t::create_engine (fd_t fd_)
{
    IPC_LISTENER_DBG ("create_engine: fd=%d", fd_);

    const endpoint_uri_pair_t endpoint_pair (
      get_socket_name<ipc_address_t> (fd_, socket_end_local),
      get_socket_name<ipc_address_t> (fd_, socket_end_remote),
      endpoint_type_bind);

    std::unique_ptr<i_asio_transport> transport (
      new (std::nothrow) ipc_transport_t ());
    alloc_assert (transport.get ());

    i_engine *engine = new (std::nothrow) asio_zmtp_engine_t (
      fd_, options, endpoint_pair, std::move (transport));
    alloc_assert (engine);

    io_thread_t *io_thread = choose_io_thread (options.affinity);
    zmq_assert (io_thread);

    session_base_t *session =
      session_base_t::create (io_thread, false, _socket, options, NULL);
    errno_assert (session);
    session->inc_seqnum ();
    launch_child (session);
    send_attach (session, engine, false);

    _socket->event_accepted (endpoint_pair, fd_);
}

void zmq::asio_ipc_listener_t::close ()
{
    if (!_acceptor.is_open ())
        return;

    const fd_t fd_for_event = _acceptor.native_handle ();
    boost::system::error_code ec;
    _acceptor.close (ec);

    if (_has_file && options.use_fd == -1) {
        int rc = 0;
        if (!_tmp_socket_dirname.empty ()) {
            rc = ::unlink (_filename.c_str ());
            if (rc == 0) {
                rc = ::rmdir (_tmp_socket_dirname.c_str ());
                _tmp_socket_dirname.clear ();
            }
        }

        if (rc != 0) {
            _socket->event_close_failed (
              make_unconnected_bind_endpoint_pair (_endpoint), zmq_errno ());
            return;
        }
    }

    _socket->event_closed (make_unconnected_bind_endpoint_pair (_endpoint),
                           fd_for_event);
}

bool zmq::asio_ipc_listener_t::apply_accept_filters (fd_t fd_)
{
    make_socket_noninheritable (fd_);

#if defined ZMQ_HAVE_SO_PEERCRED || defined ZMQ_HAVE_LOCAL_PEERCRED
    if (!filter (fd_))
        return false;
#endif

    if (zmq::set_nosigpipe (fd_))
        return false;

    return true;
}

#if defined ZMQ_HAVE_SO_PEERCRED

bool zmq::asio_ipc_listener_t::filter (fd_t sock_)
{
    if (options.ipc_uid_accept_filters.empty ()
        && options.ipc_pid_accept_filters.empty ()
        && options.ipc_gid_accept_filters.empty ())
        return true;

    struct ucred cred;
    socklen_t size = sizeof (cred);

    if (getsockopt (sock_, SOL_SOCKET, SO_PEERCRED, &cred, &size))
        return false;
    if (options.ipc_uid_accept_filters.find (cred.uid)
          != options.ipc_uid_accept_filters.end ()
        || options.ipc_gid_accept_filters.find (cred.gid)
             != options.ipc_gid_accept_filters.end ()
        || options.ipc_pid_accept_filters.find (cred.pid)
             != options.ipc_pid_accept_filters.end ())
        return true;

    const struct passwd *pw;
    const struct group *gr;

    if (!(pw = getpwuid (cred.uid)))
        return false;
    for (options_t::ipc_gid_accept_filters_t::const_iterator
           it = options.ipc_gid_accept_filters.begin (),
           end = options.ipc_gid_accept_filters.end ();
         it != end; it++) {
        if (!(gr = getgrgid (*it)))
            continue;
        for (char **mem = gr->gr_mem; *mem; mem++) {
            if (!strcmp (*mem, pw->pw_name))
                return true;
        }
    }
    return false;
}

#elif defined ZMQ_HAVE_LOCAL_PEERCRED

bool zmq::asio_ipc_listener_t::filter (fd_t sock_)
{
    if (options.ipc_uid_accept_filters.empty ()
        && options.ipc_gid_accept_filters.empty ())
        return true;

    struct xucred cred;
    socklen_t size = sizeof (cred);

    if (getsockopt (sock_, 0, LOCAL_PEERCRED, &cred, &size))
        return false;
    if (cred.cr_version != XUCRED_VERSION)
        return false;
    if (options.ipc_uid_accept_filters.find (cred.cr_uid)
        != options.ipc_uid_accept_filters.end ())
        return true;
    for (int i = 0; i < cred.cr_ngroups; i++) {
        if (options.ipc_gid_accept_filters.find (cred.cr_groups[i])
            != options.ipc_gid_accept_filters.end ())
            return true;
    }

    return false;
}

#endif

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO && ZMQ_HAVE_IPC
