using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.Runtime.Spots;

// RouteMesh 10.0.0 node-level inbound dispatch for the MeshNode builder's
// registered handlers (spec server 21-mesh-node + dotnet 02-handler-interfaces):
//   NodeSend / NodeRequest       -> AddRouteMesh(...).AddRouteSendHandler /
//                                   AddRouteRequestHandler
//                                   (IZLinkRouteSendHandler / IZLinkRouteRequestHandler)
//   ChannelSend / ChannelRequest -> ChannelName(...).AddSendHandler /
//                                   AddRequestHandler
//                                   (IZLinkSendHandler / IZLinkRequestHandler),
//                                   selected by the addressed channel name.
//
// Records arrive from the node dispatch pump (ready-record OwnerKind == Node) via
// IZLinkBackendSpotNode.OnNodeRoute. Requests reply through the record's held reply
// token exactly like the per-spot route plane; the reply envelope is the standard
// Response envelope (ZLinkEnvelopeCodec) the route/channel client decodes. Handler
// invocation reuses the existing route/channel handler-invocation paths
// (ZLinkRouteHandlerInvoker for route handlers, the channel dispatch pipelines for
// channel handlers) rather than a parallel implementation.
internal sealed class ZLinkMeshNodeRouteDispatcher
{
    // Node RID-direct route handlers are not channel-scoped; they share one
    // internal registry channel key so the inbound envelope's channel name is not
    // required to match a configured channel.
    private const string NodeRouteChannel = "";

    private static readonly IReadOnlySet<string> EmptyGroups =
        new HashSet<string>(StringComparer.Ordinal);

    private readonly ZLinkRouteHandlerRegistry _routeHandlers;
    private readonly ZLinkRouteHandlerInvoker _routeInvoker;
    private readonly ZLinkChannelCommandDispatchPipeline _channelCommandPipeline;
    private readonly ZLinkChannelRequestDispatchPipeline _channelRequestPipeline;
    private readonly ZLinkCodecRegistryBuilder _codecs;
    private readonly ZLinkDispatchErrorReporter _dispatchErrors;
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private readonly ILogger _logger;

    private ZLinkMeshNodeRouteDispatcher(
        ZLinkRouteHandlerRegistry routeHandlers,
        ZLinkRouteHandlerInvoker routeInvoker,
        ZLinkChannelCommandDispatchPipeline channelCommandPipeline,
        ZLinkChannelRequestDispatchPipeline channelRequestPipeline,
        ZLinkCodecRegistryBuilder codecs,
        ZLinkDispatchErrorReporter dispatchErrors,
        ZLinkRuntimeTaskRunner taskRunner,
        ILogger logger)
    {
        _routeHandlers = routeHandlers;
        _routeInvoker = routeInvoker;
        _channelCommandPipeline = channelCommandPipeline;
        _channelRequestPipeline = channelRequestPipeline;
        _codecs = codecs;
        _dispatchErrors = dispatchErrors;
        _taskRunner = taskRunner;
        _logger = logger;
    }

