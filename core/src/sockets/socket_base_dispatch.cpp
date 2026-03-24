/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/socket_base.hpp"
#include "utils/err.hpp"
#include "utils/macros.hpp"

namespace
{
thread_local zlink::socket_base_t *g_current_socket_msg_dispatch_socket = NULL;
thread_local zlink::socket_base_t *g_current_send_ready_dispatch_socket = NULL;
thread_local zlink::pipe_t *g_current_socket_msg_dispatch_pipe = NULL;
thread_local void *g_current_socket_msg_dispatch_subject = NULL;
thread_local zlink_routing_id_t g_current_socket_msg_dispatch_source_rid;
thread_local bool g_current_socket_msg_dispatch_source_rid_valid = false;

static void copy_routing_id (zlink_routing_id_t *out_,
                             const zlink::blob_t &routing_id_)
{
    if (!out_)
        return;
    const size_t copy_size =
      std::min (routing_id_.size (), sizeof (out_->data));
    out_->size = static_cast<uint8_t> (copy_size);
    if (copy_size > 0)
        memcpy (out_->data, routing_id_.data (), copy_size);
}
}

int zlink::socket_base_t::stream_dispatch_msg_from_io (msg_t *msg_,
                                                       pipe_t *pipe_)
{
    return xstream_dispatch_msg (msg_, pipe_);
}

int zlink::socket_base_t::socket_msg_dispatch_from_io (msg_t *msg_,
                                                       pipe_t *pipe_)
{
    if (!socket_msg_dispatch_active ())
        return 0;

    if (options.type == ZLINK_CORE_SOCKET_STREAM) {
        pipe_t *previous_pipe = g_current_socket_msg_dispatch_pipe;
        g_current_socket_msg_dispatch_pipe = pipe_;
        const int rc = xsocket_msg_dispatch (msg_, pipe_);
        g_current_socket_msg_dispatch_pipe = previous_pipe;
        return rc;
    }

    std::lock_guard<std::recursive_mutex> dispatch_lock (
      dispatch_runtime ().socket_msg_dispatch_sync);
    pipe_t *previous_pipe = g_current_socket_msg_dispatch_pipe;
    g_current_socket_msg_dispatch_pipe = pipe_;
    const int rc = xsocket_msg_dispatch (msg_, pipe_);
    g_current_socket_msg_dispatch_pipe = previous_pipe;
    return rc;
}

int zlink::socket_base_t::socket_set_msg_handler (
  zlink_socket_msg_handler_fn handler_)
{
    return socket_set_msg_handler_ex (handler_, NULL);
}

int zlink::socket_base_t::socket_set_msg_handler_ex (
  zlink_socket_msg_handler_fn handler_, void *subject_)
{
    return socket_set_msg_handler_with_userdata (handler_, subject_, NULL);
}

int zlink::socket_base_t::socket_set_msg_handler_with_userdata (
  zlink_socket_msg_handler_fn handler_, void *subject_, void *userdata_)
{
    if (!enter_public_api ())
        return -1;
    if (!handler_) {
        leave_public_api ();
        errno = EINVAL;
        return -1;
    }
    if (socket_msg_dispatch_active ()) {
        leave_public_api ();
        errno = EBUSY;
        return -1;
    }

    io_thread_t *io_thread = choose_io_thread (options.affinity);
    if (!io_thread) {
        leave_public_api ();
        errno = EAGAIN;
        return -1;
    }

    dispatch_runtime ().socket_msg_handler.store (handler_,
                                                  std::memory_order_release);
    dispatch_runtime ().socket_msg_handler_subject.store (
      subject_, std::memory_order_release);
    dispatch_runtime ().socket_msg_handler_userdata.store (
      userdata_, std::memory_order_release);
    if (start_async_mailbox_processing (io_thread) != 0) {
        dispatch_runtime ().socket_msg_handler.store (
          NULL, std::memory_order_release);
        dispatch_runtime ().socket_msg_handler_subject.store (
          NULL, std::memory_order_release);
        dispatch_runtime ().socket_msg_handler_userdata.store (
          NULL, std::memory_order_release);
        leave_public_api ();
        return -1;
    }
    leave_public_api ();
    return 0;
}

