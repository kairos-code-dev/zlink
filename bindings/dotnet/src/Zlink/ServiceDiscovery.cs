using System;
using Zlink.Native;

namespace Zlink;

public enum DiscoveryServiceType : ushort
{
    GatewayReceiver = 1,
    SpotNode = 2
}

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

    public unsafe void SetSockOpt(int role, int option, byte[] value)
    {
        EnsureNotDisposed();
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_registry_setsockopt(_handle, role,
                option, (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    public unsafe void SetSockOpt(int role, int option, int value)
    {
        EnsureNotDisposed();
        int tmp = value;
        int rc = NativeMethods.zlink_registry_setsockopt(_handle, role,
            option, (IntPtr)(&tmp), (nuint)sizeof(int));
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

    public unsafe void SetSockOpt(int role, int option, byte[] value)
    {
        EnsureNotDisposed();
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_discovery_setsockopt(_handle, role,
                option, (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    public unsafe void SetSockOpt(int role, int option, int value)
    {
        EnsureNotDisposed();
        int tmp = value;
        int rc = NativeMethods.zlink_discovery_setsockopt(_handle, role,
            option, (IntPtr)(&tmp), (nuint)sizeof(int));
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

    public unsafe void SetSockOpt(int option, byte[] value)
    {
        EnsureNotDisposed();
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_gateway_setsockopt(_handle, option,
                (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    public unsafe void SetSockOpt(int option, int value)
    {
        EnsureNotDisposed();
        int tmp = value;
        int rc = NativeMethods.zlink_gateway_setsockopt(_handle, option,
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

    public unsafe void SetSockOpt(int role, int option, byte[] value)
    {
        EnsureNotDisposed();
        if (value == null)
            throw new ArgumentNullException(nameof(value));
        fixed (byte* ptr = value)
        {
            int rc = NativeMethods.zlink_receiver_setsockopt(_handle, role,
                option, (IntPtr)ptr, (nuint)value.Length);
            ZlinkException.ThrowIfError(rc);
        }
    }

    public unsafe void SetSockOpt(int role, int option, int value)
    {
        EnsureNotDisposed();
        int tmp = value;
        int rc = NativeMethods.zlink_receiver_setsockopt(_handle, role,
            option, (IntPtr)(&tmp), (nuint)sizeof(int));
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
