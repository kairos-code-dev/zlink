/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/stream.hpp"
#include "sockets/stream_dispatch_internal.hpp"
#include "core/pipe.hpp"
#include "utils/err.hpp"

#include <chrono>
#include <thread>

namespace
{
bool wait_dispatch_send_retry (
  const std::chrono::steady_clock::time_point &deadline_, bool dontwait_)
{
    if (dontwait_)
        return false;

    unsigned int spin_count = 0;
    while (std::chrono::steady_clock::now () < deadline_) {
        if (spin_count < 32) {
            ++spin_count;
            std::this_thread::yield ();
            return true;
        }

        std::this_thread::sleep_for (std::chrono::microseconds (100));
        return true;
    }

    return false;
}

zlink::pipe_t *resolve_direct_dispatch_output_pipe_local (
  const zlink::stream_t *socket_,
  uint32_t routing_id_)
{
    if (!socket_ || !zlink::stream_dispatch_owns_socket (socket_))
        return NULL;

    if (zlink::stream_dispatch_context_t::current_routing_id () != routing_id_)
        return NULL;

    zlink::pipe_t *dispatch_pipe =
      zlink::stream_dispatch_context_t::current_pipe ();
    if (!dispatch_pipe)
        return NULL;

    zlink::pipe_t *out = dispatch_pipe->get_peer ();
    return out ? out : dispatch_pipe;
}
}

int zlink::stream_t::stream_dispatch_send_from_io (
  const zlink_routing_id_t *rid_,
  const void *data_,
  size_t size_,
  int flags_)
{
    if (!rid_ || rid_->size != 4) {
        errno = EINVAL;
        return -1;
    }
    if (!data_ && size_ > 0) {
        errno = EINVAL;
        return -1;
    }

    const uint32_t routing_id =
      (static_cast<uint32_t> (rid_->data[0]) << 24)
      | (static_cast<uint32_t> (rid_->data[1]) << 16)
      | (static_cast<uint32_t> (rid_->data[2]) << 8)
      | static_cast<uint32_t> (rid_->data[3]);
    if (routing_id == 0) {
        errno = EINVAL;
        return -1;
    }

    pipe_t *const direct_out =
      resolve_direct_dispatch_output_pipe_local (this, routing_id);

    if (size_ == 0) {
        if (direct_out) {
            direct_out->terminate (false);
            return 1;
        }

        route_shard_t &shard = route_shard_for (routing_id);
        scoped_fast_lock_t shard_lock (shard.sync);
        route_shard_t::routes_t::iterator it = shard.routes.find (routing_id);
        if (it == shard.routes.end () || !it->second) {
            errno = EHOSTUNREACH;
            return -1;
        }
        it->second->terminate (false);
        return 1;
    }

    msg_t out_msg;
    if (out_msg.init_buffer (data_, size_) != 0)
        return -1;

    if (direct_out
        && direct_out->write_single_message_and_flush_no_recursive_hwm_check (
          &out_msg)) {
        const int init_rc = out_msg.init ();
        errno_assert (init_rc == 0);
        return 1;
    }

    const bool dontwait = (flags_ & ZLINK_DONTWAIT) != 0 || options.sndtimeo == 0;
    const int sndtimeo = options.sndtimeo;
    const std::chrono::steady_clock::time_point deadline =
      sndtimeo < 0
        ? std::chrono::steady_clock::time_point::max ()
        : std::chrono::steady_clock::now ()
            + std::chrono::milliseconds (sndtimeo);

    for (;;) {
        {
            route_shard_t &shard = route_shard_for (routing_id);
            scoped_fast_lock_t shard_lock (shard.sync);
            route_shard_t::routes_t::iterator it = shard.routes.find (
              routing_id);
            if (it == shard.routes.end () || !it->second) {
                const int rc = out_msg.close ();
                errno_assert (rc == 0);
                errno = EHOSTUNREACH;
                return -1;
            }

            if (it->second
                  ->write_single_message_and_flush_no_recursive_hwm_check (
                    &out_msg)) {
                const int init_rc = out_msg.init ();
                errno_assert (init_rc == 0);
                return 1;
            }
        }

        if (dontwait) {
            const int rc = out_msg.close ();
            errno_assert (rc == 0);
            errno = EAGAIN;
            return -1;
        }

        if (sndtimeo >= 0 && std::chrono::steady_clock::now () >= deadline) {
            const int rc = out_msg.close ();
            errno_assert (rc == 0);
            errno = EAGAIN;
            return -1;
        }

        if (!wait_dispatch_send_retry (deadline, dontwait))
            break;
    }

    const int rc = out_msg.close ();
    errno_assert (rc == 0);
    errno = EAGAIN;
    return -1;
}

