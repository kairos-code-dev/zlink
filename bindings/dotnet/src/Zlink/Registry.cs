// SPDX-License-Identifier: MPL-2.0

using System;
using Zlink.Native;

namespace Zlink;

public sealed class Registry : IDisposable
{
    private IntPtr _handle;

    public Registry(Context context)
    {
        _handle = NativeMethods.zlink_registry_new(context.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    internal IntPtr Handle => _handle;

    public void SetEndpoints(string pubEndpoint, string routerEndpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_set_endpoints(_handle,
            pubEndpoint, routerEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetId(uint registryId)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_set_id(_handle, registryId);
        ZlinkException.ThrowIfError(rc);
    }

    public void AddPeer(string peerPubEndpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_add_peer(_handle,
            peerPubEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetHeartbeat(uint intervalMs, uint timeoutMs)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_set_heartbeat(_handle,
            intervalMs, timeoutMs);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetBroadcastInterval(uint intervalMs)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_set_broadcast_interval(_handle,
            intervalMs);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetSockOpt(RegistrySocketRole role, SocketOption option, byte[] value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetSockOpt(role, option, value.AsSpan());
    }

    public unsafe void SetSockOpt(RegistrySocketRole role, SocketOption option,
        ReadOnlySpan<byte> value)
    {
        EnsureNotDisposed();
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_registry_setsockopt(_handle, (int)role,
                (int)option, (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    public unsafe void SetSockOpt(RegistrySocketRole role, SocketOption option, int value)
    {
        EnsureNotDisposed();
        int tmp = value;
        int rc = NativeMethods.zlink_registry_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    public void Start()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_start(_handle);
        ZlinkException.ThrowIfError(rc);
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_registry_destroy(ref _handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~Registry()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Registry));
    }
}
