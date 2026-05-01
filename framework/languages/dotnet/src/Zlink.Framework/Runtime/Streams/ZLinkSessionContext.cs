using Systems.Zlink.Stream.Connector.Protocol;
using Systems.Zlink.Stream.Connector.Protocol.Compression;
using Systems.Zlink.Stream.Connector.Contracts;
using System.Text.Json;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionContext(
    ZLinkFrameworkRuntime runtime,
    IZLinkClient client,
    IZLinkStream stream,
    Func<CancellationToken, ValueTask> closeAsync)
    : IZLinkSessionContext, IZLinkSessionActorAttachmentContext
{
    private static readonly ZlinkStreamHeaderCodec HeaderCodec = new();
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    private IZLinkActor? _actor;
    private ZlinkStreamHeader? _currentDispatchHeader;
    private readonly ZLinkSessionRequestTracker _requests = new();
    private readonly ZLinkSessionActorBindingRegistry _actorBindings = new(runtime);

    public string SessionId => stream.SessionId;

    public RoutingId? RoutingId => stream.RoutingId;

    public string? LocalAddr => stream.LocalAddr;

    public string? RemoteAddr => stream.RemoteAddr;

    public IZLinkRequestCall RequestChannel<TRequest>(
        string channelName,
        TRequest request)
    {
        return client.Request(channelName, request);
    }

    public IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message)
    {
        return client.Send(channelName, message);
    }

    public IZLinkSessionSendCall Send<TMessage>(TMessage message)
    {
        return new ZLinkSessionSendCall<TMessage>(this, message);
    }

    public IZLinkSessionReplyCall Reply<TMessage>(TMessage message)
    {
        return new ZLinkSessionReplyCall<TMessage>(this, message);
    }

    public async ValueTask<IZLinkActorRef> CreateActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        var route = runtime.ResolveLocalActorRoute();
        await runtime.CreateLocalActorAsync(actorId, actorType, cancellationToken)
            .ConfigureAwait(false);
        return await _actorBindings.BindAsync(
            this,
            SessionId,
            actorId,
            actorType,
            route.RouterChannelId,
            route.TargetNodeRid,
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<IZLinkActorRef> CreateRemoteActorAsync(
        RoutingId actorNodeId,
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        var routerChannelId = runtime.ResolveDefaultRouterChannelId();
        var packet = new ZLinkActorCreatePacket(actorId, actorType);
        _ = await runtime.RoutedClient.RequestTo(routerChannelId, actorNodeId, packet)
            .WithPacketName(ZLinkInternalPacketNames.ActorCreate)
            .WithTimeout(runtime.Registration.DefaultTimeout)
            .Async<byte[]>(cancellationToken)
            .ConfigureAwait(false);
        return await _actorBindings.BindAsync(
            this,
            SessionId,
            actorId,
            actorType,
            routerChannelId,
            actorNodeId,
            cancellationToken).ConfigureAwait(false);
    }

    public IZLinkSessionRequestCall Request<TRequest>(TRequest request)
    {
        return new ZLinkSessionRequestCall<TRequest>(
            this,
            request,
            runtime.Registration.DefaultTimeout);
    }

    public ValueTask CloseAsync(CancellationToken cancellationToken = default)
    {
        return closeAsync(cancellationToken);
    }

    public async ValueTask AttachActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        await runtime.AttachActorAsync(actor, stream, cancellationToken);
        _actor = actor;
    }

    public ValueTask DispatchToActorAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        var actor = _actor
            ?? throw new InvalidOperationException("No actor is attached to the current session.");

        return runtime.SubmitActorAsync(actor, header, body, cancellationToken);
    }

    public async ValueTask DispatchToActorAsync(
        IZLinkActorRef actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        if (actor is not ZLinkActorRef actorRef)
        {
            throw new InvalidOperationException("Actor ref was not created by this framework runtime.");
        }

        using (body)
        {
            var packet = new ZLinkActorDispatchPacket(
                actorRef.ActorId,
                actorRef.ActorType,
                ZLinkStreamHeaderSnapshot.FromHeader(header),
                body.AsReadOnlyMemory().ToArray());

            if (header.RequestSeq is not null)
            {
                byte[] reply;
                try
                {
                    reply = await runtime.RoutedClient
                        .RequestTo(actorRef.RouterChannelId, actorRef.TargetNodeRid, packet)
                        .WithPacketName(ZLinkInternalPacketNames.ActorDispatch)
                        .WithTimeout(runtime.Registration.DefaultTimeout)
                        .Async<byte[]>(cancellationToken)
                        .ConfigureAwait(false);
                }
                catch (TimeoutException ex)
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorDispatchTimeout,
                        $"Actor dispatch request for '{actorRef.ActorId}' timed out.",
                        innerException: ex);
                }
                catch (ZLinkFrameworkException)
                {
                    throw;
                }
                catch (Exception ex)
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorDispatchHandlerFailed,
                        $"Actor dispatch request for '{actorRef.ActorId}' failed: {ex.Message}",
                        innerException: ex);
                }

                await ReplyRawAsync(header, header.Codec, reply, cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            await runtime.RoutedClient
                .SendTo(actorRef.RouterChannelId, actorRef.TargetNodeRid, packet)
                .WithPacketName(ZLinkInternalPacketNames.ActorDispatch)
                .Async(cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public async ValueTask DisconnectActorAsync(
        CancellationToken cancellationToken = default)
    {
        var actor = _actor;
        if (actor is null)
        {
            return;
        }

        await runtime.DisconnectActorAsync(actor, stream, cancellationToken);
        _actor = null;
    }

    internal ZlinkStreamHeader? CurrentDispatchHeader => _currentDispatchHeader;

    internal async ValueTask CleanupActorBindingsAsync(CancellationToken cancellationToken)
    {
        await _actorBindings.CleanupAsync(this, cancellationToken).ConfigureAwait(false);
    }

    internal void EnterDispatch(ZlinkStreamHeader header)
    {
        _currentDispatchHeader = header;
    }

    internal void ExitDispatch()
    {
        _currentDispatchHeader = null;
    }

    internal bool TryCompleteResponse(
        ZlinkStreamHeader header,
        Message body)
    {
        return _requests.TryCompleteResponse(header, body);
    }

    internal bool Write(Message payload)
    {
        return stream.Write(payload);
    }

    internal ValueTask SendRawAsync(
        string packetName,
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> body,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            codec,
            ZlinkStreamHeaderFlags.None,
            null,
            packetName,
            ZlinkStreamMetadata.Empty);
        WriteRawFrame(header, body.Span, "Client stream send failed.");
        return ValueTask.CompletedTask;
    }

    internal async ValueTask<Message> RequestRawAsync(
        string packetName,
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> body,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        using var pending = _requests.Start();

        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            codec,
            ZlinkStreamHeaderFlags.HasRequestSeq,
            pending.RequestSeq,
            packetName,
            ZlinkStreamMetadata.Empty);
        WriteRawFrame(header, body.Span, "Client stream request send failed.");

        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);
        using var registration = timeoutSource.Token.Register(static state =>
        {
            var item = (ZLinkPendingSessionRequest)state!;
            item.Cancel();
        }, pending);

        return await pending.Task.ConfigureAwait(false);
    }

    internal ValueTask ReplyRawAsync(
        ZlinkStreamHeader requestHeader,
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> body,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (requestHeader.RequestSeq is not { } requestSeq)
        {
            throw new InvalidOperationException("Raw reply is only available for a request packet.");
        }

        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Response,
            codec,
            ZlinkStreamHeaderFlags.HasRequestSeq,
            requestSeq,
            requestHeader.Name,
            ZlinkStreamMetadata.Empty);
        WriteRawFrame(header, body.Span, "Client stream reply send failed.");
        return ValueTask.CompletedTask;
    }

    internal ValueTask ReplyErrorAsync(
        ZlinkStreamHeader requestHeader,
        Exception exception,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (requestHeader.RequestSeq is not { } requestSeq)
        {
            return ValueTask.CompletedTask;
        }

        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Error,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasRequestSeq,
            requestSeq,
            requestHeader.Name,
            ZlinkStreamMetadata.Empty);
        var body = JsonSerializer.SerializeToUtf8Bytes(
            new ZLinkStreamWireError(
                exception.GetType().Name,
                exception.Message),
            JsonOptions);
        WriteRawFrame(header, body, "Client stream error reply send failed.");
        return ValueTask.CompletedTask;
    }

    internal async ValueTask<TReply> RequestClientAsync<TRequest, TReply>(
        TRequest request,
        string packetName,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        using var pending = _requests.Start();

        ReadOnlyMemory<byte> body = JsonSerializer.SerializeToUtf8Bytes(request, JsonOptions);
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasRequestSeq,
            pending.RequestSeq,
            packetName,
            ZlinkStreamMetadata.Empty);
        var frame = ZLinkStreamFrameCodec.Encode(HeaderCodec.Encode(header).Span, body.Span);
        using var payloadMessage = Message.FromBytes(frame);
        if (!Write(payloadMessage))
        {
            throw new InvalidOperationException("Client stream request send failed.");
        }

        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);
        using var registration = timeoutSource.Token.Register(static state =>
        {
            var item = (ZLinkPendingSessionRequest)state!;
            item.Cancel();
        }, pending);

        using var reply = await pending.Task.ConfigureAwait(false);
        return JsonSerializer.Deserialize<TReply>(reply.AsReadOnlySpan(), JsonOptions)
            ?? throw new InvalidOperationException("Client stream request reply body is null.");
    }

    private void WriteRawFrame(
        ZlinkStreamHeader header,
        ReadOnlySpan<byte> body,
        string failureMessage)
    {
        var frame = ZLinkStreamFrameCodec.Encode(HeaderCodec.Encode(header).Span, body);
        using var payloadMessage = Message.FromBytes(frame);
        if (!Write(payloadMessage))
        {
            throw new InvalidOperationException(failureMessage);
        }
    }

}

