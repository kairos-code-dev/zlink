// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Text;
using Zlink.Native;

namespace Zlink;

public sealed class Gateway : IDisposable
{
    private const int StackSendPartLimit = 8;
    private const int ServiceNameCacheLimit = 1024;
    private IntPtr _handle;
    private readonly ConcurrentDictionary<string, byte[]> _serviceNameUtf8Cache =
        new(StringComparer.Ordinal);

    public Gateway(Context context, Discovery discovery)
    {
        _handle = NativeMethods.zlink_gateway_new(context.Handle,
            discovery.Handle, null);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    public Gateway(Context context, Discovery discovery, string routingId)
    {
        _handle = NativeMethods.zlink_gateway_new(context.Handle,
            discovery.Handle, routingId);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    /// <summary>
    /// Sends multipart message to a service and consumes ownership of all parts.
    /// </summary>
    public void Send(string serviceName, Message[] parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        Send(serviceName, parts.AsSpan(), flags);
    }

    public unsafe void Send(string serviceName, ReadOnlySpan<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        SendCore(serviceName, default, useRoutingId: false, parts, flags);
    }

    public unsafe void Send(string serviceName, ReadOnlySpan<byte> payload,
        SendFlags flags = SendFlags.None)
    {
        SendSinglePayloadCore(serviceName, default, useRoutingId: false, payload,
            flags);
    }

    /// <summary>
    /// Sends multipart message to a specific routing id and consumes ownership
    /// of all parts.
    /// </summary>
    public void SendToRoutingId(string serviceName, byte[] routingId,
        Message[] parts, SendFlags flags = SendFlags.None)
    {
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        SendToRoutingId(serviceName, routingId.AsSpan(), parts.AsSpan(), flags);
    }

    public unsafe void SendToRoutingId(string serviceName,
        ReadOnlySpan<byte> routingId, ReadOnlySpan<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        SendCore(serviceName, routingId, useRoutingId: true, parts, flags);
    }

    public unsafe void SendToRoutingId(string serviceName,
        ReadOnlySpan<byte> routingId, ReadOnlySpan<byte> payload,
        SendFlags flags = SendFlags.None)
    {
        SendSinglePayloadCore(serviceName, routingId, useRoutingId: true, payload,
            flags);
    }

    private unsafe void SendCore(string serviceName,
        ReadOnlySpan<byte> routingId, bool useRoutingId,
        ReadOnlySpan<Message> parts, SendFlags flags)
    {
        EnsureNotDisposed();
        if (serviceName == null)
            throw new ArgumentNullException(nameof(serviceName));
        if (parts.Length == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        byte[] serviceNameUtf8 = GetServiceNameUtf8(serviceName);
        ZlinkRoutingId nativeRoutingId = default;
        if (useRoutingId)
            nativeRoutingId = NativeHelpers.WriteRoutingId(routingId);

        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackSendPartLimit
            ? stackalloc ZlinkMsg[StackSendPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        int rc = 0;
        try
        {
            for (int i = 0; i < parts.Length; i++)
            {
                if (parts[i] == null)
                    throw new ArgumentException(
                        "Parts must not contain null messages.", nameof(parts));
            }

            for (int i = 0; i < parts.Length; i++)
            {
                parts[i].MoveTo(ref nativeParts[i]);
                built++;
            }

            fixed (ZlinkMsg* ptr = nativeParts)
            fixed (byte* serviceNamePtr = serviceNameUtf8)
            {
                if (useRoutingId)
                {
                    rc = NativeMethods.zlink_gateway_send_rid(_handle,
                        serviceNamePtr, &nativeRoutingId, ptr,
                        (nuint)nativeParts.Length, (int)flags);
                }
                else
                {
                    rc = NativeMethods.zlink_gateway_send(_handle, serviceNamePtr,
                        ptr, (nuint)nativeParts.Length, (int)flags);
                }
            }
        }
        catch
        {
            CloseNativeParts(nativeParts, built);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }

        if (rc < 0)
            CloseNativeParts(nativeParts, built);
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SendSinglePayloadCore(string serviceName,
        ReadOnlySpan<byte> routingId, bool useRoutingId,
        ReadOnlySpan<byte> payload, SendFlags flags)
    {
        EnsureNotDisposed();
        if (serviceName == null)
            throw new ArgumentNullException(nameof(serviceName));

        byte[] serviceNameUtf8 = GetServiceNameUtf8(serviceName);

        ZlinkRoutingId nativeRoutingId = default;
        if (useRoutingId)
            nativeRoutingId = NativeHelpers.WriteRoutingId(routingId);

        int rc = 0;
        fixed (byte* serviceNamePtr = serviceNameUtf8)
        fixed (byte* payloadPtr = payload)
        {
            if (useRoutingId)
            {
                rc = NativeMethods.zlink_gateway_send_rid_bytes(_handle,
                    serviceNamePtr, &nativeRoutingId, payloadPtr,
                    (nuint)payload.Length, (int)flags);
            }
            else
            {
                rc = NativeMethods.zlink_gateway_send_bytes(_handle,
                    serviceNamePtr, payloadPtr, (nuint)payload.Length,
                    (int)flags);
            }
        }
        ZlinkException.ThrowIfError(rc);
    }

    private static void CloseNativeParts(Span<ZlinkMsg> nativeParts, int count)
    {
        for (int i = 0; i < count; i++)
            NativeMethods.zlink_msg_close(ref nativeParts[i]);
    }

    public GatewayMessage Receive(ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureNotDisposed();
        unsafe
        {
            byte* nameBuf = stackalloc byte[256];
            int rc = NativeMethods.zlink_gateway_recv(_handle, out var parts,
                out var count, (int)flags, nameBuf);
            if (rc != 0)
                throw ZlinkException.FromLastError();
            string service = NativeHelpers.ReadString(nameBuf, 256);
            Message[] messages = Message.FromNativeVector(parts, count);
            return new GatewayMessage(service, messages);
        }
    }

    public unsafe int ReceiveSinglePayload(Span<byte> payloadBuffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureNotDisposed();
        byte* nameBuf = stackalloc byte[256];
        int rc = NativeMethods.zlink_gateway_recv(_handle, out var parts,
            out var count, (int)flags, nameBuf);
        if (rc != 0)
            throw ZlinkException.FromLastError();
        try
        {
            return Message.CopySinglePartPayload(parts, count, payloadBuffer);
        }
        finally
        {
            if (parts != IntPtr.Zero && count > 0)
                NativeMethods.zlink_multipart_close(parts, count);
        }
    }

    public bool TryReceiveSinglePayload(Span<byte> payloadBuffer,
        out int payloadSize, ReceiveFlags flags = ReceiveFlags.DontWait)
    {
        return TryReceiveSinglePayload(payloadBuffer, out payloadSize, out _,
            flags);
    }

    public unsafe bool TryReceiveSinglePayload(Span<byte> payloadBuffer,
        out int payloadSize, out int errno,
        ReceiveFlags flags = ReceiveFlags.DontWait)
    {
        EnsureNotDisposed();
        byte* nameBuf = stackalloc byte[256];
        int rc = NativeMethods.zlink_gateway_recv(_handle, out var parts,
            out var count, (int)flags, nameBuf);
        if (rc != 0)
        {
            payloadSize = 0;
            errno = NativeMethods.zlink_errno();
            return false;
        }

        try
        {
            payloadSize = Message.CopySinglePartPayload(parts, count,
                payloadBuffer);
            errno = 0;
            return true;
        }
        finally
        {
            if (parts != IntPtr.Zero && count > 0)
                NativeMethods.zlink_multipart_close(parts, count);
        }
    }

    public bool TryReceiveSinglePayloadWithCode(Span<byte> payloadBuffer,
        out int payloadSize, out ErrorCode? errorCode,
        ReceiveFlags flags = ReceiveFlags.DontWait)
    {
        bool ok = TryReceiveSinglePayload(payloadBuffer, out payloadSize,
            out int errno, flags);
        errorCode = ZlinkException.TryMapErrorCode(errno, out var code)
            ? code
            : null;
        return ok;
    }

    public void SetLoadBalancing(string serviceName,
        GatewayLoadBalancing strategy)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_gateway_set_lb_strategy(_handle,
            serviceName, (int)strategy);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetTlsClient(string caCert, string hostname, bool trustSystem)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_gateway_set_tls_client(_handle, caCert,
            hostname, trustSystem ? 1 : 0);
        ZlinkException.ThrowIfError(rc);
    }

    public int ConnectionCount(string serviceName)
    {
        EnsureNotDisposed();
        int count = NativeMethods.zlink_gateway_connection_count(_handle,
            serviceName);
        if (count < 0)
            throw ZlinkException.FromLastError();
        return count;
    }

    public Socket CreateRouterSocket()
    {
        EnsureNotDisposed();
        IntPtr handle = NativeMethods.zlink_gateway_router_socket_unsafe(_handle);
        if (handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return Socket.Adopt(handle, false);
    }

    public PeerRecord[] GetRouterPeers()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_gateway_router_peers(_handle, IntPtr.Zero,
            ref count);
        ZlinkException.ThrowIfError(rc);
        if (count == 0)
            return Array.Empty<PeerRecord>();

        ZlinkPeerInfo[] native = ArrayPool<ZlinkPeerInfo>.Shared.Rent((int)count);
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_gateway_router_peers(_handle, native,
                ref actual);
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
            int rc = NativeMethods.zlink_gateway_setsockopt(_handle, (int)option,
                (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    public unsafe void SetOption(SocketOption option, int value)
    {
        EnsureNotDisposed();
        int tmp = value;
        int rc = NativeMethods.zlink_gateway_setsockopt(_handle, (int)option,
            (IntPtr)(&tmp), (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_gateway_destroy(ref _handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~Gateway()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Gateway));
    }

    private byte[] GetServiceNameUtf8(string serviceName)
    {
        if (_serviceNameUtf8Cache.TryGetValue(serviceName, out var cached))
            return cached;

        byte[] encoded = EncodeServiceNameUtf8(serviceName);
        if (_serviceNameUtf8Cache.Count < ServiceNameCacheLimit)
        {
            _serviceNameUtf8Cache.TryAdd(serviceName, encoded);
        }
        return encoded;
    }

    private static byte[] EncodeServiceNameUtf8(string serviceName)
    {
        int byteCount = Encoding.UTF8.GetByteCount(serviceName);
        byte[] bytes = new byte[byteCount + 1];
        Encoding.UTF8.GetBytes(serviceName, bytes.AsSpan(0, byteCount));
        bytes[byteCount] = 0;
        return bytes;
    }
}

public readonly struct GatewayMessage
{
    public GatewayMessage(string serviceName, Message[] parts)
    {
        ServiceName = serviceName;
        Parts = parts;
    }

    public string ServiceName { get; }
    public Message[] Parts { get; }
}