    // Builds a dispatcher from the SpotNode's registered node-route and
    // channel-membership handlers, or null when the node has none (nothing to wire).
    public static ZLinkMeshNodeRouteDispatcher? Create(
        IServiceProvider services,
        ZLinkFrameworkRegistration registration,
        ZLinkSpotNodeRegistration spotNode,
        ZLinkFrameworkRuntime runtime,
        ZLinkRuntimeTaskRunner taskRunner)
    {
        var descriptors = BuildRouteDescriptors(spotNode);
        // Router-capable nodes always host the framework's internal
        // remote-session push relay consumer (spec 31 §6): an actor that moved
        // to another node pushes to its bound session through this packet.
        if (spotNode.Router is not null)
            descriptors = descriptors
                .Append(ToRouteDescriptor(
                    ZLinkHandlerScanner.CreateExplicitRouteInterfaceDescriptor(
                        typeof(ZLinkRemoteSessionPushRelayHandler),
                        typeof(IZLinkRouteSendHandler<ZLinkRemoteSessionPushRelay>),
                        ZLinkMessageKind.Command,
                        ZLinkRemoteSessionPushProtocol.PacketName)))
                .Append(ToRouteDescriptor(
                    ZLinkHandlerScanner.CreateExplicitRouteInterfaceDescriptor(
                        typeof(ZLinkRemoteActorFrameRelayHandler),
                        typeof(IZLinkRouteSendHandler<ZLinkRemoteActorFrameRelay>),
                        ZLinkMessageKind.Command,
                        ZLinkRemoteActorFrameProtocol.PacketName)));
        var routeDescriptors = descriptors.ToArray();
        var channelEndpoints = BuildChannelEndpoints(spotNode).ToArray();
        if (routeDescriptors.Length == 0 && channelEndpoints.Length == 0)
            return null;

        var loggerFactory = runtime.Services.GetService<ILoggerFactory>();
        var logger = loggerFactory?.CreateLogger(typeof(ZLinkMeshNodeRouteDispatcher).FullName!)
                     ?? (ILogger)NullLogger.Instance;
        var dispatchErrors = new ZLinkDispatchErrorReporter(
            registration.DispatchOptions,
            ZLinkMessageFlowTracer.CreateLogger(loggerFactory, logger),
            runtime);

        var routeHandlers = new ZLinkRouteHandlerRegistry(routeDescriptors);
        var routeInvoker = new ZLinkRouteHandlerInvoker(services, registration.Codecs);

        // The channel pipelines are always built (registry may be empty) so a
        // channel record addressed to a channel with no matching handler yields a
        // proper "handler not registered" error reply instead of a silent drop.
        var handlerRegistry = new ZLinkHandlerRegistry(channelEndpoints);
        var handlerDispatcher = new ZLinkHandlerDispatcher(
            services.GetRequiredService<IServiceScopeFactory>(),
            registration);
        var commandPipeline = new ZLinkChannelCommandDispatchPipeline(
            handlerRegistry,
            handlerDispatcher,
            static _ => EmptyGroups,
            LogLevel.Warning,
            dispatchErrors,
            registration.Codecs,
            logger);
        var requestPipeline = new ZLinkChannelRequestDispatchPipeline(
            handlerRegistry,
            handlerDispatcher,
            static _ => EmptyGroups,
            registration.Codecs,
            dispatchErrors,
            logger);

        return new ZLinkMeshNodeRouteDispatcher(
            routeHandlers,
            routeInvoker,
            commandPipeline,
            requestPipeline,
            registration.Codecs,
            dispatchErrors,
            taskRunner,
            logger);
    }

    // Pump entry point (invoked on the single node drain loop). Dispatch runs on a
    // detached runtime task, mirroring the per-spot route plane; the record retains
    // its own message parts, so the pump can release the Core claim immediately.
    public void Dispatch(ZLinkBackendRouteReceived received)
    {
        if (!_taskRunner.TryRunDetached(
                "mesh-node-route-dispatch",
                ct => DispatchAsync(received, ct)))
            received.Dispose();
    }

    private async ValueTask DispatchAsync(
        ZLinkBackendRouteReceived received,
        CancellationToken cancellationToken)
    {
        using (received)
        {
            if (received.Parts.Count == 0)
            {
                HandleProtocolError(received, ZLinkEnvelopeCodec.MissingHeader());
                return;
            }

            ZLinkEnvelopeHeader header;
            try
            {
                header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
                ZLinkEnvelopeCodec.ValidateDispatchHeader(header);
            }
            catch (ZLinkEnvelopeProtocolException protocolError)
            {
                HandleProtocolError(received, protocolError);
                return;
            }

            using var flow = ZLinkFlowContext.Enter(
                header.FlowId,
                header.FlowOrigin,
                _dispatchErrors.Flow.CaptureEnabled,
                ZLinkFlowOrigin.Inbound);

            if (received.ChannelName is { } channelName)
                await DispatchChannelAsync(received, channelName, header, cancellationToken)
                    .ConfigureAwait(false);
            else
                await DispatchNodeRouteAsync(received, header, cancellationToken)
                    .ConfigureAwait(false);
        }
    }

