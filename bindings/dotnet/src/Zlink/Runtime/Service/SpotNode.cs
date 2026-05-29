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

internal sealed partial class SpotNode : ISpotNode
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
            return RoutingId.From(
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
            return Status().LocalEndpoint;
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

    public SpotNodeStatus Status()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_status(_handle,
            out var native);
        ZlinkException.ThrowConfigIfError(rc);
        return TopologyModelConverters.FromNative(ref native);
    }

    public SpotNodePeerEntry[] Peers()
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

    public SpotNodeSubjectEntry[] Subjects(
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

    public SpotNodeSocketEntry[] InternalSockets(
        SpotNodeSocketFilter? filter = null)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkSpotNodeSocketFilter nativeFilter = default;
            IntPtr filterPtr = IntPtr.Zero;
            if (filter != null)
            {
                SpotNodeSocketFilter value = filter;
                nativeFilter.Owner =
                    value.Owner.GetValueOrDefault(SpotNodeSocketOwner.Any);
                nativeFilter.SocketType =
                    value.SocketType.GetValueOrDefault(SpotNodeSocketType.Any);
                if (!string.IsNullOrEmpty(value.SocketName))
                {
                    BoundaryValidation.ValidateFixedUtf8(value.SocketName,
                        nameof(SpotNodeSocketFilter.SocketName));
                    WriteFixedString(value.SocketName,
                        nativeFilter.SocketName, 64);
                }
                filterPtr = (IntPtr)(&nativeFilter);
            }

            return ReadInternalSocketEntries(filterPtr);
        }
    }

    public SpotNodeSpotEntry[] Spots()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_node_spots(_handle,
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
            rc = NativeMethods.zlink_spot_node_spots(_handle,
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

    public SpotNodeActorEntry[] Actors()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_node_actors(_handle,
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
            rc = NativeMethods.zlink_spot_node_actors(_handle,
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
                CallbackExceptionHub.Report(ex);
            }
        });
    }

    private SpotNodePeerEntry[] ReadPeerEntries(IntPtr filterPtr)
    {
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_node_peers(_handle, filterPtr,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodePeerEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodePeerEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_peers(_handle, filterPtr,
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
        int rc = NativeMethods.zlink_spot_node_subjects(_handle,
            filterPtr, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeSubjectEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodeSubjectEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_subjects(_handle,
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

    private SpotNodeSocketEntry[] ReadInternalSocketEntries(
        IntPtr filterPtr)
    {
        nuint count = 0;
        int rc = NativeMethods.zlink_spot_node_internal_sockets(
            _handle, filterPtr, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeSocketEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodeSocketEntry>();
        IntPtr entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_internal_sockets(
                _handle, filterPtr, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            SpotNodeSocketEntry[] result =
                new SpotNodeSocketEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkSpotNodeSocketEntry native =
                    Marshal.PtrToStructure<ZlinkSpotNodeSocketEntry>(
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
