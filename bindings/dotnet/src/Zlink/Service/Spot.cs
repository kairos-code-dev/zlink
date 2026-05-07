// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink;
using Systems.Zlink.Native;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

public enum SpotNodeMode
{
    PubSub = 1,
    Routed = 2,
    All = 3
}

public enum SpotNodeSocketOwner
{
    Any = 0,
    Node = 1,
    Spot = 2
}

public enum SpotNodeSocketType
{
    Any = 0,
    Pair = 0x1001,
    Pub = 0x1002,
    Sub = 0x1003,
    Dealer = 0x1004,
    Router = 0x1005,
    XPub = 0x1006,
    XSub = 0x1007,
    Stream = 0x1008
}

public sealed class SpotNodeOptions
{
    private SpotNode? _owner;
    private AutoHwmProfile _routerHwmProfile = AutoHwmProfile.Balanced;
    private int _routerHighWaterMark;
    private AutoHwmProfile _pubSubHwmProfile = AutoHwmProfile.Balanced;
    private int _pubSubHighWaterMark;

    public SpotNodeMode Mode { get; init; } = SpotNodeMode.All;

    public AutoHwmProfile RouterHwmProfile
    {
        get => _routerHwmProfile;
        set
        {
            _routerHwmProfile = value;
            _owner?.SetRouterHighWaterMarkProfile(value);
        }
    }

    public int RouterHighWaterMark
    {
        get => _routerHighWaterMark;
        set
        {
            _routerHighWaterMark = value;
            _owner?.SetRouterHighWaterMark(value);
        }
    }

    public AutoHwmProfile PubSubHwmProfile
    {
        get => _pubSubHwmProfile;
        set
        {
            _pubSubHwmProfile = value;
            _owner?.SetPubSubHighWaterMarkProfile(value);
        }
    }

    public int PubSubHighWaterMark
    {
        get => _pubSubHighWaterMark;
        set
        {
            _pubSubHighWaterMark = value;
            _owner?.SetPubSubHighWaterMark(value);
        }
    }

    internal void AttachOwner(SpotNode owner)
    {
        _owner = owner;
    }
}

public sealed record SpotNodeSocketSnapshotFilter(
    SpotNodeSocketOwner? Owner = null,
    SpotNodeSocketType? SocketType = null,
    string? SocketName = null);

public sealed record SpotNodeSocketSnapshotEntry(
    SpotNodeSocketOwner Owner,
    ulong OwnerId,
    string OwnerName,
    string SocketName,
    SpotNodeSocketType SocketType,
    bool AutoHwmVisible,
    MonitorSnapshot Snapshot)
{
    internal static unsafe SpotNodeSocketSnapshotEntry FromNative(
        ref ZlinkSpotNodeSocketSnapshotEntry native)
    {
        fixed (byte* ownerName = native.OwnerName)
        fixed (byte* socketName = native.SocketName)
        {
            ZlinkMonitorSnapshot snapshot = native.Snapshot;
            return new SpotNodeSocketSnapshotEntry(
                native.Owner,
                native.OwnerId,
                NativeHelpers.ReadFixedString(ownerName, 64),
                NativeHelpers.ReadFixedString(socketName, 64),
                native.SocketType,
                native.AutoHwmVisible != 0,
                MonitorSnapshot.FromNative(ref snapshot));
        }
    }
}

public sealed class SpotOptions
{
    private readonly Spot _spot;

    internal SpotOptions(Spot spot)
    {
        _spot = spot;
    }

    public TimeSpan? RequestTimeout
    {
        get => CommonSocketOptions.DecodeDuration(
            _spot.GetOption(SpotOption.RequestTimeout));
        set => _spot.SetOption(SpotOption.RequestTimeout,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }
}

public sealed class SpotNode : IDisposable, IAsyncDisposable
{
    private IntPtr _handle;
    private readonly Dictionary<string, DealerSocket> _channelDealers =
        new(StringComparer.Ordinal);
    private readonly HashSet<Spot> _spots = new();
    private readonly object _spotsGate = new();
    private ActorAdmissionHandler? _actorAdmissionHandler;
    private NativeMethods.ZlinkActorAdmissionHandlerDelegate? _actorAdmissionNative;
    private Action? _sendReadyHandler;
    private SynchronizationContext? _sendReadyHandlerContext;
    private NativeMethods.ZlinkSendReadyHandlerDelegate? _sendReadyHandlerNative;
    public SpotNodeOptions Options { get; }
    public SpotNodePublisherOptions PublisherOptions { get; }
    public SpotNodeSubscriberOptions SubscriberOptions { get; }

    public SpotNode(Context context)
        : this(context, null)
    {
    }

    public SpotNode(Context context, SpotNodeOptions? options)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        Options = options ?? new SpotNodeOptions();
        if (options == null)
        {
            _handle = NativeMethods.zlink_spot_node_new(context.Handle,
                IntPtr.Zero);
        }
        else
        {
            ZlinkSpotNodeOptions nativeOptions = new()
            {
                Mode = Options.Mode
            };
            _handle = NativeMethods.zlink_spot_node_new(context.Handle,
                ref nativeOptions);
        }
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        PublisherOptions = new SpotNodePublisherOptions(this);
        SubscriberOptions = new SpotNodeSubscriberOptions(this);
        Options.AttachOwner(this);
        if (options != null)
            ApplyOptions(Options);
    }

    internal IntPtr Handle => _handle;

    public void SetRoutingId(RoutingId routingId)
    {
        EnsureNotDisposed();
        byte[] routingIdBytes = routingId.ToByteArray();
        unsafe
        {
            fixed (byte* routingIdPtr = routingIdBytes)
            {
                int rc = NativeMethods.zlink_set_routing_id(_handle,
                    (IntPtr)routingIdPtr, (nuint)routingIdBytes.Length);
                ZlinkException.ThrowConfigIfError(rc);
            }
        }
    }

    public RoutingId RoutingId
    {
        get
        {
            EnsureNotDisposed();
            int rc = NativeMethods.zlink_get_routing_id(_handle,
                out ZlinkRoutingId routingId);
            ZlinkException.ThrowConfigIfError(rc);
            return RoutingId.FromBytes(
                NativeHelpers.ReadRoutingId(ref routingId));
        }
    }

    public void Bind(string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_bind(_handle, endpoint);
        ZlinkException.ThrowBindIfError(rc);
    }

    /// <summary>
    /// Returns the resolved endpoint after bind (supports ephemeral port 0).
    /// </summary>
    public string LastEndpoint
    {
        get
        {
            return StatusSnapshot().LocalEndpoint;
        }
    }

