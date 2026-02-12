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
        SendCore(serviceName, default, useRoutingId: false, parts, flags,
            moveParts: false);
    }

    public unsafe void Send(string serviceName, ReadOnlySpan<byte> payload,
        SendFlags flags = SendFlags.None)
    {
        SendSinglePayloadCore(serviceName, default, useRoutingId: false, payload,
            flags);
    }

    public void SendMove(string serviceName, Message[] parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        SendMove(serviceName, parts.AsSpan(), flags);
    }

    public unsafe void SendMove(string serviceName,
        ReadOnlySpan<Message> parts, SendFlags flags = SendFlags.None)
    {
        SendCore(serviceName, default, useRoutingId: false, parts, flags,
            moveParts: true);
    }

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
        SendCore(serviceName, routingId, useRoutingId: true, parts, flags,
            moveParts: false);
    }

    public unsafe void SendToRoutingId(string serviceName,
        ReadOnlySpan<byte> routingId, ReadOnlySpan<byte> payload,
        SendFlags flags = SendFlags.None)
    {
        SendSinglePayloadCore(serviceName, routingId, useRoutingId: true, payload,
            flags);
    }

    public void SendMoveToRoutingId(string serviceName, byte[] routingId,
        Message[] parts, SendFlags flags = SendFlags.None)
    {
        if (routingId == null)
            throw new ArgumentNullException(nameof(routingId));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        SendMoveToRoutingId(serviceName, routingId.AsSpan(), parts.AsSpan(),
            flags);
    }

    public unsafe void SendMoveToRoutingId(string serviceName,
        ReadOnlySpan<byte> routingId, ReadOnlySpan<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        SendCore(serviceName, routingId, useRoutingId: true, parts, flags,
            moveParts: true);
    }

    private unsafe void SendCore(string serviceName,
        ReadOnlySpan<byte> routingId, bool useRoutingId,
        ReadOnlySpan<Message> parts, SendFlags flags, bool moveParts)
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
                if (moveParts)
                    parts[i].MoveTo(ref nativeParts[i]);
                else
                    parts[i].CopyTo(ref nativeParts[i]);
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

        ZlinkMsg part = default;
        int rc = 0;
        bool built = false;
        try
        {
            Message.InitFromSpan(payload, ref part);
            built = true;
            fixed (byte* serviceNamePtr = serviceNameUtf8)
            {
                if (useRoutingId)
                {
                    rc = NativeMethods.zlink_gateway_send_rid(_handle,
                        serviceNamePtr, &nativeRoutingId, &part, (nuint)1,
                        (int)flags);
                }
                else
                {
                    rc = NativeMethods.zlink_gateway_send(_handle, serviceNamePtr,
                        &part, (nuint)1, (int)flags);
                }
            }
        }
        catch
        {
            if (built)
                NativeMethods.zlink_msg_close(ref part);
            throw;
        }

        if (rc < 0 && built)
            NativeMethods.zlink_msg_close(ref part);
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
                NativeMethods.zlink_msgv_close(parts, count);
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
                NativeMethods.zlink_msgv_close(parts, count);
        }
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

    public void SetSockOpt(SocketOption option, byte[] value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetSockOpt(option, value.AsSpan());
    }

    public unsafe void SetSockOpt(SocketOption option, ReadOnlySpan<byte> value)
    {
        EnsureNotDisposed();
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_gateway_setsockopt(_handle, (int)option,
                (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    public unsafe void SetSockOpt(SocketOption option, int value)
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
        return _serviceNameUtf8Cache.GetOrAdd(serviceName, static key =>
        {
            int byteCount = Encoding.UTF8.GetByteCount(key);
            byte[] bytes = new byte[byteCount + 1];
            Encoding.UTF8.GetBytes(key, bytes.AsSpan(0, byteCount));
            bytes[byteCount] = 0;
            return bytes;
        });
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
