using System.Buffers.Binary;

using System.Security.Authentication;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;

using K4os.Compression.LZ4;

namespace Systems.Zlink.Stream.Connector.Connector;

public sealed class ZlinkStreamConnector : IAsyncDisposable
{
    private readonly Channel<ZlinkStreamPacket> _rawPackets =
        Channel.CreateUnbounded<ZlinkStreamPacket>(new UnboundedChannelOptions
        {
            SingleReader = false,
            SingleWriter = true
        });
    private readonly ZlinkStreamPendingRequests _pending = new();
    private readonly ZlinkStreamTypedHandlerRegistry _typedHandlers = new();
    private readonly SemaphoreSlim _sendGate = new(1, 1);
    private readonly IZlinkStreamHeaderCodec _headerCodec;
    private readonly IZlinkStreamCodecRegistry _codecRegistry;
    private readonly IZlinkStreamCompressionCodec? _compressionCodec;
    private readonly IZlinkStreamPacketNameResolver _nameResolver;
    private IZlinkStreamConnection? _connection;
    private CancellationTokenSource? _receiveCts;
    private Task? _receiveTask;
    private ReceiveMode _receiveMode;
    private Func<ZlinkStreamPacket, CancellationToken, ValueTask>? _packetReceived;
    private Func<ZlinkStreamError, CancellationToken, ValueTask>? _errorReceived;
    private Func<CancellationToken, ValueTask>? _disconnected;

    public ZlinkStreamConnector(ZlinkStreamConnectorOptions options)
    {
        Options = options ?? throw new ArgumentNullException(nameof(options));
        ZlinkStreamTransportFactory.ValidateOptions(options);

        _headerCodec = options.HeaderCodec ?? new ZlinkStreamHeaderCodec();
        _codecRegistry = options.CodecRegistry ?? new ZlinkStreamCodecRegistry(options.DefaultCodec);
        _compressionCodec = options.CompressionCodec
            ?? (options.Compression == ZlinkStreamCompression.Lz4 ? new ZlinkStreamLz4CompressionCodec() : null);
        if (_compressionCodec is not null && _compressionCodec.Compression != options.Compression)
        {
            throw Error(
                ZlinkStreamErrorCode.ConfigurationError,
                "Configured compression codec does not match the connector compression option.");
        }

        _nameResolver = options.NameResolver ?? new ZlinkStreamPacketNameResolver();
    }

    public event Func<ZlinkStreamPacket, CancellationToken, ValueTask>? PacketReceived
    {
        add
        {
            SetReceiveMode(ReceiveMode.Callback);
            _packetReceived += value;
        }
        remove => _packetReceived -= value;
    }

    public event Func<ZlinkStreamError, CancellationToken, ValueTask>? ErrorReceived
    {
        add => _errorReceived += value;
        remove => _errorReceived -= value;
    }

