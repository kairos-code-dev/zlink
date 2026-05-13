using System.Text.Json;
using Systems.Zlink.Stream.Connector.Protocol;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionStreamTransport(
    IZLinkStream stream,
    ZLinkSessionRequestTracker requests)
{
    private static readonly ZlinkStreamHeaderCodec HeaderCodec = new();

    public bool Write(Message payload)
    {
        return stream.Write(payload);
    }

    public ValueTask SendRawAsync(
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

    public async ValueTask<Message> RequestRawAsync(
        string packetName,
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> body,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        using var pending = requests.Start();

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

    public ValueTask ReplyRawAsync(
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
            ZlinkStreamMetadata.Empty);
        var body = JsonSerializer.SerializeToUtf8Bytes(
            new ZLinkStreamWireError(
                exception.GetType().Name,
                exception.Message),
            ZLinkJsonSerializerOptions.Default);
        WriteRawFrame(header, body, "Client stream error reply send failed.");
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

        ReadOnlyMemory<byte> body = JsonSerializer.SerializeToUtf8Bytes(request, ZLinkJsonSerializerOptions.Default);
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
        return JsonSerializer.Deserialize<TReply>(reply.AsReadOnlySpan(), ZLinkJsonSerializerOptions.Default)
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
