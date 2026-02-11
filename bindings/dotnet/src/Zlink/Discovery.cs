// SPDX-License-Identifier: MPL-2.0

using System;
using Zlink.Native;

namespace Zlink;

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
        _handle = NativeMethods.zlink_discovery_new_typed(context.Handle,
            (ushort)serviceType);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    internal IntPtr Handle => _handle;

    public void ConnectRegistry(string registryPubEndpoint)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_connect_registry(_handle,
            registryPubEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void Subscribe(string serviceName)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_subscribe(_handle, serviceName);
        ZlinkException.ThrowIfError(rc);
    }

    public void Unsubscribe(string serviceName)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_unsubscribe(_handle, serviceName);
        ZlinkException.ThrowIfError(rc);
    }

    public unsafe void SetSockOpt(DiscoverySocketRole role, SocketOption option, byte[] value)
    {
        EnsureNotDisposed();
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_discovery_setsockopt(_handle, (int)role,
                (int)option, (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    public unsafe void SetSockOpt(DiscoverySocketRole role, SocketOption option, int value)
    {
        EnsureNotDisposed();
        int tmp = value;
        int rc = NativeMethods.zlink_discovery_setsockopt(_handle, (int)role,
            (int)option, (IntPtr)(&tmp), (nuint)sizeof(int));
        ZlinkException.ThrowIfError(rc);
    }

    public int ReceiverCount(string serviceName)
    {
        EnsureNotDisposed();
        int count = NativeMethods.zlink_discovery_receiver_count(_handle,
            serviceName);
        if (count < 0)
            throw ZlinkException.FromLastError();
        return count;
    }

    public bool ServiceAvailable(string serviceName)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_service_available(_handle,
            serviceName);
        if (rc < 0)
            throw ZlinkException.FromLastError();
        return rc != 0;
    }

    public ReceiverInfoRecord[] GetReceivers(string serviceName)
    {
        EnsureNotDisposed();
        int count = ReceiverCount(serviceName);
        if (count == 0)
            return Array.Empty<ReceiverInfoRecord>();
        var providers = new ZlinkProviderInfo[count];
        nuint size = (nuint)providers.Length;
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
