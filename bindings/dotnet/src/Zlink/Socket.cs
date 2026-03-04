// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Text;
using Zlink.Native;

namespace Zlink;

/// <summary>
/// STREAM callback for per-packet dispatch.
/// Message ownership is transferred to the callback.
/// The callback must dispose each message exactly once
/// (or consume it via <see cref="Socket.StreamSend(uint, Message, SendFlags)"/>).
/// </summary>
public delegate int StreamPacketHandler(uint routingId, Message payload);

/// <summary>
/// STREAM callback for batch dispatch.
/// Message ownership is transferred to the callback.
/// The callback must dispose each message exactly once
/// (or consume it via <see cref="Socket.StreamSend(uint, Message, SendFlags)"/>).
/// </summary>
public delegate int StreamBatchHandler(uint routingId, Message[] parts);

public sealed class Socket : IDisposable
{
    private IntPtr _handle;
    private readonly bool _own;
    private NativeMethods.ZlinkStreamOnPacketsDelegate? _streamCallback;
    private NativeMethods.ZlinkStreamOnRawDelegate? _streamRawCallback;
    private StreamPacketHandler? _streamPacketHandler;
    private StreamBatchHandler? _streamBatchHandler;
    private bool _streamAttached;

    public Socket(Context context, SocketType type)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        _handle = NativeMethods.zlink_socket(context.Handle, (int)type);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        _own = true;
    }

    internal Socket(IntPtr handle, bool own)
    {
        _handle = handle;
        _own = own;
    }

    internal static Socket Adopt(IntPtr handle, bool own)
    {
        if (handle == IntPtr.Zero)
            throw new ArgumentException("Invalid socket handle.", nameof(handle));
        return new Socket(handle, own);
    }

    public IntPtr Handle => _handle;

    public void Bind(string address)
    {
        if (address == null)
            throw new ArgumentNullException(nameof(address));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_bind(_handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public void Connect(string address)
    {
        if (address == null)
            throw new ArgumentNullException(nameof(address));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_connect(_handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unbind(string address)
    {
        if (address == null)
            throw new ArgumentNullException(nameof(address));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_unbind(_handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public void Disconnect(string address)
    {
        if (address == null)
            throw new ArgumentNullException(nameof(address));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_disconnect(_handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public int Send(byte[] buffer, SendFlags flags = SendFlags.None)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        return Send(buffer.AsSpan(), flags);
    }

    public unsafe int Send(ReadOnlySpan<byte> buffer,
        SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        int rc;
        fixed (byte* ptr = buffer)
        {
            rc = NativeMethods.zlink_send(_handle, ptr, (nuint)buffer.Length,
                (int)flags);
        }
        ZlinkException.ThrowIfError(rc);
        return rc;
    }

    public void AttachStreamRaw(StreamPacketHandler handler)
    {
        EnsureNotDisposed();
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new InvalidOperationException(
                "STREAM callback is already attached.");

        _streamPacketHandler = handler;
        _streamBatchHandler = null;
        _streamRawCallback = OnStreamRaw;
        int rc = NativeMethods.zlink_stream_attach_raw(_handle, _streamRawCallback);
        if (rc != 0)
        {
            _streamPacketHandler = null;
            _streamBatchHandler = null;
            _streamRawCallback = null;
            throw ZlinkException.FromLastError();
        }
        _streamAttached = true;
    }

    /// <summary>
    /// Attaches LEN32BE stream callback and dispatches one callback per batch.
    /// Message ownership is transferred to the callback.
    /// The callback is responsible for disposing every message exactly once
    /// (or consuming it via <see cref="StreamSend(uint, Message, SendFlags)"/>).
    /// </summary>
    public void AttachStreamLen32Be(StreamBatchHandler handler)
    {
        EnsureNotDisposed();
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new InvalidOperationException(
                "STREAM callback is already attached.");

        _streamPacketHandler = null;
        _streamBatchHandler = handler;
        _streamCallback = OnStreamPackets;
        int rc = NativeMethods.zlink_stream_attach_len32be(_handle, _streamCallback);
        if (rc != 0)
        {
            _streamPacketHandler = null;
            _streamBatchHandler = null;
            _streamCallback = null;
            throw ZlinkException.FromLastError();
        }
        _streamAttached = true;
    }

    public void DetachStream()
    {
        EnsureNotDisposed();
        if (!_streamAttached)
            return;
        int rc = NativeMethods.zlink_stream_detach(_handle);
        _streamAttached = false;
        _streamPacketHandler = null;
        _streamBatchHandler = null;
        _streamCallback = null;
        _streamRawCallback = null;
        ZlinkException.ThrowIfError(rc);
    }

    public bool TryGetPeerRoutingId(out byte[] routingId, int index = 0)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_socket_peer_routing_id(_handle, index,
            out var rid);
        if (rc != 0 || rid.Size == 0)
        {
            routingId = Array.Empty<byte>();
            return false;
        }
        routingId = NativeHelpers.ReadRoutingId(ref rid);
        return true;
    }

    public byte[]? GetPeerRoutingId(int index = 0)
    {
        return TryGetPeerRoutingId(out var routingId, index)
            ? routingId
            : null;
    }

    public unsafe bool TryGetPeerRoutingIdU32(out uint routingId, int index = 0)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_socket_peer_routing_id(_handle, index,
            out var rid);
        if (rc != 0 || !TryDecodeRoutingIdU32(ref rid, out routingId))
        {
            routingId = 0;
            return false;
        }
        return true;
    }

    public uint? GetPeerRoutingIdU32(int index = 0)
    {
        return TryGetPeerRoutingIdU32(out uint routingId, index)
            ? routingId
            : null;
    }

    public int StreamSend(uint routingId, byte[] payload,
        SendFlags flags = SendFlags.None)
    {
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        return StreamSend(routingId, payload.AsSpan(), flags);
    }

    public int StreamSend(uint routingId, ReadOnlySpan<byte> payload,
        SendFlags flags = SendFlags.None)
    {
        Span<byte> rid = stackalloc byte[sizeof(uint)];
        BinaryPrimitives.WriteUInt32BigEndian(rid, routingId);
        return StreamSend(rid, payload, flags);
    }

    public int StreamSend(uint routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        Span<byte> rid = stackalloc byte[sizeof(uint)];
        BinaryPrimitives.WriteUInt32BigEndian(rid, routingId);
        return StreamSend(rid, message, flags);
    }

    public int StreamSend(byte[] routingId, byte[] payload,
        SendFlags flags = SendFlags.None)
    {
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        return StreamSend(routingId.AsSpan(), payload.AsSpan(), flags);
    }

    public unsafe int StreamSend(ReadOnlySpan<byte> routingId,
        ReadOnlySpan<byte> payload, SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        ZlinkRoutingId rid = NativeHelpers.WriteRoutingId(routingId);
        int rc;
        fixed (byte* payloadPtr = payload)
        {
            IntPtr payloadAddr = payload.Length == 0
                ? IntPtr.Zero
                : (IntPtr)payloadPtr;
            rc = NativeMethods.zlink_stream_send(_handle, ref rid, payloadAddr,
                (nuint)payload.Length, (int)flags);
        }
        ZlinkException.ThrowIfError(rc);
        return rc;
    }

    public int StreamSend(byte[] routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        return StreamSend(routingId.AsSpan(), message, flags);
    }

    /// <summary>
    /// Sends a message and consumes its ownership regardless of send result.
    /// </summary>
    public int StreamSend(ReadOnlySpan<byte> routingId, Message message,
        SendFlags flags = SendFlags.None)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        ZlinkRoutingId rid = NativeHelpers.WriteRoutingId(routingId);
        int rc = NativeMethods.zlink_stream_send_msg(_handle, ref rid,
            ref message.Handle, (int)flags);
        try
        {
            ZlinkException.ThrowIfError(rc);
            return rc;
        }
        finally
        {
            message.Dispose();
        }
    }

    public PeerRecord GetPeerInfo(byte[] routingId)
    {
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        return GetPeerInfo(routingId.AsSpan());
    }

    public PeerRecord GetPeerInfo(ReadOnlySpan<byte> routingId)
    {
        EnsureNotDisposed();
        ZlinkRoutingId nativeId = NativeHelpers.WriteRoutingId(routingId);
        int rc = NativeMethods.zlink_socket_peer_info(_handle, ref nativeId,
            out var info);
        ZlinkException.ThrowIfError(rc);
        return PeerRecord.FromNative(ref info);
    }

    public int PeerCount()
    {
        EnsureNotDisposed();
        int count = NativeMethods.zlink_socket_peer_count(_handle);
        if (count < 0)
            throw ZlinkException.FromLastError();
        return count;
    }

    public PeerRecord[] GetPeers()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_socket_peers(_handle, IntPtr.Zero,
            ref count);
        ZlinkException.ThrowIfError(rc);
        if (count == 0)
            return Array.Empty<PeerRecord>();

        ZlinkPeerInfo[] native = ArrayPool<ZlinkPeerInfo>.Shared
            .Rent(checked((int)count));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_socket_peers(_handle, native, ref actual);
            ZlinkException.ThrowIfError(rc);

            PeerRecord[] peers = new PeerRecord[(int)actual];
            for (int i = 0; i < peers.Length; i++)
                peers[i] = PeerRecord.FromNative(ref native[i]);
            return peers;
        }
        finally
        {
            ArrayPool<ZlinkPeerInfo>.Shared.Return(native);
        }
    }

    public int Receive(byte[] buffer, ReceiveFlags flags = ReceiveFlags.None)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        return Receive(buffer.AsSpan(), flags);
    }

    public unsafe int Receive(Span<byte> buffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureNotDisposed();
        int rc;
        fixed (byte* ptr = buffer)
        {
            rc = NativeMethods.zlink_recv(_handle, ptr, (nuint)buffer.Length,
                (int)flags);
        }
        ZlinkException.ThrowIfError(rc);
        return rc;
    }

    /// <summary>
    /// Sends a message and consumes its ownership regardless of send result.
    /// </summary>
    public void Send(Message message, SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        int rc = NativeMethods.zlink_msg_send(ref message.Handle, _handle,
            (int)flags);
        try
        {
            ZlinkException.ThrowIfError(rc);
        }
        finally
        {
            message.Dispose();
        }
    }

    public Message ReceiveMessage(ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureNotDisposed();
        var msg = new Message();
        int rc = NativeMethods.zlink_msg_recv(ref msg.Handle, _handle,
            (int)flags);
        ZlinkException.ThrowIfError(rc);
        return msg;
    }

    public void SetOption(SocketOptionKey<int> option, int value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.Int32)
            throw new ArgumentException("Expected int socket option key.",
                nameof(option));
        SetOptionInt32(option.Option, value);
    }

    public void SetOption(SocketOptionKey<long> option, long value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.Int64)
            throw new ArgumentException("Expected long socket option key.",
                nameof(option));
        SetOptionInt64(option.Option, value);
    }

    public void SetOption(SocketOptionKey<ulong> option, ulong value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.UInt64)
            throw new ArgumentException("Expected ulong socket option key.",
                nameof(option));
        SetOptionUInt64(option.Option, value);
    }

    public void SetOption(SocketOptionKey<byte[]> option, byte[] value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetOption(option, value.AsSpan());
    }

    public void SetOption(SocketOptionKey<byte[]> option, ReadOnlySpan<byte> value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.Bytes)
            throw new ArgumentException("Expected byte[] socket option key.",
                nameof(option));
        SetOptionBytes(option.Option, value);
    }

    public void SetOption(SocketOptionKey<string> option, string value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.String)
            throw new ArgumentException("Expected string socket option key.",
                nameof(option));
        SetOptionString(option.Option, value);
    }

    public int GetOption(SocketOptionKey<int> option)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.Int32)
            throw new ArgumentException("Expected int socket option key.",
                nameof(option));
        return GetOptionInt32(option.Option);
    }

    public long GetOption(SocketOptionKey<long> option)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.Int64)
            throw new ArgumentException("Expected long socket option key.",
                nameof(option));
        return GetOptionInt64(option.Option);
    }

    public ulong GetOption(SocketOptionKey<ulong> option)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.UInt64)
            throw new ArgumentException("Expected ulong socket option key.",
                nameof(option));
        return GetOptionUInt64(option.Option);
    }

    public byte[] GetOption(SocketOptionKey<byte[]> option, int initialSize = 256)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.Bytes)
            throw new ArgumentException("Expected byte[] socket option key.",
                nameof(option));
        return GetOptionBytes(option.Option, initialSize);
    }

    public int GetOption(SocketOptionKey<byte[]> option, Span<byte> destination)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.Bytes)
            throw new ArgumentException("Expected byte[] socket option key.",
                nameof(option));
        return GetOptionBytesInto(option.Option, destination);
    }

    public string GetOption(SocketOptionKey<string> option, int initialSize = 256)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.String)
            throw new ArgumentException("Expected string socket option key.",
                nameof(option));
        return GetOptionString(option.Option, initialSize);
    }

    private unsafe void SetOptionInt32(SocketOption option, int value)
    {
        int tmp = value;
        IntPtr ptr = new IntPtr(&tmp);
        int rc = NativeMethods.zlink_setsockopt(_handle, (int)option, ptr,
            (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionInt64(SocketOption option, long value)
    {
        long tmp = value;
        IntPtr ptr = new IntPtr(&tmp);
        int rc = NativeMethods.zlink_setsockopt(_handle, (int)option, ptr,
            (nuint)sizeof(long));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionUInt64(SocketOption option, ulong value)
    {
        ulong tmp = value;
        IntPtr ptr = new IntPtr(&tmp);
        int rc = NativeMethods.zlink_setsockopt(_handle, (int)option, ptr,
            (nuint)sizeof(ulong));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionBytes(SocketOption option, ReadOnlySpan<byte> value)
    {
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_setsockopt(_handle, (int)option,
                (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    private void SetOptionString(SocketOption option, string value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));

        int maxByteCount = Encoding.UTF8.GetMaxByteCount(value.Length);
        if (maxByteCount <= 512)
        {
            Span<byte> buffer = stackalloc byte[maxByteCount];
            int byteCount = Encoding.UTF8.GetBytes(value.AsSpan(), buffer);
            SetOptionBytes(option, buffer.Slice(0, byteCount));
            return;
        }

        byte[] rented = ArrayPool<byte>.Shared.Rent(maxByteCount);
        try
        {
            int byteCount = Encoding.UTF8.GetBytes(value, rented);
            SetOptionBytes(option, rented.AsSpan(0, byteCount));
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
    }

    private unsafe int GetOptionInt32(SocketOption option)
    {
        int value = 0;
        nuint size = (nuint)sizeof(int);
        IntPtr ptr = new IntPtr(&value);
        int rc = NativeMethods.zlink_getsockopt(_handle, (int)option, ptr,
            ref size);
        ZlinkException.ThrowIfError(rc);
        return value;
    }

    private unsafe long GetOptionInt64(SocketOption option)
    {
        long value = 0;
        nuint size = (nuint)sizeof(long);
        IntPtr ptr = new IntPtr(&value);
        int rc = NativeMethods.zlink_getsockopt(_handle, (int)option, ptr,
            ref size);
        ZlinkException.ThrowIfError(rc);
        return value;
    }

    private unsafe ulong GetOptionUInt64(SocketOption option)
    {
        ulong value = 0;
        nuint size = (nuint)sizeof(ulong);
        IntPtr ptr = new IntPtr(&value);
        int rc = NativeMethods.zlink_getsockopt(_handle, (int)option, ptr,
            ref size);
        ZlinkException.ThrowIfError(rc);
        return value;
    }

    private byte[] GetOptionBytes(SocketOption option, int initialSize = 256)
    {
        if (initialSize <= 0)
            throw new ArgumentOutOfRangeException(nameof(initialSize));
        byte[] rented = ArrayPool<byte>.Shared.Rent(initialSize);
        try
        {
            while (true)
            {
                unsafe
                {
                    fixed (byte* ptr = rented)
                    {
                        nuint size = (nuint)rented.Length;
                        int rc = NativeMethods.zlink_getsockopt(_handle,
                            (int)option, (IntPtr)ptr, ref size);
                        if (rc == 0)
                        {
                            int actual = checked((int)size);
                            byte[] result = new byte[actual];
                            if (actual > 0)
                            {
                                int toCopy = actual;
                                if (toCopy > rented.Length)
                                    toCopy = rented.Length;
                                Array.Copy(rented, result, toCopy);
                            }
                            return result;
                        }

                        if (size > (nuint)rented.Length)
                        {
                            ArrayPool<byte>.Shared.Return(rented);
                            rented = ArrayPool<byte>.Shared.Rent(
                                checked((int)size));
                            continue;
                        }

                        ZlinkException.ThrowIfError(rc);
                    }
                }
            }
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
    }

    private unsafe int GetOptionBytesInto(SocketOption option,
        Span<byte> destination)
    {
        fixed (byte* ptr = destination)
        {
            nuint size = (nuint)destination.Length;
            int rc = NativeMethods.zlink_getsockopt(_handle, (int)option,
                (IntPtr)ptr, ref size);
            ZlinkException.ThrowIfError(rc);
            return checked((int)size);
        }
    }

    private string GetOptionString(SocketOption option, int initialSize = 256)
    {
        byte[] bytes = GetOptionBytes(option, initialSize);
        int len = Array.IndexOf(bytes, (byte)0);
        if (len < 0)
            len = bytes.Length;
        return Encoding.UTF8.GetString(bytes, 0, len);
    }

    public void Monitor(string address, SocketEvent events)
    {
        if (address == null)
            throw new ArgumentNullException(nameof(address));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_socket_monitor(_handle, address,
            (int)events);
        ZlinkException.ThrowIfError(rc);
    }

    public MonitorSocket MonitorOpen(SocketEvent events)
    {
        EnsureNotDisposed();
        IntPtr handle = NativeMethods.zlink_socket_monitor_open(_handle,
            (int)events);
        if (handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return new MonitorSocket(Socket.Adopt(handle, true));
    }

    private static void CloseStreamPacket(IntPtr msg)
    {
        if (msg == IntPtr.Zero)
            return;
        try
        {
            NativeMethods.zlink_msg_close(msg);
        }
        catch
        {
        }
    }

    private static void CloseStreamPacketRange(IntPtr messages, int start,
        int count, int msgSize)
    {
        for (int i = start; i < count; i++)
            CloseStreamPacket(IntPtr.Add(messages, i * msgSize));
    }

    private unsafe int OnStreamPackets(IntPtr routingId, IntPtr messages,
        nuint messageCount)
    {
        StreamBatchHandler? batchHandler = _streamBatchHandler;
        if (batchHandler == null
            || routingId == IntPtr.Zero
            || messages == IntPtr.Zero
            || messageCount == 0
            || messageCount > int.MaxValue)
        {
            if (messages != IntPtr.Zero && messageCount > 0
                && messageCount <= int.MaxValue)
            {
                int cleanupSize = sizeof(ZlinkMsg);
                CloseStreamPacketRange(messages, 0, (int)messageCount,
                    cleanupSize);
            }
            return 0;
        }

        ZlinkRoutingId* rid = (ZlinkRoutingId*)routingId;
        int zlinkMsgSize = sizeof(ZlinkMsg);
        int count = (int)messageCount;
        if (!TryDecodeRoutingIdU32(ref *rid, out uint ridU32))
        {
            CloseStreamPacketRange(messages, 0, count, zlinkMsgSize);
            return 0;
        }

        Message[] parts = new Message[count];
        int movedCount = 0;

        int rc;
        bool callbackCompleted = false;
        try
        {
            for (int i = 0; i < count; i++)
            {
                IntPtr msg = IntPtr.Add(messages, i * zlinkMsgSize);
                parts[i] = Message.MoveFromNativeSingle(msg);
                movedCount++;
            }

            rc = batchHandler(ridU32, parts);
            callbackCompleted = true;
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
            rc = 1;
        }
        finally
        {
            if (!callbackCompleted)
            {
                for (int i = 0; i < movedCount; i++)
                    parts[i]?.Dispose();
            }
            CloseStreamPacketRange(messages, 0, count, zlinkMsgSize);
        }

        return rc;
    }

    private unsafe int OnStreamRaw(IntPtr routingId, IntPtr message)
    {
        StreamPacketHandler? handler = _streamPacketHandler;
        if (handler == null || routingId == IntPtr.Zero || message == IntPtr.Zero)
            return 0;

        ZlinkRoutingId* rid = (ZlinkRoutingId*)routingId;
        if (!TryDecodeRoutingIdU32(ref *rid, out uint ridU32))
        {
            CloseStreamPacket(message);
            return 0;
        }

        Message? payloadMsg = null;
        int rc;
        try
        {
            payloadMsg = Message.MoveFromNativeSingle(message);
            rc = handler(ridU32, payloadMsg);
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
            if (payloadMsg != null)
            {
                try
                {
                    payloadMsg.Dispose();
                }
                catch
                {
                }
            }
            rc = 1;
        }
        finally
        {
            CloseStreamPacket(message);
        }
        return rc;
    }

    private static unsafe bool TryDecodeRoutingIdU32(ref ZlinkRoutingId rid,
        out uint routingId)
    {
        if (rid.Size != sizeof(uint))
        {
            routingId = 0;
            return false;
        }

        routingId = ((uint)rid.Data[0] << 24)
            | ((uint)rid.Data[1] << 16)
            | ((uint)rid.Data[2] << 8)
            | rid.Data[3];
        return true;
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        if (_streamAttached)
        {
            try
            {
                NativeMethods.zlink_stream_detach(_handle);
            }
            catch
            {
            }
            _streamAttached = false;
            _streamPacketHandler = null;
            _streamBatchHandler = null;
            _streamCallback = null;
            _streamRawCallback = null;
        }
        if (_own)
            NativeMethods.zlink_close(_handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~Socket()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Socket));
    }
}
