// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Text;
using Zlink;
using Zlink.Native;

namespace Zlink.Service;

public sealed class Receiver : IDisposable
{
    private const int EndpointBufferSize = 256;
    private IntPtr _handle;

    internal IntPtr Handle => _handle;

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

    public unsafe void SetRoutingId(string routingId)
    {
        ValidateNotEmpty(routingId, nameof(routingId));
        EnsureNotDisposed();
        byte[] encoded = RoutingIdCodec.FromPublicString(routingId,
            nameof(routingId));
        fixed (byte* ptr = encoded)
        {
            int rc = NativeMethods.zlink_receiver_set_routing_id(_handle,
                (IntPtr)ptr, (nuint)encoded.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    public string GetLastEndpoint()
    {
        EnsureNotDisposed();
        byte[] endpoint = new byte[EndpointBufferSize];
        nuint size = EndpointBufferSize;
        int rc = NativeMethods.zlink_receiver_last_endpoint(_handle, endpoint,
            ref size);
        ZlinkException.ThrowIfError(rc);
        int length = (int)Math.Min((nuint)endpoint.Length, size);
        if (length > 0 && endpoint[length - 1] == 0)
            length--;
        return Encoding.UTF8.GetString(endpoint, 0, length);
    }

    public void SetOption(SocketOptionKey<int> option, int value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectInt32(option.ValueKind, nameof(option));
        SetOptionInt32(option.Option, value);
    }

    public void SetOption(SocketOptionKey<long> option, long value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectInt64(option.ValueKind, nameof(option));
        SetOptionInt64(option.Option, value);
    }

    public void SetOption(SocketOptionKey<ulong> option, ulong value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectUInt64(option.ValueKind, nameof(option));
        SetOptionUInt64(option.Option, value);
    }

    public void SetOption(SocketOptionKey<byte[]> option, byte[] value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetOption(option, value.AsSpan());
    }

    public void SetOption(SocketOptionKey<byte[]> option, ReadOnlySpan<byte> value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectBytes(option.ValueKind, nameof(option));
        SetOptionBytes(option.Option, value);
    }

    public void SetOption(SocketOptionKey<string> option, string value)
    {
        EnsureNotDisposed();
        SocketOptionValidation.ExpectString(option.ValueKind, nameof(option));
        SetOptionString(option.Option, value);
    }

    public void SetOption(ReceiverSocketRole role, SocketOptionKey<int> option,
        int value)
    {
        ValidateRole(role);
        SetOption(option, value);
    }

    public void SetOption(ReceiverSocketRole role, SocketOptionKey<long> option,
        long value)
    {
        ValidateRole(role);
        SetOption(option, value);
    }

    public void SetOption(ReceiverSocketRole role, SocketOptionKey<ulong> option,
        ulong value)
    {
        ValidateRole(role);
        SetOption(option, value);
    }

    public void SetOption(ReceiverSocketRole role, SocketOptionKey<byte[]> option,
        byte[] value)
    {
        ValidateRole(role);
        SetOption(option, value);
    }

    public void SetOption(ReceiverSocketRole role, SocketOptionKey<byte[]> option,
        ReadOnlySpan<byte> value)
    {
        ValidateRole(role);
        SetOption(option, value);
    }

    public void SetOption(ReceiverSocketRole role, SocketOptionKey<string> option,
        string value)
    {
        ValidateRole(role);
        SetOption(option, value);
    }

    private unsafe void SetOptionInt32(SocketOption option, int value)
    {
        int mapped = MapReceiverOption(option);
        int tmp = value;
        int rc = NativeMethods.zlink_receiver_set_option(_handle, mapped,
            (IntPtr)(&tmp), (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionInt64(SocketOption option, long value)
    {
        int mapped = MapReceiverOption(option);
        long tmp = value;
        int rc = NativeMethods.zlink_receiver_set_option(_handle, mapped,
            (IntPtr)(&tmp), (nuint)sizeof(long));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionUInt64(SocketOption option, ulong value)
    {
        int mapped = MapReceiverOption(option);
        ulong tmp = value;
        int rc = NativeMethods.zlink_receiver_set_option(_handle, mapped,
            (IntPtr)(&tmp), (nuint)sizeof(ulong));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionBytes(SocketOption option, ReadOnlySpan<byte> value)
    {
        int mapped = MapReceiverOption(option);
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_receiver_set_option(_handle, mapped,
                (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    private void SetOptionString(SocketOption option, string value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));

        int maxByteCount = Encoding.UTF8.GetMaxByteCount(value.Length);
        if (maxByteCount <= 512)
        {
            Span<byte> buffer = stackalloc byte[maxByteCount];
            int byteCount = Encoding.UTF8.GetBytes(value.AsSpan(), buffer);
            SetOptionBytes(option, buffer.Slice(0, byteCount));
            return;
        }

        byte[] rented = ArrayPool<byte>.Shared.Rent(maxByteCount);
        try
        {
            int byteCount = Encoding.UTF8.GetBytes(value, rented);
            SetOptionBytes(option, rented.AsSpan(0, byteCount));
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
    }

    public Socket CreateRouterSocket()
    {
        EnsureNotDisposed();
        throw new ZlinkException((int)ErrorCode.ENotSup,
            "Receiver router socket handle is not exposed by the current core API.");
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

    public ReceiverMessage Receive(ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_recv(_handle, out var parts,
            out var count, (int)flags, out var routingId);
        if (rc != 0)
            throw ZlinkException.FromLastError();
        Message[] messages = Message.FromNativeVector(parts, count);
        string publicRoutingId = RoutingIdCodec.ToPublicString(
            NativeHelpers.ReadRoutingId(ref routingId));
        return new ReceiverMessage(publicRoutingId, messages);
    }

    public int ReceiveSinglePayload(Span<byte> payloadBuffer,
        ReceiveFlags flags = ReceiveFlags.None)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_receiver_recv(_handle, out var parts,
            out var count, (int)flags, out _);
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

    private static void ValidateRole(ReceiverSocketRole role)
    {
        if (role != ReceiverSocketRole.Router
            && role != ReceiverSocketRole.Dealer)
        {
            throw new ArgumentException(
                $"Unsupported receiver socket role '{role}'.", nameof(role));
        }
    }

    private static int MapReceiverOption(SocketOption option)
    {
        return option switch
        {
            SocketOption.SndHwm => 1,
            SocketOption.RcvHwm => 2,
            SocketOption.SndTimeo => 3,
            SocketOption.RcvTimeo => 4,
            SocketOption.Linger => 5,
            SocketOption.SndBuf => 6,
            SocketOption.RcvBuf => 7,
            _ => throw new ArgumentException(
                $"Socket option '{option}' is not supported by Receiver.",
                nameof(option))
        };
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

public readonly struct ReceiverMessage
{
    public ReceiverMessage(string routingId, Message[] parts)
    {
        RoutingId = routingId;
        Parts = parts;
    }

    public string RoutingId { get; }
    public Message[] Parts { get; }
}
