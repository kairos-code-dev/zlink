// SPDX-License-Identifier: MPL-2.0

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

// Continuation of Message; the type summary lives on the primary partial in
// Message.cs.
public sealed partial class Message : IDisposable, IAsyncDisposable
{
    internal ref ZlinkMsg Handle
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get => ref _msg;
    }

    internal bool IsValid { get; private set; }

    internal void AdoptInitializedNative()
    {
        IsValid = true;
    }

    internal void ReplaceNativeOwned(ref ZlinkMsg source)
    {
        Close();
        _msg = source;
        _managedPayload = null;
        _knownSize = -1;
        IsValid = true;
        source = default;
    }

    internal void Init()
    {
        if (IsValid)
            return;
        var rc = NativeMethods.zlink_msg_init(ref _msg);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        IsValid = true;
        _knownSize = 0;
    }

    private int GetSizeCore()
    {
        EnsureValid();
        if (_managedPayload != null)
            return _managedPayload.Length;
        if (_knownSize >= 0)
            return _knownSize;
        return GetNativeSize(ref _msg);
    }

    private int GetRefCountCore()
    {
        EnsureValid();
        if (_managedPayload != null)
            return 1;
        return NativeMethods.zlink_msg_refcnt(ref _msg);
    }

    private unsafe void CopyPayloadToStorage(ReadOnlySpan<byte> data)
    {
        if (data.Length == 0)
            return;
        var dest = NativeMethods.zlink_msg_data(ref _msg);
        if (dest == IntPtr.Zero)
            throw new InvalidOperationException("Message data is null.");
        data.CopyTo(new Span<byte>((void*)dest, data.Length));
    }

    private static Message AllocateCore(int size)
    {
        if (size < 0)
            throw new ArgumentOutOfRangeException(nameof(size));
        var message = RentFromPool();
        message.InitSize(size);
        return message;
    }

    private unsafe Span<byte> AsSpanCore()
    {
        EnsureValid();
        var managed = _managedPayload;
        if (managed != null)
            return managed.Bytes.AsSpan(0, managed.Length);
        var size = _knownSize >= 0
            ? _knownSize
            : GetNativeSize(ref _msg);
        if (size == 0)
            return Span<byte>.Empty;
        var data = NativeMethods.zlink_msg_data(ref _msg);
        if (data == IntPtr.Zero)
            return Span<byte>.Empty;
        return new Span<byte>((void*)data, size);
    }

    private unsafe ReadOnlySpan<byte> AsReadOnlySpanCore()
    {
        EnsureValid();
        var managed = _managedPayload;
        if (managed != null)
            return managed.Bytes.AsSpan(0, managed.Length);
        var size = _knownSize >= 0
            ? _knownSize
            : GetNativeSize(ref _msg);
        if (size == 0)
            return ReadOnlySpan<byte>.Empty;
        var data = NativeMethods.zlink_msg_data(ref _msg);
        if (data == IntPtr.Zero)
            return ReadOnlySpan<byte>.Empty;
        return new ReadOnlySpan<byte>((void*)data, size);
    }

    private ReadOnlyMemory<byte> AsReadOnlyMemoryCore()
    {
        var managed = _managedPayload;
        if (managed != null)
            return managed.Bytes.AsMemory(0, managed.Length);
        return ToArray();
    }

    private unsafe bool TryCopyToCore(Span<byte> destination,
        out int bytesWritten)
    {
        EnsureValid();
        var managed = _managedPayload;
        if (managed != null)
        {
            if (managed.Length > destination.Length)
            {
                bytesWritten = 0;
                return false;
            }

            managed.Bytes.AsSpan(0, managed.Length).CopyTo(destination);
            bytesWritten = managed.Length;
            return true;
        }

        var size = _knownSize >= 0
            ? (nuint)_knownSize
            : NativeMethods.zlink_msg_size(ref _msg);
        if (size == 0)
        {
            bytesWritten = 0;
            return true;
        }

        if (size > (nuint)destination.Length)
        {
            bytesWritten = 0;
            return false;
        }

        var data = NativeMethods.zlink_msg_data(ref _msg);
        if (data == IntPtr.Zero)
        {
            bytesWritten = 0;
            return true;
        }

        new ReadOnlySpan<byte>((void*)data, (int)size).CopyTo(destination);
        bytesWritten = (int)size;
        return true;
    }

    private string? GetPropertyCore(string property)
    {
        EnsureValid();
        if (_managedPayload != null)
            return null;
        var ptr = NativeMethods.zlink_msg_gets(ref _msg, property);
        if (ptr == IntPtr.Zero)
            return null;
        return Marshal.PtrToStringUTF8(ptr);
    }

    private void DisposeCore()
    {
        if (!IsValid && _managedPayload == null)
        {
            TryReturnToPool();
            return;
        }

        Close();
        TryReturnToPool();
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void InitSize(int size)
    {
        if (IsValid)
            return;
        if (size < 0)
            throw new ArgumentOutOfRangeException(nameof(size));
        var rc = NativeMethods.zlink_msg_init_size(ref _msg, (nuint)size);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        IsValid = true;
        _knownSize = size;
    }

    internal Message Move()
    {
        var managed = _managedPayload;
        if (managed != null)
        {
            var movedManaged = new Message(false)
            {
                _managedPayload = managed,
                _knownSize = managed.Length,
                IsValid = true
            };
            _managedPayload = null;
            _knownSize = -1;
            IsValid = false;
            return movedManaged;
        }

        var moved = RentFromPool();
        try
        {
            MoveTo(ref moved._msg);
            moved._knownSize = _knownSize;
            moved.IsValid = true;
            return moved;
        }
        catch
        {
            moved.TryReturnToPool();
            throw;
        }
    }

    /// <summary>
    ///     Creates a new message holding an independent copy of this payload; the
    ///     caller owns and must dispose it.
    /// </summary>
    public Message Copy()
    {
        EnsureValid();
        var managed = _managedPayload;
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
        copy.IsValid = true;
        return copy;
    }

    internal unsafe void MoveTo(ref ZlinkMsg dest)
    {
        EnsureValid();
        var managed = _managedPayload;
        var rc = managed != null
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
                    var destPtr = NativeMethods.zlink_msg_data(ref dest);
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

            IsValid = false;
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
        if (IsValid)
            throw new InvalidOperationException(
                "RestoreFrom requires an invalid message state.");

        _managedPayload = null;
        _knownSize = -1;
        var rc = NativeMethods.zlink_msg_init(ref _msg);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        try
        {
            rc = NativeMethods.zlink_msg_move(ref _msg, ref src);
            if (rc != 0)
                throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            IsValid = true;
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
        var managed = _managedPayload;
        var rc = managed != null
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
                    var destPtr = NativeMethods.zlink_msg_data(ref dest);
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

    internal static Message AdoptNative(ref ZlinkMsg source)
    {
        var result = new Message(false)
        {
            _msg = source,
            _knownSize = -1,
            IsValid = true
        };
        source = default;
        return result;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static unsafe Message CopyFromNativeSingle(IntPtr message)
    {
        if (message == IntPtr.Zero)
            throw new ArgumentNullException(nameof(message));

        var src = (ZlinkMsg*)message;
        var size = GetNativeSize(ref *src);
        if (size <= 0)
            return new Message(0);

        var payloadPtr = NativeMethods.zlink_msg_data(ref *src);
        if (payloadPtr == IntPtr.Zero)
            return new Message(0);

        var payload = new ReadOnlySpan<byte>((void*)payloadPtr, size);
        return new Message(payload);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void DetachAfterSend()
    {
        EnsureValid();
        ReleaseManagedBytes();
        IsValid = false;
        _knownSize = -1;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void DisposeNativeOwned()
    {
        if (!IsValid)
        {
            TryReturnToPool();
            return;
        }

        NativeMethods.zlink_msg_close(ref _msg);
        IsValid = false;
        _knownSize = -1;
        TryReturnToPool();
    }

    private void Close()
    {
        if (!IsValid)
            return;
        if (_managedPayload == null)
            NativeMethods.zlink_msg_close(ref _msg);
        ReleaseManagedBytes();
        IsValid = false;
        _knownSize = -1;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private void EnsureValid()
    {
        if (!IsValid)
            throw new ObjectDisposedException(nameof(Message));
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static int GetNativeSize(ref ZlinkMsg msg)
    {
        return checked((int)NativeMethods.zlink_msg_size(ref msg));
    }

    private void InitializeManagedCopy(ReadOnlySpan<byte> data)
    {
        InitSize(data.Length);
        if (data.Length != 0)
            CopyPayloadToStorage(data);
        _knownSize = data.Length;
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