int zlink::socket_base_t::socket_msg_dispatch_stop ()
{
    if (!socket_msg_dispatch_active ()) {
        errno = EINVAL;
        return -1;
    }

    dispatch_runtime ().socket_msg_handler.store (
      NULL, std::memory_order_release);
    dispatch_runtime ().socket_msg_handler_subject.store (
      NULL, std::memory_order_release);
    dispatch_runtime ().socket_msg_handler_userdata.store (
      NULL, std::memory_order_release);

    if (lifecycle_coordinator ().is_async_mailbox_active ()) {
        stop_async_mailbox_processing ();
        wait_async_quiesced (10000);
    } else if (lifecycle_coordinator ().is_async_quiesce_pending ()) {
        wait_async_quiesced (10000);
    }

    return 0;
}

int zlink::socket_base_t::socket_set_spot_handler (
  zlink_subscribe_handler_fn handler_)
{
    return socket_set_spot_handler_with_userdata (handler_, NULL);
}

int zlink::socket_base_t::socket_set_spot_handler_with_userdata (
  zlink_subscribe_handler_fn handler_, void *userdata_)
{
    if (!enter_public_api ())
        return -1;
    if (!handler_) {
        leave_public_api ();
        errno = EINVAL;
        return -1;
    }
    if (sub_dispatch_active ()) {
        leave_public_api ();
        errno = EBUSY;
        return -1;
    }

    dispatch_runtime ().spot_handler.store (handler_,
                                            std::memory_order_release);
    dispatch_runtime ().spot_handler_userdata.store (userdata_,
                                                     std::memory_order_release);

    const int rc =
      sub_dispatch_start (&socket_base_t::dispatch_spot_handler_from_io, this);
    leave_public_api ();
    return rc;
}

int zlink::socket_base_t::socket_set_send_ready_handler (
  zlink_send_ready_handler_fn handler_)
{
    return socket_set_send_ready_handler_ex (handler_, NULL);
}

int zlink::socket_base_t::socket_set_send_ready_handler_ex (
  zlink_send_ready_handler_fn handler_, void *subject_)
{
    return socket_set_send_ready_handler_with_userdata (handler_, subject_,
                                                        NULL);
}

int zlink::socket_base_t::socket_set_send_ready_handler_with_userdata (
  zlink_send_ready_handler_fn handler_, void *subject_, void *userdata_)
{
    if (!enter_public_api ())
        return -1;
    if (!handler_) {
        leave_public_api ();
        errno = EINVAL;
        return -1;
    }
    if (send_ready_dispatch_in_callback ()) {
        leave_public_api ();
        errno = EDEADLK;
        return -1;
    }

    dispatch_bridge_t &dispatch = dispatch_runtime ();
    scoped_lock_t writer_lock (dispatch.send_ready_writer_sync);
    dispatch.send_ready_seq.fetch_add (1, std::memory_order_acq_rel);
    dispatch.send_ready_handler.store (handler_, std::memory_order_release);
    dispatch.send_ready_handler_subject.store (subject_,
                                               std::memory_order_release);
    dispatch.send_ready_handler_userdata.store (userdata_,
                                                std::memory_order_release);
    dispatch.send_ready_seq.fetch_add (1, std::memory_order_acq_rel);
    leave_public_api ();
    return 0;
}

bool zlink::socket_base_t::socket_msg_dispatch_active () const
{
    return dispatch_runtime ().socket_msg_handler.load (
             std::memory_order_acquire)
           != NULL;
}

bool zlink::socket_base_t::send_ready_handler_active () const
{
    return dispatch_runtime ().send_ready_handler.load (
             std::memory_order_acquire)
           != NULL;
}

