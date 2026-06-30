using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteChannelRuntime : IAsyncDisposable
{
    private readonly ZLinkRouteChannelCalls _calls;
    private readonly ZLinkCodecRegistryBuilder _codecs;
    private readonly ZLinkRouteConnectionSet _connections;
    private readonly ZLinkRouteHandlerRegistry _handlers;
    private readonly IZLinkRouteInternalPacketDispatcher _internalPackets;
    private readonly ZLinkRouteReceivePump _receivePump;
    private readonly ZLinkRouteChannelRegistration _registration;
    private readonly IZLinkBackendRouterSocket _router;
    private readonly ZLinkRouteSpotChannelCalls _spotRouteCalls;
    private readonly CancellationTokenSource _stopSource;
    private readonly ZLinkAsyncSubmitter _submitter;
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private Task? _receiveTask;
    private IZLinkBackendSpotRouteBridge? _spotRouteBridge;
    private ZLinkSpotNodeRuntime? _spotRouteBridgeOwner;

    public ZLinkRouteChannelRuntime(
        IServiceProvider services,
        ZLinkFrameworkRegistration frameworkRegistration,
        ZLinkRouteChannelRegistration registration,
        IZLinkBackendRouterSocket router,
        IZLinkBackendDiscovery? discovery,
        ZLinkRouteHandlerRegistry handlers,
        IZLinkRouteInternalPacketDispatcher? internalPackets,
        CancellationToken stopToken)
    {
        _registration = registration;
        _router = router;
        Discovery = discovery;
        _handlers = handlers;
        _internalPackets = internalPackets ?? ZLinkNoRouteInternalPacketDispatcher.Instance;
        _codecs = frameworkRegistration.Codecs;
        _stopSource = CancellationTokenSource.CreateLinkedTokenSource(stopToken);
        _taskRunner = new ZLinkRuntimeTaskRunner(new ZLinkRuntimeErrorSink(), _stopSource.Token);
        _submitter = new ZLinkAsyncSubmitter(
            router.OnSendReady,
            registration.SocketConfig.SendTimeout ?? frameworkRegistration.DefaultSocketSendTimeout,
            _stopSource.Token);
        _calls = new ZLinkRouteChannelCalls(
            services,
            frameworkRegistration,
            registration.RouterChannelId,
            router,
            _submitter);
        _spotRouteCalls = new ZLinkRouteSpotChannelCalls(
            services,
            frameworkRegistration,
            registration.RouterChannelId,
            router,
            _submitter);
        _connections = new ZLinkRouteConnectionSet(router);
        _receivePump = new ZLinkRouteReceivePump(
            router,
            () => _spotRouteBridge,
            registration.RouterChannelId,
            new ZLinkRoutePacketDispatcher(
                registration.RouterChannelId,
                router,
                handlers,
                new ZLinkRouteHandlerInvoker(services, _codecs),
                _codecs,
                _internalPackets,
                new ZLinkDispatchErrorReporter(
                    frameworkRegistration.DispatchOptions,
                    services,
                    services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkDispatchErrorReporter>()),
                services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkRoutePacketDispatcher>()));
    }

    public string RouterChannelId => _registration.RouterChannelId;

    // route mesh 의 serving socket(weight 적용 대상). server·client 가 공유하는 단일 ROUTER.
    internal IZLinkBackendWeightedSocket ServingSocket => _router;

    public IZLinkBackendDiscovery? Discovery { get; }

    internal bool HasSpotRouteBridge => _spotRouteBridge is not null;

    public async ValueTask DisposeAsync()
    {
        _stopSource.Cancel();
        if (_receiveTask is not null)
            try
            {
                await _receiveTask.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
            catch (ObjectDisposedException)
            {
            }
            catch (TimeoutException)
            {
            }

        await _submitter.DisposeAsync();

        if (Discovery is not null) await Discovery.DisposeAsync();

        if (_spotRouteBridge is not null) await _spotRouteBridge.DisposeAsync();

        await _router.DisposeAsync();
        _stopSource.Dispose();
    }

    internal bool CanDispatchRoutePacket(
        ZLinkMessageKind kind,
        string packetName)
    {
        return kind switch
        {
            ZLinkMessageKind.Command => _internalPackets.CanHandleSend(packetName)
                                        || _handlers.TryGet(RouterChannelId, kind, packetName, out _),
            ZLinkMessageKind.Request => _internalPackets.CanHandleRequest(packetName)
                                        || _handlers.TryGet(RouterChannelId, kind, packetName, out _),
            _ => false
        };
    }

    internal bool HasKnownRoutePeer(RoutingId targetNodeRid)
    {
        if (Discovery is null) return false;

        return Discovery.MemberPeers()
            .Any(peer => peer.RoutingId == targetNodeRid);
    }

    public void AttachSpotRouteBridge(
        IZLinkBackendSpotRouteBridge bridge,
        ZLinkSpotNodeRuntime owner)
    {
        if (_spotRouteBridge is not null)
            throw new ZLinkConfigurationException(
                $"Route channel '{RouterChannelId}' is already attached to a SPOT route bridge.");

        bridge.AttachRouterChannel(
            RouterChannelId,
            _router);
        _spotRouteBridge = bridge;
        _spotRouteBridgeOwner = owner;
    }

    internal bool TrySendViaSpotRouteBridge(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts)
    {
        if (_spotRouteBridge is null) return false;

        return _spotRouteBridge.Send(
            RouterChannelId,
            targetNodeRid,
            targetSpotRid,
            parts,
            SendFlags.None);
    }

    internal bool TryRequestViaSpotRouteBridge(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        TimeSpan timeout)
    {
        if (_spotRouteBridge is null) return false;

        return _spotRouteBridge.Request(
            RouterChannelId,
            targetNodeRid,
            targetSpotRid,
            parts,
            callback,
            SendFlags.None,
            timeout);
    }

    public void Start()
    {
        _receiveTask = _taskRunner.Run(
            $"route-channel:{RouterChannelId}",
            ct => new ValueTask(_receivePump.RunAsync(ct)));
    }

    public void Connect(string endpoint)
    {
        _connections.Connect(endpoint);
    }

    public void Disconnect(string endpoint)
    {
        _connections.Disconnect(endpoint);
    }

    public IReadOnlyList<string> ListConnections()
    {
        return _connections.List();
    }

    public ValueTask SubmitSendAsync<TMessage>(
        RoutingId targetNodeRid,
        string packetName,
        TMessage message,
        CancellationToken cancellationToken)
    {
        return _calls.SubmitSendAsync(
            targetNodeRid,
            packetName,
            message,
            cancellationToken);
    }

    public ValueTask SubmitSendPartsAsync(
        RoutingId targetNodeRid,
        ZLinkEnvelopeHeader header,
        IReadOnlyList<Message> payloadParts,
        CancellationToken cancellationToken)
    {
        return _calls.SubmitSendPartsAsync(
            targetNodeRid,
            header,
            payloadParts,
            cancellationToken);
    }

    public async ValueTask<TReply> RequestAsync<TRequest, TReply>(
        RoutingId targetNodeRid,
        string packetName,
        TRequest request,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await _calls.RequestAsync<TRequest, TReply>(
                targetNodeRid,
                packetName,
                request,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<TReply> RequestPartsAsync<TReply>(
        RoutingId targetNodeRid,
        ZLinkEnvelopeHeader header,
        IReadOnlyList<Message> payloadParts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await _calls.RequestPartsAsync<TReply>(
                targetNodeRid,
                header,
                payloadParts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask SubmitSpotRouteSendPartsAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return _spotRouteCalls.SubmitSendPartsAsync(
            targetNodeRid,
            targetSpotRid,
            parts,
            cancellationToken);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestToSpotPartsAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await _spotRouteCalls.RequestPartsAsync(
                targetNodeRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }
}