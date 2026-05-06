/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_data_plane_message_io_internal.hpp"
#include "services/spot/spot_message_parts_internal.hpp"
#include "services/spot/spot_mesh_pub_hwm.hpp"

#include "api/request_reply_protocol_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"

#include "services/common/monitor_decode.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/err.hpp"

#include <errno.h>
#include <map>
#include <set>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zlink
{
namespace
{
namespace spot_io = zlink::spot_data_plane_message_io;

static void spot_ready_ack_ctrl_debugf (const char *fmt_, ...)
{
    if (!getenv ("ZLINK_DEBUG_SPOT_READY_ACK"))
        return;

    va_list args;
    va_start (args, fmt_);
    fprintf (stderr, "[spot-ready-ack-ctrl] ");
    vfprintf (stderr, fmt_, args);
    fprintf (stderr, "\n");
    fflush (stderr);
    FILE *fp = fopen ("/tmp/zlink_spot_ready_ack.log", "a");
    if (fp) {
        va_list file_args;
        va_start (file_args, fmt_);
        vfprintf (fp, fmt_, file_args);
        fprintf (fp, "\n");
        va_end (file_args);
        fclose (fp);
    }
    va_end (args);
}

static uint64_t default_bootstrap_broadcast_interval_ms (
  const spot_runtime_t *runtime_)
{
    if (runtime_) {
        const std::string &bound_endpoint = runtime_->bound_endpoint;
        if (bound_endpoint.compare (0, 6, "tcp://") == 0
            || bound_endpoint.compare (0, 6, "tls://") == 0) {
            return 5000;
        }
    }

    return 1000;
}

}

int spot_data_plane_protocol_t::recv_ascii_command (
  socket_base_t *socket_, std::vector<std::string> *frames_)
{
    if (!frames_)
        return -1;
    frames_->clear ();
    while (true) {
        msg_t frame;
        if (frame.init () != 0)
            return -1;
        if (socket_->recv (&frame, 0) != 0) {
            frame.close ();
            return -1;
        }
        frames_->push_back (std::string (
          static_cast<const char *> (frame.data ()), frame.size ()));
        const bool more = (frame.flags () & msg_t::more) != 0;
        frame.close ();
        if (!more)
            break;
    }
    return frames_->empty () ? -1 : 0;
}

int spot_data_plane_protocol_t::send_subscription_update (
  socket_base_t *socket_, const std::string &raw_filter_, bool subscribe_)
{
    if (!socket_) {
        errno = EFAULT;
        return -1;
    }

    msg_t msg;
    if (msg.init_size (raw_filter_.size () + 1) != 0)
        return -1;

    unsigned char *data = static_cast<unsigned char *> (msg.data ());
    data[0] = subscribe_ ? 1 : 0;
    if (!raw_filter_.empty ())
        memcpy (data + 1, raw_filter_.data (), raw_filter_.size ());

    const int rc = socket_->send (&msg, 0);
    msg.close ();
    return rc;
}

int spot_data_plane_protocol_t::send_errno_reply (socket_base_t *socket_,
                                                  int error_)
{
    char buf[32];
    snprintf (buf, sizeof (buf), "%d", error_);
    if (spot_io::send_ascii_frame (
          socket_, spot_control_protocol::reply_error, ZLINK_SNDMORE)
        != 0)
        return -1;
    return spot_io::send_ascii_frame (socket_, buf, 0);
}

int spot_data_plane_protocol_t::send_ok_reply (socket_base_t *socket_)
{
    return spot_io::send_ascii_frame (
      socket_, spot_control_protocol::reply_ok, 0);
}

uint64_t spot_data_plane_protocol_t::resolve_bootstrap_broadcast_interval_ms (
  const spot_runtime_t *runtime_, bool bootstrap_ready_)
{
    static uint64_t env_cached = 0;
    static bool env_checked = false;
    if (env_checked)
        return env_cached != 0 ? env_cached
                               : (bootstrap_ready_
                                    ? default_bootstrap_broadcast_interval_ms (
                                        runtime_)
                                    : 1000);

    uint64_t value = 0;
    const char *env = getenv ("ZLINK_SPOT_BOOTSTRAP_INTERVAL_MS");
    if (env && *env) {
        char *end = NULL;
        const unsigned long parsed = strtoul (env, &end, 10);
        if (end != env && parsed > 0)
            value = static_cast<uint64_t> (parsed);
    }

    env_cached = value;
    env_checked = true;
    return env_cached != 0 ? env_cached
                           : (bootstrap_ready_
                                ? default_bootstrap_broadcast_interval_ms (
                                    runtime_)
                                : 1000);
}

}
