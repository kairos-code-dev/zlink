// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Text;
using Zlink;
using Zlink.Native;

namespace Zlink.Service;

public enum DiscoveryServiceType : ushort
{
    Gateway = 1,
    Spot = 2
}

public sealed class Discovery : IDisposable
{
    private IntPtr _handle;

    public Discovery(Context context, DiscoveryServiceType serviceType)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        _handle = NativeMethods.zlink_discovery_new_typed(context.Handle,
            (ushort)serviceType);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    internal IntPtr Handle => _handle;

    public void ConnectRegistry(string registryPubEndpoint)
    {
        ValidateNotEmpty(registryPubEndpoint, nameof(registryPubEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_connect_registry(_handle,
            registryPubEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void Subscribe(string serviceName)
    {
        ValidateNotEmpty(serviceName, nameof(serviceName));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_subscribe(_handle, serviceName);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unsubscribe(string serviceName)
    {
        ValidateNotEmpty(serviceName, nameof(serviceName));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_unsubscribe(_handle, serviceName);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetOption(DiscoverySocketRole role, SocketOptionKey<int> option,
        int value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.Int32)
            throw new ArgumentException("Expected int socket option key.",
                nameof(option));
        SetOptionInt32(role, option.Option, value);
    }

    public void SetOption(DiscoverySocketRole role, SocketOptionKey<long> option,
        long value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.Int64)
            throw new ArgumentException("Expected long socket option key.",
                nameof(option));
        SetOptionInt64(role, option.Option, value);
    }

    public void SetOption(DiscoverySocketRole role, SocketOptionKey<ulong> option,
        ulong value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.UInt64)
            throw new ArgumentException("Expected ulong socket option key.",
                nameof(option));
        SetOptionUInt64(role, option.Option, value);
    }

    public void SetOption(DiscoverySocketRole role, SocketOptionKey<byte[]> option,
        byte[] value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetOption(role, option, value.AsSpan());
    }

    public void SetOption(DiscoverySocketRole role, SocketOptionKey<byte[]> option,
        ReadOnlySpan<byte> value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.Bytes)
            throw new ArgumentException("Expected byte[] socket option key.",
                nameof(option));
        SetOptionBytes(role, option.Option, value);
    }

    public void SetOption(DiscoverySocketRole role, SocketOptionKey<string> option,
        string value)
    {
        EnsureNotDisposed();
        if (option.ValueKind != SocketOptionValueKind.String)
            throw new ArgumentException("Expected string socket option key.",
                nameof(option));
        SetOptionString(role, option.Option, value);
    }

    private unsafe void SetOptionInt32(DiscoverySocketRole role,
        SocketOption option, int value)
    {
        int tmp = value;
        int rc = NativeMethods.zlink_discovery_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionInt64(DiscoverySocketRole role,
        SocketOption option, long value)
    {
        long tmp = value;
        int rc = NativeMethods.zlink_discovery_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(long));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionUInt64(DiscoverySocketRole role,
        SocketOption option, ulong value)
    {
        ulong tmp = value;
        int rc = NativeMethods.zlink_discovery_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(ulong));
        ZlinkException.ThrowIfError(rc);
    }

    private unsafe void SetOptionBytes(DiscoverySocketRole role,
        SocketOption option, ReadOnlySpan<byte> value)
    {
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_discovery_setsockopt(_handle, (int)role,
                (int)option, (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    private void SetOptionString(DiscoverySocketRole role, SocketOption option,
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

    public int ReceiverCount(string serviceName)
    {
        ValidateNotEmpty(serviceName, nameof(serviceName));
        EnsureNotDisposed();
        int count = NativeMethods.zlink_discovery_receiver_count(_handle,
            serviceName);
        if (count < 0)
            throw ZlinkException.FromLastError();
        return count;
    }

    public bool ServiceAvailable(string serviceName)
    {
        ValidateNotEmpty(serviceName, nameof(serviceName));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_service_available(_handle,
            serviceName);
        if (rc < 0)
            throw ZlinkException.FromLastError();
        return rc != 0;
    }

    public ReceiverInfoRecord[] GetReceivers(string serviceName)
    {
        ValidateNotEmpty(serviceName, nameof(serviceName));
        EnsureNotDisposed();
        int count = ReceiverCount(serviceName);
        if (count == 0)
            return Array.Empty<ReceiverInfoRecord>();
        ZlinkProviderInfo[] providers = ArrayPool<ZlinkProviderInfo>.Shared
            .Rent(count);
        try
        {
            nuint size = (nuint)count;
            int rc = NativeMethods.zlink_discovery_get_receivers(_handle,
                serviceName, providers, ref size);
            ZlinkException.ThrowIfError(rc);

            int actual = (int)size;
            ReceiverInfoRecord[] result = new ReceiverInfoRecord[actual];
            for (int i = 0; i < actual; i++)
            {
                result[i] = ReceiverInfoRecord.FromNative(ref providers[i]);
            }
            return result;
        }
        finally
        {
            ArrayPool<ZlinkProviderInfo>.Shared.Return(providers);
        }
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_discovery_destroy(ref _handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~Discovery()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Discovery));
    }

    private static void ValidateNotEmpty(string value, string paramName)
    {
        if (value == null)
            throw new ArgumentNullException(paramName);
        if (value.Length == 0)
            throw new ArgumentException("Value must not be empty.", paramName);
    }
}

public readonly struct ReceiverInfoRecord
{
    public ReceiverInfoRecord(string serviceName, string endpoint,
        byte[] routingId,
        uint weight, ulong registeredAt)
    {
        ServiceName = serviceName;
        Endpoint = endpoint;
        RoutingId = routingId;
        Weight = weight;
        RegisteredAt = registeredAt;
    }

    public string ServiceName { get; }
    public string Endpoint { get; }
    public byte[] RoutingId { get; }
    public uint Weight { get; }
    public ulong RegisteredAt { get; }

    internal static ReceiverInfoRecord FromNative(ref ZlinkProviderInfo info)
    {
        string service = NativeHelpers.ReadFixedString(ref info, true);
        string endpoint = NativeHelpers.ReadFixedString(ref info, false);
        byte[] routing = NativeHelpers.ReadRoutingId(ref info.RoutingId);
        return new ReceiverInfoRecord(service, endpoint, routing, info.Weight,
            info.RegisteredAt);
    }
}