zlink::socket_base_t *
zlink::socket_base_t::current_socket_msg_dispatch_socket ()
{
    return g_current_socket_msg_dispatch_socket;
}

zlink::socket_base_t *
zlink::socket_base_t::current_send_ready_dispatch_socket ()
{
    return g_current_send_ready_dispatch_socket;
}

zlink::pipe_t *zlink::socket_base_t::current_socket_msg_dispatch_pipe ()
{
    return g_current_socket_msg_dispatch_pipe;
}

void *zlink::socket_base_t::current_socket_msg_dispatch_subject ()
{
    return g_current_socket_msg_dispatch_subject;
}

bool zlink::socket_base_t::current_socket_msg_dispatch_source_rid (
  zlink_routing_id_t *out_)
{
    if (!out_ || !g_current_socket_msg_dispatch_source_rid_valid)
        return false;
    *out_ = g_current_socket_msg_dispatch_source_rid;
    return true;
}

bool zlink::socket_base_t::send_ready_dispatch_in_callback () const
{
    return g_current_send_ready_dispatch_socket == this;
}

void zlink::socket_base_t::invoke_send_ready_handler_for_testing ()
{
    zlink_send_ready_handler_fn handler = NULL;
    void *subject = NULL;
    if (!send_ready_slot (&handler, &subject))
        return;

    if (!enter_callback_api ())
        return;

    void *userdata =
      dispatch_runtime ().send_ready_handler_userdata.load (
        std::memory_order_acquire);
    socket_base_t *previous = g_current_send_ready_dispatch_socket;
    g_current_send_ready_dispatch_socket = this;
    handler (subject ? subject : this, userdata);
    g_current_send_ready_dispatch_socket = previous;
    leave_callback_api ();
}

zlink_socket_msg_handler_fn zlink::socket_base_t::socket_msg_handler () const
{
    return dispatch_runtime ().socket_msg_handler.load (
      std::memory_order_acquire);
}

zlink_subscribe_handler_fn zlink::socket_base_t::socket_spot_handler () const
{
    return dispatch_runtime ().spot_handler.load (std::memory_order_acquire);
}

zlink_send_ready_handler_fn
zlink::socket_base_t::socket_send_ready_handler () const
{
    zlink_send_ready_handler_fn handler = NULL;
    void *subject = NULL;
    if (!send_ready_slot (&handler, &subject))
        return NULL;
    return handler;
}

void *zlink::socket_base_t::socket_msg_handler_subject () const
{
    return dispatch_runtime ().socket_msg_handler_subject.load (
      std::memory_order_acquire);
}

void *zlink::socket_base_t::socket_msg_handler_userdata () const
{
    return dispatch_runtime ().socket_msg_handler_userdata.load (
      std::memory_order_acquire);
}

void *zlink::socket_base_t::socket_spot_handler_userdata () const
{
    return dispatch_runtime ().spot_handler_userdata.load (
      std::memory_order_acquire);
}

void *zlink::socket_base_t::socket_send_ready_handler_subject () const
{
    zlink_send_ready_handler_fn handler = NULL;
    void *subject = NULL;
    if (!send_ready_slot (&handler, &subject))
        return NULL;
    return subject;
}

void *zlink::socket_base_t::socket_send_ready_handler_userdata () const
{
    return dispatch_runtime ().send_ready_handler_userdata.load (
      std::memory_order_acquire);
}

