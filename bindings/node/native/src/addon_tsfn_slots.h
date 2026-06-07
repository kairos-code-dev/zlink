/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "addon_common_api.h"

template <typename Slot> inline Slot *find_free_tsfn_slot (Slot *slots, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (!slots[i].used)
            return &slots[i];
    }
    return NULL;
}

template <typename Slot, typename Subject>
inline Slot *
find_tsfn_slot_by_subject (Slot *slots, size_t count, Subject Slot::*member, Subject subject)
{
    for (size_t i = 0; i < count; ++i) {
        if (slots[i].used && slots[i].*member == subject)
            return &slots[i];
    }
    return NULL;
}

template <typename Slot> inline void reset_tsfn_slot_base (Slot *state)
{
    if (!state)
        return;
    state->used = false;
    state->env = NULL;
    state->tsfn = NULL;
}

template <typename State> inline bool release_request_tsfn (State *state)
{
    if (!state || !state->tsfn)
        return false;
    (void) napi_release_threadsafe_function (state->tsfn, napi_tsfn_release);
    state->tsfn = NULL;
    return true;
}
