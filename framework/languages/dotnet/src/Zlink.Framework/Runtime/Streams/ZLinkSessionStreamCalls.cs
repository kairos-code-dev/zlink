using Systems.Zlink.Stream.Connector.Runtime;

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

    public ZLinkSessionStreamCallBase<TMessage> Metadata(ZLinkMessageMetadata metadata)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        foreach (var (key, value) in metadata.Values) _builder.AddMetadata(key, value);
        return this;
    }

    public ZLinkSessionStreamCallBase<TMessage> Compress()
    {
        _builder.EnableCompression();
        return this;
    }

    protected async ValueTask<ZLinkSubmitResult> ExecuteAsync(
        CancellationToken cancellationToken,
        bool validateBeforeCancellation = false)
    {
        if (!validateBeforeCancellation) cancellationToken.ThrowIfCancellationRequested();
        var frame = _builder.Build(
            (codec, flags, messageName, metadata) => CreateHeader(
                codec,
                flags,
                messageName,
                metadata,
                context.CurrentDispatchContext),
            out var header);
        if (validateBeforeCancellation && cancellationToken.IsCancellationRequested)
        {
            frame.Dispose();
            cancellationToken.ThrowIfCancellationRequested();
        }
        var result = await context.SubmitAsync(frame, cancellationToken).ConfigureAwait(false);
        if (result.Status == ZLinkSubmitStatus.Submitted) context.TraceWritten(header);
        return result;
    }

    protected abstract ZlinkStreamHeader CreateHeader(
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags,
        string messageName,
        ZlinkStreamMetadata metadata,
        ZLinkSessionDispatchContext? currentDispatch);
}

internal sealed class ZLinkSessionSendCall<TMessage>(
    ZLinkSessionContext context,
    TMessage message)
    : ZLinkSessionStreamCallBase<TMessage>(context, message), IZLinkSessionSendCall
{
    IZLinkSessionSendCall IZLinkMetadataCall<IZLinkSessionSendCall>.Metadata(
        string key,
        string value)
    {
        return (IZLinkSessionSendCall)Metadata(key, value);
    }

    IZLinkSessionSendCall IZLinkMetadataCall<IZLinkSessionSendCall>.Metadata(
        ZLinkMessageMetadata metadata)
    {
        return (IZLinkSessionSendCall)Metadata(metadata);
    }

    IZLinkSessionSendCall IZLinkSessionSendCall.Compress()
    {
        return (IZLinkSessionSendCall)Compress();
    }

    public ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
        return ExecuteAsync(cancellationToken);
    }

    protected override ZlinkStreamHeader CreateHeader(
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags,
        string messageName,
        ZlinkStreamMetadata metadata,
        ZLinkSessionDispatchContext? currentDispatch)
    {
        _ = currentDispatch;
        return new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            codec,
            flags,
            null,
            messageName,
            metadata,
            ZlinkStreamCorrelation.Next());
    }
}

internal sealed class ZLinkSessionReplyCall<TMessage>(
    ZLinkSessionContext context,
    TMessage message)
    : ZLinkSessionStreamCallBase<TMessage>(context, message), IZLinkSessionReplyCall
{
    IZLinkSessionReplyCall IZLinkSessionReplyCall.Compress()
    {
        return (IZLinkSessionReplyCall)Compress();
    }

    public ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
        return ExecuteAsync(cancellationToken, validateBeforeCancellation: true);
    }

    protected override ZlinkStreamHeader CreateHeader(
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags,
        string messageName,
        ZlinkStreamMetadata metadata,
        ZLinkSessionDispatchContext? currentDispatch)
    {
        if (currentDispatch?.Header?.RequestSeq is not { } requestSeq)
            throw new InvalidOperationException("Reply is only available while handling a request packet.");
        if (!currentDispatch.TryClaimReply())
            throw new InvalidOperationException("The reply token has already been used.");

        return ZLinkStreamReplyHeaders.CreateForRequest(
            currentDispatch.Header,
            ZlinkStreamMessageKind.Response,
            codec,
            flags,
            requestSeq,
            metadata);
    }
}
