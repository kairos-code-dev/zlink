// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using Zlink.Native;

namespace Zlink;

public sealed class Gateway : IDisposable
{
    private const int StackSendPartLimit = 8;
    private IntPtr _handle;

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
            {
                if (useRoutingId)
                {
                    rc = NativeMethods.zlink_gateway_send_rid(_handle,
                        serviceName, &nativeRoutingId, ptr,
                        (nuint)nativeParts.Length, (int)flags);
                }
                else
                {
                    rc = NativeMethods.zlink_gateway_send(_handle, serviceName,
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
