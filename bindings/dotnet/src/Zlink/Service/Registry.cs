// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Text;
using Zlink;
using Zlink.Native;

namespace Zlink.Service;

public sealed class Registry : IDisposable
{
    private IntPtr _handle;

    public Registry(Context context)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        _handle = NativeMethods.zlink_registry_new(context.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    internal IntPtr Handle => _handle;

    public void SetEndpoints(string pubEndpoint, string routerEndpoint)
    {
        ValidateNotEmpty(pubEndpoint, nameof(pubEndpoint));
        ValidateNotEmpty(routerEndpoint, nameof(routerEndpoint));
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
        ValidateNotEmpty(peerPubEndpoint, nameof(peerPubEndpoint));
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

    public void SetOption(RegistrySocketRole role, SocketOptionKey<int> option,
        int value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectInt32(option.ValueKind, nameof(option));
        SetOptionInt32(role, option.Option, value);
    }

    public void SetOption(RegistrySocketRole role, SocketOptionKey<long> option,
        long value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectInt64(option.ValueKind, nameof(option));
        SetOptionInt64(role, option.Option, value);
    }

    public void SetOption(RegistrySocketRole role, SocketOptionKey<ulong> option,
        ulong value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectUInt64(option.ValueKind, nameof(option));
        SetOptionUInt64(role, option.Option, value);
    }

    public void SetOption(RegistrySocketRole role, SocketOptionKey<byte[]> option,
        byte[] value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetOption(role, option, value.AsSpan());
    }

    public void SetOption(RegistrySocketRole role, SocketOptionKey<byte[]> option,
        ReadOnlySpan<byte> value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectBytes(option.ValueKind, nameof(option));
        SetOptionBytes(role, option.Option, value);
    }

    public void SetOption(RegistrySocketRole role, SocketOptionKey<string> option,
        string value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectString(option.ValueKind, nameof(option));
        SetOptionString(role, option.Option, value);
    }

    private unsafe void SetOptionInt32(RegistrySocketRole role,
        SocketOption option, int value)
    {
        int tmp = value;
        int rc = NativeMethods.zlink_registry_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionInt64(RegistrySocketRole role,
        SocketOption option, long value)
    {
        long tmp = value;
        int rc = NativeMethods.zlink_registry_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(long));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionUInt64(RegistrySocketRole role,
        SocketOption option, ulong value)
    {
        ulong tmp = value;
        int rc = NativeMethods.zlink_registry_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(ulong));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionBytes(RegistrySocketRole role,
        SocketOption option, ReadOnlySpan<byte> value)
    {
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_registry_setsockopt(_handle, (int)role,
                (int)option, (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    private void SetOptionString(RegistrySocketRole role, SocketOption option,
        string value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));

        int maxByteCount = Encoding.UTF8.GetMaxByteCount(value.Length);
        if (maxByteCount <= 512)
        {
            Span<byte> buffer = stackalloc byte[maxByteCount];
            int byteCount = Encoding.UTF8.GetBytes(value.AsSpan(), buffer);
            SetOptionBytes(role, option, buffer.Slice(0, byteCount));
            return;
        }

        byte[] rented = ArrayPool<byte>.Shared.Rent(maxByteCount);
        try
        {
            int byteCount = Encoding.UTF8.GetBytes(value, rented);
            SetOptionBytes(role, option, rented.AsSpan(0, byteCount));
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
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

    private static void ValidateNotEmpty(string value, string paramName)
    {
        if (value == null)
            throw new ArgumentNullException(paramName);
        if (value.Length == 0)
            throw new ArgumentException("Value must not be empty.", paramName);
    }
}
