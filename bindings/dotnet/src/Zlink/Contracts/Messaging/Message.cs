// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Runtime.CompilerServices;
using System.Text;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed class Message : IDisposable, IAsyncDisposable
{
    private ZlinkMsg _msg;
    private bool _valid;
    private ManagedPayloadState? _managedPayload;
    // Marks instances created via the thread-local hot-path pool. Dispose
    // returns these to the pool instead of letting them be GC'd, which
    // eliminates the per-message wrapper allocation in routed echo
    // workloads (100 clients × every message on both server and clients).
    private bool _pooled;

    [ThreadStatic]
    private static Message[]? t_pool;
    [ThreadStatic]
    private static int t_poolCount;
    private const int PoolCapacity = 256;

    public Message()
    {
        Init();
    }

    public Message(int size)
    {
        if (size < 0)
            throw new ArgumentOutOfRangeException(nameof(size));
        int rc = NativeMethods.zlink_msg_init_size(ref _msg, (nuint)size);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        _valid = true;
    }

    public Message(ReadOnlySpan<byte> data) : this(data.Length)
    {
        if (data.Length == 0)
            return;
        unsafe
        {
            IntPtr dest = NativeMethods.zlink_msg_data(ref _msg);
            if (dest == IntPtr.Zero)
                throw new InvalidOperationException("Message data is null.");
            data.CopyTo(new Span<byte>((void*)dest, data.Length));
        }
    }

    public Message(ReadOnlyMemory<byte> data) : this(data.Span)
    {
    }

    internal Message(bool init)
    {
        if (init)
            Init();
    }

    public int Size
    {
        get
        {
            EnsureValid();
            if (_managedPayload != null)
                return _managedPayload.Length;
            return (int)NativeMethods.zlink_msg_size(ref _msg);
        }
    }

    public int RefCount
    {
        get
        {
            EnsureValid();
            if (_managedPayload != null)
                return 1;
            return NativeMethods.zlink_msg_refcnt(ref _msg);
        }
    }

    public byte[] ToArray()
    {
        return AsReadOnlySpan().ToArray();
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public unsafe ReadOnlySpan<byte> AsReadOnlySpan()
    {
        EnsureValid();
        ManagedPayloadState? managed = _managedPayload;
        if (managed != null)
            return managed.Bytes.AsSpan(0, managed.Length);
        nuint size = NativeMethods.zlink_msg_size(ref _msg);
        if (size == 0)
            return ReadOnlySpan<byte>.Empty;
        IntPtr data = NativeMethods.zlink_msg_data(ref _msg);
        if (data == IntPtr.Zero)
            return ReadOnlySpan<byte>.Empty;
        return new ReadOnlySpan<byte>((void*)data, (int)size);
    }

    public ReadOnlyMemory<byte> AsReadOnlyMemory()
    {
        ManagedPayloadState? managed = _managedPayload;
        if (managed != null)
            return managed.Bytes.AsMemory(0, managed.Length);
        return ToArray();
    }

    public int CopyTo(Span<byte> destination)
    {
        if (!TryCopyTo(destination, out int bytesWritten))
            throw new ArgumentException("Destination buffer is too small.",
                nameof(destination));
        return bytesWritten;
    }

    public int CopyTo(IBufferWriter<byte> destination)
    {
        if (destination == null)
            throw new ArgumentNullException(nameof(destination));

        int size = Size;
        Span<byte> span = destination.GetSpan(size).Slice(0, size);
        int written = CopyTo(span);
        destination.Advance(written);
        return written;
    }

    public unsafe bool TryCopyTo(Span<byte> destination, out int bytesWritten)
    {
        EnsureValid();
        ManagedPayloadState? managed = _managedPayload;
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
        nuint size = NativeMethods.zlink_msg_size(ref _msg);
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
        IntPtr data = NativeMethods.zlink_msg_data(ref _msg);
        if (data == IntPtr.Zero)
        {
            bytesWritten = 0;
            return true;
        }
        new ReadOnlySpan<byte>((void*)data, (int)size).CopyTo(destination);
        bytesWritten = (int)size;
        return true;
    }

    /// <summary>
    /// Create a message containing a snapshot copy of <paramref name="data"/>.
    /// The caller may freely mutate or discard <paramref name="data"/> after
    /// this call returns; the message holds its own copy of the payload.
    /// </summary>
    /// <remarks>
    /// When the caller can guarantee the array will not be mutated before the
    /// message is sent or disposed, prefer <see cref="WrapBytes(byte[])"/> to
    /// avoid the snapshot copy.
    /// </remarks>
    public static Message FromBytes(byte[] data)
    {
        if (data == null)
            throw new ArgumentNullException(nameof(data));
        var message = new Message(false);
        message.InitializeManagedCopy(data.AsSpan());
        return message;
    }

    /// <summary>
    /// Create a message that references <paramref name="data"/> in place,
    /// without copying the payload bytes. The byte array is pinned only at
    /// send time and released when the underlying transport finishes with the
    /// message.
    /// </summary>
    /// <remarks>
    /// Zero-copy. The caller MUST NOT mutate <paramref name="data"/> from when
    /// this call returns until the message is either sent successfully or
    /// disposed: an in-flight mutation corrupts the transmitted payload.
    /// Use <see cref="FromBytes(byte[])"/> when the buffer might be mutated
    /// before send.
    /// </remarks>
    /// <param name="data">Byte buffer the message references. Must remain
    /// unmodified for the lifetime of the returned message.</param>
    public static Message WrapBytes(byte[] data)
    {
        if (data == null)
            throw new ArgumentNullException(nameof(data));
        var message = new Message(false);
        message.InitializeManagedOwned(data);
        return message;
    }

    public static Message FromBytes(ReadOnlySpan<byte> data)
    {
        var message = new Message(false);
        message.InitializeManagedCopy(data);
        return message;
    }

    public static Message FromBytes(ReadOnlyMemory<byte> data)
    {
        var message = new Message(false);
        message.InitializeManagedCopy(data.Span);
        return message;
    }

    public static Message FromSequence(ReadOnlySequence<byte> data)
    {
        if (data.IsSingleSegment)
            return new Message(data.First);
        return new Message((ReadOnlySpan<byte>)data.ToArray());
    }

    public static Message FromString(string value)
    {
        return FromString(value, Encoding.UTF8);
    }

    public static Message FromString(string value, Encoding encoding)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        if (encoding == null)
            throw new ArgumentNullException(nameof(encoding));
        return new Message((ReadOnlySpan<byte>)encoding.GetBytes(value));
    }

    public string GetString()
    {
        return GetString(Encoding.UTF8);
    }

    public string GetString(Encoding encoding)
    {
        if (encoding == null)
            throw new ArgumentNullException(nameof(encoding));
        return encoding.GetString(AsReadOnlySpan());
    }

    public string? GetProperty(string property)
    {
        EnsureValid();
        if (_managedPayload != null)
            return null;
        IntPtr ptr = NativeMethods.zlink_msg_gets(ref _msg, property);
        if (ptr == IntPtr.Zero)
            return null;
        return Marshal.PtrToStringUTF8(ptr);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void Dispose()
    {
        ManagedPayloadState? managed = _managedPayload;
        Close();
        if (managed is { InFlight: true })
            managed.DisposeAfterBorrowedSend = true;
        else
            ReleaseSelfHandle(managed);
        TryReturnToPool();
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

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

    internal void Init()
    {
        if (_valid)
            return;
            int rc = NativeMethods.zlink_msg_init(ref _msg);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        _valid = true;
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
                _valid = true
            };
            _managedPayload = null;
            _valid = false;
            return movedManaged;
        }

        Message moved = RentFromPool();
        try
        {
            MoveTo(ref moved._msg);
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
        if (_managedPayload != null)
            MaterializeManagedBytes();
        var copy = new Message(false);
        CopyTo(ref copy._msg);
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
        int rc = NativeMethods.zlink_msg_init(ref _msg);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        try
        {
            rc = NativeMethods.zlink_msg_move(ref _msg, ref src);
            if (rc != 0)
                throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            _valid = true;
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
            result._pooled = true;
            return result;
        }

        return new Message(false) { _pooled = true };
    }

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
        if (_managedPayload is not { InFlight: true })
            ReleaseManagedBytes();
        _valid = false;
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
        TryReturnToPool();
    }

    internal bool TryPrepareBorrowedSend(out IntPtr data, out int length,
        out IntPtr hint)
    {
        EnsureValid();
        ManagedPayloadState? managed = _managedPayload;
        if (managed == null || managed.InFlight)
        {
            data = IntPtr.Zero;
            length = 0;
            hint = IntPtr.Zero;
            return false;
        }

        managed.PinnedHandle = GCHandle.Alloc(managed.Bytes, GCHandleType.Pinned);
        managed.InFlight = true;
        data = managed.Length == 0
            ? IntPtr.Zero
            : managed.PinnedHandle.AddrOfPinnedObject();
        length = managed.Length;
        hint = GCHandle.ToIntPtr(managed.PinnedHandle);
        return true;
    }

    internal void DetachAfterPreparedSend()
    {
        EnsureValid();
        _managedPayload = null;
        _valid = false;
    }

    internal void CancelBorrowedSendPrepare()
    {
        ManagedPayloadState? managed = _managedPayload;
        if (managed == null)
            return;
        if (managed.PinnedHandle.IsAllocated)
            managed.PinnedHandle.Free();
        managed.InFlight = false;
        managed.DisposeAfterBorrowedSend = false;
    }

    internal static void CompleteBorrowedSend(GCHandle handle)
    {
        if (handle.Target is Message message)
        {
            message.CompleteBorrowedSendCore();
            return;
        }
        if (handle.IsAllocated)
            handle.Free();
    }

    private void Close()
    {
        if (!_valid)
            return;
        if (_managedPayload == null)
            NativeMethods.zlink_msg_close(ref _msg);
        if (_managedPayload is not { InFlight: true })
            ReleaseManagedBytes();
        _valid = false;
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
        _valid = true;
    }

    private void InitializeManagedOwned(byte[] data)
    {
        _managedPayload = new ManagedPayloadState(data, data.Length);
        _valid = true;
    }

    private unsafe void MaterializeManagedBytes()
    {
        ManagedPayloadState? managed = _managedPayload;
        if (managed == null)
            return;

        ZlinkMsg native = default;
        int initRc = NativeMethods.zlink_msg_init_size(ref native,
            (nuint)managed.Length);
        if (initRc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());

        try
        {
            if (managed.Length != 0)
            {
                IntPtr destPtr = NativeMethods.zlink_msg_data(ref native);
                if (destPtr == IntPtr.Zero)
                    throw new InvalidOperationException("Message data is null.");

                managed.Bytes.AsSpan(0, managed.Length).CopyTo(
                    new Span<byte>((void*)destPtr, managed.Length));
            }

            _msg = native;
            _managedPayload = null;
        }
        catch
        {
            NativeMethods.zlink_msg_close(ref native);
            throw;
        }
    }

    private void ReleaseManagedBytes()
    {
        _managedPayload = null;
    }

    private void CompleteBorrowedSendCore()
    {
        ManagedPayloadState? managed = _managedPayload;
        if (managed == null)
            return;
        if (managed.PinnedHandle.IsAllocated)
            managed.PinnedHandle.Free();
        managed.InFlight = false;
        ReleaseManagedBytes();
        if (managed.DisposeAfterBorrowedSend)
        {
            managed.DisposeAfterBorrowedSend = false;
            ReleaseSelfHandle(managed);
            TryReturnToPool();
        }
    }

    private static void ReleaseSelfHandle(ManagedPayloadState? managed)
    {
        if (managed?.SelfHandle.IsAllocated == true)
            managed.SelfHandle.Free();
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
        internal GCHandle PinnedHandle;
        internal GCHandle SelfHandle;
        internal bool InFlight;
        internal bool DisposeAfterBorrowedSend;
    }
}
