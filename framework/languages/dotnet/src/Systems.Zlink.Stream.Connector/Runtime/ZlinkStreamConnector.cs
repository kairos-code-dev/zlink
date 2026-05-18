using System.Text;

namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamConnector : IZlinkStreamConnectorInternal
{
    internal const string ReservedPacketNamePrefix = "$zlink.";
    internal const string HeartbeatPingName = "$zlink.heartbeat.ping";
    internal const string HeartbeatPongName = "$zlink.heartbeat.pong";

    private readonly ZlinkStreamPendingRequests _pending = new();
    private readonly ZlinkStreamTypedHandlerRegistry _typedHandlers = new();
    private readonly SemaphoreSlim _sendGate = new(1, 1);
    private readonly CancellationTokenSource _lifetimeCts = new();
    private readonly ZlinkStreamTaskRunner _taskRunner;
    private readonly ZlinkStreamConnectorCallbacks _callbacks;
    private readonly IZlinkStreamHeaderCodec _headerCodec;
    private readonly IZlinkStreamCompressionCodec? _compressionCodec;
    private readonly IZlinkStreamPacketNameResolver _nameResolver;
    private readonly ZlinkStreamConnectorLifecycle _lifecycle;
    private readonly ZlinkStreamFrameSender _frameSender;
    private readonly ZlinkStreamReceiveDispatcher _receiveDispatcher;
    private readonly ZlinkStreamReceiveLoop _receiveLoop;
    private int _disposed;

    internal ZlinkStreamConnector(ZlinkStreamConnectorOptions options)
    {
        Options = options ?? throw new ArgumentNullException(nameof(options));
        ZlinkStreamTransportFactory.ValidateOptions(options);
        _taskRunner = new ZlinkStreamTaskRunner(_lifetimeCts.Token);
        _callbacks = new ZlinkStreamConnectorCallbacks(_taskRunner);

        _headerCodec = options.HeaderCodec;
        _compressionCodec = CreateCompressionCodec(options.Compression);

        _nameResolver = options.NameResolver;
        _lifecycle = new ZlinkStreamConnectorLifecycle(options, _pending, _taskRunner, _callbacks);
        _frameSender = new ZlinkStreamFrameSender(
            options,
            _headerCodec,
            _compressionCodec,
            _sendGate,
            () => _lifecycle.Connection);
        _receiveDispatcher = new ZlinkStreamReceiveDispatcher(
            _headerCodec,
            _pending,
            _typedHandlers,
            _frameSender,
            _callbacks);
        _receiveLoop = new ZlinkStreamReceiveLoop(
            _receiveDispatcher,
            () => _lifecycle.Connection,
            _lifecycle.RecordInbound);
    }

    public event Func<ZlinkStreamError, CancellationToken, ValueTask>? ErrorReceived
    {
        add
        {
            _callbacks.AddErrorReceived(value);
        }
        remove
        {
            _callbacks.RemoveErrorReceived(value);
        }
    }

    public event Func<CancellationToken, ValueTask>? Disconnected
    {
        add
        {
            _callbacks.AddDisconnected(value);
        }
        remove
        {
            _callbacks.RemoveDisconnected(value);
        }
    }

    public event Func<ZlinkStreamConnectionStateChanged, CancellationToken, ValueTask>? ConnectionStateChanged
    {
        add
        {
            _callbacks.AddConnectionStateChanged(value);
        }
        remove
        {
            _callbacks.RemoveConnectionStateChanged(value);
        }
    }

    public bool IsConnected => _lifecycle.IsConnected;

    public ZlinkStreamConnectionState State => _lifecycle.State;

    public ZlinkStreamConnectorOptions Options { get; }

    public async ValueTask ConnectAsync(CancellationToken cancellationToken = default)
    {
        await _lifecycle.ConnectAsync(
                _receiveLoop.RunAsync,
                SendHeartbeatPingAsync,
                ThrowIfDisposed,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask CloseAsync(CancellationToken cancellationToken = default)
    {
        await _lifecycle.CloseAsync(cancellationToken).ConfigureAwait(false);
    }

    public IZlinkStreamSendCall Send(ZlinkStreamEncodedPayload payload)
        => new ZlinkStreamSendBuilder(this, ResolveNameOrDefault(payload), payload);

    public IZlinkStreamRequestCall Request(ZlinkStreamEncodedPayload payload)
        => new ZlinkStreamRequestBuilder(this, ResolveNameOrDefault(payload), payload);

    public IDisposable On(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, CancellationToken, ValueTask> handler)
    {
        ArgumentNullException.ThrowIfNull(handler);
        ThrowIfClosed();
        ValidateName(name);

        return _typedHandlers.Add(name, handler);
    }

    async ValueTask IZlinkStreamConnectorInternal.SendEncodedAsync(
        ZlinkStreamMessageKind kind,
        string name,
        ZlinkStreamEncodedPayload payload,
        ZlinkStreamMetadata metadata,
        bool compress,
        CancellationToken cancellationToken)
    {
        var frame = _frameSender.BuildOutboundFrame(kind, name, payload, metadata, compress, null);
        _frameSender.ValidateSendReady(frame.HeaderBytes, frame.PayloadBytes);
        try
        {
            await _frameSender.SendPacketAsync(frame.HeaderBytes, frame.PayloadBytes, cancellationToken).ConfigureAwait(false);
        }
        catch (ZlinkStreamException ex) when (ex.Error.Code == ZlinkStreamErrorCode.SendFailed)
        {
            await _lifecycle.HandleTransportErrorAsync(ex.Error, cancellationToken).ConfigureAwait(false);
            throw;
        }
    }

    async ValueTask<ZlinkStreamEncodedPayload> IZlinkStreamConnectorInternal.RequestEncodedAsync(
        string name,
        ZlinkStreamEncodedPayload payload,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await RequestEncodedCoreAsync(
                name,
                payload,
                metadata,
                compress,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZlinkStreamEncodedPayload> RequestEncodedCoreAsync(
        string name,
        ZlinkStreamEncodedPayload payload,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var pending = _pending.Create();
        var frame = _frameSender.BuildOutboundFrame(ZlinkStreamMessageKind.Request, name, payload, metadata, compress, pending.RequestSeq);

        try
        {
            _frameSender.ValidateSendReady(frame.HeaderBytes, frame.PayloadBytes);
            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutCts.CancelAfter(timeout);
            try
            {
                await _frameSender.SendPacketAsync(frame.HeaderBytes, frame.PayloadBytes, timeoutCts.Token).ConfigureAwait(false);
            }
            catch (ZlinkStreamException ex) when (ex.Error.Code == ZlinkStreamErrorCode.SendFailed)
            {
                await _lifecycle.HandleTransportErrorAsync(ex.Error, cancellationToken).ConfigureAwait(false);
                throw;
            }

            var packet = await _pending.WaitAsync(pending, timeout, cancellationToken).ConfigureAwait(false);
            var replyHeader = _headerCodec.Decode(packet.Header);
            var replyBody = _frameSender.DecompressIfNeeded(replyHeader, packet.Payload);
            return new ZlinkStreamEncodedPayload(replyHeader.Codec, replyBody);
        }
        catch
        {
            _pending.Remove(pending.RequestSeq);
            throw;
        }
    }

    void IZlinkStreamConnectorInternal.RequestEncoded(
        string name,
        ZlinkStreamEncodedPayload payload,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        Action<ZlinkStreamResult> callback)
    {
        _callbacks.QueueRequestCallback(
            () => RequestEncodedCoreAsync(name, payload, metadata, compress, timeout, CancellationToken.None),
            reply => ZlinkStreamResult.Success(),
            ZlinkStreamResult.Failure,
            callback);
    }

    void IZlinkStreamConnectorInternal.RequestEncoded(
        string name,
        ZlinkStreamEncodedPayload payload,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        Action<ZlinkStreamResult<ZlinkStreamEncodedPayload>> callback)
    {
        _callbacks.QueueRequestCallback(
            () => RequestEncodedCoreAsync(name, payload, metadata, compress, timeout, CancellationToken.None),
            ZlinkStreamResult<ZlinkStreamEncodedPayload>.Success,
            ZlinkStreamResult<ZlinkStreamEncodedPayload>.Failure,
            callback);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        await CloseAsync().ConfigureAwait(false);
        _lifetimeCts.Cancel();
        _sendGate.Dispose();
        _lifecycle.Dispose();
        _lifetimeCts.Dispose();
    }

    private string ResolveName(Type payloadType)
    {
        ThrowIfClosed();
        var name = _nameResolver.Resolve(payloadType);
        ValidateName(name);
        return name;
    }

    private string? ResolveNameOrDefault(ZlinkStreamEncodedPayload payload)
    {
        ThrowIfClosed();
        if (payload.MessageType is null)
        {
            return null;
        }

        return ResolveName(payload.MessageType);
    }

    internal static void ValidateName(string name, bool allowReserved = false)
    {
        if (string.IsNullOrEmpty(name))
        {
            throw Error(ZlinkStreamErrorCode.ValidationFailed, "Message name must not be empty.");
        }

        if (!allowReserved && name.StartsWith(ReservedPacketNamePrefix, StringComparison.Ordinal))
        {
            throw Error(ZlinkStreamErrorCode.ValidationFailed, "Message name uses a reserved zlink prefix.");
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

    private void ThrowIfClosed()
    {
        ThrowIfDisposed();
        if (_lifecycle.State == ZlinkStreamConnectionState.Closed)
        {
            throw new ObjectDisposedException(nameof(ZlinkStreamConnector), "Connector is closed.");
        }
    }

    private async ValueTask SendHeartbeatPingAsync(CancellationToken cancellationToken)
    {
        await _frameSender.SendControlAsync(HeartbeatPingName, cancellationToken).ConfigureAwait(false);
    }

    private static IZlinkStreamCompressionCodec? CreateCompressionCodec(ZlinkStreamCompression compression)
    {
        return compression switch
        {
            ZlinkStreamCompression.None => null,
            ZlinkStreamCompression.Lz4 => new ZlinkStreamLz4CompressionCodec(),
            _ => throw Error(ZlinkStreamErrorCode.ConfigurationError, "Compression option is not supported.")
        };
    }

    internal static ZlinkStreamException Error(
        ZlinkStreamErrorCode code,
        string message,
        Exception? exception = null)
        => new(new ZlinkStreamError(code, message, exception));

}
