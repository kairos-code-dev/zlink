using System.Text.Json;

namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamReceiveDispatcher(
    IZlinkStreamHeaderCodec headerCodec,
    ZlinkStreamPendingRequests pending,
    ZlinkStreamTypedHandlerRegistry typedHandlers,
    ZlinkStreamFrameSender frameSender,
    ZlinkStreamConnectorCallbacks callbacks)
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    public async ValueTask DispatchPacketAsync(ZlinkStreamFrame frame, CancellationToken cancellationToken)
    {
        var header = headerCodec.Decode(frame.Header);

        if (header.Kind == ZlinkStreamMessageKind.Control)
        {
            await DispatchControlAsync(header, frame.Payload, cancellationToken).ConfigureAwait(false);
            return;
        }

        if (pending.TryComplete(header, frame, ParseErrorPayload))
        {
            return;
        }

        if (header.Kind == ZlinkStreamMessageKind.Error && header.RequestSeq is null)
        {
            await callbacks.PublishErrorAsync(ParseErrorPayload(frame.Payload), cancellationToken).ConfigureAwait(false);
            return;
        }

        await DispatchTypedHandlersAsync(header, frame.Payload, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask DispatchControlAsync(
        ZlinkStreamHeader header,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken)
    {
        if (payload.Length != 0)
        {
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.FrameDecodeFailed,
                "Control packet payload must be empty.");
        }

        if (header.Name == ZlinkStreamConnector.HeartbeatPingName)
        {
            await frameSender.SendControlAsync(ZlinkStreamConnector.HeartbeatPongName, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        if (header.Name == ZlinkStreamConnector.HeartbeatPongName)
        {
            return;
        }

        throw ZlinkStreamConnector.Error(
            ZlinkStreamErrorCode.FrameDecodeFailed,
            "Unknown control packet.");
    }

    private async ValueTask DispatchTypedHandlersAsync(
        ZlinkStreamHeader header,
        ReadOnlyMemory<byte> wirePayload,
        CancellationToken cancellationToken)
    {
        var handlers = typedHandlers.Snapshot(header.Name);
        if (handlers.Count == 0)
        {
            return;
        }

        var payload = frameSender.DecompressIfNeeded(header, wirePayload);
        foreach (var handler in handlers)
        {
            try
            {
                var payloadObject = new ZlinkStreamEncodedPayload(header.Codec, payload);
                var message = new ZlinkStreamMessage(header.Name, header.Metadata, payloadObject);
                await callbacks.DispatchUserCallbackAsync(
                        dispatchedToken => handler.Invoke(message, payloadObject, dispatchedToken),
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                await callbacks.PublishErrorAsync(new ZlinkStreamError(
                    ZlinkStreamErrorCode.UserCallbackFailed,
                    "Typed message handler failed.",
                    ex), cancellationToken).ConfigureAwait(false);
            }
        }
    }

    private static ZlinkStreamError ParseErrorPayload(ReadOnlyMemory<byte> payload)
    {
        try
        {
            var dto = JsonSerializer.Deserialize<WireError>(payload.Span, JsonOptions);
            return new ZlinkStreamError(
                ZlinkStreamErrorCode.RemoteError,
                dto?.Message ?? "Remote stream error.");
        }
        catch (Exception ex)
        {
            return new ZlinkStreamError(
                ZlinkStreamErrorCode.FrameDecodeFailed,
                "Remote stream error payload could not be decoded.",
                ex);
        }
    }

    private sealed record WireError(string? Code, string? Message);
}
