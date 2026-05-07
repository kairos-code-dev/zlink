/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <memory>

#include "api/part_helper_internal.hpp"
#include "api/recv_result_internal.hpp"
#include "api/service_mode_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "api/socket_request_reply_wait_internal.hpp"
#include "core/socket_poller.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"

namespace reqrep = zlink::socket_reqrep_internal;

extern "C" int zlink_router_enable_spot_receive (void *router_);

namespace
{
struct router_recv_part_metadata_tls_t
{
    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_spot_rid;
};

router_recv_part_metadata_tls_t &router_recv_part_metadata_tls ()
{
    static thread_local router_recv_part_metadata_tls_t metadata;
    return metadata;
}

int validate_socket_type (void *socket_, int expected_type_)
{
    const socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket)
        return -1;

    if (socket_type (handle) != expected_type_) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

void export_router_recv_part_metadata_view (
  const zlink_routing_id_t *source_node_rid_,
  const zlink_routing_id_t *source_spot_rid_,
  uint64_t request_seq_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_)
{
    router_recv_part_metadata_tls_t &metadata = router_recv_part_metadata_tls ();
    zlink::part_helper_internal::copy_routing_id (
      source_node_rid_, &metadata.source_node_rid);
    zlink::part_helper_internal::copy_routing_id (
      source_spot_rid_, &metadata.source_spot_rid);

    if (source_node_rid_out_) {
        *source_node_rid_out_ =
          source_node_rid_ ? &metadata.source_node_rid : NULL;
    }
    if (source_spot_rid_out_) {
        *source_spot_rid_out_ =
          source_spot_rid_ ? &metadata.source_spot_rid : NULL;
    }
    if (request_seq_out_)
        *request_seq_out_ = request_seq_;
}

zlink_recv_result_t recv_router_parts_with_helper (
  const std::shared_ptr<reqrep::socket_request_reply_state_t> &state_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_)
{
    if (!state_ || !source_node_rid_out_ || !source_spot_rid_out_
        || !request_seq_out_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    const int timeout_ms = (flags_ & ZLINK_DONTWAIT) ? 0 : -1;
    const int rc = reqrep::recv_internal_router_queue (
      &state_->recv_queue, source_node_rid_out_, source_spot_rid_out_,
      request_seq_out_, parts_out_, part_count_out_,
      static_cast<int> (flags_), timeout_ms);
    if (rc != 0)
        return zlink::recv_result_internal::from_errno (errno);
    return ZLINK_RECV_OK;
}
}

zlink_recv_result_t zlink_router_recv_part (
  void *router_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_)
{
    if (!router_ || !source_node_rid_out_ || !source_spot_rid_out_
        || !request_seq_out_ || !part_out_ || !has_more_out_) {
        errno = EFAULT;
        return zlink::recv_result_internal::from_errno (errno);
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::recv_result_internal::from_errno (errno);
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return zlink::recv_result_internal::from_errno (errno);

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return zlink::recv_result_internal::from_errno (EFAULT);
    if (zlink_router_enable_spot_receive (router_) != 0)
        return zlink::recv_result_internal::from_errno (errno);

    std::shared_ptr<zlink::part_helper_internal::handle_state_t> helper_state =
      zlink::part_helper_internal::find_handle_state (router_);

    std::shared_ptr<reqrep::socket_request_reply_state_t> state =
      reqrep::find_request_reply_state (handle);
    std::shared_ptr<zlink::spot_reqrep_internal::router_spot_request_reply_state_t>
      router_spot_state =
        zlink::spot_reqrep_internal::find_or_create_router_state (router_);
    bool use_helper_queue = false;
    if (state) {
        std::lock_guard<std::mutex> lock (state->mutex);
        use_helper_queue = state->internal_dispatch_installed;
    }

    const bool recv_sequence_active =
      zlink::part_helper_internal::recv_sequence_active (helper_state);
    auto recv_router_parts_once =
      [&] (const zlink_routing_id_t **source_node_rid_out,
           const zlink_routing_id_t **source_spot_rid_out,
           uint64_t *request_seq_out,
           zlink_msg_t **parts_out,
           size_t *part_count_out) -> zlink_recv_result_t {
        reqrep::drain_router_completion_queues (router_, state,
                                                router_spot_state);

        const zlink_recv_flags_t try_flags =
          static_cast<zlink_recv_flags_t> (flags_ | ZLINK_DONTWAIT);
        const bool blocking = (flags_ & ZLINK_DONTWAIT) == 0;
        zlink::socket_base_t *input_socket =
          use_helper_queue && state ? state->recv_queue.rx : handle.socket;
        zlink::socket_base_t *socket_signal =
          state ? reqrep::completion_signal_socket (state) : NULL;
        zlink::socket_base_t *router_spot_signal =
          router_spot_state
            ? zlink::spot_reqrep_internal::router_completion_signal_socket (
                router_spot_state)
            : NULL;

        while (true) {
            zlink_recv_result_t rc =
              use_helper_queue
                ? recv_router_parts_with_helper (state, source_node_rid_out,
                                                 source_spot_rid_out,
                                                 request_seq_out, parts_out,
                                                 part_count_out, try_flags)
                : static_cast<zlink_recv_result_t> (
                    reqrep::recv_router_message_direct (
                      handle, source_node_rid_out, source_spot_rid_out,
                      request_seq_out, parts_out, part_count_out,
                      static_cast<int> (try_flags))
                    == 0
                      ? ZLINK_RECV_OK
                      : zlink::recv_result_internal::from_errno (errno));
            if (rc == ZLINK_RECV_OK)
                return rc;
            if (!blocking || errno != EAGAIN)
                return zlink::recv_result_internal::from_errno (errno);

            bool input_ready = false;
            bool socket_signal_ready = false;
            bool router_spot_signal_ready = false;
            const int wait_rc = reqrep::wait_router_input_or_completion (
              input_socket, socket_signal, router_spot_signal, -1, &input_ready,
              &socket_signal_ready, &router_spot_signal_ready);
            if (wait_rc <= 0) {
                if (wait_rc == 0)
                    errno = EAGAIN;
                return zlink::recv_result_internal::from_errno (errno);
            }

            if (socket_signal_ready || router_spot_signal_ready)
                reqrep::drain_router_completion_queues (router_, state,
                                                        router_spot_state);
            if (!input_ready && !socket_signal_ready
                && !router_spot_signal_ready) {
                errno = EAGAIN;
                return zlink::recv_result_internal::from_errno (errno);
            }
        }
      };

    if (!recv_sequence_active) {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        const zlink_recv_result_t recv_rc =
          recv_router_parts_once (&source_node_rid, &source_spot_rid,
                                  &request_seq, &parts, &part_count);
        if (recv_rc != ZLINK_RECV_OK)
            return zlink::recv_result_internal::from_errno (errno);

        if (!parts || part_count == 0) {
            errno = EPROTO;
            return zlink::recv_result_internal::from_errno (errno);
        }

        if (part_count == 1) {
            if (zlink_msg_move (part_out_, &parts[0]) != 0) {
                zlink_multipart_close (parts, part_count);
                errno = EFAULT;
                return zlink::recv_result_internal::from_errno (errno);
            }
            zlink_multipart_close (parts, part_count);
            export_router_recv_part_metadata_view (
              source_node_rid, source_spot_rid, request_seq,
              source_node_rid_out_, source_spot_rid_out_, request_seq_out_);
            *has_more_out_ = ZLINK_PART_FINAL;
            return ZLINK_RECV_OK;
        }

        if (!helper_state) {
            helper_state =
              zlink::part_helper_internal::find_or_create_handle_state (
                router_);
            if (!helper_state) {
                zlink_multipart_close (parts, part_count);
                return zlink::recv_result_internal::from_errno (errno);
            }
        }

        const int stage_rc = zlink::part_helper_internal::stage_recv_sequence (
          helper_state, zlink::part_helper_internal::recv_family_router,
          use_helper_queue && state ? state->recv_queue.rx : handle.socket,
          source_node_rid, source_spot_rid, request_seq, parts, part_count,
          std::this_thread::get_id ());
        zlink_multipart_close (parts, part_count);
        if (stage_rc != 0) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (helper_state->recv.buffered_parts.empty ()) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EPROTO;
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (zlink::part_helper_internal::take_recv_part (
              helper_state, part_out_, has_more_out_)
            != 0) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            return zlink::recv_result_internal::from_errno (errno);
        }
        zlink::part_helper_internal::export_recv_metadata (
          helper_state, source_node_rid_out_, source_spot_rid_out_,
          request_seq_out_);
        zlink::part_helper_internal::complete_recv_step (helper_state,
                                                         *has_more_out_);
        return ZLINK_RECV_OK;
    }

    zlink::socket_base_t *source_socket =
      use_helper_queue && state ? state->recv_queue.rx : handle.socket;
    bool first_part = false;
    if (zlink::part_helper_internal::prepare_recv_step (
          router_, zlink::part_helper_internal::recv_family_router,
          source_socket, &helper_state, &first_part, &source_socket)
        != 0) {
        return zlink::recv_result_internal::from_errno (errno);
    }

    if (first_part) {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        const zlink_recv_result_t recv_rc =
          recv_router_parts_once (&source_node_rid, &source_spot_rid,
                                  &request_seq, &parts, &part_count);
        if (recv_rc != ZLINK_RECV_OK) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            return zlink::recv_result_internal::from_errno (errno);
        }

        if (!parts || part_count == 0) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EPROTO;
            return zlink::recv_result_internal::from_errno (errno);
        }

