
namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionStreamTransport(
    IZLinkStream stream,
    ZLinkSessionRequestTracker requests,
    Zlink.Framework.Runtime.Diagnostics.ZLinkMessageFlowTracer flow)
{
    public bool Write(Message payload)
    {
        if (stream is ZLinkManagedStream managedStream)
        {
            return managedStream.WriteRaw(payload);
        }

        return stream.Write(ZLinkMessage.From(payload.ToArray()));
    }

    public ValueTask SendRawAsync(
        string packetName,
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var correlationId = ZLinkStreamCorrelation.Next();
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            codec,
            ZlinkStreamHeaderFlags.None,
            null,
            packetName,
            ZlinkStreamMetadata.Empty,
            correlationId);
        WriteRawFrame(header, payload.Span, "Client stream send failed.");
        TraceSent(packetName, ZLinkDispatchMessageKind.Send, correlationId);
        return ValueTask.CompletedTask;
    }

    public async ValueTask<Message> RequestRawAsync(
        string packetName,
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> payload,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        using var pending = requests.Start();

        var correlationId = ZLinkStreamCorrelation.Next();
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            codec,
            ZlinkStreamHeaderFlags.HasRequestSeq,
            pending.RequestSeq,
            packetName,
            ZlinkStreamMetadata.Empty,
            correlationId);
        WriteRawFrame(header, payload.Span, "Client stream request send failed.");
        TraceSent(packetName, ZLinkDispatchMessageKind.Request, correlationId);

        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);
        using var registration = timeoutSource.Token.Register(static state =>
        {
            var item = (ZLinkPendingSessionRequest)state!;
            item.Cancel();
        }, pending);

        return await pending.Task.ConfigureAwait(false);
    }

    public ValueTask ReplyRawAsync(
        ZlinkStreamHeader requestHeader,
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> payload,
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
            ZlinkStreamMetadata.Empty,
            requestHeader.CorrelationId);
        WriteRawFrame(header, payload.Span, "Client stream reply send failed.");
        return ValueTask.CompletedTask;
    }

    public ValueTask ReplyRawAsync(
        ZlinkStreamHeader requestHeader,
        ZLinkActorReply reply,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var frame = reply.ToFrame(requestHeader);
        WriteRawBytes(frame, "Client stream reply send failed.");
        return ValueTask.CompletedTask;
    }

    public ValueTask ReplyErrorAsync(
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
            ZlinkStreamMetadata.Empty,
            // Echo the request's correlation id onto the error reply.
            requestHeader.CorrelationId);
        var payload = ZLinkEnvelopeCodec.EncodeJsonBytes(
            new ZLinkStreamWireError(
                exception.GetType().Name,
                exception.Message));
        WriteRawFrame(header, payload, "Client stream error reply send failed.");
        return ValueTask.CompletedTask;
    }

    public async ValueTask<TReply> RequestJsonAsync<TRequest, TReply>(
        TRequest request,
        string packetName,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        using var pending = requests.Start();

        ReadOnlyMemory<byte> payload = ZLinkEnvelopeCodec.EncodeJsonBytes(request);
        var correlationId = ZLinkStreamCorrelation.Next();
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasRequestSeq,
            pending.RequestSeq,
            packetName,
            ZlinkStreamMetadata.Empty,
            correlationId);
        ZLinkStreamFrameWriter.Write(stream, header, payload, "Client stream request send failed.");
        TraceSent(packetName, ZLinkDispatchMessageKind.Request, correlationId);

        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);
        using var registration = timeoutSource.Token.Register(static state =>
        {
            var item = (ZLinkPendingSessionRequest)state!;
            item.Cancel();
        }, pending);

        using var reply = await pending.Task.ConfigureAwait(false);
        return ZLinkClientCallCodec.DecodeJsonReply<TReply>(
            reply.AsReadOnlySpan(),
            "Client stream request reply payload is null.");
    }

    private void WriteRawFrame(
        ZlinkStreamHeader header,
        ReadOnlySpan<byte> payload,
        string failureMessage)
    {
        ZLinkStreamFrameWriter.Write(stream, header, payload, failureMessage);
    }

    private void WriteRawBytes(
        ReadOnlySpan<byte> frame,
        string failureMessage)
    {
        using var message = Message.From(frame);
        if (!Write(message))
        {
            throw new InvalidOperationException(failureMessage);
        }
    }

    private void TraceSent(string packetName, ZLinkDispatchMessageKind kind, string correlationId)
    {
        if (flow.Enabled(ZLinkMessageFlowOutcome.Sent))
        {
            flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.StreamSession,
                kind,
                PacketName: packetName,
                CorrelationId: correlationId));
        }
    }
}
