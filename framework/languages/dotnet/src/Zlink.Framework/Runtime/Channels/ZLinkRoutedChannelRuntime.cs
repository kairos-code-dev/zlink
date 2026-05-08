using Zlink.Framework.Backend.Contracts;
using System.Reflection;

namespace Zlink.Framework.Runtime.Channels;

internal sealed record ZLinkRoutedHandlerDescriptor(
    ZLinkMessageKind Kind,
    string RouterChannelId,
    string PacketName,
    Type HandlerType,
    Type MessageType,
    Type? ReplyType,
    MethodInfo HandleMethod);

internal sealed class ZLinkRoutedHandlerRegistry(IEnumerable<ZLinkRoutedHandlerDescriptor> descriptors)
{
    private readonly Dictionary<(string Channel, ZLinkMessageKind Kind, string Packet), ZLinkRoutedHandlerDescriptor> _handlers =
        Build(descriptors);

    public ZLinkRoutedHandlerDescriptor Get(
        string routerChannelId,
        ZLinkMessageKind kind,
        string packetName)
    {
        return _handlers.TryGetValue((routerChannelId, kind, packetName), out var descriptor)
            ? descriptor
            : throw new InvalidOperationException(
                $"No routed handler is registered for '{routerChannelId}:{kind}:{packetName}'.");
    }

    private static Dictionary<(string Channel, ZLinkMessageKind Kind, string Packet), ZLinkRoutedHandlerDescriptor> Build(
        IEnumerable<ZLinkRoutedHandlerDescriptor> descriptors)
    {
        var handlers = new Dictionary<(string Channel, ZLinkMessageKind Kind, string Packet), ZLinkRoutedHandlerDescriptor>();
        foreach (var descriptor in descriptors)
        {
            if (!handlers.TryAdd((descriptor.RouterChannelId, descriptor.Kind, descriptor.PacketName), descriptor))
            {
                throw new ZLinkConfigurationException(
                    $"Duplicate routed handler '{descriptor.RouterChannelId}:{descriptor.Kind}:{descriptor.PacketName}'.");
            }
        }

        return handlers;
    }
}

internal sealed class ZLinkRoutedChannelRuntime : IAsyncDisposable
{
    private readonly IServiceProvider _services;
    private readonly ZLinkRoutedChannelRegistration _registration;
    private readonly IZLinkBackendRouterSocket _router;
    private readonly ZLinkAsyncSubmitter _submitter;
    private readonly ZLinkRoutedHandlerRegistry _handlers;
    private readonly ZLinkRoutedHandlerInvoker _handlerInvoker;
    private readonly IZLinkRoutedInternalPacketDispatcher _internalPackets;
    private readonly CancellationTokenSource _stopSource;
    private readonly HashSet<string> _manualConnections = new(StringComparer.Ordinal);
    private readonly IZLinkBackendDiscovery? _discovery;
    private Task? _receivePump;

    public ZLinkRoutedChannelRuntime(
        IServiceProvider services,
        ZLinkRoutedChannelRegistration registration,
        IZLinkBackendRouterSocket router,
        IZLinkBackendDiscovery? discovery,
        ZLinkRoutedHandlerRegistry handlers,
        IZLinkRoutedInternalPacketDispatcher? internalPackets,
        CancellationToken stopToken)
    {
        _services = services;
        _registration = registration;
        _router = router;
        _discovery = discovery;
        _handlers = handlers;
        _handlerInvoker = new ZLinkRoutedHandlerInvoker(services);
        _internalPackets = internalPackets ?? ZLinkNoRoutedInternalPacketDispatcher.Instance;
        _stopSource = CancellationTokenSource.CreateLinkedTokenSource(stopToken);
        _submitter = new ZLinkAsyncSubmitter(
            router.OnSendReady,
            registration.SocketOptions.SendTimeout,
            _stopSource.Token);
    }

    public string RouterChannelId => _registration.RouterChannelId;

    public void Start()
    {
        _receivePump = Task.Run(() => RunReceiveLoopAsync(_stopSource.Token), CancellationToken.None);
    }

    public void Connect(string endpoint)
    {
        if (_manualConnections.Add(endpoint))
        {
            _router.Connect(endpoint);
        }
    }

    public void Disconnect(string endpoint)
    {
        _router.Disconnect(endpoint);
        _manualConnections.Remove(endpoint);
    }

    public IReadOnlyList<string> ListConnections()
    {
        return _manualConnections.OrderBy(static endpoint => endpoint, StringComparer.Ordinal).ToArray();
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
                    (result, reply) => CompleteReply(result, reply, complete, fail),
                    SendFlags.DontWait,
                    timeout),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask DisposeAsync()
    {
        _stopSource.Cancel();
        if (_receivePump is not null)
        {
            try
            {
                await _receivePump.ConfigureAwait(false);
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

    private async Task RunReceiveLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            Received? received = null;
            try
            {
                received = _router.Recv(RecvFlags.DontWait);
                if (received is null)
                {
                    await Task.Delay(1, cancellationToken).ConfigureAwait(false);
                    continue;
                }

                await DispatchAsync(received, cancellationToken).ConfigureAwait(false);
            }
            catch (Exception) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (ZlinkRecvException ex) when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
            {
                await Task.Delay(1, cancellationToken).ConfigureAwait(false);
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (ZlinkRecvException ex)
                when (cancellationToken.IsCancellationRequested
                      || ex.Result == ZlinkRecvException.ErrorCode.InvalidHandle)
            {
                return;
            }
            catch (ZlinkRecvException ex)
                when (ex.Result == ZlinkRecvException.ErrorCode.InternalError)
            {
                await Task.Delay(1, cancellationToken).ConfigureAwait(false);
            }
            finally
            {
                received?.Dispose();
            }
        }
    }

    private async ValueTask DispatchAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        if (received.Parts.Count == 0)
        {
            return;
        }

        var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts[0]);
        if (header.Kind == ZLinkMessageKind.Command)
        {
            await DispatchSendAsync(received, header, cancellationToken).ConfigureAwait(false);
            return;
        }

        if (header.Kind == ZLinkMessageKind.Request)
        {
            await DispatchRequestAsync(received, header, cancellationToken).ConfigureAwait(false);
        }
    }

