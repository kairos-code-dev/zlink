// SPDX-License-Identifier: MPL-2.0

using System;
using Zlink.Native;

namespace Zlink;

public sealed class Receiver : IDisposable
{
    private IntPtr _handle;

    public Receiver(Context context)
    {
        _handle = NativeMethods.zlink_receiver_new(context.Handle, null);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    public Receiver(Context context, string routingId)
    {
        _handle = NativeMethods.zlink_receiver_new(context.Handle, routingId);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    public void Bind(string bindEndpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_bind(_handle, bindEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void ConnectRegistry(string registryEndpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_connect_registry(_handle,
            registryEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void Register(string serviceName, string advertiseEndpoint,
        uint weight)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_register(_handle, serviceName,
            advertiseEndpoint, weight);
        ZlinkException.ThrowIfError(rc);
    }

    public void UpdateWeight(string serviceName, uint weight)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_update_weight(_handle,
            serviceName, weight);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unregister(string serviceName)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_unregister(_handle, serviceName);
        ZlinkException.ThrowIfError(rc);
    }

    public ReceiverRegisterResult RegisterResult(string serviceName)
    {
        EnsureNotDisposed();
        unsafe
        {
            byte* resolved = stackalloc byte[256];
            byte* error = stackalloc byte[256];
            int rc = NativeMethods.zlink_receiver_register_result(_handle,
                serviceName, out int status, resolved, error);
            ZlinkException.ThrowIfError(rc);
            string resolvedEndpoint = NativeHelpers.ReadString(resolved, 256);
            string errorMessage = NativeHelpers.ReadString(error, 256);
            return new ReceiverRegisterResult(status, resolvedEndpoint,
                errorMessage);
        }
    }

    public void SetTlsServer(string cert, string key)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_set_tls_server(_handle, cert, key);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetSockOpt(ReceiverSocketRole role, SocketOption option, byte[] value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetSockOpt(role, option, value.AsSpan());
    }

    public unsafe void SetSockOpt(ReceiverSocketRole role, SocketOption option,
        ReadOnlySpan<byte> value)
    {
        EnsureNotDisposed();
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_receiver_setsockopt(_handle, (int)role,
                (int)option, (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    public unsafe void SetSockOpt(ReceiverSocketRole role, SocketOption option, int value)
    {
        EnsureNotDisposed();
        int tmp = value;
        int rc = NativeMethods.zlink_receiver_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    public Socket CreateRouterSocket()
    {
        EnsureNotDisposed();
        IntPtr handle = NativeMethods.zlink_receiver_router(_handle);
        if (handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return Socket.Adopt(handle, false);
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_receiver_destroy(ref _handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~Receiver()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Receiver));
    }
}

public readonly struct ReceiverRegisterResult
{
    public ReceiverRegisterResult(int status, string resolvedEndpoint,
        string errorMessage)
    {
        Status = status;
        ResolvedEndpoint = resolvedEndpoint;
        ErrorMessage = errorMessage;
    }

    public int Status { get; }
    public string ResolvedEndpoint { get; }
    public string ErrorMessage { get; }
}