    private async ValueTask DispatchNodeRouteAsync(
        ZLinkBackendRouteReceived received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        var isRequest = header.Kind == ZLinkMessageKind.Request;
        var sourceRid = received.SourceNodeRid ?? default;
        var scope = CreateScope(header, isRequest);
        scope.Trace(_dispatchErrors, ZLinkMessageFlowOutcome.Received);

        if (!_routeHandlers.TryGet(
                NodeRouteChannel,
                isRequest ? ZLinkMessageKind.Request : ZLinkMessageKind.Command,
                header.MessageName,
                out var descriptor)
            || descriptor is null)
        {
            if (isRequest)
            {
                var error = new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RouteHandlerNotFound,
                    $"No node route request handler is registered for '{header.MessageName}'.");
                scope.HandlerMissing(
                    _logger,
                    _dispatchErrors,
                    LogLevel.Error,
                    ZLinkDispatchErrorAction.ReplyError,
                    error);
                ReplyError(received, header, error);
            }
            else
            {
                scope.Dropped(_logger, _dispatchErrors, LogLevel.Warning);
            }

            return;
        }

        if (!isRequest)
        {
            try
            {
                await _routeInvoker.InvokeSendAsync(
                        descriptor,
                        NodeRouteChannel,
                        sourceRid,
                        header,
                        received.Parts,
                        cancellationToken)
                    .ConfigureAwait(false);
                scope.Trace(_dispatchErrors, ZLinkMessageFlowOutcome.Dispatched);
            }
            catch (Exception ex)
            {
                scope.HandlerException(
                    _logger,
                    _dispatchErrors,
                    LogLevel.Error,
                    ZLinkDispatchErrorAction.Drop,
                    ex);
            }

            return;
        }

