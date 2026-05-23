// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

public sealed class SpotNode : ISpotNode
{
    private IntPtr _handle;
    private readonly Dictionary<string, DealerSocket> _channelDealers =
        new(StringComparer.Ordinal);
    private readonly HashSet<Spot> _spots = new();
    private readonly object _spotsGate = new();
    private Action? _sendReadyHandler;
    private SynchronizationContext? _sendReadyHandlerContext;
    private NativeMethods.ZlinkSendReadyHandlerDelegate? _sendReadyHandlerNative;
    internal SpotNodeOptions Options { get; }
    internal SpotNodePublisherOptions PublisherOptions { get; }
    internal SpotNodeSubscriberOptions SubscriberOptions { get; }

    public SpotNode(Context context)
        : this(context, null)
    {
    }

    public SpotNode(Context context, SpotNodeMode mode)
        : this(context, new SpotNodeOptions { Mode = mode })
    {
    }

    internal SpotNode(Context context, SpotNodeOptions? options)
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

    public AutoHwmProfile RouterHwmProfile
    {
        get => Options.RouterHwmProfile;
        set => Options.RouterHwmProfile = value;
    }

    public int RouterHighWaterMark
    {
        get => Options.RouterHighWaterMark;
        set => Options.RouterHighWaterMark = value;
    }

    public AutoHwmProfile PubSubHwmProfile
    {
        get => Options.PubSubHwmProfile;
        set => Options.PubSubHwmProfile = value;
    }

    public int PubSubHighWaterMark
    {
        get => Options.PubSubHighWaterMark;
        set => Options.PubSubHighWaterMark = value;
    }

    public bool PublisherNoDrop
    {
        set => PublisherOptions.NoDrop = value;
    }

    public TimeSpan? PublisherSendTimeout
    {
        set => PublisherOptions.SendTimeout = value;
    }

    public int DispatchWorkersMin
    {
        get => GetAdmissionOption(SpotNodeOption.DispatchWorkersMin);
        set
        {
            if (value < 1)
                throw new ArgumentOutOfRangeException(nameof(value));
            SetAdmissionOption(SpotNodeOption.DispatchWorkersMin, value);
        }
    }