        if (part_count == 1) {
            if (zlink_msg_move (part_out_, &parts[0]) != 0) {
                zlink_multipart_close (parts, part_count);
                zlink::part_helper_internal::abort_recv_step (helper_state);
                errno = EFAULT;
                return zlink::recv_result_internal::from_errno (errno);
            }
            zlink_multipart_close (parts, part_count);
            export_router_recv_part_metadata_view (
              source_node_rid, source_spot_rid, request_seq,
              source_node_rid_out_, source_spot_rid_out_, request_seq_out_);
            *has_more_out_ = ZLINK_PART_FINAL;
            zlink::part_helper_internal::complete_recv_step (helper_state,
                                                             *has_more_out_);
            return ZLINK_RECV_OK;
        }

        const int stage_rc = zlink::part_helper_internal::stage_recv_sequence (
          helper_state, zlink::part_helper_internal::recv_family_router,
          source_socket, source_node_rid, source_spot_rid, request_seq, parts,
          part_count, std::this_thread::get_id ());
        zlink_multipart_close (parts, part_count);
        if (stage_rc != 0) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (helper_state->recv.buffered_parts.empty ()) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EPROTO;
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (zlink::part_helper_internal::take_recv_part (
              helper_state, part_out_, has_more_out_)
            != 0) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            return zlink::recv_result_internal::from_errno (errno);
        }
    } else {
        if (zlink::part_helper_internal::take_recv_part (
              helper_state, part_out_, has_more_out_)
            != 0) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            return zlink::recv_result_internal::from_errno (errno);
        }
    }

    zlink::part_helper_internal::export_recv_metadata (
      helper_state, source_node_rid_out_, source_spot_rid_out_,
      request_seq_out_);
    zlink::part_helper_internal::complete_recv_step (helper_state,
                                                     *has_more_out_);
    return ZLINK_RECV_OK;
}

