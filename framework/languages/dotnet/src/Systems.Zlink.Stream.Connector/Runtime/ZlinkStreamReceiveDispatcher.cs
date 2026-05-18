using System.Text.Json;
using System.Threading.Channels;

namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamReceiveDispatcher(
    IZlinkStreamHeaderCodec headerCodec,
    ZlinkStreamPendingRequests pending,
    ZlinkStreamTypedHandlerRegistry typedHandlers,
    Channel<ZlinkStreamHandlerWorkItem> handlerQueue,
    ZlinkStreamFrameSender frameSender,
    ZlinkStreamConnectorCallbacks callbacks)
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    public async ValueTask DispatchPacketAsync(ZlinkStreamFrame frame, CancellationToken cancellationToken)
    {
        ZlinkStreamHeader header;
        try
        {
            header = headerCodec.Decode(frame.Header);
        }
        catch (ZlinkStreamException ex)
        {
            await callbacks.PublishErrorAsync(ex.Error, cancellationToken).ConfigureAwait(false);
            return;
        }
        catch (Exception ex)
        {
            await callbacks.PublishErrorAsync(new ZlinkStreamError(
                ZlinkStreamErrorCode.FrameDecodeFailed,
                "Stream header decode failed.",
                ex), cancellationToken).ConfigureAwait(false);
            return;
        }

        if (pending.TryComplete(header, frame, ParseErrorBody))
        {
            return;
        }

        if (header.Kind == ZlinkStreamMessageKind.Error && header.RequestSeq is null)
        {
            await callbacks.PublishErrorAsync(ParseErrorBody(frame.Payload), cancellationToken).ConfigureAwait(false);
            return;
        }

        await QueueTypedHandlersAsync(header, frame.Payload, cancellationToken).ConfigureAwait(false);
    }

    public async Task HandlerPumpAsync()
    {
        await foreach (var item in handlerQueue.Reader.ReadAllAsync().ConfigureAwait(false))
        {
            await DispatchTypedHandlersAsync(item.Header, item.Payload, item.Handlers, CancellationToken.None)
                .ConfigureAwait(false);
        }
    }

    private ValueTask QueueTypedHandlersAsync(
        ZlinkStreamHeader header,
        ReadOnlyMemory<byte> wirePayload,
        CancellationToken cancellationToken)
    {
        var handlers = typedHandlers.Snapshot(header.Name);
        if (handlers.Count == 0)
        {
            return ValueTask.CompletedTask;
        }

        var payload = frameSender.DecompressIfNeeded(header, wirePayload);
        if (!handlerQueue.Writer.TryWrite(new ZlinkStreamHandlerWorkItem(header, payload, handlers)))
        {
            return callbacks.PublishErrorAsync(
                new ZlinkStreamError(ZlinkStreamErrorCode.Disconnected, "Connector handler queue is closed."),
                cancellationToken);
        }

        return ValueTask.CompletedTask;
    }

    private async ValueTask DispatchTypedHandlersAsync(
        ZlinkStreamHeader header,
        ReadOnlyMemory<byte> payload,
        IReadOnlyList<ZlinkStreamTypedHandlerRegistry.TypedHandler> handlers,
        CancellationToken cancellationToken)
    {
        foreach (var handler in handlers)
        {
            try
            {
                var bodyObject = new ZlinkStreamEncodedPayload(header.Codec, payload);
                await handler.Invoke(new ZlinkStreamMessage(header.Name, header.Metadata, bodyObject), bodyObject, cancellationToken)
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

    private static ZlinkStreamError ParseErrorBody(ReadOnlyMemory<byte> payload)
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
