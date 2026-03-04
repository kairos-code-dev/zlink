// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Text;
using System.Threading;
using Zlink;
using Zlink.Native;

namespace Zlink.Service;

public sealed class Gateway : IDisposable
{
    private const int StackSendPartLimit = 8;
    private const int ServiceNameCacheLimit = 1024;
    private const int RoutingIdCacheLimit = 1024;
    private IntPtr _handle;
    private int _serviceNameUtf8CacheCount;
    private int _routingIdUtf8CacheCount;
    private readonly ConcurrentDictionary<string, byte[]> _serviceNameUtf8Cache =
        new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<string, byte[]> _routingIdUtf8Cache =
        new(StringComparer.Ordinal);

    public Gateway(Context context, Discovery discovery)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));
        _handle = NativeMethods.zlink_gateway_new(context.Handle,
            discovery.Handle, null);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    public Gateway(Context context, Discovery discovery, string routingId)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));
        ValidateRoutingIdString(routingId, nameof(routingId));
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

    public void SendToRoutingId(string serviceName, string routingId,
        Message[] parts, SendFlags flags = SendFlags.None)
    {
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        SendToRoutingId(serviceName, GetRoutingIdUtf8(routingId).AsSpan(),
            parts.AsSpan(), flags);
    }

    public void SendToRoutingId(string serviceName, string routingId,
        ReadOnlySpan<Message> parts, SendFlags flags = SendFlags.None)
    {
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        SendToRoutingId(serviceName, GetRoutingIdUtf8(routingId).AsSpan(),
            parts, flags);
    }

    public unsafe void SendToRoutingId(string serviceName,
        ReadOnlySpan<byte> routingId, ReadOnlySpan<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        SendCore(serviceName, routingId, useRoutingId: true, parts, flags);
    }

    public void SendToRoutingId(string serviceName, string routingId,
        ReadOnlySpan<byte> payload, SendFlags flags = SendFlags.None)
    {
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        SendToRoutingId(serviceName, GetRoutingIdUtf8(routingId).AsSpan(),
            payload, flags);
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
        ValidateServiceName(serviceName, nameof(serviceName));
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
        ValidateServiceName(serviceName, nameof(serviceName));

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

    /// <summary>
    /// Receives a gateway message.
    /// Message ownership is transferred to the caller.
    /// Caller must dispose each part exactly once.
    /// </summary>
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

    public void SetLoadBalancing(string serviceName,
        GatewayLoadBalancing strategy)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_gateway_set_lb_strategy(_handle,
            serviceName, (int)strategy);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetTlsClient(string caCert, string hostname, bool trustSystem)
    {
        if (caCert == null)
            throw new ArgumentNullException(nameof(caCert));
        if (hostname == null)
            throw new ArgumentNullException(nameof(hostname));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_gateway_set_tls_client(_handle, caCert,
            hostname, trustSystem ? 1 : 0);
        ZlinkException.ThrowIfError(rc);
    }

    public int ConnectionCount(string serviceName)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
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
        if (Volatile.Read(ref _serviceNameUtf8CacheCount) < ServiceNameCacheLimit
            && _serviceNameUtf8Cache.TryAdd(serviceName, encoded))
        {
            Interlocked.Increment(ref _serviceNameUtf8CacheCount);
        }
        return encoded;
    }

    private byte[] GetRoutingIdUtf8(string routingId)
    {
        if (_routingIdUtf8Cache.TryGetValue(routingId, out var cached))
            return cached;

        byte[] encoded = EncodeRoutingIdUtf8(routingId);
        if (Volatile.Read(ref _routingIdUtf8CacheCount) < RoutingIdCacheLimit
            && _routingIdUtf8Cache.TryAdd(routingId, encoded))
        {
            Interlocked.Increment(ref _routingIdUtf8CacheCount);
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

    private static byte[] EncodeRoutingIdUtf8(string routingId)
    {
        int byteCount = Encoding.UTF8.GetByteCount(routingId);
        if (byteCount <= 0 || byteCount > 255)
        {
            throw new ArgumentOutOfRangeException(nameof(routingId),
                "routingId UTF-8 length must be between 1 and 255 bytes.");
        }

        byte[] bytes = new byte[byteCount];
        Encoding.UTF8.GetBytes(routingId, bytes.AsSpan());
        return bytes;
    }

    private static void ValidateServiceName(string serviceName, string paramName)
    {
        if (serviceName == null)
            throw new ArgumentNullException(paramName);
        if (serviceName.Length == 0)
            throw new ArgumentException("Service name must not be empty.",
                paramName);
    }

    private static void ValidateRoutingIdString(string routingId, string paramName)
    {
        if (routingId == null)
            throw new ArgumentNullException(paramName);
        if (routingId.Length == 0)
            throw new ArgumentException("routingId must not be empty.",
                paramName);
        int byteCount = Encoding.UTF8.GetByteCount(routingId);
        if (byteCount <= 0 || byteCount > 255)
        {
            throw new ArgumentOutOfRangeException(paramName,
                "routingId UTF-8 length must be between 1 and 255 bytes.");
        }
    }
}

public readonly struct GatewayMessage
{
    /// <summary>
    /// Gateway receive result.
    /// Message ownership is transferred to the caller.
    /// Caller must dispose each part exactly once.
    /// </summary>
    public GatewayMessage(string serviceName, Message[] parts)
    {
        ServiceName = serviceName;
        Parts = parts;
    }

    public string ServiceName { get; }
    public Message[] Parts { get; }
}