        try
        {
            var reply = await _routeInvoker.InvokeRequestAsync(
                    descriptor,
                    NodeRouteChannel,
                    sourceRid,
                    header,
                    received.Parts,
                    cancellationToken)
                .ConfigureAwait(false);
            ReplyResponse(received, header, reply.Message, reply.MessageType);
            scope.Trace(_dispatchErrors, ZLinkMessageFlowOutcome.Replied);
        }
        catch (Exception ex)
        {
            ReplyError(received, header, ex);
            scope.HandlerException(
                _logger,
                _dispatchErrors,
                null,
                ZLinkDispatchErrorAction.ReplyError,
                ex);
        }
    }

    private async ValueTask DispatchChannelAsync(
        ZLinkBackendRouteReceived received,
        string channelName,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        switch (header.Kind)
        {
            case ZLinkMessageKind.Request:
                await _channelRequestPipeline.DispatchAsync(
                        channelName,
                        received.Parts,
                        header,
                        (replyHeader, reply, replyType) =>
                            SubmitEnvelope(received, replyHeader, reply, replyType),
                        errorHeader => SubmitEnvelope(received, errorHeader, null, null),
                        cancellationToken,
                        received.Metadata)
                    .ConfigureAwait(false);
                return;
            case ZLinkMessageKind.Command:
                await _channelCommandPipeline.DispatchAsync(
                        channelName,
                        received.Parts,
                        header,
                        cancellationToken,
                        received.Metadata)
                    .ConfigureAwait(false);
                return;
        }
    }

    private void ReplyResponse(
        ZLinkBackendRouteReceived received,
        ZLinkEnvelopeHeader requestHeader,
        object? reply,
        Type? replyType)
    {
        SubmitEnvelope(
            received,
            ZLinkChannelReplyWriter.CreateReplyHeader(
                ZLinkMessageKind.Response,
                requestHeader.ChannelName,
                requestHeader),
            reply,
            replyType);
    }

    private void ReplyError(
        ZLinkBackendRouteReceived received,
        ZLinkEnvelopeHeader requestHeader,
        Exception exception)
    {
        SubmitEnvelope(
            received,
            ZLinkChannelReplyWriter.CreateErrorHeader(
                requestHeader.ChannelName,
                requestHeader,
                exception),
            null,
            null);
    }

    private void SubmitEnvelope(
        ZLinkBackendRouteReceived received,
        ZLinkEnvelopeHeader header,
        object? body,
        Type? bodyType)
    {
        if (!received.CanReply) return;

        var replyParts = ZLinkEnvelopeCodec.EncodeParts(header, body, bodyType, _codecs);
        ZLinkSpotReplySubmitter.SubmitAndDispose(received, replyParts);
    }

    private void HandleProtocolError(
        ZLinkBackendRouteReceived received,
        ZLinkEnvelopeProtocolException protocolError)
    {
        var header = protocolError.Header;
        var isRequest = received.RequestSeq.HasValue || received.CanReply;
        var canReply = isRequest
                       && received.CanReply
                       && ZLinkEnvelopeCodec.CanCorrelateReply(header);
        var validFlow = ZLinkEnvelopeCodec.ValidFlow(header);
        using var flow = ZLinkFlowContext.Enter(
            validFlow.FlowId,
            validFlow.FlowOrigin,
            _dispatchErrors.Flow.CaptureEnabled,
            ZLinkFlowOrigin.Inbound);
        _dispatchErrors.Report(new ZLinkDispatchFailure(
            ZLinkDispatchErrorSurface.RouteMeshChannel,
            isRequest
                ? ZLinkDispatchMessageKind.Request
                : ZLinkDispatchMessageKind.Send,
            ZLinkDispatchErrorReason.InvalidFrame,
            canReply
                ? ZLinkDispatchErrorAction.ReplyError
                : ZLinkDispatchErrorAction.Drop,
            header.MessageName,
            received.ChannelName ?? string.Empty,
            CorrelationId: header.CorrelationId,
            Exception: protocolError));
        if (!canReply) return;

        SubmitEnvelope(
            received,
            ZLinkChannelReplyWriter.CreateProtocolErrorHeader(
                received.ChannelName ?? string.Empty,
                header,
                protocolError.Message),
            null,
            null);
    }

    private ZLinkDispatchFlowScope CreateScope(ZLinkEnvelopeHeader header, bool isRequest)
    {
        return new ZLinkDispatchFlowScope(
            ZLinkDispatchErrorSurface.RouteMeshChannel,
            "RouteMeshChannel",
            isRequest ? ZLinkDispatchMessageKind.Request : ZLinkDispatchMessageKind.Send,
            isRequest ? "Request" : "Send",
            header.MessageName,
            header.ChannelName,
            header.ContentType,
            header.CorrelationId);
    }

    private static IEnumerable<ZLinkRouteHandlerDescriptor> BuildRouteDescriptors(
        ZLinkSpotNodeRegistration spotNode)
    {
        foreach (var handler in spotNode.RouteSendHandlers)
        {
            var handlerInterface = typeof(IZLinkRouteSendHandler<>).MakeGenericType(handler.MessageType);
            yield return ToRouteDescriptor(
                ZLinkHandlerScanner.CreateExplicitRouteInterfaceDescriptor(
                    handler.HandlerType,
                    handlerInterface,
                    ZLinkMessageKind.Command,
                    handler.PacketName));
        }

        foreach (var handler in spotNode.RouteRequestHandlers)
        {
            var handlerInterface = typeof(IZLinkRouteRequestHandler<,>).MakeGenericType(
                handler.MessageType,
                handler.ReplyType!);
            yield return ToRouteDescriptor(
                ZLinkHandlerScanner.CreateExplicitRouteInterfaceDescriptor(
                    handler.HandlerType,
                    handlerInterface,
                    ZLinkMessageKind.Request,
                    handler.PacketName));
        }
    }

    private static ZLinkRouteHandlerDescriptor ToRouteDescriptor(
        ZLinkRouteHandlerEndpointDescriptor endpoint)
    {
        return new ZLinkRouteHandlerDescriptor(
            endpoint.Kind,
            NodeRouteChannel,
            endpoint.MessageName,
            endpoint.DeclaringType,
            endpoint.MessageType,
            endpoint.ReplyType,
            endpoint.Invoker);
    }

    private static IEnumerable<ZLinkHandlerEndpointDescriptor> BuildChannelEndpoints(
        ZLinkSpotNodeRegistration spotNode)
    {
        foreach (var membership in spotNode.ChannelMemberships)
        {
            foreach (var handler in membership.SendHandlers)
            {
                var handlerInterface = typeof(IZLinkSendHandler<>).MakeGenericType(handler.MessageType);
                yield return ZLinkHandlerScanner.CreateExplicitInterfaceDescriptor(
                    handler.HandlerType,
                    handlerInterface,
                    ZLinkMessageKind.Command,
                    membership.ChannelName,
                    handler.PacketName);
            }

            foreach (var handler in membership.RequestHandlers)
            {
                var handlerInterface = typeof(IZLinkRequestHandler<,>).MakeGenericType(
                    handler.MessageType,
                    handler.ReplyType!);
                yield return ZLinkHandlerScanner.CreateExplicitInterfaceDescriptor(
                    handler.HandlerType,
                    handlerInterface,
                    ZLinkMessageKind.Request,
                    membership.ChannelName,
                    handler.PacketName);
            }
        }
    }
}
