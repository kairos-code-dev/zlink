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
    private int _knownSize = -1;
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
        _knownSize = size;
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
            if (_knownSize >= 0)
                return _knownSize;
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

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Message Allocate(int size)
    {
        if (size < 0)
            throw new ArgumentOutOfRangeException(nameof(size));
        Message message = RentFromPool();
        message.InitSize(size);
        return message;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public unsafe Span<byte> AsSpan()
    {
        EnsureValid();
        ManagedPayloadState? managed = _managedPayload;
        if (managed != null)
            return managed.Bytes.AsSpan(0, managed.Length);
        bool hasKnownSize = _knownSize >= 0;
        int size = hasKnownSize
            ? _knownSize
            : (int)NativeMethods.zlink_msg_size(ref _msg);
        if (size == 0)
            return Span<byte>.Empty;
        IntPtr data = NativeMethods.zlink_msg_data(ref _msg);
        if (data == IntPtr.Zero)
            return Span<byte>.Empty;
        return new Span<byte>((void*)data, size);
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
        bool hasKnownSize = _knownSize >= 0;
        int size = hasKnownSize
            ? _knownSize
            : (int)NativeMethods.zlink_msg_size(ref _msg);
        if (size == 0)
            return ReadOnlySpan<byte>.Empty;
        IntPtr data = NativeMethods.zlink_msg_data(ref _msg);
        if (data == IntPtr.Zero)
            return ReadOnlySpan<byte>.Empty;
        return new ReadOnlySpan<byte>((void*)data, size);
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
        bool hasKnownSize = _knownSize >= 0;
        nuint size = hasKnownSize
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
    public static Message FromBytes(byte[] data)
    {
        if (data == null)
            throw new ArgumentNullException(nameof(data));
        var message = new Message(false);
        message.InitializeManagedCopy(data.AsSpan());
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
        if (!_valid && _managedPayload == null)
        {
            TryReturnToPool();
            return;
        }

        Close();
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
