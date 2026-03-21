/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SOCKET_BASE_HPP_INCLUDED__
#define __ZLINK_SOCKET_BASE_HPP_INCLUDED__

#include <string>
#include <map>
#include <stdarg.h>
#include <atomic>
#include <deque>
#include <set>
#include <mutex>
#include <vector>

#include "core/own.hpp"
#include "utils/array.hpp"
#include "utils/blob.hpp"
#include "utils/stdint.hpp"
#include "core/poller.hpp"
#include "core/i_poll_events.hpp"
#include "core/i_mailbox.hpp"
#include "core/thread.hpp"
#include "utils/clock.hpp"
#include "core/pipe.hpp"
#include "core/endpoint.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/condition_variable.hpp"
#include "zlink.h"

extern "C" {
void zlink_free_event (void *data_, void *hint_);
}

typedef int (*zlink_stream_on_raw_fn) (const zlink_routing_id_t *rid_,
                                       zlink_msg_t *msg_);

namespace zlink
{
class ctx_t;
class msg_t;
class pipe_t;
class io_thread_t;
typedef void (*spot_sub_io_handler_fn) (const zlink_routing_id_t *source_rid_,
                                        const char *topic_,
                                        size_t topic_len_,
                                        zlink_msg_t *parts_,
                                        size_t part_count_,
                                        void *userdata_);
class socket_base_t : public own_t,
                      public array_item_t<>,
                      public i_poll_events,
                      public i_pipe_events
{
    friend class reaper_t;

  public:
    //  Returns false if object is not a socket.
    bool check_tag () const;

    //  Create a socket of a specified type.
    static socket_base_t *
    create (int type_, zlink::ctx_t *parent_, uint32_t tid_, int sid_);

    //  Returns the mailbox associated with this socket.
    i_mailbox *get_mailbox () const;

    //  Interrupt blocking call if the socket is stuck in one.
    //  This function can be called from a different thread!
    void stop ();

    //  Interface for communication with the API layer.
    int setsockopt (int option_, const void *optval_, size_t optvallen_);
    int getsockopt (int option_, void *optval_, size_t *optvallen_);
    int get_events (int events_, uint32_t *out_);
    int get_events_internal (int events_, uint32_t *out_);
    void set_all_pipes_nodelay ();
    int bind (const char *endpoint_uri_);
    int connect (const char *endpoint_uri_);
    int term_endpoint (const char *endpoint_uri_);
    int send (zlink::msg_t *msg_, int flags_);
    int recv (zlink::msg_t *msg_, int flags_);
    int close ();
    int socket_msg_dispatch_from_io (zlink::msg_t *msg_, zlink::pipe_t *pipe_);
    int socket_set_msg_handler (zlink_socket_msg_handler_fn handler_);
    int socket_set_msg_handler_ex (zlink_socket_msg_handler_fn handler_,
                                   void *subject_);
    int socket_set_msg_handler_with_userdata (
      zlink_socket_msg_handler_fn handler_, void *subject_, void *userdata_);
    int socket_msg_dispatch_stop ();
    int socket_set_spot_handler (zlink_subscribe_handler_fn handler_);
    int socket_set_spot_handler_with_userdata (
      zlink_subscribe_handler_fn handler_,
                                               void *userdata_);
    int socket_set_send_ready_handler (zlink_send_ready_handler_fn handler_);
    int socket_set_send_ready_handler_ex (zlink_send_ready_handler_fn handler_,
                                          void *subject_);
    int socket_set_send_ready_handler_with_userdata (
      zlink_send_ready_handler_fn handler_, void *subject_, void *userdata_);
    bool socket_msg_dispatch_active () const;
    bool send_ready_handler_active () const;
    static socket_base_t *current_socket_msg_dispatch_socket ();
    static socket_base_t *current_send_ready_dispatch_socket ();
    static zlink::pipe_t *current_socket_msg_dispatch_pipe ();
    static void *current_socket_msg_dispatch_subject ();
    static bool current_socket_msg_dispatch_source_rid (
      zlink_routing_id_t *out_);
    bool send_ready_dispatch_in_callback () const;
    void invoke_send_ready_handler_for_testing ();
    int stream_dispatch_msg_from_io (zlink::msg_t *msg_, zlink::pipe_t *pipe_);
    virtual int sub_dispatch_start (spot_sub_io_handler_fn callback_,
                                    void *userdata_);
    virtual int sub_dispatch_stop ();
    virtual bool sub_dispatch_active () const;
    virtual int xpub_dispatch_start ();
    virtual bool xpub_dispatch_active () const;
    virtual int stream_dispatch_start_raw (zlink_stream_on_raw_fn callback_);
    virtual int stream_set_msg_handler_with_userdata (
      zlink_socket_msg_handler_fn handler_, void *userdata_);
    virtual int stream_dispatch_stop ();
    virtual bool stream_dispatch_active () const;
    virtual bool stream_dispatch_in_callback () const;
    virtual uint32_t stream_dispatch_inflight () const;
    virtual int stream_dispatch_send_from_io (const zlink_routing_id_t *rid_,
                                              const void *data_,
                                              size_t size_,
                                              int flags_);
    virtual int stream_dispatch_send_msg_from_io (
      const zlink_routing_id_t *rid_,
      zlink::msg_t *msg_,
      int flags_);
    virtual std::recursive_mutex *api_sync_mutex ();

    //  These functions are used by the polling mechanism to determine
    //  which events are to be reported from this socket.
    bool has_in ();
    bool has_out ();

    //  Joining and leaving groups
    int join (const char *group_);
    int leave (const char *group_);

    //  Using this function reaper thread ask the socket to register with
    //  its poller.
    void start_reaping (poller_t *poller_);

    //  i_poll_events implementation. This interface is used when socket
    //  is handled by the poller in the reaper thread.
    void in_event () ZLINK_FINAL;
    void out_event () ZLINK_FINAL;
    void timer_event (int id_) ZLINK_FINAL;

    //  i_pipe_events interface implementation.
    void read_activated (pipe_t *pipe_) ZLINK_FINAL;
    void write_activated (pipe_t *pipe_) ZLINK_FINAL;
    void hiccuped (pipe_t *pipe_) ZLINK_FINAL;
    void pipe_terminated (pipe_t *pipe_) ZLINK_FINAL;

    int monitor (const char *endpoint_,
                 uint64_t events_,
                 int event_version_,
                 int type_);

    void event_connected (const endpoint_uri_pair_t &endpoint_uri_pair_,
                          zlink::fd_t fd_);
    void event_connect_delayed (const endpoint_uri_pair_t &endpoint_uri_pair_,
                                int err_);
    void event_connect_retried (const endpoint_uri_pair_t &endpoint_uri_pair_,
                                int interval_);
    void event_listening (const endpoint_uri_pair_t &endpoint_uri_pair_,
                          zlink::fd_t fd_);
    void event_bind_failed (const endpoint_uri_pair_t &endpoint_uri_pair_,
                            int err_);
    void event_accepted (const endpoint_uri_pair_t &endpoint_uri_pair_,
                         zlink::fd_t fd_);
    void event_accept_failed (const endpoint_uri_pair_t &endpoint_uri_pair_,
                              int err_);
    void event_closed (const endpoint_uri_pair_t &endpoint_uri_pair_,
                       zlink::fd_t fd_);
    void event_close_failed (const endpoint_uri_pair_t &endpoint_uri_pair_,
                             int err_);
    void event_disconnected (const endpoint_uri_pair_t &endpoint_uri_pair_,
                             uint64_t reason_,
                             const unsigned char *routing_id_,
                             size_t routing_id_size_);
    void event_handshake_failed_no_detail (
      const endpoint_uri_pair_t &endpoint_uri_pair_, int err_);
    void event_handshake_failed_protocol (
      const endpoint_uri_pair_t &endpoint_uri_pair_, int err_);
    void
    event_handshake_failed_auth (const endpoint_uri_pair_t &endpoint_uri_pair_,
                                 int err_);
    void
    event_connection_ready_changed (const endpoint_uri_pair_t &endpoint_uri_pair_,
                                    const unsigned char *routing_id_,
                                    size_t routing_id_size_);
    void emit_inproc_connection_ready (pipe_t *pipe_);

    //  Query the state of a specific peer. The default implementation
    //  always returns an ENOTSUP error.
    virtual int get_peer_state (const void *routing_id_,
                                size_t routing_id_size_) const;

    int monitor_snapshot (zlink_monitor_snapshot_t *out_);
    bool monitor_has_attached_pipes () const;
    void socket_peer_remote_endpoints (std::vector<std::string> *out_);
    int socket_id () const;

    bool is_disconnected () const;
    bool is_ctx_terminated () const;

  protected:
    socket_base_t (zlink::ctx_t *parent_, uint32_t tid_, int sid_);
    ~socket_base_t () ZLINK_OVERRIDE;

    //  Concrete algorithms for the x- methods are to be defined by
    //  individual socket types.
    virtual void xattach_pipe (zlink::pipe_t *pipe_,
                               bool subscribe_to_all_ = false,
                               bool locally_initiated_ = false) = 0;

    //  The default implementation assumes there are no specific socket
    //  options for the particular socket type. If not so, ZLINK_FINAL this
    //  method.
    virtual int
    xsetsockopt (int option_, const void *optval_, size_t optvallen_);

    //  The default implementation assumes there are no specific socket
    //  options for the particular socket type. If not so, ZLINK_FINAL this
    //  method.
    virtual int xgetsockopt (int option_, void *optval_, size_t *optvallen_);

    //  The default implementation assumes that send is not supported.
    virtual bool xhas_out ();
    virtual int xsend (zlink::msg_t *msg_);

    //  The default implementation assumes that recv in not supported.
    virtual bool xhas_in ();
    virtual int xrecv (zlink::msg_t *msg_);
    virtual int xsocket_msg_dispatch (zlink::msg_t *msg_,
                                      zlink::pipe_t *pipe_);
    virtual int xstream_dispatch_msg (zlink::msg_t *msg_, zlink::pipe_t *pipe_);
    virtual void xdispatch_io ();
    virtual uint32_t monitor_ready_count () const;

    //  i_pipe_events will be forwarded to these functions.
    virtual void xread_activated (pipe_t *pipe_);
    virtual void xwrite_activated (pipe_t *pipe_);
    virtual void xhiccuped (pipe_t *pipe_);
    virtual void xpipe_terminated (pipe_t *pipe_) = 0;

    //  the default implementation assumes that joub and leave are not supported.
    virtual int xjoin (const char *group_);
    virtual int xleave (const char *group_);

    void invoke_socket_msg_handler (zlink_socket_msg_handler_fn handler_,
                                    const zlink_routing_id_t *source_rid_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_);

    //  Delay actual destruction of the socket.
    void process_destroy () ZLINK_FINAL;

    int connect_internal (const char *endpoint_uri_);
    int start_async_mailbox_processing (io_thread_t *io_thread_);
    void stop_async_mailbox_processing ();
    void wait_async_quiesced (int timeout_ms_);
    zlink_socket_msg_handler_fn socket_msg_handler () const;
    zlink_subscribe_handler_fn socket_spot_handler () const;
    zlink_send_ready_handler_fn socket_send_ready_handler () const;
    void *socket_msg_handler_subject () const;
    void *socket_msg_handler_userdata () const;
    void *socket_spot_handler_userdata () const;
    void *socket_send_ready_handler_subject () const;
    void *socket_send_ready_handler_userdata () const;
    static void close_socket_msg_parts (std::vector<zlink_msg_t> *parts_);
    static void resolve_socket_msg_source_rid (pipe_t *pipe_,
                                               zlink_routing_id_t *out_);
  public:
    void store_last_recv_source_rid (pipe_t *pipe_);
    void store_last_recv_source_rid (const zlink_routing_id_t *source_rid_);
    void clear_last_recv_source_rid ();
    bool copy_last_recv_source_rid (zlink_routing_id_t *out_) const;
  protected:
    static void dispatch_spot_handler_from_io (
      const zlink_routing_id_t *source_rid_,
      const char *topic_,
      size_t topic_len_,
      zlink_msg_t *parts_,
      size_t part_count_,
      void *userdata_);
    void arm_send_ready_notification ();
    void notify_send_ready_if_armed ();
    void emit_socket_monitor_value_event (
      uint64_t event_,
      uint64_t value_,
      const endpoint_uri_pair_t &endpoint_uri_pair_);
    bool has_attached_pipes () const;

  private:
    enum
    {
        monitor_queue_hwm = 4096,
        monitor_max_values = 4
    };

    struct monitor_event_record_t
    {
        monitor_event_record_t ();

        uint64_t event;
        uint64_t values[monitor_max_values];
        uint64_t values_count;
        zlink_routing_id_t routing_id;
        endpoint_uri_pair_t endpoint_uri_pair;
    };

    // test if event should be sent and then dispatch it
    void event (const endpoint_uri_pair_t &endpoint_uri_pair_,
                const unsigned char *routing_id_,
                size_t routing_id_size_,
                uint64_t values_[],
                uint64_t values_count_,
                uint64_t type_);

    // Socket event data dispatch
    static void monitor_thread_main (void *arg_);
    void monitor_loop ();
    void enqueue_monitor_event (const monitor_event_record_t &record_);
    bool build_monitor_event_record (
      monitor_event_record_t *out_,
      uint64_t event_,
      const uint64_t values_[],
      uint64_t values_count_,
      const unsigned char *routing_id_,
      size_t routing_id_size_,
      const endpoint_uri_pair_t &endpoint_uri_pair_) const;
    bool dispatch_monitor_event (void *monitor_socket_,
                                 const monitor_event_record_t &record_) const;
    void stop_monitor_thread ();

    // Monitor socket cleanup
    void stop_monitor (bool send_monitor_stopped_event_ = true);

    //  Creates new endpoint ID and adds the endpoint to the map.
    void add_endpoint (const endpoint_uri_pair_t &endpoint_pair_,
                       own_t *endpoint_,
                       pipe_t *pipe_);

    //  Map of open endpoints.
    typedef std::pair<own_t *, pipe_t *> endpoint_pipe_t;
    typedef std::multimap<std::string, endpoint_pipe_t> endpoints_t;

    //  Map of open inproc endpoints.
    class inprocs_t
    {
      public:
        void emplace (const char *endpoint_uri_, pipe_t *pipe_);
        int erase_pipes (const std::string &endpoint_uri_str_);
        void erase_pipe (const pipe_t *pipe_);

      private:
        typedef std::multimap<std::string, pipe_t *> map_t;
        map_t _inprocs;
    };

    struct endpoint_registry_t
    {
        endpoints_t endpoints;
        inprocs_t inprocs;
    };

    struct monitor_bridge_t
    {
        monitor_bridge_t () :
            socket (NULL),
            events (0),
            events_atomic (0),
            lossy (true),
            queue_stop (false),
            thread_started (false)
        {
        }

        void *socket;
        int64_t events;
        std::atomic<int64_t> events_atomic;
        bool lossy;
        mutex_t sync;
        mutex_t queue_sync;
        condition_variable_t queue_cv;
        std::deque<monitor_event_record_t> queue;
        bool queue_stop;
        thread_t thread;
        bool thread_started;
    };

    struct dispatch_bridge_t
    {
        dispatch_bridge_t () :
            socket_msg_handler (NULL),
            socket_msg_handler_subject (NULL),
            socket_msg_handler_userdata (NULL),
            spot_handler (NULL),
            spot_handler_userdata (NULL),
            public_api_state (0),
            public_api_sync (),
            callback_api_depth (0),
            close_deferred (false),
            send_ready_handler (NULL),
            send_ready_handler_subject (NULL),
            send_ready_handler_userdata (NULL),
            send_ready_seq (0),
            send_ready_armed (false),
            last_recv_source_rid (),
            last_recv_source_rid_valid (false)
        {
        }

        std::atomic<zlink_socket_msg_handler_fn> socket_msg_handler;
        std::atomic<void *> socket_msg_handler_subject;
        std::atomic<void *> socket_msg_handler_userdata;
        std::atomic<zlink_subscribe_handler_fn> spot_handler;
        std::atomic<void *> spot_handler_userdata;
        std::atomic<uint32_t> public_api_state;
        std::atomic<bool> public_api_sync;
        std::atomic<uint32_t> callback_api_depth;
        std::atomic<bool> close_deferred;
        std::atomic<zlink_send_ready_handler_fn> send_ready_handler;
        std::atomic<void *> send_ready_handler_subject;
        std::atomic<void *> send_ready_handler_userdata;
        std::atomic<uint32_t> send_ready_seq;
        mutex_t send_ready_writer_sync;
        std::atomic<bool> send_ready_armed;
        std::recursive_mutex socket_msg_dispatch_sync;
        zlink_routing_id_t last_recv_source_rid;
        bool last_recv_source_rid_valid;
    };

    struct lifecycle_hooks_t
    {
        lifecycle_hooks_t () :
            mailbox_refcnt (0),
            destroy_pending (false),
            monitor_async_mailbox_owned (false),
            async_mailbox_active (false),
            async_quiesce_pending (false),
            async_processing_done (true)
        {
        }

        atomic_counter_t mailbox_refcnt;
        bool destroy_pending;
        bool monitor_async_mailbox_owned;
        std::atomic<bool> async_mailbox_active;
        std::atomic<bool> async_quiesce_pending;
        std::atomic<bool> async_processing_done;
        mutex_t async_done_mu;
        condition_variable_t async_done_cv;
    };

    struct socket_runtime_t
    {
        endpoint_registry_t endpoint_registry;
        monitor_bridge_t monitor_bridge;
        dispatch_bridge_t dispatch_bridge;
        lifecycle_hooks_t lifecycle_hooks;
    };

    //  To be called after processing commands or invoking any command
    //  handlers explicitly. If required, it will deallocate the socket.
    void check_destroy ();

    //  Moves the flags from the message to local variables,
    //  to be later retrieved by getsockopt.
    void extract_flags (const msg_t *msg_);

    //  Used to check whether the object is a socket.
    uint32_t _tag;

    //  If true, associated context was already terminated.
    bool _ctx_terminated;

    //  If true, object should have been already destroyed. However,
    //  destruction is delayed while we unwind the stack to the point
    //  where it doesn't intersect the object being destroyed.
    bool _destroyed;

    //  Parse URI string.
    static int
    parse_uri (const char *uri_, std::string &protocol_, std::string &path_);

    //  Check whether transport protocol, as specified in connect or
    //  bind, is available and compatible with the socket type.
    int check_protocol (const std::string &protocol_) const;

    //  Register the pipe with this socket.
    void attach_pipe (zlink::pipe_t *pipe_,
                      bool subscribe_to_all_ = false,
                      bool locally_initiated_ = false);

    //  Processes commands sent to this socket (if any). If timeout is -1,
    //  returns only after at least one command was processed.
    //  If throttle argument is true, commands are processed at most once
    //  in a predefined time period.
    int process_commands (int timeout_, bool throttle_);
    void inc_mailbox_ref ();
    void dec_mailbox_ref ();
    void finalize_destroy ();
    void process_async_mailbox ();
    static void reaper_mailbox_handler (void *arg_);
    static void reaper_mailbox_pre_post (void *arg_);
    static void async_mailbox_handler (void *arg_);
    static void async_mailbox_pre_post (void *arg_);

    //  Handlers for incoming commands.
    void process_stop () ZLINK_FINAL;
    void process_bind (zlink::pipe_t *pipe_) ZLINK_FINAL;
    void process_term (int linger_) ZLINK_FINAL;
    void process_term_endpoint (std::string *endpoint_) ZLINK_FINAL;

    void update_pipe_options (int option_);

    std::string resolve_tcp_addr (std::string endpoint_uri_,
                                  const char *tcp_address_);
    bool enter_public_api ();
    void leave_public_api ();
    bool enter_callback_api ();
    void leave_callback_api ();
    bool begin_close_or_fail_busy (bool from_self_callback_);
    void finish_close_handoff ();
    bool public_close_requested () const;
    void lock_public_api_sync ();
    void unlock_public_api_sync ();
    bool send_ready_slot (
      zlink_send_ready_handler_fn *handler_out_,
      void **subject_out_) const;

    //  Socket's mailbox object.
    i_mailbox *_mailbox;

    //  List of attached pipes.
    typedef array_t<pipe_t, 3> pipes_t;
    pipes_t _pipes;
    std::set<std::string> _ready_connection_keys;

#ifndef NDEBUG
    //  Diagnostic counters for termination accounting.
    int _term_pipe_acks_registered;
    int _term_pipe_acks_received;
#endif

    //  Reaper's poller.
    poller_t *_poller;

    //  Timestamp of when commands were processed the last time.
    uint64_t _last_tsc;

    //  Number of messages received since last command processing.
    int _ticks;

    //  True if the last message received had MORE flag set.
    bool _rcvmore;

    //  Improves efficiency of time measurement.
    clock_t _clock;

    // Last socket endpoint resolved URI
    std::string _last_endpoint;
    socket_runtime_t _runtime;
    endpoints_t &_endpoints;
    inprocs_t &_inprocs;
    void *&_monitor_socket;
    int64_t &_monitor_events;
    std::atomic<int64_t> &_monitor_events_atomic;
    bool &_monitor_lossy;
    atomic_counter_t &_mailbox_refcnt;
    bool &_destroy_pending;
    bool &_monitor_async_mailbox_owned;
    std::atomic<bool> &_async_mailbox_active;
    std::atomic<bool> &_async_quiesce_pending;
    std::atomic<bool> &_async_processing_done;
    mutex_t &_async_done_mu;
    condition_variable_t &_async_done_cv;
    std::atomic<zlink_socket_msg_handler_fn> &_socket_msg_handler;
    std::atomic<void *> &_socket_msg_handler_subject;
    std::atomic<void *> &_socket_msg_handler_userdata;
    std::atomic<zlink_subscribe_handler_fn> &_spot_handler;
    std::atomic<void *> &_spot_handler_userdata;
    std::atomic<uint32_t> &_public_api_state;
    std::atomic<bool> &_public_api_sync;
    std::atomic<uint32_t> &_callback_api_depth;
    std::atomic<bool> &_close_deferred;
    std::atomic<zlink_send_ready_handler_fn> &_send_ready_handler;
    std::atomic<void *> &_send_ready_handler_subject;
    std::atomic<void *> &_send_ready_handler_userdata;
    std::atomic<uint32_t> &_send_ready_seq;
    mutex_t &_send_ready_writer_sync;
    std::atomic<bool> &_send_ready_armed;
    std::recursive_mutex &_socket_msg_dispatch_sync;
    mutex_t &_monitor_sync;
    mutex_t &_monitor_queue_sync;
    condition_variable_t &_monitor_queue_cv;
    std::deque<monitor_event_record_t> &_monitor_queue;
    bool &_monitor_queue_stop;
    thread_t &_monitor_thread;
    bool &_monitor_thread_started;
    zlink_routing_id_t &_last_recv_source_rid;
    bool &_last_recv_source_rid_valid;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (socket_base_t)

    // Add a flag for mark disconnect action
    bool _disconnected;

};

class routing_socket_base_t : public socket_base_t
{
  protected:
    routing_socket_base_t (class ctx_t *parent_, uint32_t tid_, int sid_);
    ~routing_socket_base_t () ZLINK_OVERRIDE;

