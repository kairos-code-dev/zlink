using Systems.Zlink.Stream.Connector.Contracts;

namespace Zlink.Framework.Runtime.Streams;

internal abstract class ZLinkSessionStreamCallBase<TMessage>(
    ZLinkSessionContext context,
    TMessage message)
{
    private static readonly IZlinkStreamPacketNameResolver MessageNameResolver = ZLinkStreamProtocolDefaults.PacketNameResolver;
    private string _messageName = MessageNameResolver.Resolve(typeof(TMessage));
    private ZlinkStreamMetadata _metadata = ZlinkStreamMetadata.Empty;
    private bool _compress;
    private int _executed;

    public ZLinkSessionStreamCallBase<TMessage> Metadata(string key, string value)
    {
        _metadata = _metadata.With(key, value);
        return this;
    }

    public ZLinkSessionStreamCallBase<TMessage> PacketName(string messageName)
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

        ReadOnlyMemory<byte> payload = ZLinkEnvelopeCodec.EncodeJsonBytes(message);
        var flags = ZlinkStreamHeaderFlags.None;

        if (_compress)
        {
            payload = ZLinkStreamProtocolDefaults.Lz4Compress(payload);
            flags |= ZlinkStreamHeaderFlags.PayloadCompressed;
        }

        var header = CreateHeader(ZlinkStreamCodec.Json, flags, _messageName, _metadata, context.CurrentDispatchHeader);
        ZLinkStreamFrameWriter.Write(context.Write, context.HeaderCodec, header, payload.Span, "Client stream send failed.");

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
    IZLinkSessionSendCall IZLinkSessionSendCall.Metadata(string key, string value)
        => (IZLinkSessionSendCall)Metadata(key, value);

    IZLinkSessionSendCall IZLinkSessionSendCall.PacketName(string messageName)
        => (IZLinkSessionSendCall)PacketName(messageName);

    IZLinkSessionSendCall IZLinkSessionSendCall.Compress()
        => (IZLinkSessionSendCall)Compress();

    public ValueTask Submit(CancellationToken cancellationToken = default)
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
    IZLinkSessionReplyCall IZLinkSessionReplyCall.Metadata(string key, string value)
        => (IZLinkSessionReplyCall)Metadata(key, value);

    IZLinkSessionReplyCall IZLinkSessionReplyCall.Compress()
        => (IZLinkSessionReplyCall)Compress();

    public ValueTask Submit(CancellationToken cancellationToken = default)
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