extern "C" void zlink_socket_request_reply_cleanup (void *socket_)
{
    reqrep::cleanup_request_reply_socket (as_socket_handle (socket_));
}

extern "C" int zlink_router_enable_request_reply_receive (void *router_)
{
    if (validate_socket_type (router_, ZLINK_CORE_SOCKET_ROUTER) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> state =
      reqrep::find_or_create_request_reply_state (handle);
    if (reqrep::ensure_recv_queue_ready (state) != 0)
        return -1;
    return reqrep::ensure_internal_dispatch_installed (state);
}

extern "C" int zlink_socket_request_reply_set_default_timeout (
  void *socket_,
  const void *optval_,
  size_t optvallen_)
{
    const socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    if (type != ZLINK_CORE_SOCKET_ROUTER && type != ZLINK_CORE_SOCKET_DEALER) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_ || optvallen_ != sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    int timeout_ms = 0;
    memcpy (&timeout_ms, optval_, sizeof (timeout_ms));
    if (timeout_ms < 0) {
        errno = EINVAL;
        return -1;
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> state =
      reqrep::find_or_create_request_reply_state (handle);
    std::lock_guard<std::mutex> lock (state->mutex);
    state->default_timeout_ms = static_cast<uint32_t> (timeout_ms);
    return 0;
}

extern "C" int zlink_socket_request_reply_get_default_timeout (
  void *socket_,
  void *optval_,
  size_t *optvallen_)
{
    const socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket) {
        errno = EINVAL;
        return -1;
    }

    const int type = socket_type (handle);
    if (type != ZLINK_CORE_SOCKET_ROUTER && type != ZLINK_CORE_SOCKET_DEALER) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> state =
      reqrep::find_or_create_request_reply_state (handle);
    int timeout_ms = 0;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        timeout_ms = static_cast<int> (state->default_timeout_ms);
    }

    memcpy (optval_, &timeout_ms, sizeof (timeout_ms));
    *optvallen_ = sizeof (timeout_ms);
    return 0;
}

extern "C" int zlink_socket_request_progress_internal (void *socket_)
{
    const socket_handle_t handle = as_socket_handle (socket_);
    if (!handle.socket) {
        errno = EFAULT;
        return -1;
    }

    int drained = 0;

    const std::shared_ptr<reqrep::socket_request_reply_state_t> socket_state =
      reqrep::find_request_reply_state (handle);
    if (socket_state) {
        const int rc = reqrep::drain_reply_completions (socket_state, socket_);
        if (rc < 0)
            return -1;
        drained += rc;
    }

    const std::shared_ptr<zlink::spot_reqrep_internal::router_spot_request_reply_state_t>
      router_spot_state = handle.socket->router_spot_request_reply_state ();
    if (router_spot_state) {
        const int rc =
          zlink::spot_reqrep_internal::drain_router_reply_completions (
            router_spot_state, socket_);
        if (rc < 0)
            return -1;
        drained += rc;
    }

    errno = 0;
    return drained;
}