void zlink::socket_base_t::invoke_socket_msg_handler (
  zlink_socket_msg_handler_fn handler_,
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (!enter_callback_api ()) {
        for (size_t i = 0; i < part_count_; ++i) {
            const int rc = reinterpret_cast<msg_t *> (&parts_[i])->close ();
            errno_assert (rc == 0);
        }
        return;
    }
    socket_base_t *previous = g_current_socket_msg_dispatch_socket;
    void *previous_subject = g_current_socket_msg_dispatch_subject;
    zlink_routing_id_t previous_source_rid;
    const bool previous_source_rid_valid =
      g_current_socket_msg_dispatch_source_rid_valid;
    if (previous_source_rid_valid)
        previous_source_rid = g_current_socket_msg_dispatch_source_rid;
    g_current_socket_msg_dispatch_socket = this;
    g_current_socket_msg_dispatch_subject = socket_msg_handler_subject ();
    if (source_rid_) {
        g_current_socket_msg_dispatch_source_rid = *source_rid_;
        g_current_socket_msg_dispatch_source_rid_valid = true;
    } else {
        memset (&g_current_socket_msg_dispatch_source_rid, 0,
                sizeof (g_current_socket_msg_dispatch_source_rid));
        g_current_socket_msg_dispatch_source_rid_valid = false;
    }
    handler_ (source_rid_, parts_, part_count_,
              socket_msg_handler_userdata ());
    g_current_socket_msg_dispatch_socket = previous;
    g_current_socket_msg_dispatch_subject = previous_subject;
    if (previous_source_rid_valid) {
        g_current_socket_msg_dispatch_source_rid = previous_source_rid;
        g_current_socket_msg_dispatch_source_rid_valid = true;
    } else {
        memset (&g_current_socket_msg_dispatch_source_rid, 0,
                sizeof (g_current_socket_msg_dispatch_source_rid));
        g_current_socket_msg_dispatch_source_rid_valid = false;
    }
    leave_callback_api ();
}

void zlink::socket_base_t::close_socket_msg_parts (
  std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;

    for (size_t i = 0; i < parts_->size (); ++i) {
        const int rc =
          reinterpret_cast<msg_t *> (&(*parts_)[i])->close ();
        errno_assert (rc == 0);
    }
    parts_->clear ();
}

void zlink::socket_base_t::resolve_socket_msg_source_rid (
  pipe_t *pipe_, zlink_routing_id_t *out_)
{
    if (!out_)
        return;

    memset (out_, 0, sizeof (*out_));
    if (!pipe_)
        return;

    const blob_t &pipe_routing_id = pipe_->get_routing_id ();
    if (pipe_routing_id.size () > 0) {
        copy_routing_id (out_, pipe_routing_id);
        return;
    }

    pipe_t *peer = pipe_->get_peer ();
    if (!peer)
        return;

    copy_routing_id (out_, peer->get_routing_id ());
}

void zlink::socket_base_t::store_last_recv_source_rid (pipe_t *pipe_)
{
    zlink_routing_id_t rid;
    resolve_socket_msg_source_rid (pipe_, &rid);
    store_last_recv_source_rid (&rid);
}

void zlink::socket_base_t::store_last_recv_source_rid (
  const zlink_routing_id_t *source_rid_)
{
    if (!source_rid_) {
        clear_last_recv_source_rid ();
        return;
    }

    endpoint_runtime ().last_recv_source_rid = *source_rid_;
    endpoint_runtime ().last_recv_source_rid_valid = true;
}

void zlink::socket_base_t::clear_last_recv_source_rid ()
{
    memset (&endpoint_runtime ().last_recv_source_rid, 0,
            sizeof (endpoint_runtime ().last_recv_source_rid));
    endpoint_runtime ().last_recv_source_rid_valid = false;
}

bool zlink::socket_base_t::copy_last_recv_source_rid (
  zlink_routing_id_t *out_) const
{
    if (out_)
        memset (out_, 0, sizeof (*out_));

    if (!endpoint_runtime ().last_recv_source_rid_valid || !out_)
        return false;

    *out_ = endpoint_runtime ().last_recv_source_rid;
    return true;
}