    public void ConnectPeer(string peerEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(peerEndpoint, nameof(peerEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_connect_peer(_handle,
            peerEndpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void DisconnectPeer(string peerEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(peerEndpoint, nameof(peerEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_disconnect_peer(_handle,
            peerEndpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void DisconnectPeerRid(RoutingId targetNodeRid)
    {
        EnsureNotDisposed();
        byte[] ridBytes = RoutingIdCodec.FromRoutingId(targetNodeRid);
        ZlinkRoutingId nativeRid = NativeHelpers.WriteRoutingId(ridBytes);
        int rc = NativeMethods.zlink_spot_node_disconnect_peer_rid(_handle,
            ref nativeRid);
        ZlinkException.ThrowConnectIfError(rc);
    }

    /// <summary>
    /// Attaches this SPOT node to a discovery-owned service lifecycle.
    /// </summary>
    /// <remarks>
    /// Once attached, the discovery instance becomes the lifecycle owner for
    /// the node and is responsible for coordinated shutdown.
    /// </remarks>
    public void AttachDiscovery(Discovery discovery)
    {
        EnsureNotDisposed();
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));
        int rc = NativeMethods.zlink_spot_node_attach_discovery(_handle,
            discovery.Handle);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void AttachChannelDealer(Discovery discovery, DealerSocket dealer)
    {
        EnsureNotDisposed();
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));
        if (dealer == null)
            throw new ArgumentNullException(nameof(dealer));
        int rc = NativeMethods.zlink_spot_node_attach_channel_dealer(_handle,
            discovery.Handle, dealer.Handle);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void AttachChannelDealerManual(string channelName,
        DealerSocket dealer)
    {
        EnsureNotDisposed();
        if (dealer == null)
            throw new ArgumentNullException(nameof(dealer));
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        int rc = NativeMethods.zlink_spot_node_attach_channel_dealer_manual(
            _handle, channelName, dealer.Handle);
        ZlinkException.ThrowConfigIfError(rc);
        lock (_channelDealers)
            _channelDealers[channelName] = dealer;
    }

    public void AttachPubIngress(PubSocket pub)
    {
        EnsureNotDisposed();
        if (pub == null)
            throw new ArgumentNullException(nameof(pub));
        int rc = NativeMethods.zlink_spot_node_attach_pub_ingress(_handle,
            pub.Handle);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public Spot CreateSpot()
    {
        EnsureNotDisposed();
        Spot spot = new(this);
        RegisterSpot(spot);
        return spot;
    }

    public Spot EntrySpot()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_entry_spot(_handle,
            out IntPtr spotHandle);
        ZlinkException.ThrowConfigIfError(rc);
        Spot spot = new(this, spotHandle, ownsHandle: true);
        RegisterSpot(spot);
        return spot;
    }

    public Spot LookupSpot(RoutingId spotRid)
    {
        EnsureNotDisposed();
        ZlinkRoutingId nativeRid = NativeHelpers.WriteRoutingId(
            spotRid.ToByteArray());
        int rc = NativeMethods.zlink_spot_node_spot_lookup(_handle,
            ref nativeRid, out IntPtr spotHandle);
        ZlinkException.ThrowConfigIfError(rc);
        Spot spot = new(this, spotHandle, ownsHandle: true);
        RegisterSpot(spot);
        return spot;
    }

    public Actor Actor(string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_actor_new(_handle,
            actorId, out ZlinkActorRef actor);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        return new Actor(this, ActorInterop.FromNative(ref actor));
    }

    public ActorRef ActorLookup(string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_actor_lookup(_handle, actorId,
            out ZlinkActorRef actor);
        ZlinkException.ThrowConfigIfError(rc);
        return ActorInterop.FromNative(ref actor);
    }

    public ActorRef RemoteActorRef(RoutingId targetNodeRid, string actorId)
        => ActorRef.Remote(targetNodeRid, actorId);

    public ActorCreateResult CreateRemoteActor(RoutingId targetNodeRid,
        string actorId, Message message, TimeSpan timeout = default)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        ZlinkRoutingId nodeRid = NativeHelpers.WriteRoutingId(
            targetNodeRid.ToByteArray());
        ZlinkMsg nativeMessage = default;
        bool submitted = false;
        try
        {
            message.Copy().MoveTo(ref nativeMessage);
            int rc = NativeMethods.zlink_spot_node_create_remote_actor(_handle,
                ref nodeRid, actorId, ref nativeMessage,
                out ZlinkActorCreateResult result,
                ActorInterop.NormalizeTimeout(timeout));
            submitted = true;
            if (rc != 0)
                throw ZlinkException.CreateRequestException(
                    NativeMethods.zlink_errno());
            return new ActorCreateResult(result.Status,
                ActorInterop.FromNative(ref result.Actor));
        }
        finally
        {
            if (!submitted)
                NativeMethods.zlink_msg_close(ref nativeMessage);
        }
    }

    internal void DestroyActor(ActorRef actor, TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        ZlinkActorRef nativeActor = ActorInterop.ToNative(actor);
        int rc = NativeMethods.zlink_spot_node_actor_destroy(_handle,
            ref nativeActor, ActorInterop.NormalizeTimeout(timeout));
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }

    public void DestroyRemoteActor(ActorRef actor, TimeSpan timeout = default)
    {
        DestroyActor(actor, timeout);
    }

    public void OnSendReady(Action handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        SynchronizationContext? context = SynchronizationContext.Current;
        var native = new NativeMethods.ZlinkSendReadyHandlerDelegate(
            OnNativeSendReady);
        int rc = NativeMethods.zlink_send_ready_handler(_handle, native,
            IntPtr.Zero);
        ZlinkException.ThrowHandlerIfError(rc);
        _sendReadyHandler = handler;
        _sendReadyHandlerContext = context;
        _sendReadyHandlerNative = native;
    }

    public void OnActorAdmission(ActorAdmissionHandler handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();
        NativeMethods.ZlinkActorAdmissionHandlerDelegate native =
            OnNativeActorAdmission;
        int rc = NativeMethods.zlink_spot_node_actor_admission_handler(_handle,
            native, IntPtr.Zero);
        ZlinkException.ThrowHandlerIfError(rc);
        _actorAdmissionHandler = handler;
        _actorAdmissionNative = native;
    }

    public Task<IReadOnlyList<Message>> JoinActorAsync(ActorRef actor,
        RoutingId destSpotRid, Message message, TimeSpan timeout = default,
        SendFlags flags = SendFlags.None, CancellationToken ct = default)
        => JoinActorAsync(actor, RoutingId, destSpotRid, message, timeout,
            flags, ct);

    public Task<IReadOnlyList<Message>> JoinActorAsync(ActorRef actor,
        RoutingId destNodeRid, RoutingId destSpotRid, Message message,
        TimeSpan timeout = default, SendFlags flags = SendFlags.None,
        CancellationToken ct = default)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        ZlinkActorRef nativeActor = ActorInterop.ToNative(actor);
        ZlinkRoutingId nativeNodeRid = NativeHelpers.WriteRoutingId(
            destNodeRid.ToByteArray());
        ZlinkRoutingId nativeSpotRid = NativeHelpers.WriteRoutingId(
            destSpotRid.ToByteArray());
        ZlinkMsg nativeMessage = default;
        message.Copy().MoveTo(ref nativeMessage);
        uint timeoutMs = ActorInterop.NormalizeTimeout(timeout);
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        try
        {
            RequestCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            state.SetCancellationRegistration(ct.CanBeCanceled
                ? ct.Register(static userdata =>
                {
                    RequestCallState.CancelFromUserData(userdata);
                }, handle)
                : default);
            state.SetTimeoutTimer(ActorInterop.CreateTimeoutTimer(handle,
                timeoutMs));

            int rc = NativeMethods.zlink_spot_node_actor_join_spot(_handle,
                ref nativeActor, ref nativeNodeRid, ref nativeSpotRid,
                ref nativeMessage, ActorInterop.ReplyHandlerPtr,
                GCHandle.ToIntPtr(handle), (int)flags, timeoutMs);
            nativeMessage = default;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            return ActorInterop.TakePartsAsync(
                RequestProgressPump.AttachSpot(_handle, completion.Task));
        }
        catch
        {
            NativeMethods.zlink_msg_close(ref nativeMessage);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    public bool JoinActor(ActorRef actor, RoutingId destSpotRid, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null, SendFlags flags = SendFlags.None)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            ActorInterop.AttachPartsCallback(
                () => JoinActorAsync(actor, destSpotRid, message,
                    timeout ?? TimeSpan.Zero, flags),
                callback);
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    public bool JoinActor(ActorRef actor, RoutingId destNodeRid,
        RoutingId destSpotRid, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null, SendFlags flags = SendFlags.None)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            ActorInterop.AttachPartsCallback(
                () => JoinActorAsync(actor, destNodeRid, destSpotRid, message,
                    timeout ?? TimeSpan.Zero, flags),
                callback);
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    public void LeaveActor(ActorRef actor, RoutingId destSpotRid,
        TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        ZlinkActorRef nativeActor = ActorInterop.ToNative(actor);
        ZlinkRoutingId nativeSpotRid = NativeHelpers.WriteRoutingId(
            destSpotRid.ToByteArray());
        int rc = NativeMethods.zlink_spot_node_actor_leave_spot(_handle,
            ref nativeActor, ref nativeSpotRid,
            ActorInterop.NormalizeTimeout(timeout));
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }

    internal void SetOption(SpotNodeSocketRole role, SocketOptionKey<int> option,
        int value)
    {
        EnsureNotDisposed();
        unsafe
        {
            int local = value;
            int code = (int)option.Option;
            int rc = role switch
            {
                SpotNodeSocketRole.Node => NativeMethods.zlink_set_option(
                    _handle, code, new IntPtr(&local),
                    (nuint)sizeof(int)),
                SpotNodeSocketRole.Pub => (code & 0xFF00) == 0x3300
                    ? NativeMethods.zlink_set_pub_option(_handle, code,
                        new IntPtr(&local), (nuint)sizeof(int))
                    : NativeMethods.zlink_set_option(_handle, code,
                        new IntPtr(&local), (nuint)sizeof(int)),
                SpotNodeSocketRole.Sub => (code & 0xFF00) == 0x3400
                    ? NativeMethods.zlink_set_sub_option(_handle, code,
                        new IntPtr(&local), (nuint)sizeof(int))
                    : NativeMethods.zlink_set_option(_handle, code,
                        new IntPtr(&local), (nuint)sizeof(int)),
                _ => throw new ArgumentOutOfRangeException(nameof(role))
            };
            ZlinkException.ThrowConfigIfError(rc);
        }
    }

    internal void SetAdmissionOption(SpotNodeOption option, int value)
    {
        EnsureNotDisposed();
        unsafe
        {
            int local = value;
            int rc = NativeMethods.zlink_set_spot_node_option(_handle, option,
                new IntPtr(&local), (nuint)sizeof(int));
            ZlinkException.ThrowConfigIfError(rc);
        }
    }

    internal void SetRouterHighWaterMark(int value)
    {
        SetAdmissionOption(SpotNodeOption.RouterHwm, value);
    }

    internal void SetPubSubHighWaterMark(int value)
    {
        SetAdmissionOption(SpotNodeOption.PubSubHwm, value);
    }

    internal void SetRouterHighWaterMarkProfile(AutoHwmProfile profile)
    {
        SetAdmissionOption(SpotNodeOption.RouterHwmProfile, (int)profile);
    }

    internal void SetPubSubHighWaterMarkProfile(AutoHwmProfile profile)
    {
        SetAdmissionOption(SpotNodeOption.PubSubHwmProfile, (int)profile);
    }

    private void ApplyOptions(SpotNodeOptions options)
    {
        SetRouterHighWaterMarkProfile(options.RouterHwmProfile);
        SetPubSubHighWaterMarkProfile(options.PubSubHwmProfile);
        if (options.RouterHighWaterMark > 0)
            SetRouterHighWaterMark(options.RouterHighWaterMark);
        if (options.PubSubHighWaterMark > 0)
            SetPubSubHighWaterMark(options.PubSubHighWaterMark);
    }

    public void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false)
    {
        BoundaryValidation.ValidateFixedUtf8(certPath, nameof(certPath));
        BoundaryValidation.ValidateFixedUtf8(keyPath, nameof(keyPath));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_tls_server(_handle, certPath, keyPath,
            requireClientCert ? 1 : 0);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false)
    {
        BoundaryValidation.ValidateFixedUtf8(caCertPath, nameof(caCertPath));
        BoundaryValidation.ValidateFixedUtf8(hostname, nameof(hostname));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_tls_client(_handle, caCertPath,
            hostname, trustSystem ? 1 : 0);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public SpotNodeStatus StatusSnapshot()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_status_snapshot(_handle,
            out var native);
        ZlinkException.ThrowConfigIfError(rc);
        return SpotNodeStatus.FromNative(ref native);
    }

    public SpotNodePeerEntry[] PeersSnapshot()
    {
        EnsureNotDisposed();
        return ReadPeerEntries(IntPtr.Zero);
    }

    public SpotNodePeerEntry[] PeersQuery(SpotNodePeerFilter filter)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkSpotNodePeerFilter nativeFilter = default;
            nativeFilter.Source = (int)filter.Source.GetValueOrDefault();
            nativeFilter.State = (int)filter.State.GetValueOrDefault();
            if (!string.IsNullOrEmpty(filter.PeerEndpoint))
            {
                BoundaryValidation.ValidateFixedUtf8(filter.PeerEndpoint,
                    nameof(filter.PeerEndpoint));
                WriteFixedString(filter.PeerEndpoint, nativeFilter.PeerEndpoint,
                    256);
            }

            return ReadPeerEntries((IntPtr)(&nativeFilter));
        }
    }

    public SpotNodeSubjectEntry[] SubjectsSnapshot(
        SpotNodeSubjectFilter? filter = null)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkSpotNodeSubjectFilter nativeFilter = default;
            IntPtr filterPtr = IntPtr.Zero;
            if (filter != null)
            {
                SpotNodeSubjectFilter value = filter;
                if (value.Role.HasValue || !string.IsNullOrEmpty(value.Subject)
                    || value.SubjectKind.HasValue)
                {
                    nativeFilter.Role = (int)value.Role.GetValueOrDefault();
                    nativeFilter.SubjectKind =
                        (uint)value.SubjectKind.GetValueOrDefault();
                    if (!string.IsNullOrEmpty(value.Subject))
                    {
                        BoundaryValidation.ValidateFixedUtf8(value.Subject,
                            nameof(SpotNodeSubjectFilter.Subject));
                        WriteFixedString(value.Subject, nativeFilter.Subject,
                            256);
                    }
                    filterPtr = (IntPtr)(&nativeFilter);
                }
            }

            return ReadSubjectEntries(filterPtr);
        }
    }

    public SpotNodeSocketSnapshotEntry[] InternalSocketsSnapshot(
        SpotNodeSocketSnapshotFilter? filter = null)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkSpotNodeSocketSnapshotFilter nativeFilter = default;
            IntPtr filterPtr = IntPtr.Zero;
            if (filter != null)
            {
                SpotNodeSocketSnapshotFilter value = filter;
                nativeFilter.Owner =
                    value.Owner.GetValueOrDefault(SpotNodeSocketOwner.Any);
                nativeFilter.SocketType =
                    value.SocketType.GetValueOrDefault(SpotNodeSocketType.Any);
                if (!string.IsNullOrEmpty(value.SocketName))
                {
                    BoundaryValidation.ValidateFixedUtf8(value.SocketName,
                        nameof(SpotNodeSocketSnapshotFilter.SocketName));
                    WriteFixedString(value.SocketName,
                        nativeFilter.SocketName, 64);
                }
                filterPtr = (IntPtr)(&nativeFilter);
            }

            return ReadInternalSocketEntries(filterPtr);
        }
    }

    public SpotNodeSpotEntry[] SpotsSnapshot()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_node_spots_snapshot(_handle,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeSpotEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodeSpotEntry>();
        IntPtr entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_spots_snapshot(_handle,
                entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);
            SpotNodeSpotEntry[] result = new SpotNodeSpotEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                ZlinkSpotNodeSpotEntry native =
                    Marshal.PtrToStructure<ZlinkSpotNodeSpotEntry>(
                        IntPtr.Add(entries, i * entrySize));
                result[i] = new SpotNodeSpotEntry(
                    RoutingId.FromNative(ref native.SpotRid),
                    native.DispatchHandlerAttached != 0,
                    native.JoinedActorCount,
                    native.PendingActorJoinCount,
                    native.RouteSynced != 0,
                    native.LastChangedMs);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    public SpotNodeActorEntry[] ActorsSnapshot()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_node_actors_snapshot(_handle,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeActorEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodeActorEntry>();
        IntPtr entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_actors_snapshot(_handle,
                entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);
            SpotNodeActorEntry[] result = new SpotNodeActorEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                ZlinkSpotNodeActorEntry native =
                    Marshal.PtrToStructure<ZlinkSpotNodeActorEntry>(
                        IntPtr.Add(entries, i * entrySize));
                result[i] = new SpotNodeActorEntry(
                    ActorInterop.FromNative(ref native.Actor),
                    native.Joined != 0,
                    RoutingId.FromNative(ref native.JoinedSpotRid),
                    native.RouteSynced != 0,
                    native.PendingMessageCount,
                    native.LastChangedMs);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        Destroy(throwOnError: true);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~SpotNode()
    {
        Destroy(throwOnError: false);
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(SpotNode));
    }

    internal bool TryGetChannelDealerHandle(string channelName, out IntPtr handle)
    {
        lock (_channelDealers)
        {
            if (_channelDealers.TryGetValue(channelName, out DealerSocket? dealer))
            {
                handle = dealer.Handle;
                return handle != IntPtr.Zero;
            }
        }

        handle = IntPtr.Zero;
        return false;
    }

    internal void RegisterSpot(Spot spot)
    {
        lock (_spotsGate)
            _spots.Add(spot);
    }

    internal void UnregisterSpot(Spot spot)
    {
        lock (_spotsGate)
            _spots.Remove(spot);
    }

    private void Destroy(bool throwOnError)
    {
        if (_handle == IntPtr.Zero)
            return;

        Spot[] spots;
        lock (_spotsGate)
            spots = new List<Spot>(_spots).ToArray();

        Exception? firstError = null;
        foreach (Spot spot in spots)
        {
            try
            {
                spot.Dispose();
            }
            catch (Exception ex) when (firstError == null)
            {
                firstError = ex;
            }
            catch
            {
            }
        }

        lock (_channelDealers)
            _channelDealers.Clear();
        _actorAdmissionHandler = null;
        _actorAdmissionNative = null;
        _sendReadyHandler = null;
        _sendReadyHandlerContext = null;
        _sendReadyHandlerNative = null;

        IntPtr originalHandle = _handle;
        IntPtr handle = _handle;
        int rc = NativeMethods.zlink_spot_node_destroy(ref handle);
        if (rc == 0)
        {
            _handle = IntPtr.Zero;
        }
        else
        {
            _handle = originalHandle;
            if (throwOnError && firstError == null)
                firstError = ZlinkException.CreateCloseException(
                    NativeMethods.zlink_errno());
        }

        if (throwOnError && firstError != null)
            throw firstError;
    }

    private void OnNativeSendReady(IntPtr subject, IntPtr userData)
    {
        Action? handler = _sendReadyHandler;
        SynchronizationContext? context = _sendReadyHandlerContext;
        if (handler == null)
            return;
        CallbackDelivery.Post(context, () =>
        {
            try
            {
                handler();
            }
            catch (Exception ex)
            {
                Runtime.ReportUnhandledCallbackException(ex);
            }
        });
    }

    private SpotNodePeerEntry[] ReadPeerEntries(IntPtr filterPtr)
    {
        nuint count = 0;
        int rc = filterPtr == IntPtr.Zero
            ? NativeMethods.zlink_spot_node_peers_snapshot(_handle, IntPtr.Zero,
                ref count)
            : NativeMethods.zlink_spot_node_peers_query(_handle, filterPtr,
                IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodePeerEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodePeerEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = filterPtr == IntPtr.Zero
                ? NativeMethods.zlink_spot_node_peers_snapshot(_handle, entries,
                    ref actual)
                : NativeMethods.zlink_spot_node_peers_query(_handle, filterPtr,
                    entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            SpotNodePeerEntry[] result = new SpotNodePeerEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkSpotNodePeerEntry native =
                    Marshal.PtrToStructure<ZlinkSpotNodePeerEntry>(current);
                result[i] = SpotNodePeerEntry.FromNative(ref native);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    private SpotNodeSubjectEntry[] ReadSubjectEntries(IntPtr filterPtr)
    {
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_node_subjects_snapshot(_handle,
            filterPtr, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeSubjectEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodeSubjectEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_subjects_snapshot(_handle,
                filterPtr, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            SpotNodeSubjectEntry[] result = new SpotNodeSubjectEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkSpotNodeSubjectEntry native =
                    Marshal.PtrToStructure<ZlinkSpotNodeSubjectEntry>(current);
                result[i] = SpotNodeSubjectEntry.FromNative(ref native);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    private SpotNodeSocketSnapshotEntry[] ReadInternalSocketEntries(
        IntPtr filterPtr)
    {
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_node_internal_sockets_snapshot(
            _handle, filterPtr, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeSocketSnapshotEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodeSocketSnapshotEntry>();
        IntPtr entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_internal_sockets_snapshot(
                _handle, filterPtr, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            SpotNodeSocketSnapshotEntry[] result =
                new SpotNodeSocketSnapshotEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkSpotNodeSocketSnapshotEntry native =
                    Marshal.PtrToStructure<ZlinkSpotNodeSocketSnapshotEntry>(
                        current);
                result[i] = SpotNodeSocketSnapshotEntry.FromNative(ref native);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    private ActorAdmissionResult OnNativeActorAdmission(IntPtr node,
        string actorId, IntPtr message, IntPtr userData)
    {
        ActorAdmissionHandler? handler = _actorAdmissionHandler;
        if (handler == null)
            return ActorAdmissionResult.Reject;
        Message managedMessage = ActorInterop.CopyMessageFromPointer(message);
        try
        {
            return handler(actorId, managedMessage);
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
            return ActorAdmissionResult.Reject;
        }
        finally
        {
            managedMessage.Dispose();
        }
    }

    private static unsafe void WriteFixedString(string value, byte* destination,
        int capacity)
    {
        byte[] encoded = Encoding.UTF8.GetBytes(value);
        if (encoded.Length >= capacity)
        {
            throw new ArgumentOutOfRangeException(nameof(value),
                "UTF-8 value exceeds native fixed buffer capacity.");
        }

        for (int i = 0; i < capacity; i++)
            destination[i] = 0;
        for (int i = 0; i < encoded.Length; i++)
            destination[i] = encoded[i];
    }

}

public sealed class Spot : IDisposable, IAsyncDisposable
{
    private static readonly NativeMethods.ZlinkFreeFnDelegate BorrowedBufferFree =
        OnBorrowedBufferFree;
    private static readonly IntPtr BorrowedBufferFreePtr =
        Marshal.GetFunctionPointerForDelegate(BorrowedBufferFree);
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate RoutedReplyHandler =
        OnRoutedReply;
    private static readonly IntPtr RoutedReplyHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(RoutedReplyHandler);
    private const int StackPublishPartLimit = 8;
    private const int TopicBufferSize = 256;
    private const int DontWaitFlag = 1;
    private const int ErrnoEAgain = 11;
    private const int ErrnoEWouldBlockWin = 10035;
    private const int ErrnoENotConn = 107;
    private const int ErrnoENotConnWin = 10057;
    private const int ErrnoEHostUnreach = 113;
    private const int ErrnoEHostUnreachWin = 10065;
    private const int ErrnoETimedOut = 110;
    private const int ErrnoETimedOutWin = 10060;
    private IntPtr _handle;
    private readonly SpotNode _node;
    private readonly bool _ownsHandle;
    private Action? _sendReadyHandler;
    private Action<Received>? _routedReceiveHandler;
    private Action<Spot, SpotDispatchInfo>? _dispatchEventHandler;
    private SynchronizationContext? _sendReadyHandlerContext;
    private SynchronizationContext? _routedReceiveHandlerContext;
    private NativeMethods.ZlinkSendReadyHandlerDelegate? _sendReadyHandlerNative;
    private NativeMethods.ZlinkSpotRequestHandlerDelegate? _routedReceiveHandlerNative;
    private NativeMethods.ZlinkSpotDispatchEventHandlerDelegate? _dispatchEventHandlerNative;
    internal IntPtr Handle => _handle;
    public SpotOptions Options { get; }

    internal Spot(SpotNode node)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        if (node.Handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(node));
        _node = node;
        _handle = NativeMethods.zlink_spot_new(node.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        _ownsHandle = true;
        Options = new SpotOptions(this);
    }

    internal Spot(SpotNode node, IntPtr handle, bool ownsHandle)
    {
        if (node == null)
            throw new ArgumentNullException(nameof(node));
        if (handle == IntPtr.Zero)
            throw new ArgumentException("Spot handle must not be zero.",
                nameof(handle));
        _node = node;
        _handle = handle;
        _ownsHandle = ownsHandle;
        Options = new SpotOptions(this);
    }

    public void SetRoutingId(RoutingId routingId)
    {
        EnsureNotDisposed();
        byte[] routingIdBytes = routingId.ToByteArray();
        unsafe
        {
            fixed (byte* routingIdPtr = routingIdBytes)
            {
                int rc = NativeMethods.zlink_set_routing_id(_handle,
                    (IntPtr)routingIdPtr, (nuint)routingIdBytes.Length);
                ZlinkException.ThrowConfigIfError(rc);
            }
        }
    }

    public RoutingId RoutingId
    {
        get
        {
            EnsureNotDisposed();
            int rc = NativeMethods.zlink_get_routing_id(_handle,
                out ZlinkRoutingId routingId);
            ZlinkException.ThrowConfigIfError(rc);
            return RoutingId.FromBytes(
                NativeHelpers.ReadRoutingId(ref routingId));
        }
    }

    internal unsafe void SetOption(SpotOption option, int value)
    {
        EnsureNotDisposed();
        int local = value;
        int rc = NativeMethods.zlink_set_spot_option(_handle, option,
            (IntPtr)(&local), (nuint)sizeof(int));
        ZlinkException.ThrowConfigIfError(rc);
    }

    internal unsafe int GetOption(SpotOption option)
    {
        EnsureNotDisposed();
        int value = 0;
        nuint size = (nuint)sizeof(int);
        int rc = NativeMethods.zlink_get_spot_option(_handle, option,
            (IntPtr)(&value), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return value;
    }

    public bool Publish(string serviceName, string topic, Message message,
        SendFlags flags = SendFlags.None)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(PublishNoWaitResult(serviceName,
                topic, message));
        }

        PublishSingleCore(serviceName, topic, message, (int)flags);
        return true;
    }

    internal SendResult PublishNoWaitResult(string serviceName, string topic,
        Message message)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        return PublishNoWaitSingleCore(serviceName, topic, message);
    }

    public bool Publish(string serviceName, string topic,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(PublishNoWaitResult(serviceName,
                topic, parts));
        }

        if (parts is Message[] array)
        {
            PublishPartsWithFlags(serviceName, topic, array, (int)flags,
                nameof(parts));
            return true;
        }

        if (parts is List<Message> list)
        {
            PublishPartsWithFlags(serviceName, topic, list, (int)flags,
                nameof(parts));
            return true;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishPartsWithFlags(serviceName, topic, copied, (int)flags,
            nameof(parts));
        return true;
    }

    internal SendResult PublishNoWaitResult(string serviceName, string topic,
        IReadOnlyList<Message> parts)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        if (parts is Message[] array)
            return PublishNoWaitParts(serviceName, topic, array,
                nameof(parts));

        if (parts is List<Message> list)
            return PublishNoWaitParts(serviceName, topic,
                list, nameof(parts));

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return PublishNoWaitParts(serviceName, topic, copied, nameof(parts));
    }

    public bool SendChannel(string channelName, Message message,
        SendFlags flags = SendFlags.None)
    {
        ValidateServiceName(channelName, nameof(channelName));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        try
        {
            SendChannelCore(channelName, new[] { message }, (int)flags,
                nameof(message));
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    public bool SendChannel(string channelName, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateServiceName(channelName, nameof(channelName));
        EnsureNotDisposed();
        EnsureParts(parts, nameof(parts));

        try
        {
            if (parts is Message[] array)
            {
                SendChannelCore(channelName, array, (int)flags,
                    nameof(parts));
                return true;
            }

            if (parts is List<Message> list)
            {
                SendChannelCore(channelName, CollectionsMarshal.AsSpan(list),
                    (int)flags, nameof(parts));
                return true;
            }

            Message[] copied = new Message[parts.Count];
            for (int i = 0; i < copied.Length; i++)
                copied[i] = parts[i];
            SendChannelCore(channelName, copied.AsSpan(), (int)flags,
                nameof(parts));
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    public Task<IReadOnlyList<Message>> RequestChannelAsync(string channelName,
        Message message, TimeSpan timeout = default, CancellationToken ct = default)
    {
        ValidateServiceName(channelName, nameof(channelName));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        return RequestChannelAsyncInternal(channelName, new[] { message }, timeout,
            ct).ContinueWith(task => task.Result.Parts, TaskScheduler.Default);
    }

    public Task<IReadOnlyList<Message>> RequestChannelAsync(string channelName,
        IReadOnlyList<Message> parts, TimeSpan timeout = default,
        CancellationToken ct = default)
    {
        ValidateServiceName(channelName, nameof(channelName));
        return RequestChannelAsyncInternal(channelName, parts, timeout, ct)
            .ContinueWith(task => task.Result.Parts, TaskScheduler.Default);
    }

    public bool RequestChannel(string channelName, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
        => RequestChannel(channelName, message, callback, SendFlags.None, timeout);

    public bool RequestChannel(string channelName, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
        => RequestChannel(channelName, parts, callback, SendFlags.None, timeout);

    public bool RequestChannel(string channelName, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan? timeout = null)
        => RequestChannel(channelName, new[] { message }, callback, flags,
            timeout);

    public bool RequestChannel(string channelName, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan? timeout = null)
    {
        ValidateServiceName(channelName, nameof(channelName));
        try
        {
            RequestReplySupport.AttachResultCallback(
                () => RequestChannelAsyncInternal(channelName, parts,
                    timeout ?? TimeSpan.Zero,
                    CancellationToken.None, (int)flags),
                (result, reply) =>
                {
                    IReadOnlyList<Message> payload = Array.Empty<Message>();
                    if (reply != null)
                    {
                        payload = RequestReplySupport.TakeOwnedParts(reply);
                        reply.Dispose();
                    }
                    callback(result, payload);
                });
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0)
        {
            if (RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
            {
                return false;
            }

            throw;
        }
    }

    internal void PublishRawSingle(string serviceName, string topic,
        ReadOnlySpan<byte> payload, int flags)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        PublishRawSingleCore(serviceName, topic, payload, flags);
    }

    internal SendResult PublishRawSingleNoWait(string serviceName, string topic,
        ReadOnlySpan<byte> payload)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        return PublishRawSingleNoWaitCore(serviceName, topic, payload);
    }

    internal void PublishBorrowedSingle(string serviceName, string topic,
        byte[] payload, int flags)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        EnsureNotDisposed();
        PublishBorrowedSingleCore(serviceName, topic, payload, flags);
    }

    internal SendResult PublishBorrowedSingleNoWait(string serviceName, string topic,
        byte[] payload)
    {
        ValidateServiceName(serviceName, nameof(serviceName));
        ValidateTopicId(topic, nameof(topic));
        if (payload == null)
            throw new ArgumentNullException(nameof(payload));
        EnsureNotDisposed();
        return PublishBorrowedSingleNoWaitCore(serviceName, topic, payload);
    }

    public void SetSubscription(string topicOrPattern)
    {
        ValidateTopicId(topicOrPattern, nameof(topicOrPattern));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_subscription(_handle, topicOrPattern);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void UnsetSubscription(string topicOrPattern)
    {
        ValidateTopicId(topicOrPattern, nameof(topicOrPattern));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_unset_subscription(_handle,
            topicOrPattern);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public SubscriptionInfo SubscriptionAt(int index)
    {
        EnsureNotDisposed();
        return SubscriptionIntrospection.At(_handle, index);
    }

    public TopicMessage? Subscribe(RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        return (flags & RecvFlags.DontWait) != 0
            ? TryReceiveCore(() => SubscribeCore((int)flags))
            : SubscribeCore((int)flags);
    }

    internal bool SubscribeNoWait(out TopicMessage? subscribed)
    {
        EnsureNotDisposed();
        subscribed = TryReceiveCore(() => SubscribeCore(1));
        return subscribed != null;
    }

    public SubscriptionEvent? ReceiveSubscriptionEvent(
        RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        return (flags & RecvFlags.DontWait) != 0
            ? TryReceiveCore(() => ReceiveSubscriptionEventCore((int)flags))
            : ReceiveSubscriptionEventCore((int)flags);
    }

    internal int? TryReceiveRawSubscribedFrame(Span<byte> destination, int flags,
        out byte[][] pendingFrames)
    {
        EnsureNotDisposed();
        try
        {
            return ReceiveRawSubscribedFrameCore(destination, flags,
                out pendingFrames);
        }
        catch (ZlinkException ex) when (ZlinkException.MapErrorCode(ex.InternalErrno)
            == ErrorCode.EAgain)
        {
            pendingFrames = Array.Empty<byte[]>();
            return null;
        }
    }

    public void OnSendReady(Action handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        SynchronizationContext? context = SynchronizationContext.Current;
        var native = new NativeMethods.ZlinkSendReadyHandlerDelegate(
            OnNativeSendReady);
        int rc = NativeMethods.zlink_send_ready_handler(_handle, native,
            IntPtr.Zero);
        ZlinkException.ThrowHandlerIfError(rc);
        _sendReadyHandler = handler;
        _sendReadyHandlerContext = context;
        _sendReadyHandlerNative = native;
    }

    public void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq, Message message, SendFlags flags = SendFlags.None)
        => ReplyToSpot(destNodeRid, destSpotRid, requestSeq, new[] { message },
            flags);

    public Task<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId destNodeRid, RoutingId destSpotRid, Message message,
        TimeSpan timeout = default, CancellationToken ct = default)
        => RequestToSpotAsync(destNodeRid, destSpotRid, new[] { message },
            timeout, ct);

    public Task<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId destNodeRid, RoutingId destSpotRid, IReadOnlyList<Message> parts,
        TimeSpan timeout = default, CancellationToken ct = default)
        => RequestToSpotAsyncInternal(destNodeRid, destSpotRid, parts, timeout,
            ct).ContinueWith(task => task.Result.Parts, TaskScheduler.Default);

    public bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
        => RequestToSpot(destNodeRid, destSpotRid, new[] { message }, callback,
            flags, timeout);

    public bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            RequestReplySupport.AttachResultCallback(
                () => RequestToSpotAsyncInternal(destNodeRid, destSpotRid, parts,
                    timeout ?? TimeSpan.Zero, CancellationToken.None, (int)flags),
                (result, reply) =>
                {
                    IReadOnlyList<Message> payload = Array.Empty<Message>();
                    if (reply != null)
                    {
                        payload = RequestReplySupport.TakeOwnedParts(reply);
                        reply.Dispose();
                    }
                    callback(result, payload);
                });
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    public bool SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, SendFlags flags = SendFlags.None)
        => SendToSpot(destNodeRid, destSpotRid, new[] { message }, flags);

    public unsafe bool SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        EnsureParts(parts, nameof(parts));
        byte[] nodeRidBytes = RoutingIdCodec.FromRoutingId(destNodeRid);
        byte[] spotRidBytes = RoutingIdCodec.FromRoutingId(destSpotRid);
        ZlinkRoutingId nodeRid = NativeHelpers.WriteRoutingId(nodeRidBytes);
        ZlinkRoutingId spotRid = NativeHelpers.WriteRoutingId(spotRidBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_spot_send_spot_part(_handle,
                        ref nodeRid, ref spotRid, ref nativePart, (int)flags,
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final);
                    submitted = true;
                    if (rc != 0)
                        throw ZlinkException.CreateSubmitException(
                            NativeMethods.zlink_errno());
                }
                finally
                {
                    if (!submitted)
                        NativeMethods.zlink_msg_close(ref nativePart);
                }
            }
            return true;
        }
        catch (ZlinkException ex) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(ex)
                == SendResult.Backpressured)
        {
            return false;
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    public unsafe void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        _ = flags;
        EnsureParts(parts, nameof(parts));
        byte[] nodeRidBytes = RoutingIdCodec.FromRoutingId(destNodeRid);
        byte[] spotRidBytes = RoutingIdCodec.FromRoutingId(destSpotRid);
        ZlinkRoutingId nodeRid = NativeHelpers.WriteRoutingId(nodeRidBytes);
        ZlinkRoutingId spotRid = NativeHelpers.WriteRoutingId(spotRidBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_spot_reply_spot_part(_handle,
                        ref nodeRid, ref spotRid, requestSeq, ref nativePart,
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final);
                    submitted = true;
                    if (rc != 0)
                        throw ZlinkException.CreateSubmitException(
                            NativeMethods.zlink_errno());
                }
                finally
                {
                    if (!submitted)
                        NativeMethods.zlink_msg_close(ref nativePart);
                }
            }
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    public void ReplyToRouter(RoutingId peerRid, ulong requestSeq,
        Message message, SendFlags flags = SendFlags.None)
        => ReplyToRouter(peerRid, requestSeq, new[] { message }, flags);

    public Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
        Message message, TimeSpan timeout = default,
        CancellationToken ct = default)
        => RequestToRouterAsync(peerRid, new[] { message }, timeout, ct);

    public Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout = default,
        CancellationToken ct = default)
        => RequestToRouterAsyncInternal(peerRid, parts, timeout, ct)
            .ContinueWith(task => task.Result.Parts, TaskScheduler.Default);

    public bool RequestToRouter(RoutingId peerRid, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
        => RequestToRouter(peerRid, new[] { message }, callback, flags,
            timeout);

    public bool RequestToRouter(RoutingId peerRid, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            RequestReplySupport.AttachResultCallback(
                () => RequestToRouterAsyncInternal(peerRid, parts,
                    timeout ?? TimeSpan.Zero, CancellationToken.None, (int)flags),
                (result, reply) =>
                {
                    IReadOnlyList<Message> payload = Array.Empty<Message>();
                    if (reply != null)
                    {
                        payload = RequestReplySupport.TakeOwnedParts(reply);
                        reply.Dispose();
                    }
                    callback(result, payload);
                });
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    public unsafe void ReplyToRouter(RoutingId peerRid, ulong requestSeq,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        _ = flags;
        EnsureParts(parts, nameof(parts));
        byte[] peerRidBytes = RoutingIdCodec.FromRoutingId(peerRid);
        ZlinkRoutingId routingId = NativeHelpers.WriteRoutingId(peerRidBytes);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_spot_reply_router_part(_handle,
                        ref routingId, requestSeq, ref nativePart,
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final);
                    submitted = true;
                    if (rc != 0)
                        throw ZlinkException.CreateSubmitException(
                            NativeMethods.zlink_errno());
                }
                finally
                {
                    if (!submitted)
                        NativeMethods.zlink_msg_close(ref nativePart);
                }
            }
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    public unsafe Received? RecvRouted(RecvFlags flags = RecvFlags.None)
    {
        MultipartMessageCollection? parts;
        byte[]? nodeRidBytes;
        byte[]? spotRidBytes;
        ulong requestSeq;
        try
        {
            parts = ReceiveSpotRoutedParts((int)flags, out nodeRidBytes,
                out spotRidBytes, out requestSeq);
        }
        catch (ZlinkException ex) when ((flags & RecvFlags.DontWait) != 0
            && ZlinkException.MapErrorCode(ex.InternalErrno) == ErrorCode.EAgain)
        {
            return null;
        }
        if (parts == null)
            return null;
        RoutingId? nodeRid = nodeRidBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(nodeRidBytes);
        RoutingId? spotRid = spotRidBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(spotRidBytes);
        if (requestSeq == 0)
            return Received.Create(nodeRid, parts, spotRid: spotRid);
        return Received.Create(nodeRid, parts, requestSeq, spotRid,
            (replyParts, sendFlags) => ReplyToSpot(
                nodeRid ?? throw new ZlinkSubmitException(
                    SubmitResult.InvalidArgument, (int)ErrorCode.EInval),
                spotRid ?? throw new ZlinkSubmitException(
                    SubmitResult.InvalidArgument, (int)ErrorCode.EInval),
                requestSeq, replyParts, sendFlags));
    }

    public ActorJoinRequest? RecvActorJoin(RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        ZlinkMsg nativeMessage = default;
        int rc = NativeMethods.zlink_spot_actor_join_recv(_handle,
            out ZlinkActorJoinInfo nativeInfo, ref nativeMessage, (int)flags);
        if (rc != 0)
        {
            int errno = NativeMethods.zlink_errno();
            if ((flags & RecvFlags.DontWait) != 0
                && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain)
                return null;
            throw ZlinkException.CreateRecvException(errno);
        }

        ActorJoinInfo info = ActorInterop.FromNative(ref nativeInfo);
        Message message = Message.MoveFromNative(ref nativeMessage);
        return new ActorJoinRequest(info, message);
    }

    public void ReplyActorJoin(ActorJoinInfo info, bool accepted,
        Message message)
    {
        if (info == null)
            throw new ArgumentNullException(nameof(info));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        ZlinkActorJoinInfo nativeInfo = info.NativeInfo;
        ZlinkMsg nativeMessage = default;
        bool submitted = false;
        try
        {
            message.Copy().MoveTo(ref nativeMessage);
            int rc = NativeMethods.zlink_spot_actor_join_reply(_handle,
                ref nativeInfo, accepted ? 1u : 0u, ref nativeMessage);
            submitted = true;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        finally
        {
            if (!submitted)
                NativeMethods.zlink_msg_close(ref nativeMessage);
        }
    }

    public ActorRef[] ActorsSnapshot()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_actors_snapshot(_handle,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<ActorRef>();

        int entrySize = Marshal.SizeOf<ZlinkActorRef>();
        IntPtr entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_actors_snapshot(_handle, entries,
                ref actual);
            ZlinkException.ThrowConfigIfError(rc);
            ActorRef[] result = new ActorRef[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                ZlinkActorRef native =
                    Marshal.PtrToStructure<ZlinkActorRef>(
                        IntPtr.Add(entries, i * entrySize));
                result[i] = ActorInterop.FromNative(ref native);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    public unsafe void OnRoutedReceive(Action<Received> handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        _routedReceiveHandler = handler;
        _routedReceiveHandlerContext = SynchronizationContext.Current;
        _routedReceiveHandlerNative = OnNativeRoutedReceive;
        int rc = NativeMethods.zlink_spot_handler(_handle,
            _routedReceiveHandlerNative, IntPtr.Zero);
        if (rc != 0)
            throw ZlinkException.CreateHandlerException(NativeMethods.zlink_errno());
    }

    public unsafe void OnDispatchEvent(Action<Spot, SpotDispatchInfo> handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        _dispatchEventHandler = handler;
        _dispatchEventHandlerNative = OnNativeDispatchEvent;
        int rc = NativeMethods.zlink_spot_dispatch_event_handler(_handle,
            _dispatchEventHandlerNative, IntPtr.Zero);
        if (rc != 0)
            throw ZlinkException.CreateHandlerException(NativeMethods.zlink_errno());
    }

    public void DrainChannelReplyFrom(IntPtr dealerSubject)
    {
        EnsureNotDisposed();
        if (dealerSubject == IntPtr.Zero)
            throw new ArgumentException("dealerSubject must not be null.",
                nameof(dealerSubject));
        int rc = NativeMethods.zlink_spot_channel_reply_progress_from(_handle,
            dealerSubject);
        if (rc < 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        Destroy(throwOnError: true);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~Spot()
    {
        Destroy(throwOnError: false);
    }

    private void Destroy(bool throwOnError)
    {
        if (_handle == IntPtr.Zero)
            return;

        IntPtr originalHandle = _handle;
        IntPtr handle = _handle;
        int rc = _ownsHandle
            ? NativeMethods.zlink_spot_destroy(ref handle)
            : 0;
        if (rc != 0)
        {
            _handle = originalHandle;
            if (throwOnError)
            {
                throw ZlinkException.CreateCloseException(
                    NativeMethods.zlink_errno());
            }
            return;
        }

        _handle = IntPtr.Zero;
        _sendReadyHandler = null;
        _routedReceiveHandler = null;
        _dispatchEventHandler = null;
        _sendReadyHandlerContext = null;
        _routedReceiveHandlerContext = null;
        _sendReadyHandlerNative = null;
        _routedReceiveHandlerNative = null;
        _dispatchEventHandlerNative = null;
        _node.UnregisterSpot(this);
    }

    private unsafe void PublishCore(string serviceName, string topic,
        ReadOnlySpan<Message> parts, int flags, string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackPublishPartLimit
            ? stackalloc ZlinkMsg[StackPublishPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        int submitted = 0;
        try
        {
            for (int i = 0; i < parts.Length; i++)
            {
                if (parts[i] == null)
                {
                    throw new ArgumentException(
                        "Parts must not contain null messages.", paramName);
                }
            }

            for (int i = 0; i < parts.Length; i++)
            {
                parts[i].MoveTo(ref nativeParts[i]);
                built++;
            }

            for (int i = 0; i < built; i++)
            {
                int rc = NativeMethods.zlink_spot_publish_part(_handle,
                    serviceName, topic, ref nativeParts[i], (int)flags,
                    i + 1 < built
                        ? NativeMethods.ZlinkPartFlag.More
                        : NativeMethods.ZlinkPartFlag.Final);
                submitted = i + 1;
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
        }
        catch
        {
            for (int i = submitted; i < built; i++)
                parts[i].RestoreFrom(ref nativeParts[i]);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe void PublishSingleCore(string serviceName, string topic,
        Message message, int flags)
    {
        ZlinkMsg nativePart = default;
        bool submitted = false;
        try
        {
            message.MoveTo(ref nativePart);
            int rc = NativeMethods.zlink_spot_publish_part(_handle, serviceName,
                topic, ref nativePart, (int)flags,
                NativeMethods.ZlinkPartFlag.Final);
            submitted = true;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        catch
        {
            if (!submitted)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Spot));
    }

    private void OnNativeSendReady(IntPtr subject, IntPtr userData)
    {
        Action? handler = _sendReadyHandler;
        SynchronizationContext? context = _sendReadyHandlerContext;
        if (handler == null)
            return;

        try
        {
            CallbackDelivery.Post(context, handler);
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }

    private unsafe Task<Received> RequestChannelAsyncInternal(string channelName,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        EnsureParts(parts, nameof(parts));
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        uint timeoutMs = NormalizeTimeout(timeout);
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        RequestCallState? state = null;

        try
        {
            state = new RequestCallState(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
            {
                state.SetCancellationRegistration(ct.Register(static userdata =>
                {
                    RequestCallState.CancelFromUserData(userdata);
                }, handle));
            }

            state.SetTimeoutTimer(new System.Threading.Timer(static userdata =>
            {
                RequestCallState.TimeoutFromUserData(userdata);
            }, handle, (int)timeoutMs, Timeout.Infinite));

            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_spot_request_channel_part(_handle,
                        channelName, ref nativePart,
                        i + 1 < cloned.Length ? IntPtr.Zero : RoutedReplyHandlerPtr,
                        i + 1 < cloned.Length ? IntPtr.Zero : GCHandle.ToIntPtr(handle),
                        flags,
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final,
                        i + 1 < cloned.Length ? 0u : timeoutMs);
                    submitted = true;
                    if (rc != 0)
                        throw ZlinkException.CreateSubmitException(
                            NativeMethods.zlink_errno());
                }
                finally
                {
                    if (!submitted)
                        NativeMethods.zlink_msg_close(ref nativePart);
                }
            }

            return RequestProgressPump.AttachSpot(_handle, completion.Task);
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe Task<Received> RequestToSpotAsyncInternal(
        RoutingId destNodeRid, RoutingId destSpotRid, IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct, int flags = 0)
    {
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nodeRid = NativeHelpers.WriteRoutingId(
            RoutingIdCodec.FromRoutingId(destNodeRid));
        ZlinkRoutingId spotRid = NativeHelpers.WriteRoutingId(
            RoutingIdCodec.FromRoutingId(destSpotRid));
        return RequestRoutedAsyncInternal(parts, timeout, ct, flags,
            (ref ZlinkMsg nativePart, IntPtr handler, IntPtr userData,
                NativeMethods.ZlinkPartFlag partFlag, uint timeoutMs) =>
                NativeMethods.zlink_spot_request_spot_part(_handle,
                    ref nodeRid, ref spotRid, ref nativePart, handler, userData,
                    flags, partFlag, timeoutMs));
    }

    private unsafe Task<Received> RequestToRouterAsyncInternal(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nativePeerRid = NativeHelpers.WriteRoutingId(
            RoutingIdCodec.FromRoutingId(peerRid));
        return RequestRoutedAsyncInternal(parts, timeout, ct, flags,
            (ref ZlinkMsg nativePart, IntPtr handler, IntPtr userData,
                NativeMethods.ZlinkPartFlag partFlag, uint timeoutMs) =>
                NativeMethods.zlink_spot_request_router_part(_handle,
                    ref nativePeerRid, ref nativePart, handler, userData,
                    flags, partFlag, timeoutMs));
    }

    private unsafe delegate int SpotRequestPartSubmitter(
        ref ZlinkMsg nativePart, IntPtr handler, IntPtr userData,
        NativeMethods.ZlinkPartFlag partFlag, uint timeoutMs);

    private unsafe Task<Received> RequestRoutedAsyncInternal(
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags, SpotRequestPartSubmitter submit)
    {
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        uint timeoutMs = NormalizeTimeout(timeout);
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        RequestCallState? state = null;

        try
        {
            state = new RequestCallState(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
            {
                state.SetCancellationRegistration(ct.Register(static userdata =>
                {
                    RequestCallState.CancelFromUserData(userdata);
                }, handle));
            }

            state.SetTimeoutTimer(new System.Threading.Timer(static userdata =>
            {
                RequestCallState.TimeoutFromUserData(userdata);
            }, handle, (int)timeoutMs, Timeout.Infinite));

            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = submit(ref nativePart,
                        i + 1 < cloned.Length ? IntPtr.Zero : RoutedReplyHandlerPtr,
                        i + 1 < cloned.Length ? IntPtr.Zero : GCHandle.ToIntPtr(handle),
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final,
                        i + 1 < cloned.Length ? 0u : timeoutMs);
                    submitted = true;
                    if (rc != 0)
                        throw ZlinkException.CreateSubmitException(
                            NativeMethods.zlink_errno());
                }
                finally
                {
                    if (!submitted)
                        NativeMethods.zlink_msg_close(ref nativePart);
                }
            }

            return RequestProgressPump.AttachSpot(_handle, completion.Task);
        }
        catch
        {
            if (state != null)
                state.Dispose();
            if (handle.IsAllocated)
                handle.Free();
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private unsafe void SendChannelCore(string channelName, ReadOnlySpan<Message> parts,
        int flags, string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackPublishPartLimit
            ? stackalloc ZlinkMsg[StackPublishPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        int submitted = 0;
        try
        {
            for (int i = 0; i < parts.Length; i++)
            {
                if (parts[i] == null)
                {
                    throw new ArgumentException(
                        "Parts must not contain null messages.", paramName);
                }
            }

            for (int i = 0; i < parts.Length; i++)
            {
                parts[i].MoveTo(ref nativeParts[i]);
                built++;
            }

            for (int i = 0; i < built; i++)
            {
                int rc = NativeMethods.zlink_spot_send_channel_part(_handle,
                    channelName, ref nativeParts[i], flags,
                    i + 1 < built
                        ? NativeMethods.ZlinkPartFlag.More
                        : NativeMethods.ZlinkPartFlag.Final);
                submitted = i + 1;
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
        }
        catch
        {
            for (int i = submitted; i < built; i++)
                parts[i].RestoreFrom(ref nativeParts[i]);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe void OnNativeRoutedReceive(ZlinkRoutingId* sourceRoutingId,
        ZlinkRoutingId* spotRoutingId, ulong requestSeq, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        Action<Received>? handler = _routedReceiveHandler;
        if (handler == null)
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            return;
        }

        Message[] managedParts = Message.FromNativeVector(parts, partCount);
        parts = IntPtr.Zero;
        partCount = 0;
        RoutingId? nodeRid = sourceRoutingId == null ? null :
            RoutingIdCodec.ToRoutingId(
                NativeHelpers.ReadRoutingId(ref *sourceRoutingId));
        RoutingId? spotRid = spotRoutingId == null ? null :
            RoutingIdCodec.ToRoutingId(
                NativeHelpers.ReadRoutingId(ref *spotRoutingId));
        Received received = requestSeq == 0
            ? Received.Create(nodeRid, managedParts, spotRid: spotRid)
            : Received.Create(nodeRid, managedParts, requestSeq, spotRid,
                (replyParts, sendFlags) => ReplyToSpot(
                    nodeRid ?? throw new ZlinkSubmitException(
                        SubmitResult.InvalidArgument, (int)ErrorCode.EInval),
                    spotRid ?? throw new ZlinkSubmitException(
                        SubmitResult.InvalidArgument, (int)ErrorCode.EInval),
                    requestSeq, replyParts, sendFlags));
        CallbackDelivery.Post(_routedReceiveHandlerContext, () => handler(received));
    }

    private unsafe void OnNativeDispatchEvent(IntPtr spot,
        ZlinkSpotDispatchInfoNative* info, IntPtr userData)
    {
        Action<Spot, SpotDispatchInfo>? handler = _dispatchEventHandler;
        if (handler == null || info == null)
            return;

        SpotDispatchEvent eventKind = (SpotDispatchEvent)info->Event;
        SpotDispatchSubjectKind subjectKind =
            (SpotDispatchSubjectKind)info->SubjectKind;
        ActorPart[]? actorParts = eventKind == SpotDispatchEvent.ActorReadable
            && subjectKind == SpotDispatchSubjectKind.Actor
            && info->Subject != IntPtr.Zero
                ? ActorInterop.DrainActorParts(_node.Handle, info->Subject)
                : null;
        Timer? timer = eventKind == SpotDispatchEvent.TimerReadable
            && subjectKind == SpotDispatchSubjectKind.Timer
                ? Timer.FromDispatchSubject(info->Subject)
                : null;
        IntPtr channelDealerSubject = eventKind
            == SpotDispatchEvent.ChannelReplyReadable
            && subjectKind == SpotDispatchSubjectKind.ChannelDealer
                ? info->Subject
                : IntPtr.Zero;
        SpotDispatchInfo dispatchInfo = new(eventKind, subjectKind,
            timer, channelDealerSubject, DrainChannelReplyFrom, actorParts);
        try
        {
            handler(this, dispatchInfo);
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }

    private static void OnRoutedReply(int result, IntPtr parts, nuint partCount,
        IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        RequestCallState state = (RequestCallState)handle.Target!;
        try
        {
            if (result != 0)
            {
                state.TrySetException(new ZlinkRequestException(
                    (RequestResult)result));
                return;
            }

            Message[] replyParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            Received received = Received.Create((RoutingId?)null, replyParts);
            if (!state.TrySetResult(received))
                RequestReplySupport.DisposeParts(replyParts);
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            handle.Free();
        }
    }

    private static uint NormalizeTimeout(TimeSpan timeout)
    {
        if (timeout <= TimeSpan.Zero)
            return 0;
        double millis = timeout.TotalMilliseconds;
        if (millis <= 1)
            return 1;
        if (millis >= uint.MaxValue)
            return uint.MaxValue;
        return (uint)millis;
    }

    private static void EnsureParts(IReadOnlyList<Message> parts, string paramName)
    {
        if (parts == null)
            throw new ArgumentNullException(paramName);
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", paramName);
    }

    private static void ValidateTopicId(string value, string paramName)
    {
        if (value == null)
            throw new ArgumentNullException(paramName);
        if (value.Length == 0)
            throw new ArgumentException("Value must not be empty.", paramName);

        int byteCount = Encoding.UTF8.GetByteCount(value);
        if (byteCount == 0 || byteCount > 255)
        {
            throw new ArgumentOutOfRangeException(paramName,
                "UTF-8 length must be between 1 and 255 bytes.");
        }
    }

    private static void ValidateServiceName(string value, string paramName)
    {
        BoundaryValidation.ValidateFixedUtf8(value, paramName);
    }

    private void PublishPartsWithFlags(string serviceName, string topic,
        IReadOnlyList<Message> parts, int flags, string paramName)
    {
        if (parts is Message[] array)
        {
            PublishCore(serviceName, topic, array.AsSpan(), flags, paramName);
            return;
        }

        if (parts is List<Message> list)
        {
            PublishCore(serviceName, topic, CollectionsMarshal.AsSpan(list),
                flags, paramName);
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishCore(serviceName, topic, copied.AsSpan(), flags, paramName);
    }

    private SendResult PublishNoWaitParts(string serviceName, string topic,
        IReadOnlyList<Message> parts, string paramName)
    {
        if (parts is Message[] array)
            return PublishNoWaitCore(serviceName, topic, array.AsSpan(), paramName);

        if (parts is List<Message> list)
            return PublishNoWaitCore(serviceName, topic,
                CollectionsMarshal.AsSpan(list), paramName);

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return PublishNoWaitCore(serviceName, topic, copied.AsSpan(), paramName);
    }

    private unsafe TopicMessage SubscribeCore(int flags)
    {
        byte[] serviceBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            MultipartMessageCollection parts = ReceiveSpotSubscribedParts(flags,
                serviceBuffer, topicBuffer, out byte[]? routingIdBytes,
                out string serviceName, out string topic)
                ?? throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            RoutingId? routingId = routingIdBytes == null
                ? null
                : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
            if (parts.Count == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            return new TopicMessage(routingId, serviceName, topic, parts);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(serviceBuffer);
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe SubscriptionEvent ReceiveSubscriptionEventCore(int flags)
    {
        byte[] serviceBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            int rc = NativeMethods.zlink_spot_subscription_event_recv(_handle,
                out IntPtr sourceRoutingId, out int subscribedInt, serviceBuffer,
                (nuint)serviceBuffer.Length, out nuint serviceLength, topicBuffer,
                (nuint)topicBuffer.Length, out nuint topicLength, flags);
            if (rc != 0)
            {
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            }

            byte[]? routingIdBytes = CopyRoutingIdBytes(sourceRoutingId);
            RoutingId? routingId = routingIdBytes == null
                ? null
                : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
            string serviceName = DecodeBuffer(serviceBuffer, serviceLength);
            string topic = DecodeBuffer(topicBuffer, topicLength);
            return new SubscriptionEvent(routingId, serviceName, topic,
                subscribedInt != 0);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(serviceBuffer);
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe SendResult PublishNoWaitCore(string serviceName, string topic,
        ReadOnlySpan<Message> parts, string paramName)
    {
        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackPublishPartLimit
            ? stackalloc ZlinkMsg[StackPublishPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        int submitted = 0;
        try
        {
            for (int i = 0; i < parts.Length; i++)
            {
                if (parts[i] == null)
                {
                    throw new ArgumentException(
                        "Parts must not contain null messages.", paramName);
                }
            }

            for (int i = 0; i < parts.Length; i++)
            {
                parts[i].MoveTo(ref nativeParts[i]);
                built++;
            }

            for (int i = 0; i < built; i++)
            {
                int rc = NativeMethods.zlink_spot_publish_part(_handle,
                    serviceName, topic, ref nativeParts[i], DontWaitFlag,
                    i + 1 < built
                        ? NativeMethods.ZlinkPartFlag.More
                        : NativeMethods.ZlinkPartFlag.Final);
                submitted = i + 1;
                if (rc == 0)
                    continue;

                for (int j = submitted; j < built; j++)
                    parts[j].RestoreFrom(ref nativeParts[j]);
                SendResult? sendResult = TryMapSendResultFromErrno();
                if (sendResult != null)
                    return sendResult.Value;
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }
            return SendResult.Sent;
        }
        catch
        {
            for (int i = submitted; i < built; i++)
                parts[i].RestoreFrom(ref nativeParts[i]);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private unsafe SendResult PublishNoWaitSingleCore(string serviceName,
        string topic, Message message)
    {
        ZlinkMsg nativePart = default;
        bool submitted = false;
        try
        {
            message.MoveTo(ref nativePart);
            int rc = NativeMethods.zlink_spot_publish_part(_handle, serviceName,
                topic, ref nativePart, DontWaitFlag,
                NativeMethods.ZlinkPartFlag.Final);
            submitted = true;
            if (rc == 0)
                return SendResult.Sent;

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult == null)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            return sendResult.Value;
        }
        catch
        {
            if (!submitted)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }

    private unsafe void PublishRawSingleCore(string serviceName, string topic,
        ReadOnlySpan<byte> payload, int flags)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        try
        {
            int initRc = NativeMethods.zlink_msg_init_size(ref nativePart,
                (nuint)payload.Length);
            ZlinkException.ThrowSubmitIfError(initRc);
            initialized = true;

            if (payload.Length != 0)
            {
                IntPtr dataPtr = NativeMethods.zlink_msg_data(ref nativePart);
                if (dataPtr == IntPtr.Zero)
                    throw new InvalidOperationException("Message data is null.");
                payload.CopyTo(new Span<byte>((void*)dataPtr, payload.Length));
            }

            int rc = NativeMethods.zlink_spot_publish_part(_handle, serviceName,
                topic, ref nativePart, flags, NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            throw;
        }
    }

    private unsafe SendResult PublishRawSingleNoWaitCore(string serviceName,
        string topic,
        ReadOnlySpan<byte> payload)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        try
        {
            int initRc = NativeMethods.zlink_msg_init_size(ref nativePart,
                (nuint)payload.Length);
            ZlinkException.ThrowSubmitIfError(initRc);
            initialized = true;

            if (payload.Length != 0)
            {
                IntPtr dataPtr = NativeMethods.zlink_msg_data(ref nativePart);
                if (dataPtr == IntPtr.Zero)
                    throw new InvalidOperationException("Message data is null.");
                payload.CopyTo(new Span<byte>((void*)dataPtr, payload.Length));
            }

            int rc = NativeMethods.zlink_spot_publish_part(_handle, serviceName,
                topic, ref nativePart, DontWaitFlag,
                NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc == 0)
                return SendResult.Sent;

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult != null)
                return sendResult.Value;
            throw ZlinkException.CreateSubmitException(
                NativeMethods.zlink_errno());
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            throw;
        }
    }

    private unsafe void PublishBorrowedSingleCore(string serviceName,
        string topic, byte[] payload, int flags)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                handle.AddrOfPinnedObject(), (nuint)payload.Length,
                BorrowedBufferFreePtr, GCHandle.ToIntPtr(handle));
            ZlinkException.ThrowSubmitIfError(initRc);
            initialized = true;
            handle = default;

            int rc = NativeMethods.zlink_spot_publish_part(_handle, serviceName,
                topic, ref nativePart, flags, NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe SendResult PublishBorrowedSingleNoWaitCore(string serviceName,
        string topic, byte[] payload)
    {
        ZlinkMsg nativePart = default;
        bool initialized = false;
        GCHandle handle = default;
        try
        {
            handle = GCHandle.Alloc(payload, GCHandleType.Pinned);
            int initRc = NativeMethods.zlink_msg_init_data(ref nativePart,
                handle.AddrOfPinnedObject(), (nuint)payload.Length,
                BorrowedBufferFreePtr, GCHandle.ToIntPtr(handle));
            ZlinkException.ThrowSubmitIfError(initRc);
            initialized = true;
            handle = default;

            int rc = NativeMethods.zlink_spot_publish_part(_handle, serviceName,
                topic, ref nativePart, DontWaitFlag,
                NativeMethods.ZlinkPartFlag.Final);
            initialized = false;
            if (rc == 0)
                return SendResult.Sent;

            SendResult? sendResult = TryMapSendResultFromErrno();
            if (sendResult != null)
                return sendResult.Value;
            throw ZlinkException.CreateSubmitException(
                NativeMethods.zlink_errno());
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref nativePart);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private unsafe int ReceiveRawSubscribedFrameCore(Span<byte> destination,
        int flags, out byte[][] pendingFrames)
    {
        byte[] serviceBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            List<byte[]> frames = ReceiveSpotSubscribedFrames(flags, serviceBuffer,
                topicBuffer);
            if (frames.Count == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            return CopyFirstFrameAndCollectPending(frames, destination,
                out pendingFrames);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(serviceBuffer);
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe MultipartMessageCollection? ReceiveSpotRoutedParts(int flags,
        out byte[]? nodeRidBytes, out byte[]? spotRidBytes,
        out ulong requestSeq)
    {
        ZlinkMsg[] nativeParts = Array.Empty<ZlinkMsg>();
        int nativePartCount = 0;
        nodeRidBytes = null;
        spotRidBytes = null;
        requestSeq = 0;
        try
        {
            while (true)
            {
                ZlinkMsg part = default;
                int initRc = NativeMethods.zlink_msg_init(ref part);
                ZlinkException.ThrowIfError(initRc);
                bool initialized = true;
                int rc = NativeMethods.zlink_spot_recv_part(_handle,
                    out IntPtr sourceNodeRid, out IntPtr sourceSpotRid,
                    out requestSeq, ref part, out int hasMore, flags);
                if (rc != 0)
                {
                    if (initialized)
                        NativeMethods.zlink_msg_close(ref part);
                    throw ZlinkException.CreateRecvException(
                        NativeMethods.zlink_errno());
                }

                initialized = false;
                nodeRidBytes ??= CopyRoutingIdBytes(sourceNodeRid);
                spotRidBytes ??= CopyRoutingIdBytes(sourceSpotRid);
                AppendNativePart(ref nativeParts, ref nativePartCount, ref part);
                if (hasMore == 0)
                    break;
            }

            return MultipartMessageCollection.FromNativeParts(nativeParts,
                nativePartCount);
        }
        catch
        {
            CloseNativeParts(nativeParts, nativePartCount);
            throw;
        }
    }

    private unsafe MultipartMessageCollection? ReceiveSpotSubscribedParts(int flags,
        byte[] serviceBuffer, byte[] topicBuffer, out byte[]? routingIdBytes,
        out string serviceName, out string topic)
    {
        ZlinkMsg[] nativeParts = Array.Empty<ZlinkMsg>();
        int nativePartCount = 0;
        routingIdBytes = null;
        serviceName = string.Empty;
        topic = string.Empty;
        try
        {
            while (true)
            {
                ZlinkMsg part = default;
                int initRc = NativeMethods.zlink_msg_init(ref part);
                ZlinkException.ThrowIfError(initRc);
                bool initialized = true;
                int rc = NativeMethods.zlink_spot_subscribe_part(_handle,
                    out IntPtr sourceRoutingId, serviceBuffer,
                    (nuint)serviceBuffer.Length, out nuint serviceLength,
                    topicBuffer, (nuint)topicBuffer.Length, out nuint topicLength,
                    ref part, out int hasMore, flags);
                if (rc != 0)
                {
                    if (initialized)
                        NativeMethods.zlink_msg_close(ref part);
                    throw ZlinkException.CreateRecvException(
                        NativeMethods.zlink_errno());
                }

                initialized = false;
                if (nativePartCount == 0)
                {
                    routingIdBytes = CopyRoutingIdBytes(sourceRoutingId);
                    serviceName = DecodeBuffer(serviceBuffer, serviceLength);
                    topic = DecodeBuffer(topicBuffer, topicLength);
                }
                AppendNativePart(ref nativeParts, ref nativePartCount, ref part);
                if (hasMore == 0)
                    break;
            }

            return MultipartMessageCollection.FromNativeParts(nativeParts,
                nativePartCount);
        }
        catch
        {
            CloseNativeParts(nativeParts, nativePartCount);
            throw;
        }
    }

    private unsafe List<byte[]> ReceiveSpotSubscribedFrames(int flags,
        byte[] serviceBuffer, byte[] topicBuffer)
    {
        List<byte[]> frames = new();
        while (true)
        {
            ZlinkMsg part = default;
            int initRc = NativeMethods.zlink_msg_init(ref part);
            ZlinkException.ThrowIfError(initRc);
            bool initialized = true;
            int rc = NativeMethods.zlink_spot_subscribe_part(_handle,
                out _, serviceBuffer, (nuint)serviceBuffer.Length, out _,
                topicBuffer, (nuint)topicBuffer.Length, out _, ref part,
                out int hasMore, flags);
            if (rc != 0)
            {
                if (initialized)
                    NativeMethods.zlink_msg_close(ref part);
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            }

            initialized = false;
            frames.Add(CopyAndClosePart(ref part));
            if (hasMore == 0)
                return frames;
        }
    }

    private static int CopyFirstFrameAndCollectPending(IReadOnlyList<byte[]> frames,
        Span<byte> destination, out byte[][] pendingFrames)
    {
        if (frames.Count == 0)
        {
            pendingFrames = Array.Empty<byte[]>();
            return 0;
        }

        byte[] first = frames[0];
        if (first.Length > destination.Length)
        {
            throw new ArgumentException("Destination buffer is too small.",
                nameof(destination));
        }

        first.AsSpan().CopyTo(destination);
        if (frames.Count <= 1)
        {
            pendingFrames = Array.Empty<byte[]>();
            return first.Length;
        }

        pendingFrames = new byte[frames.Count - 1][];
        for (int i = 1; i < frames.Count; i++)
            pendingFrames[i - 1] = frames[i];
        return first.Length;
    }

    private static unsafe byte[] CopyAndClosePart(ref ZlinkMsg part)
    {
        try
        {
            int size = checked((int)NativeMethods.zlink_msg_size(ref part));
            if (size == 0)
                return Array.Empty<byte>();

            IntPtr data = NativeMethods.zlink_msg_data(ref part);
            if (data == IntPtr.Zero)
                return Array.Empty<byte>();

            byte[] payload = new byte[size];
            new ReadOnlySpan<byte>((void*)data, size).CopyTo(payload);
            return payload;
        }
        finally
        {
            NativeMethods.zlink_msg_close(ref part);
        }
    }

    private static ZlinkMsg MoveStoredPart(ref ZlinkMsg source)
    {
        ZlinkMsg stored = default;
        int initRc = NativeMethods.zlink_msg_init(ref stored);
        ZlinkException.ThrowIfError(initRc);
        try
        {
            int rc = NativeMethods.zlink_msg_move(ref stored, ref source);
            if (rc != 0)
                throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
            return stored;
        }
        catch
        {
            NativeMethods.zlink_msg_close(ref stored);
            throw;
        }
    }

    private static void AppendNativePart(ref ZlinkMsg[] nativeParts,
        ref int count, ref ZlinkMsg source)
    {
        if (count == nativeParts.Length)
            Array.Resize(ref nativeParts, count == 0 ? 4 : count * 2);
        nativeParts[count++] = MoveStoredPart(ref source);
    }

    private static unsafe void CloseNativeParts(ZlinkMsg[] parts, int count)
    {
        for (int i = 0; i < count; i++)
            NativeMethods.zlink_msg_close(ref parts[i]);
    }

    private static unsafe byte[]? CopyRoutingIdBytes(IntPtr routingIdPtr)
    {
        if (routingIdPtr == IntPtr.Zero)
            return null;
        return NativeHelpers.ReadRoutingId(ref *(ZlinkRoutingId*)routingIdPtr);
    }

    private static string DecodeBuffer(byte[] buffer, nuint length)
    {
        int boundedLength = length > (nuint)buffer.Length
            ? buffer.Length
            : (int)length;
        return boundedLength == 0
            ? string.Empty
            : Encoding.UTF8.GetString(buffer, 0, boundedLength);
    }

    private static T? TryReceiveCore<T>(Func<T> operation) where T : class
    {
        try
        {
            return operation();
        }
        catch (ZlinkException ex) when (ZlinkException.MapErrorCode(
            ex.InternalErrno) is ErrorCode.EAgain or ErrorCode.EBusy)
        {
            return null;
        }
    }

    private static SendResult MapSendResult(int rc)
    {
        return rc switch
        {
            0 => SendResult.Sent,
            1 => SendResult.Backpressured,
            2 => SendResult.NotReady,
            _ => throw new InvalidOperationException(
                $"Unexpected send result code '{rc}'.")
        };
    }

    private static SendResult? TryMapSendResultFromErrno()
    {
        int errno = NativeMethods.zlink_errno();
        return errno switch
        {
            ErrnoEAgain => SendResult.Backpressured,
            ErrnoEWouldBlockWin => SendResult.Backpressured,
            ErrnoENotConn => SendResult.NotReady,
            ErrnoENotConnWin => SendResult.NotReady,
            ErrnoEHostUnreach => SendResult.NotReady,
            ErrnoEHostUnreachWin => SendResult.NotReady,
            ErrnoETimedOut => SendResult.NotReady,
            ErrnoETimedOutWin => SendResult.NotReady,
            _ => null
        };
    }

    private static unsafe int CopyFirstFrameAndCollectPending(IntPtr nativeParts,
        nuint partCount, Span<byte> destination, out byte[][] pendingFrames)
    {
        int total = checked((int)partCount);
        if (total <= 0)
        {
            pendingFrames = Array.Empty<byte[]>();
            return 0;
        }

        ZlinkMsg* src = (ZlinkMsg*)nativeParts;
        IntPtr firstPtr = new IntPtr(src);
        int firstSize = checked((int)NativeMethods.zlink_msg_size(firstPtr));
        if (firstSize > destination.Length)
        {
            throw new ArgumentException("Destination buffer is too small.",
                nameof(destination));
        }

        IntPtr firstData = NativeMethods.zlink_msg_data(firstPtr);
        if (firstSize != 0 && firstData != IntPtr.Zero)
            new ReadOnlySpan<byte>((void*)firstData, firstSize).CopyTo(destination);

        if (total == 1)
        {
            pendingFrames = Array.Empty<byte[]>();
            return firstSize;
        }

        pendingFrames = new byte[total - 1][];
        for (int i = 1; i < total; i++)
        {
            IntPtr msgPtr = new IntPtr(src + i);
            int size = checked((int)NativeMethods.zlink_msg_size(msgPtr));
            if (size == 0)
            {
                pendingFrames[i - 1] = Array.Empty<byte>();
                continue;
            }

            IntPtr dataPtr = NativeMethods.zlink_msg_data(msgPtr);
            if (dataPtr == IntPtr.Zero)
            {
                pendingFrames[i - 1] = Array.Empty<byte>();
                continue;
            }

            byte[] payload = new byte[size];
            new ReadOnlySpan<byte>((void*)dataPtr, size).CopyTo(payload);
            pendingFrames[i - 1] = payload;
        }

        return firstSize;
    }

    private static void OnBorrowedBufferFree(IntPtr data, IntPtr hint)
    {
        if (hint == IntPtr.Zero)
            return;

        GCHandle.FromIntPtr(hint).Free();
    }
}