    // methods from socket_base_t
    int xsetsockopt (int option_,
                     const void *optval_,
                     size_t optvallen_) ZLINK_OVERRIDE;
    void xwrite_activated (pipe_t *pipe_) ZLINK_FINAL;

    // own methods
    std::string extract_connect_routing_id ();
    bool connect_routing_id_is_set () const;

    struct out_pipe_t
    {
        pipe_t *pipe;
        bool active;
    };

    void add_out_pipe (blob_t routing_id_, pipe_t *pipe_);
    bool has_out_pipe (const blob_t &routing_id_) const;
    out_pipe_t *lookup_out_pipe (const blob_t &routing_id_);
    const out_pipe_t *lookup_out_pipe (const blob_t &routing_id_) const;
    void erase_out_pipe (const pipe_t *pipe_);
    out_pipe_t try_erase_out_pipe (const blob_t &routing_id_);
    template <typename Func> bool any_of_out_pipes (Func func_)
    {
        bool res = false;
        for (out_pipes_t::iterator it = _out_pipes.begin (),
                                   end = _out_pipes.end ();
             it != end && !res; ++it) {
            res |= func_ (*it->second.pipe);
        }

        return res;
    }

  private:
    //  Outbound pipes indexed by the peer IDs.
    typedef std::map<blob_t, out_pipe_t> out_pipes_t;
    out_pipes_t _out_pipes;

    // Next assigned name on a zlink_connect() call used by ROUTER socket type.
    std::string _connect_routing_id;
};
}

#endif
