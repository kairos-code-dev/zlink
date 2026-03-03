// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Text;
using Zlink.Native;

namespace Zlink;

public delegate int StreamPacketsHandler(ReadOnlySpan<byte> routingId,
    ReadOnlySpan<byte> payload);

public sealed class Socket : IDisposable
{
    private IntPtr _handle;
    private readonly bool _own;
    private NativeMethods.ZlinkStreamOnPacketsDelegate? _streamCallback;
    private NativeMethods.ZlinkStreamOnRawDelegate? _streamRawCallback;
    private StreamPacketsHandler? _streamHandler;
    private bool _streamAttached;

    public Socket(Context context, SocketType type)
    {
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
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_bind(_handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public void Connect(string address)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_connect(_handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unbind(string address)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_unbind(_handle, address);
        ZlinkException.ThrowIfError(rc);
    }

    public void Disconnect(string address)
    {
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

    public bool TrySend(ReadOnlySpan<byte> buffer, out int bytesSent,
        SendFlags flags = SendFlags.DontWait)
    {
        return TrySend(buffer, out bytesSent, out _, flags);
    }

    public bool TrySend(byte[] buffer, out int bytesSent,
        SendFlags flags = SendFlags.DontWait)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        return TrySend(buffer.AsSpan(), out bytesSent, flags);
    }

    public unsafe bool TrySend(ReadOnlySpan<byte> buffer, out int bytesSent,
        out int errno, SendFlags flags = SendFlags.DontWait)
    {
        EnsureNotDisposed();
        int rc;
        fixed (byte* ptr = buffer)
        {
            rc = NativeMethods.zlink_send(_handle, ptr, (nuint)buffer.Length,
                (int)flags);
        }
        if (rc >= 0)
        {
            bytesSent = rc;
            errno = 0;
            return true;
        }

        bytesSent = 0;
        errno = NativeMethods.zlink_errno();
        return false;
    }

    public int SendConst(byte[] buffer, SendFlags flags = SendFlags.None)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        return SendConst(buffer.AsSpan(), flags);
    }

    public unsafe int SendConst(ReadOnlySpan<byte> buffer,
        SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        int rc;
        fixed (byte* ptr = buffer)
        {
            rc = NativeMethods.zlink_send_const(_handle, ptr,
                (nuint)buffer.Length, (int)flags);
        }
        ZlinkException.ThrowIfError(rc);
        return rc;
    }

    public void AttachStream(StreamPacketsHandler handler,
        StreamDispatchMode mode = StreamDispatchMode.None)
    {
        EnsureNotDisposed();
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new InvalidOperationException(
                "STREAM callback is already attached.");

        _streamHandler = handler;
        _streamCallback = OnStreamPackets;
        int rc = NativeMethods.zlink_stream_attach(_handle, _streamCallback,
            (int)mode);
        if (rc != 0)
        {
            _streamHandler = null;
            _streamCallback = null;
            throw ZlinkException.FromLastError();
        }
        _streamAttached = true;
    }

    public void AttachStreamRaw(StreamPacketsHandler handler)
    {
        EnsureNotDisposed();
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new InvalidOperationException(
                "STREAM callback is already attached.");

        _streamHandler = handler;
        _streamRawCallback = OnStreamRaw;
        int rc = NativeMethods.zlink_stream_attach_raw(_handle, _streamRawCallback);
        if (rc != 0)
        {
            _streamHandler = null;
            _streamRawCallback = null;
            throw ZlinkException.FromLastError();
        }
        _streamAttached = true;
    }

    public void AttachStreamLen32Be(StreamPacketsHandler handler)
    {
        EnsureNotDisposed();
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        if (_streamAttached)
            throw new InvalidOperationException(
                "STREAM callback is already attached.");

        _streamHandler = handler;
        _streamCallback = OnStreamPackets;
        int rc = NativeMethods.zlink_stream_attach_len32be(_handle, _streamCallback);
        if (rc != 0)
        {
            _streamHandler = null;
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
        _streamHandler = null;
        _streamCallback = null;
        _streamRawCallback = null;
        ZlinkException.ThrowIfError(rc);
    }

    public byte[]? StreamPeerRoutingId(int index = 0)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_socket_peer_routing_id(_handle, index,
            out var rid);
        if (rc != 0 || rid.Size == 0)
            return null;
        return NativeHelpers.ReadRoutingId(ref rid);
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

    public bool TryReceive(Span<byte> buffer, out int bytesReceived,
        ReceiveFlags flags = ReceiveFlags.DontWait)
    {
        return TryReceive(buffer, out bytesReceived, out _, flags);
    }

    public bool TryReceive(byte[] buffer, out int bytesReceived,
        ReceiveFlags flags = ReceiveFlags.DontWait)
    {
        if (buffer == null)
            throw new ArgumentNullException(nameof(buffer));
        return TryReceive(buffer.AsSpan(), out bytesReceived, flags);
    }

    public unsafe bool TryReceive(Span<byte> buffer, out int bytesReceived,
        out int errno, ReceiveFlags flags = ReceiveFlags.DontWait)
    {
        EnsureNotDisposed();
        int rc;
        fixed (byte* ptr = buffer)
        {
            rc = NativeMethods.zlink_recv(_handle, ptr, (nuint)buffer.Length,
                (int)flags);
        }
        if (rc >= 0)
        {
            bytesReceived = rc;
            errno = 0;
            return true;
        }

        bytesReceived = 0;
        errno = NativeMethods.zlink_errno();
        return false;
    }

    public void Send(Message message, SendFlags flags = SendFlags.None)
    {
        EnsureNotDisposed();
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        int rc = NativeMethods.zlink_msg_send(ref message.Handle, _handle,
            (int)flags);
        ZlinkException.ThrowIfError(rc);
        if (rc >= 0)
            message.Dispose();
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

    public void SetOption(SocketOption option, int value)
    {
        EnsureNotDisposed();
        unsafe
        {
            int tmp = value;
            IntPtr ptr = new IntPtr(&tmp);
            int rc = NativeMethods.zlink_setsockopt(_handle, (int)option, ptr,
                (nuint)sizeof(int));
            ZlinkException.ThrowIfError(rc);
        }
    }

    public void SetOption(SocketOption option, byte[] value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetOption(option, value.AsSpan());
    }

    public unsafe void SetOption(SocketOption option, ReadOnlySpan<byte> value)
    {
        EnsureNotDisposed();
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_setsockopt(_handle, (int)option,
                (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    public void SetOption(SocketOption option, string value)
    {
        EnsureNotDisposed();
        if (value == null)
            throw new ArgumentNullException(nameof(value));

        int maxByteCount = Encoding.UTF8.GetMaxByteCount(value.Length);
        if (maxByteCount <= 512)
        {
            Span<byte> buffer = stackalloc byte[maxByteCount];
            int byteCount = Encoding.UTF8.GetBytes(value.AsSpan(), buffer);
            SetOption(option, buffer.Slice(0, byteCount));
            return;
        }

        byte[] rented = ArrayPool<byte>.Shared.Rent(maxByteCount);
        try
        {
            int byteCount = Encoding.UTF8.GetBytes(value, rented);
            SetOption(option, rented.AsSpan(0, byteCount));
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
    }

    public int GetOption(SocketOption option)
    {
        EnsureNotDisposed();
        unsafe
        {
            int value = 0;
            nuint size = (nuint)sizeof(int);
            IntPtr ptr = new IntPtr(&value);
            int rc = NativeMethods.zlink_getsockopt(_handle, (int)option, ptr,
                ref size);
            ZlinkException.ThrowIfError(rc);
            return value;
        }
    }

    public byte[] GetOptionBytes(SocketOption option, int initialSize = 256)
    {
        EnsureNotDisposed();
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

    public unsafe int GetOption(SocketOption option, Span<byte> destination)
    {
        EnsureNotDisposed();
        fixed (byte* ptr = destination)
        {
            nuint size = (nuint)destination.Length;
            int rc = NativeMethods.zlink_getsockopt(_handle, (int)option,
                (IntPtr)ptr, ref size);
            ZlinkException.ThrowIfError(rc);
            return checked((int)size);
        }
    }

    public string GetOptionString(SocketOption option, int initialSize = 256)
    {
        byte[] bytes = GetOptionBytes(option, initialSize);
        int len = Array.IndexOf(bytes, (byte)0);
        if (len < 0)
            len = bytes.Length;
        return System.Text.Encoding.UTF8.GetString(bytes, 0, len);
    }

    public void Monitor(string address, SocketEvent events)
    {
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
        StreamPacketsHandler? handler = _streamHandler;
        if (handler == null || routingId == IntPtr.Zero
            || messages == IntPtr.Zero || messageCount == 0)
            return 0;

        ZlinkRoutingId* rid = (ZlinkRoutingId*)routingId;
        int ridSize = rid->Size;
        if (ridSize < 0 || ridSize > 255)
            ridSize = 0;
        ReadOnlySpan<byte> ridSpan = ridSize > 0
            ? new ReadOnlySpan<byte>(rid->Data, ridSize)
            : ReadOnlySpan<byte>.Empty;

        int zlinkMsgSize = sizeof(ZlinkMsg);
        int count = checked((int)messageCount);
        for (int i = 0; i < count; i++)
        {
            IntPtr msg = IntPtr.Add(messages, i * zlinkMsgSize);
            int rc;
            try
            {
                IntPtr payloadPtr = NativeMethods.zlink_msg_data(msg);
                int payloadSize = checked((int)NativeMethods.zlink_msg_size(msg));
                ReadOnlySpan<byte> payload = payloadSize > 0
                    && payloadPtr != IntPtr.Zero
                    ? new ReadOnlySpan<byte>((void*)payloadPtr, payloadSize)
                    : ReadOnlySpan<byte>.Empty;
                rc = handler(ridSpan, payload);
            }
            catch
            {
                CloseStreamPacketRange(messages, i + 1, count, zlinkMsgSize);
                return 1;
            }
            finally
            {
                CloseStreamPacket(msg);
            }

            if (rc != 0)
            {
                CloseStreamPacketRange(messages, i + 1, count, zlinkMsgSize);
                return rc;
            }
        }
        return 0;
    }

    private unsafe int OnStreamRaw(IntPtr routingId, IntPtr message)
    {
        StreamPacketsHandler? handler = _streamHandler;
        if (handler == null || routingId == IntPtr.Zero || message == IntPtr.Zero)
            return 0;

        ZlinkRoutingId* rid = (ZlinkRoutingId*)routingId;
        int ridSize = rid->Size;
        if (ridSize < 0 || ridSize > 255)
            ridSize = 0;
        ReadOnlySpan<byte> ridSpan = ridSize > 0
            ? new ReadOnlySpan<byte>(rid->Data, ridSize)
            : ReadOnlySpan<byte>.Empty;

        int rc;
        try
        {
            IntPtr payloadPtr = NativeMethods.zlink_msg_data(message);
            int payloadSize = checked((int)NativeMethods.zlink_msg_size(message));
            ReadOnlySpan<byte> payload = payloadSize > 0
                && payloadPtr != IntPtr.Zero
                ? new ReadOnlySpan<byte>((void*)payloadPtr, payloadSize)
                : ReadOnlySpan<byte>.Empty;
            rc = handler(ridSpan, payload);
        }
        catch
        {
            CloseStreamPacket(message);
            return 1;
        }

        CloseStreamPacket(message);
        return rc;
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
            _streamHandler = null;
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
