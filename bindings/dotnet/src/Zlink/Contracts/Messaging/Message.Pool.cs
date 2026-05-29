// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Systems.Zlink.Native;

namespace Systems.Zlink;

/// <summary>
/// Represents message.
/// </summary>
public sealed partial class Message : IDisposable, IAsyncDisposable
{
    // Pool-aware adoption used by the routed single-part receive fast path,
    // where Message lifetime is bounded by the immediate caller scope.
    internal static Message AdoptNativeFromPool(ref ZlinkMsg source)
    {
        Message result = RentFromPool();
        result._msg = source;
        result._valid = true;
        result._managedPayload = null;
        result._knownSize = -1;
        source = default;
        return result;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static Message RentFromPool()
    {
        Message[]? pool = t_pool;
        int count = t_poolCount;
        if (pool != null && count > 0)
        {
            count--;
            Message result = pool[count];
            pool[count] = null!;
            t_poolCount = count;
            result._msg = default;
            result._valid = false;
            result._managedPayload = null;
            result._knownSize = -1;
            result._pooled = true;
            return result;
        }

        return new Message(false) { _pooled = true };
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private void TryReturnToPool()
    {
        if (!_pooled)
            return;
        // Defensive: only return when fully released (no managed payload
        // in flight, no native handle pending close).
        if (_valid)
            return;
        if (_managedPayload != null)
            return;
        _pooled = false;
        Message[]? pool = t_pool;
        if (pool == null)
        {
            pool = new Message[PoolCapacity];
            t_pool = pool;
        }
        int count = t_poolCount;
        if (count >= PoolCapacity)
            return;
        pool[count] = this;
        t_poolCount = count + 1;
    }
}
