// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Runtime.CompilerServices;
using System.Text;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

/// <summary>
/// Owns one zlink message payload.
/// </summary>
/// <remarks>
/// A message is disposable because it may own native storage. Span-returning
/// APIs expose storage owned by this instance and are valid only while the
/// message remains undisposed.
/// </remarks>
public sealed partial class Message : IDisposable, IAsyncDisposable
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

    /// <summary>
    /// Create an empty message.
    /// </summary>
    public Message()
    {
        Init();
    }

    /// <summary>
    /// Create a message with writable payload storage of <paramref name="size"/> bytes.
    /// </summary>
    /// <param name="size">Payload size in bytes. The value must be non-negative.</param>
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

    /// <summary>
    /// Create a message containing a snapshot copy of <paramref name="data"/>.
    /// </summary>
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

    /// <summary>
    /// Create a message containing a snapshot copy of <paramref name="data"/>.
    /// </summary>
    public Message(ReadOnlyMemory<byte> data) : this(data.Span)
    {
    }

    internal Message(bool init)
    {
        if (init)
            Init();
    }

    /// <summary>
    /// Gets the payload size in bytes.
    /// </summary>
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

    /// <summary>
    /// Gets whether the payload is empty.
    /// </summary>
    public bool IsEmpty => Size == 0;

    /// <summary>
    /// Gets the native reference count when available.
    /// </summary>
    /// <remarks>
    /// Managed-copy messages report 1 because their storage is owned solely by
    /// this instance.
    /// </remarks>
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

    /// <summary>
    /// Allocate a message with writable payload storage.
    /// </summary>
    /// <param name="size">Payload size in bytes. The value must be non-negative.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Message Allocate(int size)
    {
        if (size < 0)
            throw new ArgumentOutOfRangeException(nameof(size));
        Message message = RentFromPool();
        message.InitSize(size);
        return message;
    }

    /// <summary>
    /// Returns a writable view of the message payload.
    /// </summary>
    /// <remarks>
    /// The returned span is backed by storage owned by this message. It must
    /// not be used after the message is disposed or moved.
    /// </remarks>
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

    /// <summary>
    /// Returns a new byte array containing a copy of the payload.
    /// </summary>
    public byte[] ToArray()
    {
        return AsReadOnlySpan().ToArray();
    }

    /// <summary>
    /// Returns a read-only view of the message payload.
    /// </summary>
    /// <remarks>
    /// The returned span is backed by storage owned by this message. It must
    /// not be used after the message is disposed or moved.
    /// </remarks>
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

    /// <summary>
    /// Returns a read-only memory view of the payload.
    /// </summary>
    /// <remarks>
    /// Native-backed messages are copied into managed memory because
    /// <see cref="ReadOnlyMemory{T}"/> cannot safely reference native storage.
    /// </remarks>
    public ReadOnlyMemory<byte> AsReadOnlyMemory()
    {
        ManagedPayloadState? managed = _managedPayload;
        if (managed != null)
            return managed.Bytes.AsMemory(0, managed.Length);
        return ToArray();
    }

    /// <summary>
    /// Copies the payload into <paramref name="destination"/>.
    /// </summary>
    /// <returns>The number of bytes written.</returns>
    public int CopyTo(Span<byte> destination)
    {
        if (!TryCopyTo(destination, out int bytesWritten))
            throw new ArgumentException("Destination buffer is too small.",
                nameof(destination));
        return bytesWritten;
    }

    /// <summary>
    /// Copies the payload to an <see cref="IBufferWriter{T}"/>.
    /// </summary>
    /// <returns>The number of bytes written.</returns>
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

    /// <summary>
    /// Attempts to copy the payload into <paramref name="destination"/>.
    /// </summary>
    /// <returns>true when the destination was large enough; otherwise false.</returns>
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
    public static Message From(byte[] data)
    {
        if (data == null)
            throw new ArgumentNullException(nameof(data));
        var message = new Message(false);
        message.InitializeManagedCopy(data.AsSpan());
        return message;
    }

    /// <summary>
    /// Create a message containing a snapshot copy of <paramref name="data"/>.
    /// </summary>
    public static Message From(ReadOnlySpan<byte> data)
    {
        var message = new Message(false);
        message.InitializeManagedCopy(data);
        return message;
    }

    /// <summary>
    /// Create a message containing a snapshot copy of <paramref name="data"/>.
    /// </summary>
    public static Message From(ReadOnlyMemory<byte> data)
    {
        var message = new Message(false);
        message.InitializeManagedCopy(data.Span);
        return message;
    }

    /// <summary>
    /// Create a message containing a snapshot copy of <paramref name="data"/>.
    /// </summary>
    public static Message From(ReadOnlySequence<byte> data)
    {
        if (data.IsSingleSegment)
            return From(data.First);
        return From(data.ToArray());
    }

    /// <summary>
    /// Create a message containing a copy of another message payload.
    /// </summary>
    public static Message From(Message source)
    {
        if (source == null)
            throw new ArgumentNullException(nameof(source));
        return source.Copy();
    }

    /// <summary>
    /// Encode <paramref name="value"/> as UTF-8 and copy it into a message.
    /// </summary>
    public static Message From(string value)
    {
        return From(value, Encoding.UTF8);
    }

    /// <summary>
    /// Encode <paramref name="value"/> with <paramref name="encoding"/> and
    /// copy it into a message.
    /// </summary>
    public static Message From(string value, Encoding encoding)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        if (encoding == null)
            throw new ArgumentNullException(nameof(encoding));
        return From(encoding.GetBytes(value));
    }

    /// <summary>
    /// Decode the payload as UTF-8.
    /// </summary>
    public string GetString()
    {
        return GetString(Encoding.UTF8);
    }

    /// <summary>
    /// Decode the payload with <paramref name="encoding"/>.
    /// </summary>
    public string GetString(Encoding encoding)
    {
        if (encoding == null)
            throw new ArgumentNullException(nameof(encoding));
        return encoding.GetString(AsReadOnlySpan());
    }

    /// <summary>
    /// Gets a native message property when the message is native-backed.
    /// </summary>
    /// <returns>
    /// The property value, or null when the property is absent or the message
    /// is managed-backed.
    /// </returns>
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

    /// <summary>
    /// Releases the payload storage owned by this message.
    /// </summary>
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

    /// <summary>
    /// Releases the payload storage owned by this message.
    /// </summary>
    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }
}
