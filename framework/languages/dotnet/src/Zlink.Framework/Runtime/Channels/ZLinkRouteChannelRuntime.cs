using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Codecs;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteChannelRuntime : IAsyncDisposable
{
    private readonly ZLinkRouteChannelRegistration _registration;
    private readonly IZLinkBackendRouterSocket _router;
    private readonly ZLinkAsyncSubmitter _submitter;
    private readonly ZLinkRouteConnectionSet _connections;
    private readonly ZLinkRouteReceivePump _receivePump;
    private readonly CancellationTokenSource _stopSource;
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private readonly IZLinkBackendDiscovery? _discovery;
    private readonly ZLinkCodecRegistryBuilder _codecs;
    private readonly ZLinkRouteChannelCalls _calls;
    private readonly ZLinkRouteSpotChannelCalls _spotRouteCalls;
    private IZLinkBackendSpotRouteBridge? _spotRouteBridge;
    private Task? _receiveTask;

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
        _discovery = discovery;
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
            _submitter,
            () => _spotRouteBridge);
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
                internalPackets ?? ZLinkNoRouteInternalPacketDispatcher.Instance,
                new ZLinkDispatchErrorReporter(
                    frameworkRegistration.DispatchOptions,
                    services,
                    services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkDispatchErrorReporter>()),
                services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkRoutePacketDispatcher>()));
    }

    public string RouterChannelId => _registration.RouterChannelId;

    // route mesh 의 serving socket(weight 적용 대상). server·client 가 공유하는 단일 ROUTER.
    internal IZLinkBackendWeightedSocket ServingSocket => _router;

    public IZLinkBackendDiscovery? Discovery => _discovery;

    internal bool HasSpotRouteBridge => _spotRouteBridge is not null;

    public void AttachSpotRouteBridge(IZLinkBackendSpotRouteBridge bridge)
    {
        if (_spotRouteBridge is not null)
        {
            throw new ZLinkConfigurationException(
                $"Route channel '{RouterChannelId}' is already attached to a SPOT route bridge.");
        }

        bridge.AttachRouterChannel(
            RouterChannelId,
            _router,
            new SpotRouteBridgeEndpointOptions
            {
                Capabilities = SpotRouteBridgeEndpointCapabilities.RouteWithChannelInbound
            });
        _spotRouteBridge = bridge;
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

    public async ValueTask DisposeAsync()
    {
        _stopSource.Cancel();
        if (_receiveTask is not null)
        {
            try
            {
                await _receiveTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
            catch (ObjectDisposedException)
            {
            }
        }

        await _submitter.DisposeAsync();

        if (_discovery is not null)
        {
            await _discovery.DisposeAsync();
        }

        if (_spotRouteBridge is not null)
        {
            await _spotRouteBridge.DisposeAsync();
        }

        await _router.DisposeAsync();
        _stopSource.Dispose();
    }
}
