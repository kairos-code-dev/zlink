using Zlink.Framework.Runtime.Backend.Contracts;

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
        _taskRunner = new ZLinkRuntimeTaskRunner(new ZLinkRuntimeErrorSink(), _stopSource.Token);
        _submitter = new ZLinkAsyncSubmitter(
            router.OnSendReady,
            registration.SocketConfig.SendTimeout,
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

    public IZLinkBackendDiscovery? Discovery => _discovery;

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
        var parts = ZLinkEnvelopeCodec.EncodeParts(header, message, message?.GetType() ?? typeof(TMessage));
        return _submitter.SubmitAsync(
            parts,
            pending => _router.Send(targetNodeRid, pending, SendFlags.DontWait),
            cancellationToken);
    }

    public ValueTask SubmitSendPartsAsync(
        RoutingId targetNodeRid,
        ZLinkEnvelopeHeader header,
        IReadOnlyList<Message> payloadParts,
        CancellationToken cancellationToken)
    {
        var parts = PrependHeader(header, payloadParts);
        return _submitter.SubmitAsync(
            parts,
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
        var parts = ZLinkEnvelopeCodec.EncodeParts(header, request, request?.GetType() ?? typeof(TRequest));
        return await _submitter
            .SubmitRequestAsync<TReply>(
                parts,
                (pending, complete, fail) => _router.Request(
                    targetNodeRid,
                    pending,
                    (result, reply) => ZLinkRouteReplyCompletion.Complete(result, reply, complete, fail),
                    SendFlags.DontWait,
                    timeout),
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
        var parts = PrependHeader(header, payloadParts);
        return await _submitter
            .SubmitRequestAsync<TReply>(
                parts,
                (pending, complete, fail) => _router.Request(
                    targetNodeRid,
                    pending,
                    (result, reply) => ZLinkRouteReplyCompletion.Complete(result, reply, complete, fail),
                    SendFlags.DontWait,
                    timeout),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask SubmitSpotSendPartsAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return _submitter.SubmitAsync(
            parts,
            pending => _router.SendToSpot(
                targetNodeRid,
                targetSpotRid,
                pending,
                SendFlags.DontWait),
            cancellationToken);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestSpotPartsAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await _submitter
            .SubmitRequestAsync<IReadOnlyList<Message>>(
                parts,
                (pending, complete, fail) => _router.RequestToSpot(
                    targetNodeRid,
                    targetSpotRid,
                    pending,
                    (result, reply) => CompleteRawReply(
                        result,
                        reply,
                        complete,
                        fail,
                        $"SPOT routed request failed with result '{result}'."),
                    SendFlags.DontWait,
                    timeout),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static IReadOnlyList<Message> PrependHeader(
        ZLinkEnvelopeHeader header,
        IReadOnlyList<Message> payloadParts)
    {
        var parts = new Message[payloadParts.Count + 1];
        parts[0] = ZLinkEnvelopeCodec.EncodeHeader(header);
        for (int index = 0; index < payloadParts.Count; index++)
        {
            parts[index + 1] = payloadParts[index];
        }

        return parts;
    }

    private static void CompleteRawReply(
        RequestResult result,
        IReadOnlyList<Message> reply,
        Action<IReadOnlyList<Message>> complete,
        Action<Exception> fail,
        string failureMessage)
    {
        if (result == RequestResult.Ok)
        {
            complete(reply);
            return;
        }

        foreach (var replyPart in reply)
        {
            replyPart.Dispose();
        }

        fail(new TimeoutException(failureMessage));
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