internal sealed record ZLinkStreamWireError(
    string? Code,
    string? Message);

internal abstract class ZLinkSessionStreamCallBase<TMessage>(
    ZLinkSessionContext context,
    TMessage message)
{
    private static readonly IZlinkStreamPacketNameResolver MessageNameResolver = new ZlinkStreamPacketNameResolver();
    private static readonly IZlinkStreamCompressionCodec CompressionCodec = new ZlinkStreamLz4CompressionCodec();
    private static readonly ZlinkStreamHeaderCodec HeaderCodec = new();
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    private string _messageName = MessageNameResolver.Resolve(typeof(TMessage));
    private ZlinkStreamMetadata _metadata = ZlinkStreamMetadata.Empty;
    private bool _compress;
    private int _executed;

    public ZLinkSessionStreamCallBase<TMessage> WithMetadata(string key, string value)
    {
        _metadata = _metadata.With(key, value);
        return this;
    }

    public ZLinkSessionStreamCallBase<TMessage> WithPacketName(string messageName)
    {
        if (string.IsNullOrWhiteSpace(messageName))
        {
            throw new InvalidOperationException("Stream packet name must not be empty.");
        }

        _messageName = messageName;
        return this;
    }

    public ZLinkSessionStreamCallBase<TMessage> Compress()
    {
        _compress = true;
        return this;
    }

    protected ValueTask ExecuteAsync(CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;

        if (Interlocked.Exchange(ref _executed, 1) != 0)
        {
            throw new InvalidOperationException("Stream send builders can be executed only once.");
        }

        ReadOnlyMemory<byte> body = JsonSerializer.SerializeToUtf8Bytes(message, JsonOptions);
        var flags = ZlinkStreamHeaderFlags.None;

        if (_compress)
        {
            body = CompressionCodec.Compress(body);
            flags |= ZlinkStreamHeaderFlags.BodyCompressed;
        }

        var header = CreateHeader(ZlinkStreamCodec.Json, flags, _messageName, _metadata, context.CurrentDispatchHeader);
        var frame = ZLinkStreamFrameCodec.Encode(HeaderCodec.Encode(header).Span, body.Span);
        using var payloadMessage = Message.FromBytes(frame);

        if (!context.Write(payloadMessage))
        {
            throw new InvalidOperationException("Client stream send failed.");
        }

        return ValueTask.CompletedTask;
    }

    protected abstract ZlinkStreamHeader CreateHeader(
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags,
        string messageName,
        ZlinkStreamMetadata metadata,
        ZlinkStreamHeader? currentDispatchHeader);

}