    public event Func<CancellationToken, ValueTask>? Disconnected
    {
        add => _disconnected += value;
        remove => _disconnected -= value;
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

    public async ValueTask CloseAsync(CancellationToken cancellationToken = default)
    {
        var connection = Interlocked.Exchange(ref _connection, null);
        _receiveCts?.Cancel();

        if (connection is not null)
        {
            await connection.CloseAsync(cancellationToken).ConfigureAwait(false);
        }

        _pending.FailAll(new ZlinkStreamError(ZlinkStreamErrorCode.Disconnected, "Connector closed."));
        _rawPackets.Writer.TryComplete();

        if (_receiveTask is not null)
        {
            try
            {
                await _receiveTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
        }
    }

    public void SendRaw(
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> body,
        CancellationToken cancellationToken = default)
    {
        QueueSend(header, body, Options.SendTimeout, cancellationToken);
    }

    public void SendRaw(
        ZlinkStreamPacket packet,
        CancellationToken cancellationToken = default)
        => SendRaw(packet.Header, packet.Body, cancellationToken);

    public async ValueTask<ZlinkStreamPacket> ReceiveRawAsync(CancellationToken cancellationToken = default)
    {
        SetReceiveMode(ReceiveMode.Raw);
        return await _rawPackets.Reader.ReadAsync(cancellationToken).ConfigureAwait(false);
    }

    public ZlinkStreamSendBuilder<TBody> Send<TBody>(TBody body)
        => new(this, ResolveName(typeof(TBody)), body);

    public ZlinkStreamSendBuilder<TBody> Send<TBody>(string name, TBody body)
        => new(this, name, body);

    public ZlinkStreamRequestBuilder<TBody> Request<TBody>(TBody body)
        => new(this, ResolveName(typeof(TBody)), body);

    public ZlinkStreamRequestBuilder<TBody> Request<TBody>(string name, TBody body)
        => new(this, name, body);

    public IDisposable On<TBody>(
        Func<ZlinkStreamMessage<TBody>, CancellationToken, ValueTask> handler)
        => On(ResolveName(typeof(TBody)), handler);

    public IDisposable On<TBody>(
        string name,
        Func<ZlinkStreamMessage<TBody>, CancellationToken, ValueTask> handler)
    {
        ArgumentNullException.ThrowIfNull(handler);
        ValidateName(name);
        SetReceiveMode(ReceiveMode.Callback);

        return _typedHandlers.Add(name, handler);
    }

    internal void SendTyped<TBody>(
        ZlinkStreamMessageKind kind,
        string name,
        TBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        ValidateName(name);
        var codec = _codecRegistry.ResolveForSend(typeof(TBody));
        var bodyBytes = codec.Serialize(body);
        var flags = ZlinkStreamHeaderFlags.None;

        if (compress)
        {
            bodyBytes = CompressBody(bodyBytes);
            flags |= ZlinkStreamHeaderFlags.BodyCompressed;
        }

        var header = new ZlinkStreamHeader(kind, codec.Codec, flags, null, name, metadata);
        var headerBytes = EncodeHeaderForSend(header);
        QueueSend(headerBytes, bodyBytes, timeout, cancellationToken);
    }

    internal async ValueTask<TReply> RequestTypedAsync<TBody, TReply>(
        string name,
        TBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        ValidateName(name);
        var codec = _codecRegistry.ResolveForSend(typeof(TBody));
        var bodyBytes = codec.Serialize(body);
        var flags = ZlinkStreamHeaderFlags.HasRid;

        if (compress)
        {
            bodyBytes = CompressBody(bodyBytes);
            flags |= ZlinkStreamHeaderFlags.BodyCompressed;
        }

        var pending = _pending.Create(name);

        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            codec.Codec,
            flags,
            pending.RequestId,
            name,
            metadata);

        try
        {
            var headerBytes = EncodeHeaderForSend(header);
            ValidateSendReady(headerBytes, bodyBytes);
            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutCts.CancelAfter(timeout);
            await SendPacketAsync(headerBytes, bodyBytes, timeoutCts.Token).ConfigureAwait(false);
            var packet = await _pending.WaitAsync(pending, timeout, cancellationToken).ConfigureAwait(false);
            var replyHeader = _headerCodec.Decode(packet.Header);
            var replyBody = DecompressIfNeeded(replyHeader, packet.Body);
            var replyCodec = _codecRegistry.ResolveForReceive(typeof(TReply), replyHeader.Codec);
            return replyCodec.Deserialize<TReply>(replyBody);
        }
        catch
        {
            _pending.Remove(pending.RequestId);
            throw;
        }
    }

    internal void RequestTyped<TBody>(
        string name,
        TBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        Action<ZlinkStreamResult> callback)
    {
        _ = Task.Run(async () =>
        {
            try
            {
                await RequestTypedAsync<TBody, object?>(name, body, metadata, compress, timeout, CancellationToken.None)
                    .ConfigureAwait(false);
                callback(ZlinkStreamResult.Success());
            }
            catch (ZlinkStreamException ex)
            {
                callback(ZlinkStreamResult.Failure(ex.Error));
            }
            catch (Exception ex)
            {
                callback(ZlinkStreamResult.Failure(new ZlinkStreamError(
                    ZlinkStreamErrorCode.SendFailed,
                    ex.Message,
                    ex)));
            }
        });
    }

    internal void RequestTyped<TBody, TReply>(
        string name,
        TBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        Action<ZlinkStreamResult<TReply>> callback)
    {
        _ = Task.Run(async () =>
        {
            try
            {
                var reply = await RequestTypedAsync<TBody, TReply>(name, body, metadata, compress, timeout, CancellationToken.None)
                    .ConfigureAwait(false);
                callback(ZlinkStreamResult<TReply>.Success(reply));
            }
            catch (ZlinkStreamException ex)
            {
                callback(ZlinkStreamResult<TReply>.Failure(ex.Error));
            }
            catch (Exception ex)
            {
                callback(ZlinkStreamResult<TReply>.Failure(new ZlinkStreamError(
                    ZlinkStreamErrorCode.SendFailed,
                    ex.Message,
                    ex)));
            }
        });
    }

    public async ValueTask DisposeAsync()
    {
        await CloseAsync().ConfigureAwait(false);
        _sendGate.Dispose();
        _receiveCts?.Dispose();
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
            _rawPackets.Writer.TryComplete();
            if (_disconnected is not null)
            {
                await InvokeUserCallbackAsync(
                    () => _disconnected(CancellationToken.None),
                    CancellationToken.None).ConfigureAwait(false);
            }
        }
    }