    private async ValueTask DispatchSendAsync(
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        if (_internalPackets.CanHandleSend(header.MessageName))
        {
            await _internalPackets.DispatchSendAsync(received, cancellationToken).ConfigureAwait(false);
            return;
        }

        var descriptor = _handlers.Get(RouterChannelId, ZLinkMessageKind.Command, header.MessageName);
        var sourceRid = received.RoutingId
            ?? throw new InvalidOperationException("Routed send requires a source routing id.");

        await _handlerInvoker.InvokeSendAsync(
                descriptor,
                RouterChannelId,
                sourceRid,
                header,
                received.Parts[0],
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DispatchRequestAsync(
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        if (_internalPackets.CanHandleRequest(header.MessageName))
        {
            await DispatchInternalRequestAsync(
                received,
                header,
                cancellationToken,
                (_, requestHeader, token) => _internalPackets.DispatchRequestAsync(received, requestHeader, token))
                .ConfigureAwait(false);
            return;
        }

        var descriptor = _handlers.Get(RouterChannelId, ZLinkMessageKind.Request, header.MessageName);
        var sourceRid = received.RoutingId
            ?? throw new InvalidOperationException("Routed request requires a source routing id.");

        try
        {
            var reply = await _handlerInvoker.InvokeRequestAsync(
                    descriptor,
                    RouterChannelId,
                    sourceRid,
                    header,
                    received.Parts[0],
                    cancellationToken)
                .ConfigureAwait(false);
            var replyHeader = new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Response,
                RouterChannelId,
                header.MessageName,
                ZLinkEnvelopeCodec.DefaultContentType,
                header.CorrelationId,
                null,
                null,
                null,
                null);
            using var replyMessage = ZLinkEnvelopeCodec.Encode(replyHeader, reply.Message, reply.MessageType);
            _router.Reply(sourceRid, received.RequestSeq ?? 0UL, replyMessage);
        }
        catch (Exception ex)
        {
            var errorHeader = new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Error,
                RouterChannelId,
                header.MessageName,
                ZLinkEnvelopeCodec.DefaultContentType,
                header.CorrelationId,
                null,
                null,
                ex.GetType().Name,
                ex.Message);
            using var replyMessage = ZLinkEnvelopeCodec.Encode(errorHeader, null, null);
            _router.Reply(sourceRid, received.RequestSeq ?? 0UL, replyMessage);
        }
    }

    private async ValueTask DispatchInternalRequestAsync(
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken,
        Func<RoutingId, ZLinkEnvelopeHeader, CancellationToken, ValueTask<byte[]>> dispatch)
    {
        var sourceRid = received.RoutingId
            ?? throw new InvalidOperationException("Internal routed request requires a source routing id.");

        try
        {
            var reply = await dispatch(sourceRid, header, cancellationToken).ConfigureAwait(false);
            var replyHeader = new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Response,
                RouterChannelId,
                header.MessageName,
                ZLinkEnvelopeCodec.DefaultContentType,
                header.CorrelationId,
                null,
                null,
                null,
                null);
            using var replyMessage = ZLinkEnvelopeCodec.Encode(replyHeader, reply, typeof(byte[]));
            _router.Reply(sourceRid, received.RequestSeq ?? 0UL, replyMessage);
        }
        catch (Exception ex)
        {
            var errorHeader = new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Error,
                RouterChannelId,
                header.MessageName,
                ZLinkEnvelopeCodec.DefaultContentType,
                header.CorrelationId,
                null,
                null,
                ex.GetType().Name,
                ex.Message);
            using var replyMessage = ZLinkEnvelopeCodec.Encode(errorHeader, null, null);
            _router.Reply(sourceRid, received.RequestSeq ?? 0UL, replyMessage);
        }
    }

    private static void CompleteReply<TReply>(
        RequestResult result,
        IReadOnlyList<Message> reply,
        Action<TReply> complete,
        Action<Exception> fail)
    {
        try
        {
            if (result != RequestResult.Ok)
            {
                fail(new TimeoutException($"ZLink routed request failed with result '{result}'."));
                return;
            }

            if (reply.Count == 0)
            {
                fail(new InvalidOperationException("ZLink routed request reply is empty."));
                return;
            }

            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply[0]);
            if (replyHeader.Kind == ZLinkMessageKind.Error)
            {
                fail(new InvalidOperationException(replyHeader.ErrorMessage ?? "ZLink routed request failed."));
                return;
            }

            complete((TReply?)ZLinkEnvelopeCodec.DecodeBody(reply[0], typeof(TReply))
                ?? throw new InvalidOperationException("ZLink routed request reply body is null."));
        }
        catch (Exception exception)
        {
            fail(exception);
        }
        finally
        {
            foreach (var replyPart in reply)
            {
                replyPart.Dispose();
            }
        }
    }
}
