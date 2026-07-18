using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteChannelRuntime : IAsyncDisposable
{
    private readonly ZLinkRouteChannelCalls _calls;
    private readonly ZLinkRouteConnectionSet _connections;
    private readonly ZLinkRouteReceivePump _receivePump;
    private readonly ZLinkRouteChannelRegistration _registration;
    private readonly IZLinkBackendRouterSocket _router;
    private readonly CancellationTokenSource _stopSource;
    private readonly ZLinkAsyncSubmitter _submitter;
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private readonly object _disposeGate = new();
    private Task? _disposeTask;

    public ZLinkRouteChannelRuntime(
        IServiceProvider services,
        ZLinkFrameworkRegistration frameworkRegistration,
        ZLinkRouteChannelRegistration registration,
        IZLinkBackendRouterSocket router,
        ZLinkRouteHandlerRegistry handlers,
        IZLinkRouteInternalPacketDispatcher? internalPackets,
        CancellationToken stopToken,
        object executionOwner,
        ZLinkFrameworkRuntime? frameworkRuntime = null,
        IZLinkRuntimeErrorSink? errorSink = null)
    {
        _registration = registration;
        _router = router;
        var internalPacketDispatcher = internalPackets ?? ZLinkNoRouteInternalPacketDispatcher.Instance;
        var codecs = frameworkRegistration.Codecs;
        _stopSource = CancellationTokenSource.CreateLinkedTokenSource(stopToken);
        errorSink ??= frameworkRuntime?.ErrorSink
                      ?? throw new InvalidOperationException("Route channel runtime requires a runtime error sink.");
        _taskRunner = new ZLinkRuntimeTaskRunner(
            errorSink,
            _stopSource.Token,
            executionOwner);
        _submitter = new ZLinkAsyncSubmitter(
            router.OnSendReady,
            registration.SocketConfig.SendTimeout ?? frameworkRegistration.DefaultSocketSendTimeout,
            _stopSource.Token);
        _calls = new ZLinkRouteChannelCalls(
            services,
            frameworkRuntime,
            frameworkRegistration,
            registration.RouterChannelId,
            router,
            _submitter);
        _connections = new ZLinkRouteConnectionSet(router);
        _receivePump = new ZLinkRouteReceivePump(
            router,
            new ZLinkRoutePacketDispatcher(
                registration.RouterChannelId,
                router,
                handlers,
                new ZLinkRouteHandlerInvoker(services, codecs),
                codecs,
                internalPacketDispatcher,
                new ZLinkDispatchErrorReporter(
                    frameworkRegistration.DispatchOptions,
                    ZLinkMessageFlowTracer.CreateLogger(services.GetService<ILoggerFactory>()),
                    frameworkRuntime),
                frameworkRuntime,
                services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkRoutePacketDispatcher>()),
            errorSink);
    }

    public string RouterChannelId => _registration.RouterChannelId;

    // route mesh 의 serving socket(weight 적용 대상). server·client 가 공유하는 단일 ROUTER.
    internal IZLinkBackendWeightedSocket ServingSocket => _router;

    internal void RequestStop() => _stopSource.Cancel();

    public ValueTask DisposeAsync()
    {
        lock (_disposeGate)
            return new ValueTask(_disposeTask ??= DisposeCoreAsync());
    }

    private async Task DisposeCoreAsync()
    {
        var failures = new List<Exception>();
        Capture(RequestStop);
        await CaptureAsync(_taskRunner.StopAsync).ConfigureAwait(false);
        await CaptureAsync(_submitter.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(_router.DisposeAsync).ConfigureAwait(false);
        Capture(_stopSource.Dispose);
        if (failures.Count == 1)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures.Count > 1) throw new AggregateException(failures);
        return;

        async ValueTask CaptureAsync(Func<ValueTask> cleanup)
        {
            try
            {
                await cleanup().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }

        void Capture(Action cleanup)
        {
            try
            {
                cleanup();
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }
    }

    public void Start()
    {
        _taskRunner.RunDetached(
            $"route-channel:{RouterChannelId}",
            ct => new ValueTask(_receivePump.RunAsync(ct)));
    }

    public void ConnectManual(string endpoint)
    {
        _connections.ConnectManual(endpoint);
    }

    /// <summary>Connect to a route mesh peer advertised with a routing id;
    /// the rid is assigned to the dial so rid-addressed requests reach it.</summary>
    public void ConnectManual(RoutingId peerRid, string endpoint)
    {
        _connections.ConnectManual(peerRid, endpoint);
    }

    public void DisconnectManual(string endpoint)
    {
        _connections.DisconnectManual(endpoint);
    }

    public bool ConnectAuto(RoutingId? peerRid, string endpoint)
        => _connections.ConnectAuto(peerRid, endpoint);

    public bool DisconnectAuto(string endpoint)
        => _connections.DisconnectAuto(endpoint);

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

    public ValueTask<TReply> RequestAsync<TRequest, TReply>(
        RoutingId targetNodeRid,
        string packetName,
        TRequest request,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return _calls.RequestAsync<TRequest, TReply>(
            targetNodeRid,
            packetName,
            request,
            timeout,
            cancellationToken);
    }

    public ValueTask<TReply> RequestPartsAsync<TReply>(
        RoutingId targetNodeRid,
        ZLinkEnvelopeHeader header,
        IReadOnlyList<Message> payloadParts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return _calls.RequestPartsAsync<TReply>(
            targetNodeRid,
            header,
            payloadParts,
            timeout,
            cancellationToken);
    }

}
