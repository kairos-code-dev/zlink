/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <new>

#include "core/ctx_runtime_resources.hpp"

#include "core/ctx.hpp"
#include "core/ctx_socket_registry.hpp"
#include "core/mailbox.hpp"
#include "core/io_thread.hpp"
#include "core/reaper.hpp"
#include "services/control/service_control_runtime.hpp"

namespace
{
const int term_and_reaper_threads_count = 2;

}

zlink::ctx_runtime_resources_t::ctx_runtime_resources_t () :
    _reaper (NULL), _service_control_runtime (NULL)
{
}

bool zlink::ctx_runtime_resources_t::start_locked (ctx_t &ctx_,
                                                   ctx_socket_registry_t &socket_registry_,
                                                   mailbox_t &term_mailbox_,
                                                   int max_sockets_,
                                                   int io_thread_count_)
{
    const int slot_count = max_sockets_ + io_thread_count_ + term_and_reaper_threads_count;

    if (!socket_registry_.initialize_slot_pool (
          slot_count, io_thread_count_ + term_and_reaper_threads_count, &term_mailbox_))
        return false;

    if (!start_reaper_locked (ctx_, socket_registry_) || !start_service_runtime_locked (ctx_)
        || !start_io_threads_locked (ctx_, socket_registry_, io_thread_count_)) {
        cleanup_failed_start_locked (ctx_, socket_registry_);
        return false;
    }

    return true;
}

void zlink::ctx_runtime_resources_t::teardown (ctx_t &ctx_, ctx_socket_registry_t &socket_registry_)
{
    LIBZLINK_UNUSED (ctx_);

    if (_service_control_runtime) {
        _service_control_runtime->stop ();
        delete _service_control_runtime;
        _service_control_runtime = NULL;
    }
    _io_thread_registry.stop_all ();
    _io_thread_registry.destroy_all ();

    LIBZLINK_DELETE (_reaper);
    _reaper = NULL;
    socket_registry_.clear ();
}

zlink::service_control_runtime_t *zlink::ctx_runtime_resources_t::service_control_runtime () const
{
    return _service_control_runtime;
}

zlink::object_t *zlink::ctx_runtime_resources_t::reaper_object () const
{
    return _reaper;
}

void zlink::ctx_runtime_resources_t::stop_reaper ()
{
    if (_reaper)
        _reaper->stop ();
}

zlink::io_thread_t *zlink::ctx_runtime_resources_t::choose_io_thread (uint64_t affinity_)
{
    return _io_thread_registry.choose (affinity_);
}

zlink::io_thread_t *zlink::ctx_runtime_resources_t::choose_io_thread_stream (uint64_t affinity_)
{
    return _io_thread_registry.choose_stream (affinity_);
}

void zlink::ctx_runtime_resources_t::cleanup_failed_start_locked (
  ctx_t &ctx_, ctx_socket_registry_t &socket_registry_)
{
    teardown (ctx_, socket_registry_);
}

bool zlink::ctx_runtime_resources_t::start_reaper_locked (ctx_t &ctx_,
                                                          ctx_socket_registry_t &socket_registry_)
{
    _reaper = new (std::nothrow) reaper_t (&ctx_, ctx_t::reaper_tid);
    if (!_reaper) {
        errno = ENOMEM;
        return false;
    }
    if (!_reaper->get_mailbox ()->valid ())
        return false;

    socket_registry_.bind_mailbox (ctx_t::reaper_tid, _reaper->get_mailbox ());
    _reaper->start ();
    return true;
}

bool zlink::ctx_runtime_resources_t::start_service_runtime_locked (ctx_t &ctx_)
{
    _service_control_runtime = new (std::nothrow) service_control_runtime_t (&ctx_, "service-ctrl");
    if (!_service_control_runtime) {
        errno = ENOMEM;
        return false;
    }

    return _service_control_runtime->start ();
}

bool zlink::ctx_runtime_resources_t::start_io_threads_locked (
  ctx_t &ctx_, ctx_socket_registry_t &socket_registry_, int io_thread_count_)
{
    for (int i = term_and_reaper_threads_count;
         i != io_thread_count_ + term_and_reaper_threads_count; ++i) {
        io_thread_t *io_thread = new (std::nothrow) io_thread_t (&ctx_, i);
        if (!io_thread) {
            errno = ENOMEM;
            return false;
        }
        if (!io_thread->get_mailbox ()->valid ()) {
            LIBZLINK_DELETE (io_thread);
            return false;
        }

        _io_thread_registry.add (io_thread);
        socket_registry_.bind_mailbox (i, io_thread->get_mailbox ());
        io_thread->start ();
    }

    return true;
}
