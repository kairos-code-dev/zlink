/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#ifndef ZLINK_HAVE_WINDOWS
#include <unistd.h>
#endif

#include "core/ctx.hpp"
#include "core/ctx_bootstrap.hpp"
#include "services/control/service_control_runtime.hpp"

namespace
{
const int ctx_bootstrap_retry_count = 50;
}

bool zlink::ctx_bootstrap_t::start_runtime_locked (ctx_t &ctx_)
{
    ctx_._opt_sync.lock ();
    const int max_sockets = ctx_._max_sockets;
    const int ios = ctx_._io_thread_count;
    ctx_._opt_sync.unlock ();

    if (!ctx_._runtime_resources.start_locked (
          ctx_, ctx_._socket_registry, ctx_._term_mailbox, max_sockets, ios))
        return false;

    ctx_._starting = false;
    return true;
}

zlink::service_control_runtime_t *
zlink::ctx_bootstrap_t::ensure_service_runtime (ctx_t &ctx_)
{
    int last_errno = ENOTSUP;
    for (int attempt = 0; attempt < ctx_bootstrap_retry_count; ++attempt) {
        ctx_._slot_sync.lock ();
        if (ctx_._terminating) {
            ctx_._slot_sync.unlock ();
            errno = ETERM;
            return NULL;
        }
        service_control_runtime_t *runtime =
          ctx_._runtime_resources.service_control_runtime ();
        if (runtime) {
            ctx_._slot_sync.unlock ();
            return runtime;
        }
        if (!ctx_._starting) {
            ctx_._slot_sync.unlock ();
            errno = ENOTSUP;
            return NULL;
        }

        const bool started = start_runtime_locked (ctx_);
        runtime = ctx_._runtime_resources.service_control_runtime ();
        last_errno = errno;
        ctx_._slot_sync.unlock ();
        if (started && runtime)
            return runtime;
#ifndef ZLINK_HAVE_WINDOWS
        usleep (1000);
#endif
    }

    errno = last_errno;
    return NULL;
}
