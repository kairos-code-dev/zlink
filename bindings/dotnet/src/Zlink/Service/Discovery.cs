// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
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

    public void SetOption(SocketOptionKey<int> option, int value)
    {
        SocketOptionValidation.ExpectInt32(option.ValueKind, nameof(option));
        ThrowSocketOptionNotSupported(option.Option);
    }

    public void SetOption(SocketOptionKey<long> option, long value)
    {
        SocketOptionValidation.ExpectInt64(option.ValueKind, nameof(option));
        ThrowSocketOptionNotSupported(option.Option);
    }

    public void SetOption(SocketOptionKey<ulong> option, ulong value)
    {
        SocketOptionValidation.ExpectUInt64(option.ValueKind, nameof(option));
        ThrowSocketOptionNotSupported(option.Option);
    }

    public void SetOption(SocketOptionKey<byte[]> option, byte[] value)
    {
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        SetOption(option, value.AsSpan());
    }

    public void SetOption(SocketOptionKey<byte[]> option, ReadOnlySpan<byte> value)
    {
        SocketOptionValidation.ExpectBytes(option.ValueKind, nameof(option));
        ThrowSocketOptionNotSupported(option.Option);
    }

    public void SetOption(SocketOptionKey<string> option, string value)
    {
        SocketOptionValidation.ExpectString(option.ValueKind, nameof(option));
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        ThrowSocketOptionNotSupported(option.Option);
    }

    public void SetOption(DiscoverySocketRole role, SocketOptionKey<int> option,
        int value)
    {
        ValidateRole(role);
        SetOption(option, value);
    }

    public void SetOption(DiscoverySocketRole role, SocketOptionKey<long> option,
        long value)
    {
        ValidateRole(role);
        SetOption(option, value);
    }

    public void SetOption(DiscoverySocketRole role, SocketOptionKey<ulong> option,
        ulong value)
    {
        ValidateRole(role);
        SetOption(option, value);
    }

    public void SetOption(DiscoverySocketRole role, SocketOptionKey<byte[]> option,
        byte[] value)
    {
        ValidateRole(role);
        SetOption(option, value);
    }

    public void SetOption(DiscoverySocketRole role, SocketOptionKey<byte[]> option,
        ReadOnlySpan<byte> value)
    {
        ValidateRole(role);
        SetOption(option, value);
    }

    public void SetOption(DiscoverySocketRole role, SocketOptionKey<string> option,
        string value)
    {
        ValidateRole(role);
        SetOption(option, value);
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
                result[i] = ReceiverInfoRecord.FromNative(ref providers[i]);
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

    private static void ValidateRole(DiscoverySocketRole role)
    {
        if (role != DiscoverySocketRole.Sub)
        {
            throw new ArgumentException(
                $"Unsupported discovery socket role '{role}'.", nameof(role));
        }
    }

    private void ThrowSocketOptionNotSupported(SocketOption option)
    {
        EnsureNotDisposed();
        throw new ZlinkException((int)ErrorCode.ENotSup,
            $"Discovery socket option '{option}' is not supported.");
    }
}

public readonly struct ReceiverInfoRecord
{
    public ReceiverInfoRecord(string serviceName, string endpoint,
        string routingId,
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
    public string RoutingId { get; }
    public uint Weight { get; }
    public ulong RegisteredAt { get; }

    internal static ReceiverInfoRecord FromNative(ref ZlinkProviderInfo info)
    {
        string service = NativeHelpers.ReadFixedString(ref info, true);
        string endpoint = NativeHelpers.ReadFixedString(ref info, false);
        byte[] routing = NativeHelpers.ReadRoutingId(ref info.RoutingId);
        string routingId = RoutingIdCodec.ToPublicString(routing);
        return new ReceiverInfoRecord(service, endpoint, routingId, info.Weight,
            info.RegisteredAt);
    }
}
