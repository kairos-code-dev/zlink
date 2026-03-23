/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/ctx.hpp"
#include "utils/err.hpp"
#include "utils/ip.hpp"

int zlink_ctx_set_ext (void *ctx_,
                       int option_,
                       const void *optval_,
                       size_t optvallen_);

namespace
{
static bool is_public_ctx_set_option (int option_)
{
    switch (option_) {
        case ZLINK_IO_THREADS:
        case ZLINK_MAX_SOCKETS:
        case ZLINK_THREAD_PRIORITY:
        case ZLINK_THREAD_SCHED_POLICY:
        case ZLINK_MAX_MSGSZ:
        case ZLINK_THREAD_AFFINITY_CPU_ADD:
        case ZLINK_THREAD_AFFINITY_CPU_REMOVE:
        case ZLINK_THREAD_NAME_PREFIX:
        case ZLINK_CTX_OPT_BLOCKY:
            return true;
        default:
            return false;
    }
}

static bool is_public_ctx_get_option (int option_)
{
    return option_ == ZLINK_IO_THREADS || option_ == ZLINK_MAX_SOCKETS
           || option_ == ZLINK_SOCKET_LIMIT || option_ == ZLINK_THREAD_PRIORITY
           || option_ == ZLINK_THREAD_SCHED_POLICY
           || option_ == ZLINK_MAX_MSGSZ || option_ == ZLINK_MSG_T_SIZE
           || option_ == ZLINK_THREAD_AFFINITY_CPU_ADD
           || option_ == ZLINK_THREAD_AFFINITY_CPU_REMOVE
           || option_ == ZLINK_THREAD_NAME_PREFIX
           || option_ == ZLINK_CTX_OPT_BLOCKY;
}
}

void zlink_version (int *major_, int *minor_, int *patch_)
{
    *major_ = ZLINK_VERSION_MAJOR;
    *minor_ = ZLINK_VERSION_MINOR;
    *patch_ = ZLINK_VERSION_PATCH;
}

const char *zlink_strerror (int errnum_)
{
    return zlink::errno_to_string (errnum_);
}

int zlink_errno (void)
{
    return errno;
}

void zlink_monitor_ignore_handler (const zlink_monitor_event_t *, void *userdata_)
{
    LIBZLINK_UNUSED (userdata_);
}

void *zlink_ctx_new (void)
{
    if (!zlink::initialize_network ()) {
        return NULL;
    }

    zlink::ctx_t *ctx = new (std::nothrow) zlink::ctx_t;
    if (ctx) {
        if (!ctx->valid ()) {
            delete ctx;
            return NULL;
        }
    }
    return ctx;
}

int zlink_ctx_term (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }

    const int rc = (static_cast<zlink::ctx_t *> (ctx_))->terminate ();
    const int en = errno;

    if (!rc || en != EINTR) {
        zlink::shutdown_network ();
    }

    errno = en;
    return rc;
}

int zlink_ctx_shutdown (void *ctx_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return (static_cast<zlink::ctx_t *> (ctx_))->shutdown ();
}

int zlink_ctx_set (void *ctx_, zlink_ctx_option_t option_, int optval_)
{
    if (!is_public_ctx_set_option (option_)) {
        errno = EINVAL;
        return -1;
    }
    return zlink_ctx_set_ext (ctx_, option_, &optval_, sizeof (int));
}

int zlink_ctx_set_ext (void *ctx_,
                       int option_,
                       const void *optval_,
                       size_t optvallen_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return (static_cast<zlink::ctx_t *> (ctx_))
      ->set (option_, optval_, optvallen_);
}

int zlink_ctx_get (void *ctx_, zlink_ctx_option_t option_)
{
    if (!is_public_ctx_get_option (option_)) {
        errno = EINVAL;
        return -1;
    }
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return (static_cast<zlink::ctx_t *> (ctx_))->get (option_);
}