    private async ValueTask DispatchPacketAsync(ZlinkStreamPacket packet, CancellationToken cancellationToken)
    {
        ZlinkStreamHeader? header = null;
        try
        {
            header = _headerCodec.Decode(packet.Header);
        }
        catch (ZlinkStreamException)
        {
        }

        if (header is not null && _pending.TryComplete(header, packet, ParseErrorBody))
        {
            return;
        }

        if (_receiveMode != ReceiveMode.Callback)
        {
            await _rawPackets.Writer.WriteAsync(packet, cancellationToken).ConfigureAwait(false);
            return;
        }

        if (header?.Kind == ZlinkStreamMessageKind.Error && header.RequestId is null)
        {
            await PublishErrorAsync(ParseErrorBody(packet.Body), cancellationToken).ConfigureAwait(false);
            return;
        }

        if (_packetReceived is not null)
        {
            await InvokeUserCallbackAsync(
                () => _packetReceived(packet, cancellationToken),
                cancellationToken).ConfigureAwait(false);
        }

        if (header is not null)
        {
            await DispatchTypedHandlersAsync(header, packet.Body, cancellationToken).ConfigureAwait(false);
        }
    }

    private async ValueTask DispatchTypedHandlersAsync(
        ZlinkStreamHeader header,
        ReadOnlyMemory<byte> body,
        CancellationToken cancellationToken)
    {
        var handlers = _typedHandlers.Snapshot(header.Name);
        if (handlers.Count == 0)
        {
            return;
        }

        var payload = DecompressIfNeeded(header, body);
        foreach (var handler in handlers)
        {
            try
            {
                var codec = _codecRegistry.ResolveForReceive(handler.BodyType, header.Codec);
                var bodyObject = codec.Deserialize(handler.BodyType, payload);
                await handler.Invoke(new ZlinkStreamMessage(header.Name, header.Metadata, bodyObject), bodyObject, cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                await PublishErrorAsync(new ZlinkStreamError(
                    ZlinkStreamErrorCode.UserCallbackFailed,
                    "Typed packet handler failed.",
                    ex), cancellationToken).ConfigureAwait(false);
            }
        }
    }

    private void QueueSend(
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> body,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        ValidateSendReady(header, body);
        _ = Task.Run(async () =>
        {
            try
            {
                using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                timeoutCts.CancelAfter(timeout);
                await SendPacketAsync(header, body, timeoutCts.Token).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                var error = ex is ZlinkStreamException streamException
                    ? streamException.Error
                    : new ZlinkStreamError(ZlinkStreamErrorCode.SendFailed, "Send failed.", ex);
                await PublishErrorAsync(error, CancellationToken.None).ConfigureAwait(false);
            }
        });
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

    private void SetReceiveMode(ReceiveMode mode)
    {
        var current = _receiveMode;
        if (current == mode || current == ReceiveMode.Unset)
        {
            _receiveMode = mode;
            return;
        }

        throw Error(
            ZlinkStreamErrorCode.ConfigurationError,
            "ReceiveRawAsync cannot be mixed with event or typed handler receive on the same connector.");
    }

    private async ValueTask PublishErrorAsync(ZlinkStreamError error, CancellationToken cancellationToken)
    {
        if (_errorReceived is not null)
        {
            await InvokeUserCallbackAsync(() => _errorReceived(error, cancellationToken), cancellationToken)
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
            if (_errorReceived is not null)
            {
                await _errorReceived(new ZlinkStreamError(
                    ZlinkStreamErrorCode.UserCallbackFailed,
                    "User callback failed.",
                    ex), cancellationToken).ConfigureAwait(false);
            }
        }
    }

    private static ZlinkStreamError ParseErrorBody(ReadOnlyMemory<byte> body)
    {
        try
        {
            var dto = JsonSerializer.Deserialize<WireError>(body.Span);
            return new ZlinkStreamError(
                ZlinkStreamErrorCode.FrameDecodeFailed,
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
            throw Error(ZlinkStreamErrorCode.ValidationFailed, "Packet name must not be empty.");
        }

        if (Encoding.UTF8.GetByteCount(name) > byte.MaxValue)
        {
            throw Error(ZlinkStreamErrorCode.ValidationFailed, "Packet name must not exceed 255 UTF-8 bytes.");
        }
    }

    internal static ZlinkStreamException Error(
        ZlinkStreamErrorCode code,
        string message,
        Exception? exception = null)
        => new(new ZlinkStreamError(code, message, exception));

    private enum ReceiveMode
    {
        Unset,
        Raw,
        Callback
    }

    private sealed record WireError(string? Code, string? Message);
}
