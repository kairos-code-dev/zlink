namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionStreamTransport(IZLinkStream stream)
{
    public bool Write(Message payload)
    {
        if (stream is ZLinkManagedStream managedStream) return managedStream.WriteRaw(payload);

        return stream.Write(ZLinkMessage.From(payload.ToArray()));
    }

    public ValueTask ReplyRawAsync(
        ZlinkStreamHeader requestHeader,
        ZLinkActorReply reply,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var frame = reply.ToFrame(requestHeader);
        using var message = Message.From(frame);
        if (!Write(message)) throw new InvalidOperationException("Client stream reply send failed.");
        return ValueTask.CompletedTask;
    }

    public ValueTask ReplyErrorAsync(
        ZlinkStreamHeader requestHeader,
        Exception exception,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (requestHeader.RequestSeq is not { } requestSeq) return ValueTask.CompletedTask;

        var header = ZLinkStreamReplyHeaders.CreateForRequest(
            requestHeader,
            ZlinkStreamMessageKind.Error,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.None,
            requestSeq,
            ZlinkStreamMetadata.Empty);
        var payload = ZLinkEnvelopeCodec.EncodeJsonBytes(
            new ZLinkStreamWireError(
                exception.GetType().Name,
                exception.Message));
        ZLinkStreamFrameWriter.Write(stream, header, (ReadOnlySpan<byte>)payload, "Client stream error reply send failed.");
        return ValueTask.CompletedTask;
    }
}
