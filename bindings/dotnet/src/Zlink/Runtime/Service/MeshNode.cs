// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using System.Text;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class MeshNode : IMeshNode
{
    private const int RouterHwmOption = 0x3621;
    private const int MailboxMessageBudgetOption = 0x3622;
    private const int MailboxByteBudgetOption = 0x3623;

    private readonly HashSet<Spot> _spots = new();
    private readonly object _spotsGate = new();
    private MeshReadyHandler? _readyHandler;
    private NativeMethods.ZlinkMeshReadyHandlerDelegate? _readyHandlerNative;

    internal MeshNode(Context context, MeshNodeOptions? options)
    {
        Context = context ?? throw new ArgumentNullException(nameof(context));
        Handle = CreateHandle(context, options);
        if (Handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    internal Context Context { get; }

    internal IntPtr Handle { get; private set; }

    public RoutingId RoutingId => Status().RoutingId;

    public unsafe void SetRoutingId(RoutingId routingId)
    {
        EnsureNotDisposed();
        var bytes = routingId.ToBytes();
        if (bytes.Length is 0 or > 255)
            throw new ArgumentOutOfRangeException(nameof(routingId),
                "Routing id must be between 1 and 255 bytes.");
        fixed (byte* data = bytes)
        {
            ZlinkException.ThrowConfigIfError(
                NativeMethods.zlink_set_routing_id(Handle, (IntPtr)data,
                    (nuint)bytes.Length));
        }
    }

    public void SetBind(string endpoint)
    {
        BoundaryValidation.ValidateMeshEndpoint(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_mesh_node_set_bind(Handle, endpoint));
    }

    public void Start()
    {
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_mesh_node_start(Handle));
    }

    public void Shutdown(TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_mesh_node_shutdown(Handle,
            MeshSend.EncodeTimeout(timeout));
        if (rc != 0)
            throw new ZlinkRequestException((RequestResult)rc);
    }

    public void AddChannel(string channelName)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_mesh_node_add_channel_name(Handle, channelName));
    }

    public void SetChannelWeight(string channelName, uint weight)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_mesh_node_set_channel_weight(Handle, channelName,
                weight));
    }

    public long MaxMessageSize
    {
        get => GetInt64CommonOption(SocketOption.MaxMsgSize);
        set => SetInt64CommonOption(SocketOption.MaxMsgSize, value);
    }

    public int RouterHighWaterMark
    {
        get => GetInt32MeshOption(RouterHwmOption);
        set => SetInt32MeshOption(RouterHwmOption, value);
    }

    public ulong MailboxMessageBudget
    {
        get => GetUInt64MeshOption(MailboxMessageBudgetOption);
        set => SetUInt64MeshOption(MailboxMessageBudgetOption, value);
    }

    public ulong MailboxByteBudget
    {
        get => GetUInt64MeshOption(MailboxByteBudgetOption);
        set => SetUInt64MeshOption(MailboxByteBudgetOption, value);
    }

    public TimeSpan? SendTimeout
    {
        get => DecodeDuration(GetInt32CommonOption(SocketOption.SndTimeo));
        set => SetInt32CommonOption(
            SocketOption.SndTimeo, EncodeDuration(value, nameof(value)));
    }

    public ulong ConnectPeer(string endpoint, RoutingId? expectedRid = null)
    {
        BoundaryValidation.ValidateMeshEndpoint(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        var endpointBytes = Encoding.UTF8.GetBytes(endpoint);
        var endpointHandle = GCHandle.Alloc(endpointBytes, GCHandleType.Pinned);
        try
        {
            var options = new ZlinkMeshPeerConnectionOptions
            {
                StructSize =
                    (uint)Marshal.SizeOf<ZlinkMeshPeerConnectionOptions>(),
                Version = 1,
                Endpoint = endpointHandle.AddrOfPinnedObject(),
                EndpointSize = (nuint)endpointBytes.Length,
                HasExpectedRid = expectedRid.HasValue ? 1u : 0u,
                ExpectedRid = expectedRid?.ToNative() ?? default
            };
            var rc = NativeMethods.zlink_mesh_node_connect_peer(Handle,
                ref options, out var intentId);
            ZlinkException.ThrowConnectIfError(rc);
            return intentId;
        }
        finally
        {
            endpointHandle.Free();
        }
    }

    private unsafe int GetInt32MeshOption(int option)
    {
        EnsureNotDisposed();
        var value = 0;
        var size = (nuint)sizeof(int);
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_get_mesh_node_option(
                Handle, option, (IntPtr)(&value), ref size));
        return value;
    }

    private unsafe void SetInt32MeshOption(int option, int value)
    {
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_set_mesh_node_option(
                Handle, option, (IntPtr)(&value), (nuint)sizeof(int)));
    }

    private unsafe ulong GetUInt64MeshOption(int option)
    {
        EnsureNotDisposed();
        ulong value = 0;
        var size = (nuint)sizeof(ulong);
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_get_mesh_node_option(
                Handle, option, (IntPtr)(&value), ref size));
        return value;
    }

    private unsafe void SetUInt64MeshOption(int option, ulong value)
    {
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_set_mesh_node_option(
                Handle, option, (IntPtr)(&value), (nuint)sizeof(ulong)));
    }

    private unsafe int GetInt32CommonOption(SocketOption option)
    {
        EnsureNotDisposed();
        var value = 0;
        var size = (nuint)sizeof(int);
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_get_option(
                Handle, (int)option, (IntPtr)(&value), ref size));
        return value;
    }

    private unsafe void SetInt32CommonOption(SocketOption option, int value)
    {
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_set_option(
                Handle, (int)option, (IntPtr)(&value), (nuint)sizeof(int)));
    }

    private unsafe long GetInt64CommonOption(SocketOption option)
    {
        EnsureNotDisposed();
        long value = 0;
        var size = (nuint)sizeof(long);
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_get_option(
                Handle, (int)option, (IntPtr)(&value), ref size));
        return value;
    }

    private unsafe void SetInt64CommonOption(SocketOption option, long value)
    {
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_set_option(
                Handle, (int)option, (IntPtr)(&value), (nuint)sizeof(long)));
    }

    private static TimeSpan? DecodeDuration(int value) =>
        value < 0 ? null : TimeSpan.FromMilliseconds(value);

    private static int EncodeDuration(TimeSpan? value, string paramName)
    {
        if (value is null)
            return -1;
        var milliseconds = value.Value.TotalMilliseconds;
        if (double.IsNaN(milliseconds)
            || double.IsInfinity(milliseconds)
            || milliseconds < 0
            || milliseconds > int.MaxValue)
            throw new ArgumentOutOfRangeException(paramName);
        return (int)Math.Ceiling(milliseconds);
    }

    public void RemovePeerConnection(ulong connectionIntentId)
    {
        EnsureNotDisposed();
        ZlinkException.ThrowConnectIfError(
            NativeMethods.zlink_mesh_node_remove_peer_connection(Handle,
                connectionIntentId));
    }

    public void DisconnectPeer(RoutingId peerRid, ulong lifecycleGeneration = 0)
    {
        EnsureNotDisposed();
        var nativeRid = peerRid.ToNative();
        ZlinkException.ThrowConnectIfError(
            NativeMethods.zlink_mesh_node_disconnect_peer(Handle, ref nativeRid,
                lifecycleGeneration));
    }

    public SubmitResult SendToNode(RoutingId targetRid,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        EnsureNotDisposed();
        var nativeRid = targetRid.ToNative();
        return MeshSend.SubmitWithMetadata(parts, nameof(parts), metadata,
            (np, count, meta) =>
                NativeMethods.zlink_mesh_node_send_to_node(Handle, ref nativeRid,
                    meta, np, count, (int)flags));
    }

    public SubmitResult RequestToNode(RoutingId targetRid,
        IReadOnlyList<Message> parts, out MeshOperationId operationId,
        TimeSpan timeout = default, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        EnsureNotDisposed();
        var nativeRid = targetRid.ToNative();
        var timeoutMs = MeshSend.EncodeTimeout(timeout);
        ZlinkMeshOperationId op = default;
        var result = MeshSend.SubmitWithMetadata(parts, nameof(parts), metadata,
            (np, count, meta) =>
                NativeMethods.zlink_mesh_node_request_to_node(Handle, ref nativeRid,
                    meta, np, count, out op, (int)flags, timeoutMs));
        operationId = new MeshOperationId(op.High, op.Low);
        return result;
    }

    public SubmitResult SendToChannel(string channelName,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        return MeshSend.SubmitWithMetadata(parts, nameof(parts), metadata,
            (np, count, meta) =>
                NativeMethods.zlink_mesh_node_send_to_channel(Handle, channelName,
                    meta, np, count, (int)flags));
    }

    public SubmitResult RequestToChannel(string channelName,
        IReadOnlyList<Message> parts, out MeshOperationId operationId,
        TimeSpan timeout = default, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        var timeoutMs = MeshSend.EncodeTimeout(timeout);
        ZlinkMeshOperationId op = default;
        var result = MeshSend.SubmitWithMetadata(parts, nameof(parts), metadata,
            (np, count, meta) =>
                NativeMethods.zlink_mesh_node_request_to_channel(Handle, channelName,
                    meta, np, count, out op, (int)flags, timeoutMs));
        operationId = new MeshOperationId(op.High, op.Low);
        return result;
    }

    public IMeshNodePublisher CreatePublisher()
    {
        EnsureNotDisposed();
        return new MeshNodePublisher(this);
    }

    public MeshNodeStatus Status()
    {
        EnsureNotDisposed();
        var native = new ZlinkMeshNodeStatus
        {
            StructSize = (uint)Marshal.SizeOf<ZlinkMeshNodeStatus>(),
            Version = 1
        };
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_mesh_node_status(Handle, ref native));
        return ConvertStatus(ref native);
    }

    public MeshNodePeer[] Peers()
    {
        EnsureNotDisposed();
        return NativeSnapshotReader.Read<ZlinkMeshPeerEntry, MeshNodePeer>(
            (IntPtr entries, ref nuint count) =>
                NativeMethods.zlink_mesh_node_peers(Handle, entries, ref count),
            (ref ZlinkMeshPeerEntry native) => ConvertPeer(ref native));
    }

    public MeshPeerChannel[] PeerChannels(
        RoutingId peerRid,
        ulong lifecycleGeneration)
    {
        EnsureNotDisposed();
        var nativeRid = peerRid.ToNative();
        nuint count = 0;
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_mesh_node_peer_channels(
                Handle,
                ref nativeRid,
                lifecycleGeneration,
                IntPtr.Zero,
                IntPtr.Zero,
                ref count));
        if (count == 0)
            return [];
        if (count > int.MaxValue)
            throw new OverflowException("Peer channel count exceeds the managed array limit.");

        const int nameStride = 256;
        var names = Marshal.AllocHGlobal(checked((int)count * nameStride));
        var weights = Marshal.AllocHGlobal(checked((int)count * sizeof(uint)));
        try
        {
            var returned = count;
            ZlinkException.ThrowConfigIfError(
                NativeMethods.zlink_mesh_node_peer_channels(
                    Handle,
                    ref nativeRid,
                    lifecycleGeneration,
                    names,
                    weights,
                    ref returned));
            if (returned > count)
                throw new InvalidOperationException(
                    "Peer channel snapshot grew while it was being copied.");

            var result = new MeshPeerChannel[(int)returned];
            for (var index = 0; index < result.Length; index++)
            {
                var name = Marshal.PtrToStringUTF8(IntPtr.Add(names, index * nameStride))
                           ?? string.Empty;
                var weight = unchecked((uint)Marshal.ReadInt32(
                    weights,
                    index * sizeof(uint)));
                result[index] = new MeshPeerChannel(name, weight);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(weights);
            Marshal.FreeHGlobal(names);
        }
    }

    public IMeshNodeMonitor OpenMonitor(
        MeshMonitorEventMask events = MeshMonitorEventMask.All)
    {
        EnsureNotDisposed();
        return new MeshNodeMonitor(this, events);
    }

    public void SetReadyHandler(MeshReadyHandler handler)
    {
        ArgumentNullException.ThrowIfNull(handler);
        EnsureNotDisposed();
        var native = new NativeMethods.ZlinkMeshReadyHandlerDelegate(
            OnNativeReady);
        _readyHandler = handler;
        _readyHandlerNative = native;
        var rc = NativeMethods.zlink_mesh_node_set_ready_handler(Handle, native,
            IntPtr.Zero);
        if (rc == 0)
            return;
        _readyHandler = null;
        _readyHandlerNative = null;
        ZlinkException.ThrowHandlerIfError(rc);
    }

    public bool DrainReady(MeshReadyDomains domains, MeshReadyBatch batch,
        RecvFlags flags = RecvFlags.None)
    {
        if (batch == null)
            throw new ArgumentNullException(nameof(batch));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_mesh_node_drain_ready(Handle,
            (uint)domains, batch.Handle, out var hasResidue, (int)flags);
        if (rc == (int)RecvResult.NoData)
            return false;
        if (rc != 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        return hasResidue != 0;
    }

    public ISpot CreateSpot()
    {
        EnsureNotDisposed();
        var handle = NativeMethods.zlink_spot_new(Handle);
        if (handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        return Register(new Spot(this, handle));
    }

    public ISpot EntrySpot()
    {
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_mesh_node_entry_spot(Handle, out var handle));
        return Register(new Spot(this, handle));
    }

    public ISpot? SpotLookup(RoutingId spotRid)
    {
        EnsureNotDisposed();
        var nativeRid = spotRid.ToNative();
        var rc = NativeMethods.zlink_mesh_node_spot_lookup(Handle, ref nativeRid,
            out var handle);
        if (rc == (int)ConfigResult.NotFound)
            return null;
        ZlinkException.ThrowConfigIfError(rc);
        return handle == IntPtr.Zero ? null : Register(new Spot(this, handle));
    }

    public ISpot GetOrCreateSpot(RoutingId spotRid, out bool created)
    {
        EnsureNotDisposed();
        var nativeRid = spotRid.ToNative();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_mesh_node_spot_get_or_new(Handle, ref nativeRid,
                out var handle, out var createdValue));
        if (handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        created = createdValue != 0;
        return Register(new Spot(this, handle));
    }

    public IStreamSessionService CreateStreamSessionService(IStreamSocket stream)
    {
        EnsureNotDisposed();
        if (stream is not StreamSocket concrete)
            throw new ArgumentException(
                "stream must be a concrete zlink stream socket instance",
                nameof(stream));
        return new StreamSessionService(this, concrete);
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (Handle == IntPtr.Zero)
            return;
        Destroy(true);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~MeshNode()
    {
        Destroy(false);
    }

    internal void EnsureNotDisposed()
    {
        if (Handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(MeshNode));
    }

    internal void UnregisterSpot(Spot spot)
    {
        lock (_spotsGate)
        {
            _spots.Remove(spot);
        }
    }

    private Spot Register(Spot spot)
    {
        lock (_spotsGate)
        {
            _spots.Add(spot);
        }

        return spot;
    }

    private uint OnNativeReady(IntPtr meshNode, uint readyDomains,
        IntPtr userdata)
    {
        var handler = _readyHandler;
        if (handler == null)
            return 0;
        try
        {
            return (uint)handler((MeshReadyDomains)readyDomains);
        }
        catch (Exception error)
        {
            CallbackExceptionHub.Report(error);
            return 0;
        }
    }

    private void Destroy(bool throwOnError)
    {
        if (Handle == IntPtr.Zero)
            return;

        Spot[] spots;
        lock (_spotsGate)
        {
            spots = new Spot[_spots.Count];
            _spots.CopyTo(spots);
        }

        foreach (var spot in spots)
            try
            {
                spot.Dispose();
            }
            catch
            {
                // Best-effort child cleanup.
            }

        var handle = Handle;
        var rc = NativeMethods.zlink_mesh_node_destroy(ref handle);
        if (rc == 0)
        {
            Handle = IntPtr.Zero;
            _readyHandler = null;
            _readyHandlerNative = null;
        }
        else if (throwOnError)
        {
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
        }
    }

    private static IntPtr CreateHandle(Context context, MeshNodeOptions? options)
    {
        if (options == null
            || (options.MeshName == null && options.TrustProfile == null))
            return NativeMethods.zlink_mesh_node_new(context.Handle,
                IntPtr.Zero);

        var meshBytes = options.MeshName != null
            ? Encoding.UTF8.GetBytes(options.MeshName)
            : null;
        var trustBytes = options.TrustProfile != null
            ? Encoding.UTF8.GetBytes(options.TrustProfile)
            : null;
        var meshHandle = meshBytes != null
            ? GCHandle.Alloc(meshBytes, GCHandleType.Pinned)
            : default;
        var trustHandle = trustBytes != null
            ? GCHandle.Alloc(trustBytes, GCHandleType.Pinned)
            : default;
        try
        {
            var native = new ZlinkMeshNodeOptions
            {
                StructSize = (uint)Marshal.SizeOf<ZlinkMeshNodeOptions>(),
                Version = 1,
                MeshName = meshBytes != null
                    ? meshHandle.AddrOfPinnedObject()
                    : IntPtr.Zero,
                MeshNameSize = (nuint)(meshBytes?.Length ?? 0),
                TrustProfile = trustBytes != null
                    ? trustHandle.AddrOfPinnedObject()
                    : IntPtr.Zero,
                TrustProfileSize = (nuint)(trustBytes?.Length ?? 0)
            };
            return NativeMethods.zlink_mesh_node_new(context.Handle, ref native);
        }
        finally
        {
            if (meshHandle.IsAllocated)
                meshHandle.Free();
            if (trustHandle.IsAllocated)
                trustHandle.Free();
        }
    }

    private static unsafe MeshNodeStatus ConvertStatus(
        ref ZlinkMeshNodeStatus native)
    {
        string meshName;
        string localEndpoint;
        fixed (byte* mn = native.MeshName)
        {
            meshName = NativeHelpers.ReadFixedString(mn, 256);
        }

        fixed (byte* le = native.LocalEndpoint)
        {
            localEndpoint = NativeHelpers.ReadFixedString(le, 512);
        }

        return new MeshNodeStatus(
            (MeshNodeState)native.State,
            RoutingId.FromNative(ref native.RoutingId) ?? default,
            meshName,
            localEndpoint,
            native.LifecycleGeneration,
            native.DescriptorRevision,
            native.ChannelCount,
            native.ConfiguredPeerCount,
            native.AdmittedPeerCount,
            native.DrainingPeerCount,
            native.PendingApplicationMessages,
            native.PendingInfrastructureMessages,
            native.PendingBytes,
            native.MulticastSubmitted,
            native.MulticastDroppedTargets,
            native.LastError,
            native.LastChangedMs);
    }

    private static unsafe MeshNodePeer ConvertPeer(ref ZlinkMeshPeerEntry native)
    {
        string endpoint;
        fixed (byte* ep = native.Endpoint)
        {
            endpoint = NativeHelpers.ReadFixedString(ep, 512);
        }

        return new MeshNodePeer(
            native.ConnectionIntentId,
            (MeshPeerSource)native.Source,
            (MeshPeerState)native.State,
            RoutingId.FromNative(ref native.RoutingId) ?? default,
            native.LifecycleGeneration,
            native.DescriptorRevision,
            endpoint,
            native.ChannelCount,
            native.LastError,
            native.LastChangedMs);
    }
}
