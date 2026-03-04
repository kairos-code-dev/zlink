// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Text;
using Zlink;
using Zlink.Native;

namespace Zlink.Service;

public sealed class Receiver : IDisposable
{
    private IntPtr _handle;

    public Receiver(Context context)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        _handle = NativeMethods.zlink_receiver_new(context.Handle, null);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    public Receiver(Context context, string routingId)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        ValidateNotEmpty(routingId, nameof(routingId));
        _handle = NativeMethods.zlink_receiver_new(context.Handle, routingId);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    public void Bind(string bindEndpoint)
    {
        ValidateNotEmpty(bindEndpoint, nameof(bindEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_bind(_handle, bindEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void ConnectRegistry(string registryEndpoint)
    {
        ValidateNotEmpty(registryEndpoint, nameof(registryEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_connect_registry(_handle,
            registryEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void Register(string serviceName, string advertiseEndpoint,
        uint weight)
    {
        ValidateNotEmpty(serviceName, nameof(serviceName));
        ValidateNotEmpty(advertiseEndpoint, nameof(advertiseEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_register(_handle, serviceName,
            advertiseEndpoint, weight);
        ZlinkException.ThrowIfError(rc);
    }

    public void UpdateWeight(string serviceName, uint weight)
    {
        ValidateNotEmpty(serviceName, nameof(serviceName));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_update_weight(_handle,
            serviceName, weight);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unregister(string serviceName)
    {
        ValidateNotEmpty(serviceName, nameof(serviceName));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_unregister(_handle, serviceName);
        ZlinkException.ThrowIfError(rc);
    }

    public ReceiverRegisterResult GetRegisterResult(string serviceName)
    {
        ValidateNotEmpty(serviceName, nameof(serviceName));
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
        ValidateNotEmpty(cert, nameof(cert));
        ValidateNotEmpty(key, nameof(key));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_set_tls_server(_handle, cert, key);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetOption(ReceiverSocketRole role, SocketOptionKey<int> option,
        int value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.Int32)
            throw new ArgumentException("Expected int socket option key.",
                nameof(option));
        SetOptionInt32(role, option.Option, value);
    }

    public void SetOption(ReceiverSocketRole role, SocketOptionKey<long> option,
        long value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.Int64)
            throw new ArgumentException("Expected long socket option key.",
                nameof(option));
        SetOptionInt64(role, option.Option, value);
    }

    public void SetOption(ReceiverSocketRole role, SocketOptionKey<ulong> option,
        ulong value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.UInt64)
            throw new ArgumentException("Expected ulong socket option key.",
                nameof(option));
        SetOptionUInt64(role, option.Option, value);
    }

    public void SetOption(ReceiverSocketRole role, SocketOptionKey<byte[]> option,
        byte[] value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetOption(role, option, value.AsSpan());
    }

    public void SetOption(ReceiverSocketRole role, SocketOptionKey<byte[]> option,
        ReadOnlySpan<byte> value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.Bytes)
            throw new ArgumentException("Expected byte[] socket option key.",
                nameof(option));
        SetOptionBytes(role, option.Option, value);
    }

    public void SetOption(ReceiverSocketRole role, SocketOptionKey<string> option,
        string value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.String)
            throw new ArgumentException("Expected string socket option key.",
                nameof(option));
        SetOptionString(role, option.Option, value);
    }

    private unsafe void SetOptionInt32(ReceiverSocketRole role,
        SocketOption option, int value)
    {
        int tmp = value;
        int rc = NativeMethods.zlink_receiver_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionInt64(ReceiverSocketRole role,
        SocketOption option, long value)
    {
        long tmp = value;
        int rc = NativeMethods.zlink_receiver_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(long));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionUInt64(ReceiverSocketRole role,
        SocketOption option, ulong value)
    {
        ulong tmp = value;
        int rc = NativeMethods.zlink_receiver_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(ulong));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionBytes(ReceiverSocketRole role,
        SocketOption option, ReadOnlySpan<byte> value)
    {
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_receiver_setsockopt(_handle, (int)role,
                (int)option, (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    private void SetOptionString(ReceiverSocketRole role, SocketOption option,
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

    public Socket CreateRouterSocket()
    {
        EnsureNotDisposed();
        IntPtr handle = NativeMethods.zlink_receiver_router_socket_unsafe(_handle);
        if (handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return Socket.Adopt(handle, false);
    }

    public PeerRecord[] GetRouterPeers()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_receiver_router_peers(_handle,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowIfError(rc);
        if (count == 0)
            return Array.Empty<PeerRecord>();

        ZlinkPeerInfo[] native = ArrayPool<ZlinkPeerInfo>.Shared.Rent((int)count);
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_receiver_router_peers(_handle, native,
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

    private static void ValidateNotEmpty(string value, string paramName)
    {
        if (value == null)
            throw new ArgumentNullException(paramName);
        if (value.Length == 0)
            throw new ArgumentException("Value must not be empty.", paramName);
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
