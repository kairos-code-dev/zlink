// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed partial class Message : IDisposable, IAsyncDisposable
{
    internal ref ZlinkMsg Handle
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get => ref _msg;
    }

    internal bool IsValid => _valid;

    internal void AdoptInitializedNative()
    {
        _valid = true;
    }

    internal void ReplaceNativeOwned(ref ZlinkMsg source)
    {
        Close();
        _msg = source;
        _managedPayload = null;
        _knownSize = -1;
        _valid = true;
        source = default;
    }

    internal void Init()
    {
        if (_valid)
            return;
        int rc = NativeMethods.zlink_msg_init(ref _msg);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        _valid = true;
        _knownSize = 0;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void InitSize(int size)
    {
        if (_valid)
            return;
        if (size < 0)
            throw new ArgumentOutOfRangeException(nameof(size));
        int rc = NativeMethods.zlink_msg_init_size(ref _msg, (nuint)size);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        _valid = true;
        _knownSize = size;
    }

    /// <summary>
    /// Moves native ownership to a new <see cref="Message"/> instance.
    /// </summary>
    /// <returns>The destination message that now owns the payload.</returns>
    /// <remarks>
    /// After a successful move, this instance becomes invalid and any further
    /// access throws <see cref="ObjectDisposedException"/>.
    /// </remarks>
    public Message Move()
    {
        ManagedPayloadState? managed = _managedPayload;
        if (managed != null)
        {
            var movedManaged = new Message(false)
            {
                _managedPayload = managed,
                _knownSize = managed.Length,
                _valid = true
            };
            _managedPayload = null;
            _knownSize = -1;
            _valid = false;
            return movedManaged;
        }

        Message moved = RentFromPool();
        try
        {
            MoveTo(ref moved._msg);
            moved._knownSize = _knownSize;
            moved._valid = true;
            return moved;
        }
        catch
        {
            moved.TryReturnToPool();
            throw;
        }
    }

    public Message Copy()
    {
        EnsureValid();
        ManagedPayloadState? managed = _managedPayload;
        if (managed != null)
        {
            var managedCopy = new Message(false);
            managedCopy.InitializeManagedCopy(
                managed.Bytes.AsSpan(0, managed.Length));
            return managedCopy;
        }
        var copy = new Message(false);
        CopyTo(ref copy._msg);
        copy._knownSize = _knownSize;
        copy._valid = true;
        return copy;
    }

    internal unsafe void MoveTo(ref ZlinkMsg dest)
    {
        EnsureValid();
        ManagedPayloadState? managed = _managedPayload;
        int rc = managed != null
            ? NativeMethods.zlink_msg_init_size(ref dest, (nuint)managed.Length)
            : NativeMethods.zlink_msg_init(ref dest);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        try
        {
            if (managed != null)
            {
                if (managed.Length != 0)
                {
                    IntPtr destPtr = NativeMethods.zlink_msg_data(ref dest);
                    if (destPtr == IntPtr.Zero)
                        throw new InvalidOperationException("Message data is null.");
                    managed.Bytes.AsSpan(0, managed.Length).CopyTo(
                        new Span<byte>((void*)destPtr, managed.Length));
                }
                ReleaseManagedBytes();
            }
            else
            {
                rc = NativeMethods.zlink_msg_move(ref dest, ref _msg);
                if (rc != 0)
                    throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            }
            _valid = false;
            _knownSize = -1;
        }
        catch
        {
            try
            {
                NativeMethods.zlink_msg_close(ref dest);
            }
            catch
            {
            }
            throw;
        }
    }

    internal void RestoreFrom(ref ZlinkMsg src)
    {
        if (_valid)
            throw new InvalidOperationException(
                "RestoreFrom requires an invalid message state.");

        _managedPayload = null;
        _knownSize = -1;
        int rc = NativeMethods.zlink_msg_init(ref _msg);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        try
        {
            rc = NativeMethods.zlink_msg_move(ref _msg, ref src);
            if (rc != 0)
                throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            _valid = true;
            _knownSize = -1;
        }
        catch
        {
            try
            {
                NativeMethods.zlink_msg_close(ref _msg);
            }
            catch
            {
            }
            throw;
        }
    }

    internal unsafe void CopyTo(ref ZlinkMsg dest)
    {
        EnsureValid();
        ManagedPayloadState? managed = _managedPayload;
        int rc = managed != null
            ? NativeMethods.zlink_msg_init_size(ref dest, (nuint)managed.Length)
            : NativeMethods.zlink_msg_init(ref dest);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        try
        {
            if (managed != null)
            {
                if (managed.Length != 0)
                {
                    IntPtr destPtr = NativeMethods.zlink_msg_data(ref dest);
                    if (destPtr == IntPtr.Zero)
                        throw new InvalidOperationException("Message data is null.");
                    managed.Bytes.AsSpan(0, managed.Length).CopyTo(
                        new Span<byte>((void*)destPtr, managed.Length));
                }
            }
            else
            {
                rc = NativeMethods.zlink_msg_copy(ref dest, ref _msg);
                if (rc != 0)
                    throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            }
        }
        catch
        {
            try
            {
                NativeMethods.zlink_msg_close(ref dest);
            }
            catch
            {
            }
            throw;
        }
    }

    internal static unsafe int CopySinglePartPayload(IntPtr parts, nuint count,
        Span<byte> destination)
    {
        if (parts == IntPtr.Zero || count == 0)
            return 0;
        if (count != 1)
            throw new InvalidOperationException(
                "Expected a single-part message.");

        ZlinkMsg* msgv = (ZlinkMsg*)parts;
        int payloadSize = (int)NativeMethods.zlink_msg_size(ref msgv[0]);
        if (payloadSize <= 0)
            return 0;

        IntPtr payloadPtr = NativeMethods.zlink_msg_data(ref msgv[0]);
        if (payloadPtr == IntPtr.Zero)
            return 0;

        int toCopy = payloadSize;
        if (toCopy > destination.Length)
            toCopy = destination.Length;
        new ReadOnlySpan<byte>((void*)payloadPtr, toCopy).CopyTo(destination);
        return payloadSize;
    }

    internal static unsafe Message[] CopyFromNativeReadOnlyVector(IntPtr parts,
        nuint count)
    {
        if (parts == IntPtr.Zero || count == 0)
            return Array.Empty<Message>();

        int length = checked((int)count);
        Message[] result = new Message[length];
        int built = 0;
        try
        {
            ZlinkMsg* msgv = (ZlinkMsg*)parts;
            for (int i = 0; i < length; i++)
            {
                int size = checked((int)NativeMethods.zlink_msg_size(ref msgv[i]));
                if (size <= 0)
                {
                    result[i] = new Message(0);
                }
                else
                {
                    IntPtr payloadPtr = NativeMethods.zlink_msg_data(ref msgv[i]);
                    if (payloadPtr == IntPtr.Zero)
                    {
                        result[i] = new Message(0);
                    }
                    else
                    {
                        ReadOnlySpan<byte> payload = new ReadOnlySpan<byte>(
                            (void*)payloadPtr, size);
                        result[i] = new Message(payload);
                    }
                }
                built++;
            }
            return result;
        }
        catch
        {
            for (int i = 0; i < built; i++)
                result[i]?.Dispose();
            throw;
        }
    }

    internal static Message[] FromNativeVector(IntPtr parts, nuint count)
    {
        if (parts == IntPtr.Zero || count == 0)
            return Array.Empty<Message>();
        int length = checked((int)count);
        Message[] result = new Message[length];
        int built = 0;
        try
        {
            unsafe
            {
                ZlinkMsg* src = (ZlinkMsg*)parts;
                for (int i = 0; i < length; i++)
                {
                    var msg = new Message(false);
                    msg.Init();
                    int rc = NativeMethods.zlink_msg_move(ref msg._msg,
                        ref src[i]);
                    if (rc != 0)
                    {
                        msg.Dispose();
                        throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
                    }
                    msg._knownSize = -1;
                    result[i] = msg;
                    built++;
                }
            }
        }
        catch
        {
            for (int i = 0; i < built; i++)
                result[i]?.Dispose();
            throw;
        }
        finally
        {
            NativeMethods.zlink_multipart_close(parts, count);
        }
        return result;
    }

    internal static unsafe Message MoveFromNativeSingle(IntPtr message)
    {
        if (message == IntPtr.Zero)
            throw new ArgumentNullException(nameof(message));

        var result = new Message(false);
        result.Init();
        try
        {
            ZlinkMsg* src = (ZlinkMsg*)message;
            int rc = NativeMethods.zlink_msg_move(ref result._msg, ref *src);
            if (rc != 0)
                throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            result._knownSize = -1;
            return result;
        }
        catch
        {
            result.Dispose();
            throw;
        }
    }

    internal static Message MoveFromNative(ref ZlinkMsg source)
    {
        var result = new Message(false);
        result.Init();
        try
        {
            int rc = NativeMethods.zlink_msg_move(ref result._msg, ref source);
            if (rc != 0)
                throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            result._knownSize = -1;
            return result;
        }
        catch
        {
            result.Dispose();
            throw;
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static Message AdoptNative(ref ZlinkMsg source)
    {
        var result = new Message(false)
        {
            _msg = source,
            _knownSize = -1,
            _valid = true
        };
        source = default;
        return result;
    }

    // Pool-aware adoption. Returns either a recycled Message from the
    // thread-local pool (no heap allocation) or a fresh instance tagged
    // for pool-return on Dispose. Used by the routed single-part recv
    // fast path (TryReceiveRoutedSingleUnchecked) where Message lifetime
    // is bounded by the immediate using-scope of the perf caller.
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
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

    internal static unsafe Message CopyFromNativeSingle(IntPtr message)
    {
        if (message == IntPtr.Zero)
            throw new ArgumentNullException(nameof(message));

        ZlinkMsg* src = (ZlinkMsg*)message;
        int size = checked((int)NativeMethods.zlink_msg_size(ref *src));
        if (size <= 0)
            return new Message(0);

        IntPtr payloadPtr = NativeMethods.zlink_msg_data(ref *src);
        if (payloadPtr == IntPtr.Zero)
            return new Message(0);

        ReadOnlySpan<byte> payload = new ReadOnlySpan<byte>((void*)payloadPtr, size);
        return new Message(payload);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void DetachAfterSend()
    {
        EnsureValid();
        ReleaseManagedBytes();
        _valid = false;
        _knownSize = -1;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void DisposeNativeOwned()
    {
        if (!_valid)
        {
            TryReturnToPool();
            return;
        }
        NativeMethods.zlink_msg_close(ref _msg);
        _valid = false;
        _knownSize = -1;
        TryReturnToPool();
    }

    private void Close()
    {
        if (!_valid)
            return;
        if (_managedPayload == null)
            NativeMethods.zlink_msg_close(ref _msg);
        ReleaseManagedBytes();
        _valid = false;
        _knownSize = -1;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private void EnsureValid()
    {
        if (!_valid)
            throw new ObjectDisposedException(nameof(Message));
    }

    private void InitializeManagedCopy(ReadOnlySpan<byte> data)
    {
        _managedPayload = new ManagedPayloadState(data.ToArray(), data.Length);
        _knownSize = data.Length;
        _valid = true;
    }

    private void ReleaseManagedBytes()
    {
        _managedPayload = null;
    }

    private sealed class ManagedPayloadState
    {
        internal ManagedPayloadState(byte[] bytes, int length)
        {
            Bytes = bytes;
            Length = length;
        }

        internal byte[] Bytes { get; }
        internal int Length { get; }
    }
}
