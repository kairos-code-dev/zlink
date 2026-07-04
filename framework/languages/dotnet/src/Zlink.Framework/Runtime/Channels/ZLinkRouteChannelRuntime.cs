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
    private volatile bool _autoConnectManaged;
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
        ZLinkRouteHandlerRegistry handlers,
        IZLinkRouteInternalPacketDispatcher? internalPackets,
        CancellationToken stopToken)
    {
        _registration = registration;
        _router = router;
        _handlers = handlers;
        _internalPackets = internalPackets ?? ZLinkNoRouteInternalPacketDispatcher.Instance;
        _codecs = frameworkRegistration.Codecs;
        _stopSource = CancellationTokenSource.CreateLinkedTokenSource(stopToken);
        _taskRunner = new ZLinkRuntimeTaskRunner(new ZLinkRuntimeErrorSink(), _stopSource.Token);
        _submitter = new ZLinkAsyncSubmitter(
            router.OnSendReady,
            registration.SocketConfig.SendTimeout ?? frameworkRegistration.DefaultSocketSendTimeout,
            _stopSource.Token,
            failFastNotConnected: () => _autoConnectManaged);
        _calls = new ZLinkRouteChannelCalls(
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

        var accepted = _spotRouteBridge.Send(
            RouterChannelId,
            targetNodeRid,
            targetSpotRid,
            parts,
            SendFlags.DontWait);
        if (!accepted)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RouteNotConnected,
                $"Route channel '{RouterChannelId}' is not ready for SPOT route bridge send.");
        if (accepted) _spotRouteBridge.Drain();
        return accepted;
    }

    internal bool TryRequestViaSpotRouteBridge(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        TimeSpan timeout)
    {
        if (_spotRouteBridge is null) return false;

        var accepted = _spotRouteBridge.Request(
            RouterChannelId,
            targetNodeRid,
            targetSpotRid,
            parts,
            callback,
            SendFlags.DontWait,
            timeout);
        if (!accepted)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RouteNotConnected,
                $"Route channel '{RouterChannelId}' is not ready for SPOT route bridge request.");
        if (accepted) _spotRouteBridge.Drain();
        return accepted;
    }

    public void Start()
    {
        _receiveTask = _taskRunner.Run(
            $"route-channel:{RouterChannelId}",
            ct => new ValueTask(_receivePump.RunAsync(ct)));
    }

    /// <summary>
    /// Called by the auto-connect host when a reconcile loop manages this
    /// channel's mesh. From then on rid-addressed submits fail fast on
    /// NotConnected instead of buffering: the loop owns convergence, and a
    /// still-unconnected target is a typed, retriable condition for the
    /// caller (spot-address messaging draft §7).
    /// </summary>
    internal void MarkAutoConnectManaged()
    {
        _autoConnectManaged = true;
    }

    public void Connect(string endpoint)
    {
        _connections.Connect(endpoint);
    }

    /// <summary>Connect to a route mesh peer advertised with a routing id;
    /// the rid is assigned to the dial so rid-addressed requests reach it.</summary>
    public void Connect(RoutingId peerRid, string endpoint)
    {
        _connections.Connect(peerRid, endpoint);
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

}
