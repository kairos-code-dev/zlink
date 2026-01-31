/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_CTX_HPP_INCLUDED__
#define __ZLINK_CTX_HPP_INCLUDED__

#include <map>
#include <vector>
#include <string>
#include <stdarg.h>

#include "core/mailbox.hpp"
#include "utils/array.hpp"
#include "utils/config.hpp"
#include "utils/mutex.hpp"
#include "utils/stdint.hpp"
#include "core/options.hpp"
#include "utils/atomic_counter.hpp"
#include "core/thread.hpp"

namespace zlink
{
class object_t;
class io_thread_t;
class socket_base_t;
class reaper_t;
class pipe_t;

//  Information associated with inproc endpoint. Note that endpoint options
//  are registered as well so that the peer can access them without a need
//  for synchronisation, handshaking or similar.
struct endpoint_t
{
    socket_base_t *socket;
    options_t options;
};

class thread_ctx_t
{
  public:
    thread_ctx_t ();

    //  Start a new thread with proper scheduling parameters.
    void start_thread (thread_t &thread_,
                       thread_fn *tfn_,
                       void *arg_,
                       const char *name_ = NULL) const;

    int set (int option_, const void *optval_, size_t optvallen_);
    int get (int option_, void *optval_, const size_t *optvallen_);

  protected:
    //  Synchronisation of access to context options.
    mutex_t _opt_sync;

  private:
    //  Thread parameters.
    int _thread_priority;
    int _thread_sched_policy;
    std::set<int> _thread_affinity_cpus;
    std::string _thread_name_prefix;
};

//  Context object encapsulates all the global state associated with
//  the library.

class ctx_t ZLINK_FINAL : public thread_ctx_t
{
  public:
    //  Create the context object.
    ctx_t ();

    //  Returns false if object is not a context.
    bool check_tag () const;

    //  This function is called when user invokes zlink_ctx_term. If there are
    //  no more sockets open it'll cause all the infrastructure to be shut
    //  down. If there are open sockets still, the deallocation happens
    //  after the last one is closed.
    int terminate ();

    // This function starts the terminate process by unblocking any blocking
    // operations currently in progress and stopping any more socket activity
    // (except zlink_close).
    // This function is non-blocking.
    // terminate must still be called afterwards.
    // This function is optional, terminate will unblock any current
    // operations as well.
    int shutdown ();

    //  Set and get context properties.
    int set (int option_, const void *optval_, size_t optvallen_);
    int get (int option_, void *optval_, const size_t *optvallen_);
    int get (int option_);

    //  Create and destroy a socket.
    zlink::socket_base_t *create_socket (int type_);
    void destroy_socket (zlink::socket_base_t *socket_);

    //  Send command to the destination thread.
    void send_command (uint32_t tid_, const command_t &command_);

    //  Returns the I/O thread that is the least busy at the moment.
    //  Affinity specifies which I/O threads are eligible (0 = all).
    //  Returns NULL if no I/O thread is available.
    zlink::io_thread_t *choose_io_thread (uint64_t affinity_);

    //  Returns reaper thread object.
    zlink::object_t *get_reaper () const;

    //  Management of inproc endpoints.
    int register_endpoint (const char *addr_, const endpoint_t &endpoint_);
    int unregister_endpoint (const std::string &addr_,
                             const socket_base_t *socket_);
    void unregister_endpoints (const zlink::socket_base_t *socket_);
    endpoint_t find_endpoint (const char *addr_);
    void pend_connection (const std::string &addr_,
                          const endpoint_t &endpoint_,
                          pipe_t **pipes_);
    void connect_pending (const char *addr_, zlink::socket_base_t *bind_socket_);


    enum
    {
        term_tid = 0,
        reaper_tid = 1
    };

    ~ctx_t ();

    bool valid () const;

  private:
    bool start ();

    struct pending_connection_t
    {
        endpoint_t endpoint;
        pipe_t *connect_pipe;
        pipe_t *bind_pipe;
    };

    //  Used to check whether the object is a context.
    uint32_t _tag;

    //  Sockets belonging to this context. We need the list so that
    //  we can notify the sockets when zlink_ctx_term() is called.
    //  The sockets will return ETERM then.
    typedef array_t<socket_base_t> sockets_t;
    sockets_t _sockets;

    //  List of unused thread slots.
    typedef std::vector<uint32_t> empty_slots_t;
    empty_slots_t _empty_slots;

    //  If true, zlink_ctx_new has been called but no socket has been created
    //  yet. Launching of I/O threads is delayed.
    bool _starting;

    //  If true, zlink_ctx_term was already called.
    bool _terminating;

    //  Synchronisation of accesses to global slot-related data:
    //  sockets, empty_slots, terminating. It also synchronises
    //  access to zombie sockets as such (as opposed to slots) and provides
    //  a memory barrier to ensure that all CPU cores see the same data.
    mutex_t _slot_sync;

    //  The reaper thread.
    zlink::reaper_t *_reaper;

    //  I/O threads.
    typedef std::vector<zlink::io_thread_t *> io_threads_t;
    io_threads_t _io_threads;

    //  Array of pointers to mailboxes for both application and I/O threads.
    std::vector<i_mailbox *> _slots;

    //  Mailbox for zlink_ctx_term thread.
    mailbox_t _term_mailbox;

    //  List of inproc endpoints within this context.
    typedef std::map<std::string, endpoint_t> endpoints_t;
    endpoints_t _endpoints;

    // List of inproc connection endpoints pending a bind
    typedef std::multimap<std::string, pending_connection_t>
      pending_connections_t;
    pending_connections_t _pending_connections;

    //  Synchronisation of access to the list of inproc endpoints.
    mutex_t _endpoints_sync;

    //  Maximum socket ID.
    static atomic_counter_t max_socket_id;

    //  Maximum number of sockets that can be opened at the same time.
    int _max_sockets;

    //  Maximum allowed message size
    int _max_msgsz;

    //  Number of I/O threads to launch.
    int _io_thread_count;

    //  Does context wait (possibly forever) on termination?
    bool _blocky;

    //  Is IPv6 enabled on this context?
    bool _ipv6;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (ctx_t)

#ifdef HAVE_FORK
    // the process that created this context. Used to detect forking.
    pid_t _pid;
#endif
    enum side
    {
        connect_side,
        bind_side
    };

    static void
    connect_inproc_sockets (zlink::socket_base_t *bind_socket_,
                            const options_t &bind_options_,
                            const pending_connection_t &pending_connection_,
                            side side_);

};
}

#endif
