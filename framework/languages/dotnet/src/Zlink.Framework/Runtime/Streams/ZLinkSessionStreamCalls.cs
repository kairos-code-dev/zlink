namespace Zlink.Framework.Runtime.Streams;

internal abstract class ZLinkSessionStreamCallBase<TMessage>(
    ZLinkSessionContext context,
    TMessage message)
{
    private readonly ZLinkStreamSendBuilder<TMessage> _builder = new(
        message,
        context.Codecs,
        context.CompressionCodec);

    public ZLinkSessionStreamCallBase<TMessage> Metadata(string key, string value)
    {
        _builder.AddMetadata(key, value);
        return this;
    }

    public ZLinkSessionStreamCallBase<TMessage> Compress()
    {
        _builder.EnableCompression();
        return this;
    }

    protected ValueTask Execute(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        _builder.Write(
            (codec, flags, messageName, metadata) => CreateHeader(
                codec,
                flags,
                messageName,
                metadata,
                context.CurrentDispatchHeader),
            context.Write,
            "Client stream send failed.");

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
    {
        return (IZLinkSessionSendCall)Metadata(key, value);
    }

    IZLinkSessionSendCall IZLinkSessionSendCall.Compress()
    {
        return (IZLinkSessionSendCall)Compress();
    }

    public void Submit(CancellationToken cancellationToken = default)
    {
        _ = Execute(cancellationToken).AsTask();
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
            metadata,
            ZLinkStreamCorrelation.Next());
    }
}

internal sealed class ZLinkSessionReplyCall<TMessage>(
    ZLinkSessionContext context,
    TMessage message)
    : ZLinkSessionStreamCallBase<TMessage>(context, message), IZLinkSessionReplyCall
{
    IZLinkSessionReplyCall IZLinkSessionReplyCall.Metadata(string key, string value)
    {
        return (IZLinkSessionReplyCall)Metadata(key, value);
    }

    IZLinkSessionReplyCall IZLinkSessionReplyCall.Compress()
    {
        return (IZLinkSessionReplyCall)Compress();
    }

    public void Submit(CancellationToken cancellationToken = default)
    {
        _ = Execute(cancellationToken).AsTask();
    }

    protected override ZlinkStreamHeader CreateHeader(
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags,
        string messageName,
        ZlinkStreamMetadata metadata,
        ZlinkStreamHeader? currentDispatchHeader)
    {
        if (currentDispatchHeader?.RequestSeq is not { } requestSeq)
            throw new InvalidOperationException("Reply is only available while handling a request packet.");

        return ZLinkStreamReplyHeaders.CreateForRequest(
            currentDispatchHeader,
            ZlinkStreamMessageKind.Response,
            codec,
            flags,
            requestSeq,
            metadata);
    }
}
