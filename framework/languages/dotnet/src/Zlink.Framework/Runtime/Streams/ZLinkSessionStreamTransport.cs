namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionStreamTransport(
    IZLinkStream stream,
    Action<ZlinkStreamHeader> traceWritten)
{
    public bool Write(Message payload)
    {
        if (stream is ZLinkManagedStream managedStream)
            return managedStream.WriteRaw(payload, SendFlags.DontWait);

        return stream.Write(ZLinkMessage.From(payload.ToArray()), SendFlags.DontWait);
    }

    public ValueTask ReplyRawAsync(
        ZlinkStreamHeader requestHeader,
        ZLinkActorReply reply,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var responseHeader = reply.CreateResponseHeader(requestHeader);
        var frame = reply.EncodeFrame(responseHeader);
        using var message = Message.From(frame);
        if (!Write(message)) throw new InvalidOperationException("Client stream reply send failed.");
        traceWritten(responseHeader);
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
        traceWritten(header);
        return ValueTask.CompletedTask;
    }
}
