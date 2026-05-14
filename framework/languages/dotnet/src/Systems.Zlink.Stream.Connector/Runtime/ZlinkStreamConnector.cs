using System.Text;
using System.Threading.Channels;

namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamConnector : IZlinkStreamConnectorInternal
{
    private readonly ZlinkStreamPendingRequests _pending = new();
    private readonly ZlinkStreamTypedHandlerRegistry _typedHandlers = new();
    private readonly Channel<ZlinkStreamHandlerWorkItem> _handlerQueue;
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
    private readonly Task _handlerPump;
    private int _disposed;

    internal ZlinkStreamConnector(ZlinkStreamConnectorOptions options)
    {
        Options = options ?? throw new ArgumentNullException(nameof(options));
        ZlinkStreamTransportFactory.ValidateOptions(options);
        _taskRunner = new ZlinkStreamTaskRunner(_lifetimeCts.Token);
        _callbacks = new ZlinkStreamConnectorCallbacks(_taskRunner);

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
        _lifecycle = new ZlinkStreamConnectorLifecycle(options, _pending, _taskRunner);
        _handlerQueue = Channel.CreateBounded<ZlinkStreamHandlerWorkItem>(
            new BoundedChannelOptions(options.HandlerQueueCapacity)
            {
                SingleReader = true,
                SingleWriter = false,
                FullMode = BoundedChannelFullMode.Wait,
                AllowSynchronousContinuations = false
            });
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
            _handlerQueue,
            _frameSender,
            _callbacks);
        _receiveLoop = new ZlinkStreamReceiveLoop(
            _pending,
            _receiveDispatcher,
            _callbacks,
            () => _lifecycle.Connection,
            _lifecycle.ClearConnection);
        _handlerPump = _taskRunner.Run(
            "stream-handler-pump",
            _ => new ValueTask(_receiveDispatcher.HandlerPumpAsync()));
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

    public bool IsConnected => _lifecycle.IsConnected;

    public ZlinkStreamConnectorOptions Options { get; }

    public async ValueTask ConnectAsync(CancellationToken cancellationToken = default)
    {
        await _lifecycle.ConnectAsync(
                _receiveLoop.RunAsync,
                ThrowIfDisposed,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask CloseAsync(CancellationToken cancellationToken = default)
    {
        await _lifecycle.CloseAsync(cancellationToken).ConfigureAwait(false);
    }

    public IZlinkStreamSendCall Send(ZlinkStreamEncodedBody body)
        => new ZlinkStreamSendBuilder(this, ResolveNameOrDefault(body), body);

    public IZlinkStreamRequestCall Request(ZlinkStreamEncodedBody body)
        => new ZlinkStreamRequestBuilder(this, ResolveNameOrDefault(body), body);

    public IDisposable On(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedBody>, CancellationToken, ValueTask> handler)
    {
        ArgumentNullException.ThrowIfNull(handler);
        ValidateName(name);

        return _typedHandlers.Add(name, handler);
    }

    async ValueTask IZlinkStreamConnectorInternal.SendEncodedAsync(
        ZlinkStreamMessageKind kind,
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        CancellationToken cancellationToken)
    {
        var frame = _frameSender.BuildOutboundFrame(kind, name, body, metadata, compress, null);
        _frameSender.ValidateSendReady(frame.HeaderBytes, frame.BodyBytes);
        await _frameSender.SendPacketAsync(frame.HeaderBytes, frame.BodyBytes, cancellationToken).ConfigureAwait(false);
    }

    async ValueTask<ZlinkStreamEncodedBody> IZlinkStreamConnectorInternal.RequestEncodedAsync(
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await RequestEncodedCoreAsync(
                name,
                body,
                metadata,
                compress,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZlinkStreamEncodedBody> RequestEncodedCoreAsync(
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var pending = _pending.Create();
        var frame = _frameSender.BuildOutboundFrame(ZlinkStreamMessageKind.Request, name, body, metadata, compress, pending.RequestSeq);

        try
        {
            _frameSender.ValidateSendReady(frame.HeaderBytes, frame.BodyBytes);
            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutCts.CancelAfter(timeout);
            await _frameSender.SendPacketAsync(frame.HeaderBytes, frame.BodyBytes, timeoutCts.Token).ConfigureAwait(false);
            var packet = await _pending.WaitAsync(pending, timeout, cancellationToken).ConfigureAwait(false);
            var replyHeader = _headerCodec.Decode(packet.Header);
            var replyBody = _frameSender.DecompressIfNeeded(replyHeader, packet.Body);
            return new ZlinkStreamEncodedBody(replyHeader.Codec, replyBody);
        }
        catch
        {
            _pending.Remove(pending.RequestSeq);
            throw;
        }
    }

    void IZlinkStreamConnectorInternal.RequestEncoded(
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        Action<ZlinkStreamResult> callback)
    {
        _callbacks.QueueRequestCallback(
            () => RequestEncodedCoreAsync(name, body, metadata, compress, timeout, CancellationToken.None),
            reply => ZlinkStreamResult.Success(),
            ZlinkStreamResult.Failure,
            callback);
    }

    void IZlinkStreamConnectorInternal.RequestEncoded(
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        Action<ZlinkStreamResult<ZlinkStreamEncodedBody>> callback)
    {
        _callbacks.QueueRequestCallback(
            () => RequestEncodedCoreAsync(name, body, metadata, compress, timeout, CancellationToken.None),
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
        _lifetimeCts.Cancel();
        _handlerQueue.Writer.TryComplete();
        await _handlerPump.ConfigureAwait(false);
        _sendGate.Dispose();
        _lifecycle.Dispose();
        _lifetimeCts.Dispose();
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

}
