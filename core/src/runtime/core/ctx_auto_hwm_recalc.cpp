/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/auto_hwm_policy.hpp"
#include "core/ctx.hpp"
#include "runtime/services/control/service_control_runtime.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/clock.hpp"

void zlink::ctx_t::auto_hwm_recalc_task_main (void *arg_)
{
    static_cast<ctx_t *> (arg_)->auto_hwm_recalc_task ();
}

void zlink::ctx_t::ensure_auto_hwm_recalc_task_started ()
{
    if (_auto_hwm_recalc_task_id != 0)
        return;

    service_control_runtime_t *runtime = service_control_runtime ();
    if (!runtime)
        return;

    _auto_hwm_recalc_task_id =
      runtime->add_periodic_task (&ctx_t::auto_hwm_recalc_task_main, this, 10, false);
}

void zlink::ctx_t::stop_auto_hwm_recalc_task ()
{
    const uint64_t task_id = _auto_hwm_recalc_task_id;
    _auto_hwm_recalc_task_id = 0;
    if (task_id == 0)
        return;

    service_control_runtime_t *runtime = _runtime_resources.service_control_runtime ();
    if (runtime)
        (void) runtime->remove_task (task_id);
}

void zlink::ctx_t::schedule_auto_hwm_recalculate ()
{
    const uint64_t now_ms = zlink::clock_t ().now_ms ();
    int debounce_ms = 0;
    {
        scoped_lock_t locker (_opt_sync);
        debounce_ms = _auto_hwm_recalc_debounce_ms;
    }

    {
        scoped_lock_t lock (_slot_sync);
        _auto_hwm_last_change_ms = now_ms;
        ++_auto_hwm_pending_generation;

        if (debounce_ms <= 0) {
            _auto_hwm_recalc_pending = false;
            _auto_hwm_recalc_deadline_ms = now_ms;
        } else {
            _auto_hwm_recalc_pending = true;
            _auto_hwm_recalc_deadline_ms = now_ms + static_cast<uint64_t> (debounce_ms);
            ensure_auto_hwm_recalc_task_started ();
            if (_auto_hwm_recalc_task_id != 0) {
                service_control_runtime_t *runtime = _runtime_resources.service_control_runtime ();
                if (runtime)
                    (void) runtime->wakeup_task (_auto_hwm_recalc_task_id);
            }
        }
    }

    if (debounce_ms <= 0)
        (void) auto_hwm_recalculate_now ();
}

int zlink::ctx_t::auto_hwm_recalculate_now ()
{
    bool enabled = false;
    zlink_auto_hwm_profile_t profile = ZLINK_CTX_AUTO_HWM_PROFILE_DFLT;
    int message_unit_bytes = ZLINK_CTX_AUTO_HWM_MSG_UNIT_BYTES_DFLT;
    {
        scoped_lock_t locker (_opt_sync);
        enabled = _auto_hwm_enabled;
        profile = _auto_hwm_profile;
        message_unit_bytes = _auto_hwm_msg_unit_bytes;
    }

    scoped_lock_t runtime_lock (_slot_sync);
    _auto_hwm_recalc_pending = false;
    _auto_hwm_recalc_deadline_ms = 0;
    _auto_hwm_last_applied_generation = _auto_hwm_pending_generation;

    if (!enabled)
        return 0;

    std::vector<socket_base_t *> sockets;
    _socket_registry.collect_sockets (&sockets);
    if (sockets.empty ())
        return 0;

    auto_hwm_context_plan_t context_plan;
    auto_hwm_context_plan_make (enabled, profile, &context_plan, message_unit_bytes);

    std::vector<auto_hwm_socket_plan_t> plans;
    plans.reserve (sockets.size ());
    for (size_t i = 0; i < sockets.size (); ++i) {
        socket_base_t *socket = sockets[i];
        if (!socket) {
            plans.push_back (auto_hwm_socket_plan_t ());
            continue;
        }

        auto_hwm_socket_plan_t plan = socket->prepare_auto_hwm_socket_plan (context_plan);
        if (!socket->auto_hwm_policy_enabled ())
            plan.socket_message_slots = 0;
        plans.push_back (plan);
    }

    if (!plans.empty ())
        auto_hwm_context_finalize (&context_plan, &plans[0], plans.size ());

    for (size_t i = 0; i < sockets.size (); ++i) {
        if (!sockets[i])
            continue;
        sockets[i]->apply_auto_hwm_socket_plan (context_plan, plans[i], false,
                                                ZLINK_AUTO_HWM_RECALC_REASON_REFRESH);
    }
    return 0;
}

void zlink::ctx_t::auto_hwm_recalc_task ()
{
    bool should_run = false;
    {
        scoped_lock_t lock (_slot_sync);
        if (_auto_hwm_recalc_pending && zlink::clock_t ().now_ms () >= _auto_hwm_recalc_deadline_ms
            && _auto_hwm_pending_generation != _auto_hwm_last_applied_generation)
            should_run = true;
    }

    if (should_run)
        (void) auto_hwm_recalculate_now ();
}

zlink_auto_hwm_profile_t zlink::ctx_t::auto_hwm_profile () const
{
    scoped_lock_t locker (const_cast<mutex_t &> (_opt_sync));
    return _auto_hwm_profile;
}

bool zlink::ctx_t::auto_hwm_enabled () const
{
    scoped_lock_t locker (const_cast<mutex_t &> (_opt_sync));
    return _auto_hwm_enabled;
}

int zlink::ctx_t::auto_hwm_msg_unit_bytes () const
{
    scoped_lock_t locker (const_cast<mutex_t &> (_opt_sync));
    return _auto_hwm_msg_unit_bytes;
}
