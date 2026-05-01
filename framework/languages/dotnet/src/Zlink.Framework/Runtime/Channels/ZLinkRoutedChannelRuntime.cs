using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Channels;

internal sealed record ZLinkRoutedHandlerDescriptor(
    ZLinkMessageKind Kind,
    string RouterChannelId,
    string PacketName,
    Type HandlerType,
    Type MessageType,
    Type? ReplyType);

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
        CancellationToken stopToken)
    {
        _services = services;
        _registration = registration;
        _router = router;
        _discovery = discovery;
        _handlers = handlers;
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
            catch (ZlinkRecvException ex) when (ex.Result == RecvResult.NoData)
            {
                await Task.Delay(1, cancellationToken).ConfigureAwait(false);
            }
            catch (ObjectDisposedException)
            {
                return;
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
        if (received.Count == 0)
        {
            return;
        }

        var header = ZLinkEnvelopeCodec.DecodeHeader(received[0]);
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
        if (header.MessageName == "ZLink.ActorRelay"
            && _registration.SessionProxyHandlerType is not null)
        {
            await DispatchActorRelaySendAsync(received, cancellationToken).ConfigureAwait(false);
            return;
        }

        if (header.MessageName == "ZLink.SessionGateway"
            && _registration.SessionGatewayEnabled)
        {
            await DispatchSessionGatewaySendAsync(received, cancellationToken).ConfigureAwait(false);
            return;
        }

        var descriptor = _handlers.Get(RouterChannelId, ZLinkMessageKind.Command, header.MessageName);
        var message = ZLinkEnvelopeCodec.DecodeBody(received[0], descriptor.MessageType);
        var sourceRid = received.RoutingId
            ?? throw new InvalidOperationException("Routed send requires a source routing id.");

        await using var scope = _services.CreateAsyncScope();
        var context = new ZLinkRoutedSendContext(
            RouterChannelId,
            sourceRid,
            header.MessageName,
            header.ContentType,
            header.CorrelationId,
            scope.ServiceProvider,
            cancellationToken);
        var handler = scope.ServiceProvider.GetRequiredService(descriptor.HandlerType);
        var result = descriptor.HandlerType
            .GetMethod(nameof(IZLinkRoutedSendHandler<object>.HandleAsync))!
            .Invoke(handler, [message, context, cancellationToken]);
        await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
    }

    private async ValueTask DispatchRequestAsync(
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        if (header.MessageName == "ZLink.ActorRelay"
            && _registration.SessionProxyHandlerType is not null)
        {
            await DispatchInternalRequestAsync(
                received,
                header,
                cancellationToken,
                (sourceRid, _, token) => DispatchActorRelayRequestAsync(received, sourceRid, token))
                .ConfigureAwait(false);
            return;
        }

        if (header.MessageName == "ZLink.SessionGateway"
            && _registration.SessionGatewayEnabled)
        {
            await DispatchInternalRequestAsync(
                received,
                header,
                cancellationToken,
                (sourceRid, requestHeader, token) => DispatchSessionGatewayRequestAsync(received, sourceRid, requestHeader, token))
                .ConfigureAwait(false);
            return;
        }

        var descriptor = _handlers.Get(RouterChannelId, ZLinkMessageKind.Request, header.MessageName);
        var message = ZLinkEnvelopeCodec.DecodeBody(received[0], descriptor.MessageType);
        var sourceRid = received.RoutingId
            ?? throw new InvalidOperationException("Routed request requires a source routing id.");

        try
        {
            await using var scope = _services.CreateAsyncScope();
            var context = new ZLinkRoutedRequestContext(
                RouterChannelId,
                sourceRid,
                header.MessageName,
                header.ContentType,
                header.CorrelationId,
                header.Deadline,
                scope.ServiceProvider,
                cancellationToken);
            var handler = scope.ServiceProvider.GetRequiredService(descriptor.HandlerType);
            var result = descriptor.HandlerType
                .GetMethod(nameof(IZLinkRoutedRequestHandler<object, object>.HandleAsync))!
                .Invoke(handler, [message, context, cancellationToken]);
            var reply = await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
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
            using var replyMessage = ZLinkEnvelopeCodec.Encode(replyHeader, reply, descriptor.ReplyType);
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

    private async ValueTask DispatchActorRelaySendAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var sourceRid = received.RoutingId
            ?? throw new InvalidOperationException("Actor relay send requires a source routing id.");
        var packet = (ZLinkActorRelayPacket?)ZLinkEnvelopeCodec.DecodeBody(received[0], typeof(ZLinkActorRelayPacket))
            ?? throw new InvalidOperationException("Actor relay packet body is null.");
        await using var scope = _services.CreateAsyncScope();
        using var body = Message.FromBytes(packet.Body);
        var envelope = packet.ToEnvelope();
        var context = new ZLinkSessionProxyContext(
            sourceRid,
            scope.ServiceProvider.GetRequiredService<IZLinkSessionGateway>());
        var handler = (IZLinkSessionProxyHandler)scope.ServiceProvider.GetRequiredService(
            _registration.SessionProxyHandlerType!);
        var reply = await handler.HandleAsync(
            context,
            new ZLinkActorRelayMessage(envelope, body),
            cancellationToken).ConfigureAwait(false);
        reply?.Dispose();
    }

    private async ValueTask<byte[]> DispatchActorRelayRequestAsync(
        Received received,
        RoutingId sourceRid,
        CancellationToken cancellationToken)
    {
        var packet = (ZLinkActorRelayPacket?)ZLinkEnvelopeCodec.DecodeBody(received[0], typeof(ZLinkActorRelayPacket))
            ?? throw new InvalidOperationException("Actor relay packet body is null.");
        await using var scope = _services.CreateAsyncScope();
        using var body = Message.FromBytes(packet.Body);
        var envelope = packet.ToEnvelope();
        var context = new ZLinkSessionProxyContext(
            sourceRid,
            scope.ServiceProvider.GetRequiredService<IZLinkSessionGateway>());
        var handler = (IZLinkSessionProxyHandler)scope.ServiceProvider.GetRequiredService(
            _registration.SessionProxyHandlerType!);
        using var reply = await handler.HandleAsync(
            context,
            new ZLinkActorRelayMessage(envelope, body),
            cancellationToken).ConfigureAwait(false);
        return reply?.AsReadOnlyMemory().ToArray() ?? [];
    }

    private async ValueTask DispatchSessionGatewaySendAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var packet = (ZLinkSessionGatewayPacket?)ZLinkEnvelopeCodec.DecodeBody(received[0], typeof(ZLinkSessionGatewayPacket))
            ?? throw new InvalidOperationException("Session gateway packet body is null.");
        var runtime = _services.GetRequiredService<ZLinkFrameworkRuntime>();
        if (!runtime.TryGetSessionActorContext(packet.Envelope.ActorId, out var sessionContext))
        {
            throw new InvalidOperationException($"No session is bound to actor '{packet.Envelope.ActorId}'.");
        }

        await sessionContext.SendRawAsync(
            packet.Envelope.PacketName,
            ZlinkStreamCodec.Json,
            packet.Body,
            cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask<byte[]> DispatchSessionGatewayRequestAsync(
        Received received,
        RoutingId sourceRid,
        ZLinkEnvelopeHeader routedHeader,
        CancellationToken cancellationToken)
    {
        _ = sourceRid;
        var packet = (ZLinkSessionGatewayPacket?)ZLinkEnvelopeCodec.DecodeBody(received[0], typeof(ZLinkSessionGatewayPacket))
            ?? throw new InvalidOperationException("Session gateway packet body is null.");
        var runtime = _services.GetRequiredService<ZLinkFrameworkRuntime>();
        if (!runtime.TryGetSessionActorContext(packet.Envelope.ActorId, out var sessionContext))
        {
            throw new InvalidOperationException($"No session is bound to actor '{packet.Envelope.ActorId}'.");
        }

        using var reply = await sessionContext.RequestRawAsync(
            packet.Envelope.PacketName,
            ZlinkStreamCodec.Json,
            packet.Body,
            ResolveInternalTimeout(routedHeader),
            cancellationToken).ConfigureAwait(false);
        return reply.AsReadOnlyMemory().ToArray();
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

    private static TimeSpan ResolveInternalTimeout(ZLinkEnvelopeHeader header)
    {
        if (header.Deadline is not { } deadline)
        {
            return TimeSpan.FromSeconds(30);
        }

        var remaining = deadline - DateTimeOffset.UtcNow;
        return remaining > TimeSpan.Zero
            ? remaining
            : TimeSpan.Zero;
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

internal sealed class ZLinkSessionProxyContext(
    RoutingId sourceSessionNodeRid,
    IZLinkSessionGateway sessionGateway) : IZLinkSessionProxyContext
{
    public RoutingId SourceSessionNodeRid { get; } = sourceSessionNodeRid;

    public IZLinkSessionGateway SessionGateway { get; } = sessionGateway;
}
