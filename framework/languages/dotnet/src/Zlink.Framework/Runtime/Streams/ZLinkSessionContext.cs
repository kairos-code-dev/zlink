using Systems.Zlink.Stream.Connector.Protocol;
using Systems.Zlink.Stream.Connector.Protocol.Compression;
using Systems.Zlink.Stream.Connector.Contracts;
using System.Buffers.Binary;
using System.Text.Json;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionContext(
    ZLinkFrameworkRuntime runtime,
    IZLinkClient client,
    IZLinkStream stream,
    Func<CancellationToken, ValueTask> closeAsync)
    : IZLinkSessionContext
{
    private IZLinkActor? _actor;
    private ZlinkStreamHeader? _currentDispatchHeader;

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

    internal void EnterDispatch(ZlinkStreamHeader header)
    {
        _currentDispatchHeader = header;
    }

    internal void ExitDispatch()
    {
        _currentDispatchHeader = null;
    }

    internal bool Write(Message payload)
    {
        return stream.Write(payload);
    }
}

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

    public ValueTask SendAsync(CancellationToken cancellationToken = default)
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
        var frame = EncodeFrame(HeaderCodec.Encode(header).Span, body.Span);
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

    private static byte[] EncodeFrame(ReadOnlySpan<byte> header, ReadOnlySpan<byte> body)
    {
        var frame = new byte[6 + header.Length + body.Length];
        BinaryPrimitives.WriteUInt16BigEndian(frame.AsSpan(0, 2), (ushort)header.Length);
        BinaryPrimitives.WriteUInt32BigEndian(frame.AsSpan(2, 4), (uint)body.Length);
        header.CopyTo(frame.AsSpan(6, header.Length));
        body.CopyTo(frame.AsSpan(6 + header.Length, body.Length));
        return frame;
    }
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
        return SendAsync(cancellationToken);
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
        return SendAsync(cancellationToken);
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