void zlink::socket_base_t::dispatch_spot_handler_from_io (
  const zlink_routing_id_t *source_rid_,
  const char *topic_,
  size_t topic_len_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_)
{
    socket_base_t *self = static_cast<socket_base_t *> (userdata_);
    if (!self) {
        for (size_t i = 0; i < part_count_; ++i) {
            const int rc = reinterpret_cast<msg_t *> (&parts_[i])->close ();
            errno_assert (rc == 0);
        }
        return;
    }

    zlink_subscribe_handler_fn handler = self->socket_spot_handler ();
    if (!handler) {
        for (size_t i = 0; i < part_count_; ++i) {
            const int rc = reinterpret_cast<msg_t *> (&parts_[i])->close ();
            errno_assert (rc == 0);
        }
        return;
    }

    if (!self->enter_callback_api ()) {
        for (size_t i = 0; i < part_count_; ++i) {
            const int rc = reinterpret_cast<msg_t *> (&parts_[i])->close ();
            errno_assert (rc == 0);
        }
        return;
    }

    handler (source_rid_, topic_, topic_len_, parts_, part_count_,
             self->socket_spot_handler_userdata ());
    self->leave_callback_api ();
}

void zlink::socket_base_t::arm_send_ready_notification ()
{
    if (socket_send_ready_handler ())
        dispatch_runtime ().send_ready_armed.store (true,
                                                    std::memory_order_release);
}

void zlink::socket_base_t::notify_send_ready_if_armed ()
{
    if (!dispatch_runtime ().send_ready_armed.load (std::memory_order_acquire)
        || !has_out ())
        return;

    bool expected = true;
    if (!dispatch_runtime ().send_ready_armed.compare_exchange_strong (
          expected, false, std::memory_order_acq_rel,
          std::memory_order_acquire))
        return;

    zlink_send_ready_handler_fn handler = socket_send_ready_handler ();
    if (handler) {
        invoke_send_ready_handler_for_testing ();
    }
}

int zlink::socket_base_t::sub_dispatch_start (spot_sub_io_handler_fn callback_,
                                              void *userdata_)
{
    LIBZLINK_UNUSED (callback_);
    LIBZLINK_UNUSED (userdata_);
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::sub_dispatch_stop ()
{
    errno = ENOTSUP;
    return -1;
}

bool zlink::socket_base_t::sub_dispatch_active () const
{
    return false;
}

int zlink::socket_base_t::xpub_dispatch_start ()
{
    errno = ENOTSUP;
    return -1;
}

bool zlink::socket_base_t::xpub_dispatch_active () const
{
    return false;
}

int zlink::socket_base_t::stream_dispatch_start_raw (zlink_stream_on_raw_fn)
{
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::stream_set_msg_handler_with_userdata (
  zlink_socket_msg_handler_fn,
  void *)
{
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::stream_dispatch_stop ()
{
    return 0;
}

bool zlink::socket_base_t::stream_dispatch_active () const
{
    return false;
}

bool zlink::socket_base_t::stream_dispatch_in_callback () const
{
    return false;
}

uint32_t zlink::socket_base_t::stream_dispatch_inflight () const
{
    return 0;
}

int zlink::socket_base_t::stream_dispatch_send_from_io (
  const zlink_routing_id_t *,
  const void *,
  size_t,
  int)
{
    return 0;
}

int zlink::socket_base_t::stream_dispatch_send_msg_from_io (
  const zlink_routing_id_t *,
  msg_t *,
  int)
{
    return 0;
}

int zlink::socket_base_t::xsocket_msg_dispatch (msg_t *msg_, pipe_t *pipe_)
{
    LIBZLINK_UNUSED (msg_);
    LIBZLINK_UNUSED (pipe_);
    return 0;
}

int zlink::socket_base_t::xstream_dispatch_msg (msg_t *msg_, pipe_t *pipe_)
{
    LIBZLINK_UNUSED (msg_);
    LIBZLINK_UNUSED (pipe_);
    return 0;
}

void zlink::socket_base_t::xdispatch_io ()
{
}