internal sealed class ZLinkSessionSendCall<TMessage>(
    ZLinkSessionContext context,
    TMessage message)
    : ZLinkSessionStreamCallBase<TMessage>(context, message), IZLinkSessionSendCall
{
    IZLinkSessionSendCall IZLinkSessionSendCall.WithMetadata(string key, string value)
        => (IZLinkSessionSendCall)WithMetadata(key, value);

    IZLinkSessionSendCall IZLinkSessionSendCall.WithPacketName(string messageName)
        => (IZLinkSessionSendCall)WithPacketName(messageName);

    IZLinkSessionSendCall IZLinkSessionSendCall.Compress()
        => (IZLinkSessionSendCall)Compress();

    public ValueTask Async(CancellationToken cancellationToken = default)
    {
        return ExecuteAsync(cancellationToken);
    }

    protected override ZlinkStreamHeader CreateHeader(
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags,
        string messageName,
        ZlinkStreamMetadata metadata,
        ZlinkStreamHeader? currentDispatchHeader)
    {
        _ = currentDispatchHeader;
        return new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            codec,
            flags,
            null,
            messageName,
            metadata);
    }
}

internal sealed class ZLinkSessionReplyCall<TMessage>(
    ZLinkSessionContext context,
    TMessage message)
    : ZLinkSessionStreamCallBase<TMessage>(context, message), IZLinkSessionReplyCall
{
    IZLinkSessionReplyCall IZLinkSessionReplyCall.WithMetadata(string key, string value)
        => (IZLinkSessionReplyCall)WithMetadata(key, value);

    IZLinkSessionReplyCall IZLinkSessionReplyCall.Compress()
        => (IZLinkSessionReplyCall)Compress();

    public ValueTask Async(CancellationToken cancellationToken = default)
    {
        return ExecuteAsync(cancellationToken);
    }

    protected override ZlinkStreamHeader CreateHeader(
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags,
        string messageName,
        ZlinkStreamMetadata metadata,
        ZlinkStreamHeader? currentDispatchHeader)
    {
        if (currentDispatchHeader?.RequestSeq is not { } requestSeq)
        {
            throw new InvalidOperationException("Reply is only available while handling a request packet.");
        }

        return new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Response,
            codec,
            flags | ZlinkStreamHeaderFlags.HasRequestSeq,
            requestSeq,
            currentDispatchHeader.Name,
            metadata);
    }
}