    public int DispatchWorkersMax
    {
        get => GetAdmissionOption(SpotNodeOption.DispatchWorkersMax);
        set
        {
            if (value < 1 || value < DispatchWorkersMin)
                throw new ArgumentOutOfRangeException(nameof(value));
            SetAdmissionOption(SpotNodeOption.DispatchWorkersMax, value);
        }
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

    public void SetPubBind(string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_set_pub_bind(_handle, endpoint);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void SetRouterBind(string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_set_router_bind(
            _handle, endpoint);
        ZlinkException.ThrowConfigIfError(rc);
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
        ZlinkRoutingId nativeRid = targetNodeRid.ToNative();
        int rc = NativeMethods.zlink_spot_node_disconnect_peer_rid(_handle,
            ref nativeRid);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void ConnectRouterChannelPeer(string channelName, string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_connect_router_channel_peer(
            _handle, channelName, endpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void ConnectRouterChannelPeerRid(
        string channelName,
        RoutingId peerRid,
        string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        ZlinkRoutingId nativeRid = peerRid.ToNative();
        int rc = NativeMethods.zlink_spot_node_connect_router_channel_peer_rid(
            _handle, channelName, ref nativeRid, endpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void DisconnectRouterChannelPeer(string channelName, string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_disconnect_router_channel_peer(
            _handle, channelName, endpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void DisconnectRouterChannelPeerRid(string channelName,
        RoutingId peerRid)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        ZlinkRoutingId nativeRid = peerRid.ToNative();
        int rc = NativeMethods.zlink_spot_node_disconnect_router_channel_peer_rid(
            _handle, channelName, ref nativeRid);
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

    void ISpotNode.AttachDiscovery(IDiscovery discovery)
    {
        AttachDiscovery(SocketInterop.RequireDiscovery(discovery,
            nameof(discovery)));
    }

    public void AttachSpotRouteChannelDiscovery(string channelName,
        Discovery discovery)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        if (discovery == null)
            throw new ArgumentNullException(nameof(discovery));
        int rc = NativeMethods.zlink_spot_node_attach_router_channel_discovery(
            _handle, channelName, discovery.Handle);
        ZlinkException.ThrowConfigIfError(rc);
    }

    void ISpotNode.AttachSpotRouteChannelDiscovery(string channelName,
        IDiscovery discovery)
    {
        AttachSpotRouteChannelDiscovery(channelName,
            SocketInterop.RequireDiscovery(discovery, nameof(discovery)));
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

    void ISpotNode.AttachChannelDealer(IDiscovery discovery,
        IDealerSocket dealer)
    {
        AttachChannelDealer(
            SocketInterop.RequireDiscovery(discovery, nameof(discovery)),
            SocketInterop.RequireDealerSocket(dealer, nameof(dealer)));
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

    void ISpotNode.AttachChannelDealerManual(string channelName,
        IDealerSocket dealer)
    {
        AttachChannelDealerManual(channelName,
            SocketInterop.RequireDealerSocket(dealer, nameof(dealer)));
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

    void ISpotNode.AttachPubIngress(IPubSocket pub)
    {
        AttachPubIngress(SocketInterop.RequirePubSocket(pub, nameof(pub)));
    }

    public Spot CreateSpot()
    {
        EnsureNotDisposed();
        Spot spot = new(this);
        RegisterSpot(spot);
        return spot;
    }

    ISpot ISpotNode.CreateSpot()
    {
        return CreateSpot();
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

    ISpot ISpotNode.EntrySpot()
    {
        return EntrySpot();
    }

    public Spot GetOrCreateSpot(RoutingId spotRid, out bool created)
    {
        EnsureNotDisposed();
        ZlinkRoutingId nativeRid = spotRid.ToNative();
        int rc = NativeMethods.zlink_spot_node_spot_get_or_new(_handle,
            ref nativeRid, out IntPtr spotHandle, out uint createdValue);
        ZlinkException.ThrowConfigIfError(rc);
        if (spotHandle == IntPtr.Zero)
        {
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        }

        created = createdValue != 0;
        Spot spot = new(this, spotHandle, ownsHandle: true);
        RegisterSpot(spot);
        return spot;
    }

    ISpot ISpotNode.GetOrCreateSpot(RoutingId spotRid, out bool created)
    {
        return GetOrCreateSpot(spotRid, out created);
    }

    public Spot? SpotLookup(RoutingId spotRid)
    {
        EnsureNotDisposed();
        ZlinkRoutingId nativeRid = spotRid.ToNative();
        int rc = NativeMethods.zlink_spot_node_spot_lookup(_handle,
            ref nativeRid, out IntPtr spotHandle);
        try
        {
            ZlinkException.ThrowConfigIfError(rc);
        }
        catch (ZlinkConfigException error)
            when (error.Result == ZlinkConfigException.ErrorCode.NotFound)
        {
            return null;
        }
        if (spotHandle == IntPtr.Zero)
            return null;
        Spot spot = new(this, spotHandle, ownsHandle: true);
        RegisterSpot(spot);
        return spot;
    }

    ISpot? ISpotNode.SpotLookup(RoutingId spotRid)
    {
        return SpotLookup(spotRid);
    }

    public Actor CreateActor(string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_actor_new(_handle,
            actorId, out ZlinkActorRef actor);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        return new Actor(this, ActorInterop.FromNative(ref actor));
    }

    IActor ISpotNode.CreateActor(string actorId)
    {
        return CreateActor(actorId);
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

    public SendOperation SendActorBoundSession(ActorRef actor)
    {
        EnsureNotDisposed();
        return new ActorSendBoundSessionOperation(this, actor);
    }

    public void CloseActorBoundSession(ActorRef actor, TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        ZlinkActorRef nativeActor = ActorInterop.ToNative(actor);
        int rc = NativeMethods.zlink_spot_node_actor_close_bound_session(
            _handle, ref nativeActor, ActorInterop.NormalizeTimeout(timeout));
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }

    internal static ActorRef RemoteActorRef(RoutingId targetNodeRid, string actorId)
        => ActorRef.Remote(targetNodeRid, actorId);

    public ActorLookupOperation RemoteActorGetRef(RoutingId targetNodeRid,
        string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        EnsureNotDisposed();
        return new ActorLookupOperationImpl(this, targetNodeRid, actorId);
    }

    public ActorDestroyOperation DestroyActor(ActorRef actor)
    {
        EnsureNotDisposed();
        return new ActorDestroyOperationImpl(this, actor);
    }

    internal void DestroyActor(ActorRef actor, TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        ZlinkActorRef nativeActor = ActorInterop.ToNative(actor);
        int rc = NativeMethods.zlink_spot_node_actor_destroy(_handle,
            ref nativeActor, ActorInterop.NoopReplyHandlerPtr, IntPtr.Zero,
            ActorInterop.NormalizeTimeout(timeout));
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }

    internal void DestroyRemoteActor(ActorRef actor, TimeSpan timeout = default)
    {
        DestroyActor(actor, timeout);
    }

    internal void OnSendReady(Action handler)
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

    internal Task<IReadOnlyList<Message>> JoinActor(ActorRef actor,
        RoutingId destSpotRid, Message message, TimeSpan timeout = default,
        SendFlags flags = SendFlags.None, CancellationToken ct = default)
        => JoinActor(actor, RoutingId, destSpotRid, message, timeout,
            flags, ct);

    internal Task<IReadOnlyList<Message>> JoinActor(ActorRef actor,
        RoutingId destNodeRid, RoutingId destSpotRid, Message message,
        TimeSpan timeout = default,
        CancellationToken ct = default)
        => JoinActor(actor, destNodeRid, destSpotRid, message, timeout,
            SendFlags.None, ct);

    public ActorJoinOperation JoinActor(ActorRef actor, RoutingId destNodeRid,
        RoutingId destSpotRid)
    {
        EnsureNotDisposed();
        return new ActorJoinOperationImpl(this, actor, destNodeRid,
            destSpotRid);
    }

    internal Task<IReadOnlyList<Message>> JoinActor(ActorRef actor,
        RoutingId destNodeRid, RoutingId destSpotRid, Message message,
        TimeSpan timeout, SendFlags flags, CancellationToken ct)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        ZlinkActorRef nativeActor = ActorInterop.ToNative(actor);
        ZlinkRoutingId nativeNodeRid = destNodeRid.ToNative();
        ZlinkRoutingId nativeSpotRid = destSpotRid.ToNative();
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
                ref nativeMessage, 1, ActorInterop.JoinHandlerPtr,
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

    internal bool JoinActor(ActorRef actor, RoutingId destSpotRid, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null, SendFlags flags = SendFlags.None)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            ActorInterop.AttachPartsCallback(
                () => JoinActor(actor, destSpotRid, message,
                    timeout ?? TimeSpan.Zero, flags, CancellationToken.None),
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

    internal bool JoinActor(ActorRef actor, RoutingId destNodeRid,
        RoutingId destSpotRid, Message message,
        RequestCallback callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            ActorInterop.AttachPartsCallback(
                () => JoinActor(actor, destNodeRid, destSpotRid, message,
                    timeout ?? TimeSpan.Zero, flags, CancellationToken.None),
                (result, parts) => callback(result, parts));
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    public ActorLeaveOperation LeaveActor(ActorRef actor,
        RoutingId currentSpotRid)
    {
        EnsureNotDisposed();
        return new ActorLeaveOperationImpl(this, actor, currentSpotRid);
    }

    internal void LeaveActor(ActorRef actor, RoutingId destSpotRid,
        TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        ZlinkActorRef nativeActor = ActorInterop.ToNative(actor);
        ZlinkRoutingId nativeSpotRid = destSpotRid.ToNative();
        int rc = NativeMethods.zlink_spot_node_actor_leave_spot(_handle,
            ref nativeActor, ref nativeSpotRid,
            ActorInterop.NoopReplyHandlerPtr, IntPtr.Zero,
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

    internal int GetAdmissionOption(SpotNodeOption option)
    {
        EnsureNotDisposed();
        unsafe
        {
            int value = 0;
            nuint size = (nuint)sizeof(int);
            int rc = NativeMethods.zlink_get_spot_node_option(_handle, option,
                new IntPtr(&value), ref size);
            ZlinkException.ThrowConfigIfError(rc);
            return value;
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
        return TopologyModelConverters.FromNative(ref native);
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
                    RoutingIdInterop.FromNative(ref native.SpotRid),
                    (SpotKind)native.SpotKind,
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
                    RoutingIdInterop.FromNative(ref native.CurrentSpotRid)
                        ?? throw new ZlinkConfigException(
                            ZlinkConfigException.ErrorCode.InternalError),
                    (SpotKind)native.CurrentSpotKind,
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
                result[i] = TopologyModelConverters.FromNative(ref native);
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
                result[i] = TopologyModelConverters.FromNative(ref native);
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
                result[i] = TopologyModelConverters.FromNative(ref native);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
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

public sealed partial class Spot : ISpot
{
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate RoutedReplyHandler =
        OnRoutedReply;
    private static readonly IntPtr RoutedReplyHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(RoutedReplyHandler);
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate RoutedReplyCallbackHandler =
        OnRoutedReplyCallback;
    private static readonly IntPtr RoutedReplyCallbackHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(RoutedReplyCallbackHandler);
    [ThreadStatic]
    private static RoutedPartRoutingIdCache? t_routedPartRoutingIdCache;
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
    private Action<SpotDispatchInfo>? _dispatchEventHandler;
    private Action<SpotActorLifecycleInfo>? _actorJoinHandler;
    private Action<SpotActorLifecycleInfo>? _actorLeaveHandler;
    private SynchronizationContext? _sendReadyHandlerContext;
    private SynchronizationContext? _routedReceiveHandlerContext;
    private NativeMethods.ZlinkSendReadyHandlerDelegate? _sendReadyHandlerNative;
    private NativeMethods.ZlinkSpotRequestHandlerDelegate? _routedReceiveHandlerNative;
    private NativeMethods.ZlinkSpotDispatchEventHandlerDelegate? _dispatchEventHandlerNative;
    private NativeMethods.ZlinkSpotActorLifecycleHandlerDelegate? _actorJoinHandlerNative;
    private NativeMethods.ZlinkSpotActorLifecycleHandlerDelegate? _actorLeaveHandlerNative;
    private string? _publishTopicCacheKey;
    private byte[]? _publishTopicCacheUtf8;

    internal IntPtr Handle => _handle;
    internal SpotOptions Options { get; }

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

    internal unsafe void SetSocketOption(SocketOptionKey<int> option, int value)
    {
        EnsureNotDisposed();
        int local = value;
        int rc = NativeMethods.zlink_set_option(_handle, (int)option.Option,
            (IntPtr)(&local), (nuint)sizeof(int));
        ZlinkException.ThrowConfigIfError(rc);
    }

    internal unsafe int GetSocketOption(SocketOptionKey<int> option)
    {
        EnsureNotDisposed();
        int value = 0;
        nuint size = (nuint)sizeof(int);
        int rc = NativeMethods.zlink_get_option(_handle, (int)option.Option,
            (IntPtr)(&value), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return value;
    }

    public TimeSpan? RequestTimeout
    {
        get => Options.RequestTimeout;
        set => Options.RequestTimeout = value;
    }

    public int SendHighWaterMark
    {
        get => Options.SendHighWaterMark;
        set => Options.SendHighWaterMark = value;
    }

    public int ReceiveHighWaterMark
    {
        get => Options.ReceiveHighWaterMark;
        set => Options.ReceiveHighWaterMark = value;
    }

    public int SendBufferSize
    {
        get => Options.SendBufferSize;
        set => Options.SendBufferSize = value;
    }

    public int ReceiveBufferSize
    {
        get => Options.ReceiveBufferSize;
        set => Options.ReceiveBufferSize = value;
    }

    public TimeSpan? SendTimeout
    {
        get => Options.SendTimeout;
        set => Options.SendTimeout = value;
    }

    public TimeSpan? ReceiveTimeout
    {
        get => Options.ReceiveTimeout;
        set => Options.ReceiveTimeout = value;
    }

    public TimeSpan? Linger
    {
        get => Options.Linger;
        set => Options.Linger = value;
    }

    public SendOperation Publish(string topic)
        => new SpotSendOperation(this, SpotOperationKind.Publish,
            topicOrChannel: topic);

    public SendOperation SendChannel(string channelName)
        => new SpotSendOperation(this, SpotOperationKind.SendChannel,
            topicOrChannel: channelName);

    public RequestOperation RequestChannel(string channelName)
        => new SpotRequestOperation(this, SpotOperationKind.RequestChannel,
            channelName: channelName);

    public SendOperation SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid)
        => new SpotSendOperation(this, SpotOperationKind.SendToSpot,
            destNodeRid: destNodeRid, destSpotRid: destSpotRid);

    public RequestOperation RequestToSpot(RoutingId destNodeRid,
        RoutingId destSpotRid)
        => new SpotRequestOperation(this, SpotOperationKind.RequestToSpot,
            destNodeRid: destNodeRid, destSpotRid: destSpotRid);

    public RequestOperation RequestToRouter(RoutingId peerRid)
        => new SpotRequestOperation(this, SpotOperationKind.RequestToRouter,
            peerRid: peerRid);

    public ReplyOperation ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq)
        => new SpotReplyOperation(this, SpotOperationKind.ReplyToSpot,
            destNodeRid: destNodeRid, destSpotRid: destSpotRid,
            requestSeq: requestSeq);

    public ReplyOperation ReplyToRouter(RoutingId peerRid, ulong requestSeq)
        => new SpotReplyOperation(this, SpotOperationKind.ReplyToRouter,
            peerRid: peerRid, requestSeq: requestSeq);

    public bool Publish(string topic, Message message,
        SendFlags flags = SendFlags.None)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        byte[] topicUtf8 = GetValidatedPublishTopicUtf8(topic,
            nameof(topic));
        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(PublishNoWaitSingleCore(topicUtf8,
                message));
        }

        PublishSingleCore(topicUtf8, message, (int)flags);
        return true;
    }

    internal SendResult PublishNoWaitResult(string topic, Message message)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        return PublishNoWaitSingleCore(GetValidatedPublishTopicUtf8(topic,
            nameof(topic)), message);
    }

    internal bool Publish(string topic, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        if ((flags & SendFlags.DontWait) != 0)
        {
            return SocketKernel.TrySendOrThrow(PublishNoWaitResult(topic,
                parts));
        }

        if (parts is Message[] array)
        {
            PublishPartsWithFlags(topic, array, (int)flags, nameof(parts));
            return true;
        }

        if (parts is List<Message> list)
        {
            PublishPartsWithFlags(topic, list, (int)flags, nameof(parts));
            return true;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishPartsWithFlags(topic, copied, (int)flags, nameof(parts));
        return true;
    }

    internal SendResult PublishNoWaitResult(string topic,
        IReadOnlyList<Message> parts)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateTopicId(topic, nameof(topic));
        EnsureNotDisposed();
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", nameof(parts));

        if (parts is Message[] array)
            return PublishNoWaitParts(topic, array, nameof(parts));

        if (parts is List<Message> list)
            return PublishNoWaitParts(topic, list, nameof(parts));

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return PublishNoWaitParts(topic, copied, nameof(parts));
    }

    internal bool SendChannel(string channelName, Message message,
        SendFlags flags = SendFlags.None)
    {
        ValidateChannelName(channelName, nameof(channelName));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        try
        {
            SubmitSingleChannel(channelName, message, (int)flags,
                mapNoWaitResult: false);
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    internal bool SendChannel(string channelName, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        ValidateChannelName(channelName, nameof(channelName));
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

    internal async Task<IReadOnlyList<Message>> RequestChannelAsync(string channelName,
        Message message, TimeSpan timeout = default, CancellationToken ct = default)
    {
        ValidateChannelName(channelName, nameof(channelName));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        Received received = await RequestChannelAsyncInternal(channelName,
            new[] { message }, timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal async Task<IReadOnlyList<Message>> RequestChannelAsync(string channelName,
        IReadOnlyList<Message> parts, TimeSpan timeout = default,
        CancellationToken ct = default)
    {
        ValidateChannelName(channelName, nameof(channelName));
        Received received = await RequestChannelAsyncInternal(channelName,
            parts, timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestChannel(string channelName, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
        => RequestChannel(channelName, message, callback, SendFlags.None, timeout);

    internal bool RequestChannel(string channelName, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
        => RequestChannel(channelName, parts, callback, SendFlags.None, timeout);

    internal bool RequestChannel(string channelName, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan? timeout = null)
        => RequestChannel(channelName, new[] { message }, callback, flags,
            timeout);

    internal bool RequestChannel(string channelName, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan? timeout = null)
    {
        ValidateChannelName(channelName, nameof(channelName));
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

    public SubscriptionEntry? SubscriptionAt(int index)
    {
        EnsureNotDisposed();
        return SubscriptionIntrospection.At(_handle, index);
    }

    public bool SubscribePart(Message result, Span<byte> topicBuffer,
        out int topicLength, out bool hasMore,
        RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        if (topicBuffer.IsEmpty)
            throw new ArgumentException("Topic buffer must not be empty.",
                nameof(topicBuffer));
        return SubscribePartInto(result, topicBuffer, out topicLength,
            out hasMore, (int)flags);
    }

    public bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        try
        {
            return SubscribeInto(result, (int)flags);
        }
        catch (ZlinkException ex) when ((flags & RecvFlags.DontWait) != 0
            && ZlinkException.MapErrorCode(ex.InternalErrno) is ErrorCode.EAgain
                or ErrorCode.EBusy)
        {
            return false;
        }
    }

    internal bool SubscribeNoWait(TopicMessage result)
    {
        return Subscribe(result, RecvFlags.DontWait);
    }

    public bool ReceiveSubscriptionEvent(SubscriptionEvent result,
        RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        return ReceiveSubscriptionEventInto(result, (int)flags);
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

    internal void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq, Message message, SendFlags flags = SendFlags.None)
        => ReplyToSpot(destNodeRid, destSpotRid, requestSeq, new[] { message },
            flags);

    internal Task<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId destNodeRid, RoutingId destSpotRid, Message message,
        TimeSpan timeout = default, CancellationToken ct = default)
        => RequestToSpotAsync(destNodeRid, destSpotRid, new[] { message },
            timeout, ct);

    internal async Task<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId destNodeRid, RoutingId destSpotRid, IReadOnlyList<Message> parts,
        TimeSpan timeout = default, CancellationToken ct = default)
    {
        Received received = await RequestToSpotAsyncInternal(destNodeRid,
            destSpotRid, parts, timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
        => RequestToSpot(destNodeRid, destSpotRid, new[] { message }, callback,
            flags, timeout);

    internal bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            return RequestToSpotCallbackInternal(destNodeRid, destSpotRid, parts,
                callback, flags, timeout ?? TimeSpan.Zero);
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
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        try
        {
            int rc = SubmitCopiedSpotSendSingle(ref nodeRid, ref spotRid,
                message, (int)flags);
            if (rc == 0)
                return true;
            throw ZlinkException.CreateSubmitException(NativeMethods.zlink_errno());
        }
        catch (ZlinkException ex) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(ex)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    internal unsafe bool SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_spot_send_spot_part(_handle,
                        ref nodeRid, ref spotRid, ref nativePart, (int)flags,
                        partFlag));
            return true;
        }
        catch (ZlinkException ex) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(ex)
                == SendResult.Backpressured)
        {
            RequestReplySupport.DisposeParts(cloned);
            return false;
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private unsafe int SubmitCopiedSpotSendSingle(
        ref ZlinkRoutingId nodeRid, ref ZlinkRoutingId spotRid, Message source,
        int flags)
    {
        ZlinkMsg nativePart = default;
        bool submitted = false;
        source.CopyTo(ref nativePart);
        try
        {
            int rc = NativeMethods.zlink_spot_send_spot_part(_handle,
                ref nodeRid, ref spotRid, ref nativePart, flags,
                NativeMethods.ZlinkPartFlag.Final);
            submitted = true;
            return rc;
        }
        finally
        {
            if (!submitted)
                NativeMethods.zlink_msg_close(ref nativePart);
        }
    }

    internal unsafe void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        _ = flags;
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_spot_reply_spot_part(_handle,
                        ref nodeRid, ref spotRid, requestSeq, ref nativePart,
                        partFlag));
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    internal void ReplyToRouter(RoutingId peerRid, ulong requestSeq,
        Message message, SendFlags flags = SendFlags.None)
        => ReplyToRouter(peerRid, requestSeq, new[] { message }, flags);

    internal Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
        Message message, TimeSpan timeout = default,
        CancellationToken ct = default)
        => RequestToRouterAsync(peerRid, new[] { message }, timeout, ct);

    internal async Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout = default,
        CancellationToken ct = default)
    {
        Received received = await RequestToRouterAsyncInternal(peerRid, parts,
            timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestToRouter(RoutingId peerRid, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
        => RequestToRouter(peerRid, new[] { message }, callback, flags,
            timeout);

    internal bool RequestToRouter(RoutingId peerRid, IReadOnlyList<Message> parts,
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

    internal unsafe void ReplyToRouter(RoutingId peerRid, ulong requestSeq,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        _ = flags;
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId routingId = peerRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_spot_reply_router_part(_handle,
                        ref routingId, requestSeq, ref nativePart, partFlag));
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    public unsafe bool RecvRoutedPart(Message result, out RoutingId? routingId,
        out RoutingId? spotRid, out ulong? requestSeq, out bool hasMore,
        RecvFlags flags = RecvFlags.None)
    {
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        EnsureNotDisposed();

        routingId = null;
        spotRid = null;
        requestSeq = null;
        hasMore = false;

        ZlinkMsg part = default;
        int initRc = NativeMethods.zlink_msg_init(ref part);
        if (initRc != 0)
            throw ZlinkException.CreateRecvException(
                NativeMethods.zlink_errno());

        bool initialized = true;
        try
        {
            int rc = NativeMethods.zlink_spot_recv_part(_handle,
                out IntPtr sourceRoutingId, out IntPtr sourceSpotRid,
                out ulong nativeRequestSeq, ref part, out int more,
                (int)flags);
            if (rc != 0)
            {
                NativeMethods.zlink_msg_close(ref part);
                initialized = false;
                int errno = NativeMethods.zlink_errno();
                if ((flags & RecvFlags.DontWait) != 0
                    && ZlinkException.MapErrorCode(errno) is ErrorCode.EAgain
                        or ErrorCode.EBusy)
                {
                    return false;
                }

                throw ZlinkException.CreateRecvException(errno);
            }

            initialized = false;
            result.ReplaceNativeOwned(ref part);
            RoutedPartRoutingIdCache cache =
                t_routedPartRoutingIdCache ??= new RoutedPartRoutingIdCache();
            routingId = cache.NodeFromPointer(sourceRoutingId);
            spotRid = cache.SpotFromPointer(sourceSpotRid);
            requestSeq = nativeRequestSeq == 0 ? null : nativeRequestSeq;
            hasMore = more != 0;
            return true;
        }
        catch
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref part);
            throw;
        }
    }

    private sealed unsafe class RoutedPartRoutingIdCache
    {
        private byte[]? _nodeBytes;
        private RoutingId? _nodeRoutingId;
        private byte[]? _spotBytes;
        private RoutingId? _spotRoutingId;

        internal RoutingId? NodeFromPointer(IntPtr routingIdPtr)
            => FromPointer(routingIdPtr, ref _nodeBytes, ref _nodeRoutingId);

        internal RoutingId? SpotFromPointer(IntPtr routingIdPtr)
            => FromPointer(routingIdPtr, ref _spotBytes, ref _spotRoutingId);

        private static RoutingId? FromPointer(IntPtr routingIdPtr,
            ref byte[]? cachedBytes, ref RoutingId? cachedRoutingId)
        {
            if (routingIdPtr == IntPtr.Zero)
                return null;

            ref ZlinkRoutingId native =
                ref *(ZlinkRoutingId*)routingIdPtr;
            int size = native.Size;
            if (size <= 0)
                return null;

            fixed (byte* source = native.Data)
            {
                byte[]? cached = cachedBytes;
                if (cached != null && cached.Length == size)
                {
                    bool same = true;
                    for (int i = 0; i < size; i++)
                    {
                        if (cached[i] != source[i])
                        {
                            same = false;
                            break;
                        }
                    }
                    if (same)
                        return cachedRoutingId;
                }

                byte[] bytes = new byte[size];
                new ReadOnlySpan<byte>(source, size).CopyTo(bytes);
                RoutingId? routingId = RoutingId.FromOwnedOptionalBytes(bytes);
                cachedBytes = bytes;
                cachedRoutingId = routingId;
                return routingId;
            }
        }
    }

    public unsafe bool RecvRouted(Received result,
        RecvFlags flags = RecvFlags.None)
    {
        if (result == null)
            throw new ArgumentNullException(nameof(result));
        MultipartMessageCollection? parts;
        byte[]? nodeRidBytes;
        byte[]? spotRidBytes;
        ulong requestSeq;
        try
        {
            parts = ReceiveSpotRoutedParts((int)flags, out nodeRidBytes,
                out spotRidBytes, out requestSeq,
                allowNoData: (flags & RecvFlags.DontWait) != 0);
        }
        catch (ZlinkException ex) when ((flags & RecvFlags.DontWait) != 0
            && ZlinkException.MapErrorCode(ex.InternalErrno) is ErrorCode.EAgain
                or ErrorCode.EBusy)
        {
            return false;
        }
        if (parts == null)
            return false;
        RoutingId? nodeRid = nodeRidBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(nodeRidBytes);
        RoutingId? spotRid = spotRidBytes == null
            ? null
            : RoutingId.FromOwnedOptionalBytes(spotRidBytes);
        RoutingIdSnapshot nodeRidSnapshot = RoutingIdSnapshot.FromBytes(nodeRidBytes);
        RoutingIdSnapshot spotRidSnapshot = RoutingIdSnapshot.FromBytes(spotRidBytes);
        ReceivedReplyHandler? replyHandler = requestSeq == 0
            ? null
            : CreateRoutedReplyHandler(nodeRid, spotRid, requestSeq);
        result.PopulateRoutedMultipart(parts, nodeRidSnapshot, spotRidSnapshot,
            requestSeq == 0 ? null : requestSeq, replyHandler,
            CreateRoutedSendHandler(nodeRid, spotRid));
        return true;
    }

    public ActorJoinRequest? RecvActorJoin(RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        IntPtr parts = IntPtr.Zero;
        nuint partCount = 0;
        int rc = NativeMethods.zlink_spot_actor_join_recv(_handle,
            out ZlinkActorJoinInfo nativeInfo, out parts, out partCount,
            (int)flags);
        if (rc != 0)
        {
            int errno = NativeMethods.zlink_errno();
            if ((flags & RecvFlags.DontWait) != 0
                && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain)
                return null;
            throw ZlinkException.CreateRecvException(errno);
        }

        ActorJoinInfo info = ActorInterop.FromNative(ref nativeInfo);
        Message[] messages = Message.FromNativeVector(parts, partCount);
        NativeMethods.zlink_multipart_close(parts, partCount);
        return new ActorJoinRequest(info, messages, nativeInfo);
    }

    public ActorJoinReplyOperation ReplyActorJoin(ActorJoinRequest request,
        bool accepted)
    {
        if (request == null)
            throw new ArgumentNullException(nameof(request));
        return new ActorJoinReplyOperationImpl(this, request, accepted);
    }

    internal void ReplyActorJoinInternal(ActorJoinRequest request, bool accepted,
        IReadOnlyList<Message> parts)
    {
        if (request == null)
            throw new ArgumentNullException(nameof(request));
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        EnsureNotDisposed();
        ZlinkActorJoinInfo nativeInfo = request.RuntimeState is ZlinkActorJoinInfo value
            ? value
            : throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidArgument);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        ZlinkMsg[] nativeParts = new ZlinkMsg[cloned.Length];
        bool[] submitted = new bool[cloned.Length];
        try
        {
            for (int i = 0; i < cloned.Length; i++)
                cloned[i].MoveTo(ref nativeParts[i]);

            int rc = nativeParts.Length == 0
                ? NativeMethods.zlink_spot_actor_join_reply_empty(_handle,
                    ref nativeInfo, accepted ? 1u : 0u, IntPtr.Zero, 0)
                : NativeMethods.zlink_spot_actor_join_reply(_handle,
                    ref nativeInfo, accepted ? 1u : 0u, ref nativeParts[0],
                    (nuint)nativeParts.Length);
            Array.Fill(submitted, true);
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
        }
        finally
        {
            for (int i = 0; i < nativeParts.Length; i++)
            {
                if (!submitted[i])
                    NativeMethods.zlink_msg_close(ref nativeParts[i]);
            }
            foreach (Message message in cloned)
                message.Dispose();
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

    public unsafe void OnDispatchEvent(Action<SpotDispatchInfo> handler)
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

    public unsafe void OnActorLifecycle(
        Action<SpotActorLifecycleInfo>? onJoin,
        Action<SpotActorLifecycleInfo>? onLeave)
    {
        EnsureNotDisposed();
        _actorJoinHandler = onJoin;
        _actorLeaveHandler = onLeave;
        _actorJoinHandlerNative = onJoin == null ? null : OnNativeActorJoin;
        _actorLeaveHandlerNative = onLeave == null ? null : OnNativeActorLeave;
        int rc = NativeMethods.zlink_spot_actor_lifecycle_handler(
            _handle,
            _actorJoinHandlerNative,
            _actorLeaveHandlerNative,
            IntPtr.Zero);
        if (rc != 0)
            throw ZlinkException.CreateHandlerException(NativeMethods.zlink_errno());
    }

    internal void DrainChannelReplyFrom(IntPtr dealerSubject)
    {
        EnsureNotDisposed();
        if (dealerSubject == IntPtr.Zero)
            throw new ArgumentException("dealerSubject must not be null.",
                nameof(dealerSubject));
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
        _actorJoinHandler = null;
        _actorLeaveHandler = null;
        _sendReadyHandlerContext = null;
        _routedReceiveHandlerContext = null;
        _sendReadyHandlerNative = null;
        _routedReceiveHandlerNative = null;
        _dispatchEventHandlerNative = null;
        _actorJoinHandlerNative = null;
        _actorLeaveHandlerNative = null;
        _node.UnregisterSpot(this);
    }

    private void PublishSingleCore(string topic, Message message,
        int flags)
    {
        PublishSingleCore(GetPublishTopicUtf8(topic), message, flags);
    }

    private unsafe void PublishSingleCore(byte[] topicUtf8, Message message,
        int flags)
    {
        ZlinkMsg nativePart = default;
        bool submitted = false;
        try
        {
            message.MoveTo(ref nativePart);
            fixed (byte* topicPtr = topicUtf8)
            {
                int rc = NativeMethods.zlink_spot_publish_part_utf8(_handle,
                    topicPtr, ref nativePart, (int)flags,
                    NativeMethods.ZlinkPartFlag.Final);
                submitted = true;
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
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
                        RoutedReplyHandlerPtr,
                        GCHandle.ToIntPtr(handle),
                        flags,
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final,
                        timeoutMs);
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
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        return RequestRoutedAsyncInternal(parts, timeout, ct, flags,
            (ref ZlinkMsg nativePart, IntPtr handler, IntPtr userData,
                NativeMethods.ZlinkPartFlag partFlag, uint timeoutMs) =>
                NativeMethods.zlink_spot_request_spot_part(_handle,
                    ref nodeRid, ref spotRid, ref nativePart, handler, userData,
                    flags, partFlag, timeoutMs));
    }

    private unsafe bool RequestToSpotCallbackInternal(
        RoutingId destNodeRid, RoutingId destSpotRid, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback, SendFlags flags,
        TimeSpan timeout)
    {
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        uint timeoutMs = NormalizeTimeout(timeout);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        GCHandle handle = default;
        SpotRequestCallbackState? state = null;

        try
        {
            state = new SpotRequestCallbackState(callback,
                RequestProgressPump.AttachSpotCallback(_handle));
            handle = GCHandle.Alloc(state, GCHandleType.Normal);

            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_spot_request_spot_part(_handle,
                        ref nodeRid, ref spotRid, ref nativePart,
                        RoutedReplyCallbackHandlerPtr, GCHandle.ToIntPtr(handle),
                        (int)flags,
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final,
                        timeoutMs);
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
        catch
        {
            state?.DisposeProgress();
            if (handle.IsAllocated)
                handle.Free();
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private unsafe Task<Received> RequestToRouterAsyncInternal(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nativePeerRid = peerRid.ToNative();
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
                        RoutedReplyHandlerPtr,
                        GCHandle.ToIntPtr(handle),
                        i + 1 < cloned.Length
                            ? NativeMethods.ZlinkPartFlag.More
                            : NativeMethods.ZlinkPartFlag.Final,
                        timeoutMs);
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

    private sealed class SpotRequestCallbackState
    {
        private readonly Action<RequestResult, IReadOnlyList<Message>> _callback;
        private readonly RequestProgressPump.ProgressLease _progress;
        private int _completed;

        internal SpotRequestCallbackState(
            Action<RequestResult, IReadOnlyList<Message>> callback,
            RequestProgressPump.ProgressLease progress)
        {
            _callback = callback;
            _progress = progress;
        }

        internal bool TryStartCompletion()
        {
            if (Interlocked.Exchange(ref _completed, 1) != 0)
                return false;
            DisposeProgress();
            return true;
        }

        internal void Invoke(RequestResult result,
            IReadOnlyList<Message> payload)
        {
            try
            {
                _callback(result, payload);
            }
            catch (Exception ex)
            {
                Runtime.ReportUnhandledCallbackException(ex);
            }
        }

        internal void DisposeProgress()
        {
            _progress.Dispose();
        }
    }

    private static void OnRoutedReplyCallback(int result, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        SpotRequestCallbackState state =
            (SpotRequestCallbackState)handle.Target!;
        try
        {
            if (!state.TryStartCompletion())
                return;

            if (result != 0)
            {
                state.Invoke((RequestResult)result, Array.Empty<Message>());
                return;
            }

            Message[] replyParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            state.Invoke(RequestResult.Ok, replyParts);
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            handle.Free();
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
        return BoundaryValidation.EncodeTimeoutMilliseconds(timeout,
            nameof(timeout));
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
        BoundaryValidation.ValidateTopicOrFilterUtf8(value, paramName,
            allowEmpty: false);
    }

    private byte[] GetPublishTopicUtf8(string topic)
    {
        byte[]? cached = _publishTopicCacheUtf8;
        string? cachedKey = _publishTopicCacheKey;
        if (cached != null
            && (ReferenceEquals(cachedKey, topic)
                || string.Equals(cachedKey, topic, StringComparison.Ordinal)))
        {
            return cached;
        }

        byte[] encoded = PublishTopicEncoding.GetNullTerminatedUtf8(topic);
        _publishTopicCacheKey = topic;
        _publishTopicCacheUtf8 = encoded;
        return encoded;
    }

    private byte[] GetValidatedPublishTopicUtf8(string topic, string paramName)
    {
        byte[]? cached = _publishTopicCacheUtf8;
        string? cachedKey = _publishTopicCacheKey;
        if (cached != null
            && (ReferenceEquals(cachedKey, topic)
                || string.Equals(cachedKey, topic, StringComparison.Ordinal)))
        {
            return cached;
        }

        ValidateTopicId(topic, paramName);
        return GetPublishTopicUtf8(topic);
    }

    private static void ValidateChannelName(string value, string paramName)
    {
        BoundaryValidation.ValidateFixedUtf8(value, paramName);
    }

    private void PublishPartsWithFlags(string topic, IReadOnlyList<Message> parts,
        int flags, string paramName)
    {
        if (parts is Message[] array)
        {
            PublishCore(topic, array.AsSpan(), flags, paramName);
            return;
        }

        if (parts is List<Message> list)
        {
            PublishCore(topic, CollectionsMarshal.AsSpan(list), flags,
                paramName);
            return;
        }

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        PublishCore(topic, copied.AsSpan(), flags, paramName);
    }

    private SendResult PublishNoWaitParts(string topic,
        IReadOnlyList<Message> parts, string paramName)
    {
        if (parts is Message[] array)
            return PublishNoWaitCore(topic, array.AsSpan(), paramName);

        if (parts is List<Message> list)
            return PublishNoWaitCore(topic, CollectionsMarshal.AsSpan(list),
                paramName);

        Message[] copied = new Message[parts.Count];
        for (int i = 0; i < copied.Length; i++)
            copied[i] = parts[i];
        return PublishNoWaitCore(topic, copied.AsSpan(), paramName);
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

}
