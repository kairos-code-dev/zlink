// SPDX-License-Identifier: MPL-2.0

using System;
using Zlink.Native;

namespace Zlink;

public sealed class Gateway : IDisposable
{
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
        EnsureNotDisposed();
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Length == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));
        var tmp = new ZlinkMsg[parts.Length];
        int built = 0;
        for (int i = 0; i < parts.Length; i++)
        {
            parts[i].CopyTo(ref tmp[i]);
            built++;
        }
        int rc = NativeMethods.zlink_gateway_send(_handle, serviceName, tmp,
            (nuint)tmp.Length, (int)flags);
        if (rc < 0)
        {
            for (int i = 0; i < built; i++)
            {
                NativeMethods.zlink_msg_close(ref tmp[i]);
            }
        }
        ZlinkException.ThrowIfError(rc);
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