int zlink::stream_t::stream_dispatch_send_msg_from_io (
  const zlink_routing_id_t *rid_,
  msg_t *msg_,
  int flags_)
{
    if (!rid_ || rid_->size != 4 || !msg_) {
        errno = EINVAL;
        return -1;
    }

    const uint32_t routing_id =
      (static_cast<uint32_t> (rid_->data[0]) << 24)
      | (static_cast<uint32_t> (rid_->data[1]) << 16)
      | (static_cast<uint32_t> (rid_->data[2]) << 8)
      | static_cast<uint32_t> (rid_->data[3]);
    if (routing_id == 0) {
        errno = EINVAL;
        return -1;
    }

    pipe_t *const direct_out =
      resolve_direct_dispatch_output_pipe_local (this, routing_id);

    if (direct_out) {
        if (msg_->size () == 0) {
            direct_out->terminate (false);
            const int init_rc = msg_->init ();
            errno_assert (init_rc == 0);
            return 1;
        }

        if (direct_out->write_single_message_and_flush_no_recursive_hwm_check (
              msg_)) {
            const int init_rc = msg_->init ();
            errno_assert (init_rc == 0);
            return 1;
        }
    }

    const bool dontwait = (flags_ & ZLINK_DONTWAIT) != 0 || options.sndtimeo == 0;
    const int sndtimeo = options.sndtimeo;
    const std::chrono::steady_clock::time_point deadline =
      sndtimeo < 0
        ? std::chrono::steady_clock::time_point::max ()
        : std::chrono::steady_clock::now ()
            + std::chrono::milliseconds (sndtimeo);

    for (;;) {
        {
            route_shard_t &shard = route_shard_for (routing_id);
            scoped_fast_lock_t shard_lock (shard.sync);
            route_shard_t::routes_t::iterator it = shard.routes.find (
              routing_id);
            if (it == shard.routes.end () || !it->second) {
                errno = EHOSTUNREACH;
                return -1;
            }

            if (msg_->size () == 0) {
                it->second->terminate (false);
                const int init_rc = msg_->init ();
                errno_assert (init_rc == 0);
                return 1;
            }

            if (it->second
                  ->write_single_message_and_flush_no_recursive_hwm_check (
                    msg_)) {
                const int init_rc = msg_->init ();
                errno_assert (init_rc == 0);
                return 1;
            }
        }

        if (dontwait) {
            errno = EAGAIN;
            return -1;
        }

        if (sndtimeo >= 0 && std::chrono::steady_clock::now () >= deadline) {
            errno = EAGAIN;
            return -1;
        }

        if (!wait_dispatch_send_retry (deadline, dontwait))
            break;
    }

    errno = EAGAIN;
    return -1;
}

int zlink::stream_t::stream_dispatch_send_current_msg_from_io (msg_t *msg_,
                                                               int flags_)
{
    if (!msg_) {
        errno = EINVAL;
        return -1;
    }

    pipe_t *dispatch_pipe = zlink::stream_dispatch_context_t::current_pipe ();
    pipe_t *direct_out = dispatch_pipe ? dispatch_pipe->get_peer () : NULL;
    if (!direct_out && dispatch_pipe)
        direct_out = dispatch_pipe;
    if (!direct_out) {
        errno = EAGAIN;
        return -1;
    }

    if (msg_->size () == 0) {
        direct_out->terminate (false);
        const int init_rc = msg_->init ();
        errno_assert (init_rc == 0);
        return 1;
    }

    if (direct_out->write_single_message_and_flush_no_recursive_hwm_check (
          msg_)) {
        const int init_rc = msg_->init ();
        errno_assert (init_rc == 0);
        return 1;
    }

    if (dispatch_pipe) {
        direct_out->refresh_write_credit (dispatch_pipe->get_msgs_read ());
        if (direct_out->write_single_message_and_flush_no_recursive_hwm_check (
              msg_)) {
            const int init_rc = msg_->init ();
            errno_assert (init_rc == 0);
            return 1;
        }
    }

    LIBZLINK_UNUSED (flags_);
    errno = EAGAIN;
    return -1;
}
