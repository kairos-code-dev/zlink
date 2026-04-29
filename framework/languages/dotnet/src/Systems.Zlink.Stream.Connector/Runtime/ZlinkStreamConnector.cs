using System.Buffers.Binary;

using System.Security.Authentication;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;

using K4os.Compression.LZ4;

namespace Systems.Zlink.Stream.Connector.Runtime;

public sealed class ZlinkStreamConnector : IAsyncDisposable
{
    private readonly ZlinkStreamPendingRequests _pending = new();
    private readonly ZlinkStreamTypedHandlerRegistry _typedHandlers = new();
    private readonly Channel<ZlinkStreamHandlerWorkItem> _handlerQueue =
        Channel.CreateUnbounded<ZlinkStreamHandlerWorkItem>(new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false
        });
    private readonly SemaphoreSlim _sendGate = new(1, 1);
    private readonly SemaphoreSlim _lifecycleGate = new(1, 1);
    private readonly object _callbackGate = new();
    private readonly IZlinkStreamHeaderCodec _headerCodec;
    private readonly IZlinkStreamCompressionCodec? _compressionCodec;
    private readonly IZlinkStreamPacketNameResolver _nameResolver;
    private IZlinkStreamConnection? _connection;
    private CancellationTokenSource? _receiveCts;
    private Task? _receiveTask;
    private readonly Task _handlerPump;
    private Func<ZlinkStreamError, CancellationToken, ValueTask>? _errorReceived;
    private Func<CancellationToken, ValueTask>? _disconnected;
    private int _disposed;

    public ZlinkStreamConnector(ZlinkStreamConnectorOptions options)
    {
        Options = options ?? throw new ArgumentNullException(nameof(options));
        ZlinkStreamTransportFactory.ValidateOptions(options);

        _headerCodec = options.HeaderCodec ?? new ZlinkStreamHeaderCodec();
        _compressionCodec = options.CompressionCodec
            ?? (options.Compression == ZlinkStreamCompression.Lz4 ? new ZlinkStreamLz4CompressionCodec() : null);
        if (_compressionCodec is not null && _compressionCodec.Compression != options.Compression)
        {
            throw Error(
                ZlinkStreamErrorCode.ConfigurationError,
                "Configured compression codec does not match the connector compression option.");
        }

        _nameResolver = options.NameResolver ?? new ZlinkStreamPacketNameResolver();
        _handlerPump = Task.Run(HandlerPumpAsync);
    }

    public event Func<ZlinkStreamError, CancellationToken, ValueTask>? ErrorReceived
    {
        add
        {
            lock (_callbackGate)
            {
                _errorReceived += value;
            }
        }
        remove
        {
            lock (_callbackGate)
            {
                _errorReceived -= value;
            }
        }
    }

    public event Func<CancellationToken, ValueTask>? Disconnected
    {
        add
        {
            lock (_callbackGate)
            {
                _disconnected += value;
            }
        }
        remove
        {
            lock (_callbackGate)
            {
                _disconnected -= value;
            }
        }
    }

    public bool IsConnected => _connection is not null;

    public ZlinkStreamConnectorOptions Options { get; }

    public static async ValueTask<ZlinkStreamConnector> ConnectAsync(
        ZlinkStreamConnectorOptions options,
        CancellationToken cancellationToken = default)
    {
        var connector = new ZlinkStreamConnector(options);
        await connector.ConnectAsync(cancellationToken).ConfigureAwait(false);
        return connector;
    }

    public async ValueTask ConnectAsync(CancellationToken cancellationToken = default)
    {
        await _lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposed();
            if (_connection is not null)
            {
                return;
            }

            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutCts.CancelAfter(Options.ConnectTimeout);

            try
            {
                _connection = await ZlinkStreamTransportFactory.ConnectAsync(Options, timeoutCts.Token).ConfigureAwait(false);
                _receiveCts = new CancellationTokenSource();
                _receiveTask = Task.Run(() => ReceiveLoopAsync(_receiveCts.Token));
            }
            catch (OperationCanceledException ex) when (!cancellationToken.IsCancellationRequested)
            {
                throw Error(ZlinkStreamErrorCode.ConnectTimeout, "Connect timed out.", ex);
            }
            catch (AuthenticationException ex)
            {
                throw Error(ZlinkStreamErrorCode.TlsValidationFailed, "TLS validation failed.", ex);
            }
            catch (Exception ex) when (ex is not ZlinkStreamException)
            {
                throw Error(ZlinkStreamErrorCode.Disconnected, "Connect failed.", ex);
            }
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }

    public async ValueTask CloseAsync(CancellationToken cancellationToken = default)
    {
        IZlinkStreamConnection? connection;
        CancellationTokenSource? receiveCts;
        Task? receiveTask;

        await _lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            connection = _connection;
            receiveCts = _receiveCts;
            receiveTask = _receiveTask;
            _connection = null;
            _receiveCts = null;
            _receiveTask = null;
        }
        finally
        {
            _lifecycleGate.Release();
        }

        receiveCts?.Cancel();

        if (connection is not null)
        {
            await connection.CloseAsync(cancellationToken).ConfigureAwait(false);
        }

        _pending.FailAll(new ZlinkStreamError(ZlinkStreamErrorCode.Disconnected, "Connector closed."));

        if (receiveTask is not null)
        {
            try
            {
                await receiveTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
        }

        receiveCts?.Dispose();
    }

    public ZlinkStreamSendBuilder Send(ZlinkStreamEncodedBody body)
        => new(this, ResolveNameOrDefault(body), body);

    public ZlinkStreamRequestBuilder Request(ZlinkStreamEncodedBody body)
        => new(this, ResolveNameOrDefault(body), body);

    public IDisposable On(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedBody>, CancellationToken, ValueTask> handler)
    {
        ArgumentNullException.ThrowIfNull(handler);
        ValidateName(name);

        return _typedHandlers.Add(name, handler);
    }

    internal async ValueTask SendEncodedAsync(
        ZlinkStreamMessageKind kind,
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var frame = BuildOutboundFrame(kind, name, body, metadata, compress, null);
        ValidateSendReady(frame.HeaderBytes, frame.BodyBytes);
        using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutCts.CancelAfter(timeout);
        await SendPacketAsync(frame.HeaderBytes, frame.BodyBytes, timeoutCts.Token).ConfigureAwait(false);
    }

    internal async ValueTask<ZlinkStreamEncodedBody> RequestEncodedAsync(
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var pending = _pending.Create();
        var frame = BuildOutboundFrame(ZlinkStreamMessageKind.Request, name, body, metadata, compress, pending.RequestSeq);

        try
        {
            ValidateSendReady(frame.HeaderBytes, frame.BodyBytes);
            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutCts.CancelAfter(timeout);
            await SendPacketAsync(frame.HeaderBytes, frame.BodyBytes, timeoutCts.Token).ConfigureAwait(false);
            var packet = await _pending.WaitAsync(pending, timeout, cancellationToken).ConfigureAwait(false);
            var replyHeader = _headerCodec.Decode(packet.Header);
            var replyBody = DecompressIfNeeded(replyHeader, packet.Body);
            return new ZlinkStreamEncodedBody(replyHeader.Codec, replyBody);
        }
        catch
        {
            _pending.Remove(pending.RequestSeq);
            throw;
        }
    }

    internal void RequestEncoded(
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        Action<ZlinkStreamResult> callback)
    {
        QueueRequestCallback(
            () => RequestEncodedAsync(name, body, metadata, compress, timeout, CancellationToken.None),
            reply => ZlinkStreamResult.Success(),
            ZlinkStreamResult.Failure,
            callback);
    }

    internal void RequestEncoded(
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        Action<ZlinkStreamResult<ZlinkStreamEncodedBody>> callback)
    {
        QueueRequestCallback(
            () => RequestEncodedAsync(name, body, metadata, compress, timeout, CancellationToken.None),
            ZlinkStreamResult<ZlinkStreamEncodedBody>.Success,
            ZlinkStreamResult<ZlinkStreamEncodedBody>.Failure,
            callback);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        await CloseAsync().ConfigureAwait(false);
        _handlerQueue.Writer.TryComplete();
        await _handlerPump.ConfigureAwait(false);
        _sendGate.Dispose();
        _lifecycleGate.Dispose();
    }

    private async Task ReceiveLoopAsync(CancellationToken cancellationToken)
    {
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                var connection = _connection;
                if (connection is null)
                {
                    return;
                }

                var packet = await ZlinkStreamFrameCodec.ReadAsync(connection, cancellationToken).ConfigureAwait(false);
                await DispatchPacketAsync(packet, cancellationToken).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception ex)
        {
            var error = ex is ZlinkStreamException streamException
                ? streamException.Error
                : new ZlinkStreamError(ZlinkStreamErrorCode.FrameDecodeFailed, "Receive loop failed.", ex);
            await PublishErrorAsync(error, CancellationToken.None).ConfigureAwait(false);
        }
        finally
        {
            Interlocked.Exchange(ref _connection, null);
            _pending.FailAll(new ZlinkStreamError(ZlinkStreamErrorCode.Disconnected, "Connector disconnected."));
            var disconnected = SnapshotDisconnected();
            if (disconnected is not null)
            {
                await InvokeUserCallbackAsync(
                    () => disconnected(CancellationToken.None),
                    CancellationToken.None).ConfigureAwait(false);
            }
        }
    }

    private async ValueTask DispatchPacketAsync(ZlinkStreamFrame frame, CancellationToken cancellationToken)
    {
        ZlinkStreamHeader header;
        try
        {
            header = _headerCodec.Decode(frame.Header);
        }
        catch (ZlinkStreamException ex)
        {
            await PublishErrorAsync(ex.Error, cancellationToken).ConfigureAwait(false);
            return;
        }
        catch (Exception ex)
        {
            await PublishErrorAsync(new ZlinkStreamError(
                ZlinkStreamErrorCode.FrameDecodeFailed,
                "Stream header decode failed.",
                ex), cancellationToken).ConfigureAwait(false);
            return;
        }

        if (_pending.TryComplete(header, frame, ParseErrorBody))
        {
            return;
        }

        if (header.Kind == ZlinkStreamMessageKind.Error && header.RequestSeq is null)
        {
            await PublishErrorAsync(ParseErrorBody(frame.Body), cancellationToken).ConfigureAwait(false);
            return;
        }

        await QueueTypedHandlersAsync(header, frame.Body, cancellationToken).ConfigureAwait(false);
    }

    private ValueTask QueueTypedHandlersAsync(
        ZlinkStreamHeader header,
        ReadOnlyMemory<byte> body,
        CancellationToken cancellationToken)
    {
        var handlers = _typedHandlers.Snapshot(header.Name);
        if (handlers.Count == 0)
        {
            return ValueTask.CompletedTask;
        }

        var payload = DecompressIfNeeded(header, body);
        if (!_handlerQueue.Writer.TryWrite(new ZlinkStreamHandlerWorkItem(header, payload, handlers)))
        {
            return PublishErrorAsync(
                new ZlinkStreamError(ZlinkStreamErrorCode.Disconnected, "Connector handler queue is closed."),
                cancellationToken);
        }

        return ValueTask.CompletedTask;
    }

    private async Task HandlerPumpAsync()
    {
        await foreach (var item in _handlerQueue.Reader.ReadAllAsync().ConfigureAwait(false))
        {
            await DispatchTypedHandlersAsync(item.Header, item.Payload, item.Handlers, CancellationToken.None)
                .ConfigureAwait(false);
        }
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
                var bodyObject = new ZlinkStreamEncodedBody(header.Codec, payload);
                await handler.Invoke(new ZlinkStreamMessage(header.Name, header.Metadata, bodyObject), bodyObject, cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                await PublishErrorAsync(new ZlinkStreamError(
                    ZlinkStreamErrorCode.UserCallbackFailed,
                    "Typed message handler failed.",
                    ex), cancellationToken).ConfigureAwait(false);
            }
        }
    }

    private void ValidateSendReady(ReadOnlyMemory<byte> header, ReadOnlyMemory<byte> body)
    {
        ZlinkStreamFrameCodec.ValidateSendFrame(header.Length, body.Length, Options.MaxSendFrameSize);
        if (_connection is null)
        {
            throw Error(ZlinkStreamErrorCode.Disconnected, "Connector is not connected.");
        }
    }

    private async ValueTask SendPacketAsync(
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> body,
        CancellationToken cancellationToken)
    {
        var connection = _connection;
        if (connection is null)
        {
            throw Error(ZlinkStreamErrorCode.Disconnected, "Connector is not connected.");
        }

        try
        {
            await _sendGate.WaitAsync(cancellationToken).ConfigureAwait(false);
            try
            {
                if (Options.EnableSegmentedSend && connection.CanWriteSegments)
                {
                    await connection.WriteAsync(
                        ZlinkStreamFrameCodec.EncodePrefix(header.Length, body.Length),
                        cancellationToken).ConfigureAwait(false);
                    if (header.Length > 0)
                    {
                        await connection.WriteAsync(header, cancellationToken).ConfigureAwait(false);
                    }

                    if (body.Length > 0)
                    {
                        await connection.WriteAsync(body, cancellationToken).ConfigureAwait(false);
                    }
                }
                else
                {
                    var frame = ZlinkStreamFrameCodec.Encode(header, body, Options.MaxSendFrameSize);
                    await connection.WriteAsync(frame, cancellationToken).ConfigureAwait(false);
                }
            }
            finally
            {
                _sendGate.Release();
            }
        }
        catch (Exception ex) when (ex is not ZlinkStreamException)
        {
            throw Error(ZlinkStreamErrorCode.SendFailed, "Send failed.", ex);
        }
    }

    private ReadOnlyMemory<byte> EncodeHeaderForSend(ZlinkStreamHeader header)
    {
        var encoded = _headerCodec.Encode(header);
        var metadataLength = ZlinkStreamHeaderCodec.GetMetadataPayloadSize(header.Metadata);
        if (metadataLength > Options.MaxSendMetadataSize)
        {
            throw Error(
                ZlinkStreamErrorCode.ValidationFailed,
                $"Metadata payload exceeds MaxSendMetadataSize ({Options.MaxSendMetadataSize}).");
        }

        return encoded;
    }

    private ZlinkStreamOutboundFrame BuildOutboundFrame(
        ZlinkStreamMessageKind kind,
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        ZlinkStreamRequestSeq? requestSeq)
    {
        ValidateName(name);
        var bodyBytes = body.Body;
        var flags = requestSeq is null
            ? ZlinkStreamHeaderFlags.None
            : ZlinkStreamHeaderFlags.HasRequestSeq;

        if (compress)
        {
            bodyBytes = CompressBody(bodyBytes);
            flags |= ZlinkStreamHeaderFlags.BodyCompressed;
        }

        var header = new ZlinkStreamHeader(kind, body.Codec, flags, requestSeq, name, metadata);
        return new ZlinkStreamOutboundFrame(header, EncodeHeaderForSend(header), bodyBytes);
    }

    private ReadOnlyMemory<byte> CompressBody(ReadOnlyMemory<byte> body)
    {
        if (Options.Compression == ZlinkStreamCompression.None || _compressionCodec is null)
        {
            throw Error(ZlinkStreamErrorCode.CompressionFailed, "Compression codec is not configured.");
        }

        try
        {
            return _compressionCodec.Compress(body);
        }
        catch (Exception ex)
        {
            throw Error(ZlinkStreamErrorCode.CompressionFailed, "Compression failed.", ex);
        }
    }

    private ReadOnlyMemory<byte> DecompressIfNeeded(ZlinkStreamHeader header, ReadOnlyMemory<byte> body)
    {
        if (!header.Flags.HasFlag(ZlinkStreamHeaderFlags.BodyCompressed))
        {
            return body;
        }

        if (Options.Compression == ZlinkStreamCompression.None || _compressionCodec is null)
        {
            throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Compressed body received without a compression codec.");
        }

        try
        {
            return _compressionCodec.Decompress(body);
        }
        catch (Exception ex)
        {
            throw Error(ZlinkStreamErrorCode.DecompressionFailed, "Decompression failed.", ex);
        }
    }

    private string ResolveName(Type bodyType)
    {
        var name = _nameResolver.Resolve(bodyType);
        ValidateName(name);
        return name;
    }

    private string? ResolveNameOrDefault(ZlinkStreamEncodedBody body)
    {
        if (body.MessageType is null)
        {
            return null;
        }

        return ResolveName(body.MessageType);
    }

    private async ValueTask PublishErrorAsync(ZlinkStreamError error, CancellationToken cancellationToken)
    {
        var handler = SnapshotErrorReceived();
        if (handler is not null)
        {
            await InvokeUserCallbackAsync(() => handler(error, cancellationToken), cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private async ValueTask InvokeUserCallbackAsync(
        Func<ValueTask> callback,
        CancellationToken cancellationToken)
    {
        try
        {
            await callback().ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            var handler = SnapshotErrorReceived();
            if (handler is not null)
            {
                try
                {
                    await handler(new ZlinkStreamError(
                        ZlinkStreamErrorCode.UserCallbackFailed,
                        "User callback failed.",
                        ex), cancellationToken).ConfigureAwait(false);
                }
                catch
                {
                }
            }
        }
    }

    private void QueueRequestCallback<TResult>(
        Func<ValueTask<ZlinkStreamEncodedBody>> request,
        Func<ZlinkStreamEncodedBody, TResult> success,
        Func<ZlinkStreamError, TResult> failure,
        Action<TResult> callback)
    {
        _ = Task.Run(async () =>
        {
            try
            {
                var reply = await request().ConfigureAwait(false);
                callback(success(reply));
            }
            catch (ZlinkStreamException ex)
            {
                callback(failure(ex.Error));
            }
            catch (Exception ex)
            {
                callback(failure(new ZlinkStreamError(
                    ZlinkStreamErrorCode.SendFailed,
                    ex.Message,
                    ex)));
            }
        });
    }

    private Func<ZlinkStreamError, CancellationToken, ValueTask>? SnapshotErrorReceived()
    {
        lock (_callbackGate)
        {
            return _errorReceived;
        }
    }

    private Func<CancellationToken, ValueTask>? SnapshotDisconnected()
    {
        lock (_callbackGate)
        {
            return _disconnected;
        }
    }

    private static ZlinkStreamError ParseErrorBody(ReadOnlyMemory<byte> body)
    {
        try
        {
            var dto = JsonSerializer.Deserialize<WireError>(body.Span);
            return new ZlinkStreamError(
                ZlinkStreamErrorCode.RemoteError,
                dto?.Message ?? "Remote stream error.");
        }
        catch (Exception ex)
        {
            return new ZlinkStreamError(
                ZlinkStreamErrorCode.FrameDecodeFailed,
                "Remote stream error body could not be decoded.",
                ex);
        }
    }

    internal static void ValidateName(string name)
    {
        if (string.IsNullOrEmpty(name))
        {
            throw Error(ZlinkStreamErrorCode.ValidationFailed, "Message name must not be empty.");
        }

        if (Encoding.UTF8.GetByteCount(name) > byte.MaxValue)
        {
            throw Error(ZlinkStreamErrorCode.ValidationFailed, "Message name must not exceed 255 UTF-8 bytes.");
        }
    }

    private void ThrowIfDisposed()
    {
        if (_disposed != 0)
        {
            throw new ObjectDisposedException(nameof(ZlinkStreamConnector));
        }
    }

    internal static ZlinkStreamException Error(
        ZlinkStreamErrorCode code,
        string message,
        Exception? exception = null)
        => new(new ZlinkStreamError(code, message, exception));

    private sealed record WireError(string? Code, string? Message);
}
