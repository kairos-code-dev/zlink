using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteChannelRuntime : IAsyncDisposable
{
    private readonly ZLinkRouteChannelRegistration _registration;
    private readonly IZLinkBackendRouterSocket _router;
    private readonly ZLinkAsyncSubmitter _submitter;
    private readonly ZLinkRouteConnectionSet _connections;
    private readonly ZLinkRouteReceivePump _receivePump;
    private readonly CancellationTokenSource _stopSource;
    private readonly IZLinkBackendDiscovery? _discovery;
    private Task? _receiveTask;

    public ZLinkRouteChannelRuntime(
        IServiceProvider services,
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
        _stopSource = CancellationTokenSource.CreateLinkedTokenSource(stopToken);
        _submitter = new ZLinkAsyncSubmitter(
            router.OnSendReady,
            registration.SocketOptions.SendTimeout,
            _stopSource.Token);
        _connections = new ZLinkRouteConnectionSet(router);
        _receivePump = new ZLinkRouteReceivePump(
            router,
            new ZLinkRoutePacketDispatcher(
                registration.RouterChannelId,
                router,
                handlers,
                new ZLinkRouteHandlerInvoker(services),
                internalPackets ?? ZLinkNoRouteInternalPacketDispatcher.Instance));
    }

    public string RouterChannelId => _registration.RouterChannelId;

    public void Start()
    {
        _receiveTask = Task.Run(() => _receivePump.RunAsync(_stopSource.Token), CancellationToken.None);
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
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            RouterChannelId,
            packetName,
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            null,
            null,
            null,
            null);
        var envelope = ZLinkEnvelopeCodec.Encode(header, message, message?.GetType() ?? typeof(TMessage));
        return _submitter.SubmitAsync(
            envelope,
            pending => _router.Send(targetNodeRid, pending, SendFlags.DontWait),
            cancellationToken);
    }

    public async ValueTask<TReply> RequestAsync<TRequest, TReply>(
        RoutingId targetNodeRid,
        string packetName,
        TRequest request,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            RouterChannelId,
            packetName,
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            DateTimeOffset.UtcNow.Add(timeout),
            null,
            null,
            null);
        var envelope = ZLinkEnvelopeCodec.Encode(header, request, request?.GetType() ?? typeof(TRequest));
        return await _submitter
            .SubmitRequestAsync<TReply>(
                envelope,
                (pending, complete, fail) => _router.Request(
                    targetNodeRid,
                    pending,
                    (result, reply) => ZLinkRouteReplyCompletion.Complete(result, reply, complete, fail),
                    SendFlags.DontWait,
                    timeout),
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

        await _router.DisposeAsync();
        _stopSource.Dispose();
    }
}
