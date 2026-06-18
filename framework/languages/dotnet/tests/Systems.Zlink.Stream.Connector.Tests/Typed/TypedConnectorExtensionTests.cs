using Google.Protobuf.WellKnownTypes;
using Systems.Zlink.Stream.Connector.Codecs;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Contracts.Calls;
using Systems.Zlink.Stream.Connector.MessagePack;
using Systems.Zlink.Stream.Connector.Protobuf;
using Xunit;
using StreamJson = Systems.Zlink.Stream.Connector.Json.ZlinkStreamJsonExtensions;
using JsonConnector = Systems.Zlink.Stream.Connector.Json.ZlinkStreamJsonConnectorExtensions;
using MessagePackConnector = Systems.Zlink.Stream.Connector.MessagePack.ZlinkStreamMessagePackConnectorExtensions;
using ProtobufConnector = Systems.Zlink.Stream.Connector.Protobuf.ZlinkStreamProtobufConnectorExtensions;

public sealed partial class StreamConnectorTests
{
    [Fact]
    public async Task JsonConnectorExtensionsDelegateBuilderAndDecodeReply()
    {
        var connector = new RecordingConnector();

        await JsonConnector.Send(connector, new JsonPayload("send"))
            .PacketName("json.send")
            .Metadata("k", "v")
            .Metadata(ZlinkStreamMetadata.Empty.With("m", "n"))
            .Compress()
            .Async();

        Assert.Equal(ZlinkStreamCodec.Json, connector.SendCall.Payload.Codec);
        Assert.Equal("json.send", connector.SendCall.Name);
        Assert.True(connector.SendCall.Compressed);
        Assert.Equal("v", connector.SendCall.RecordedMetadata.Get("k"));
        Assert.Equal("n", connector.SendCall.RecordedMetadata.Get("m"));

        connector.NextReply = StreamJson.ToJson(new JsonPayload("reply"));
        var reply = await JsonConnector.Request(connector, new JsonPayload("request"))
            .PacketName("json.request")
            .Metadata("rk", "rv")
            .Timeout(TimeSpan.FromSeconds(3))
            .Compress()
            .Async<JsonPayload>();

        Assert.Equal("reply", reply.Text);
        Assert.Equal("json.request", connector.RequestCall.Name);
        Assert.Equal(TimeSpan.FromSeconds(3), connector.RequestCall.TimeoutValue);
        Assert.True(connector.RequestCall.Compressed);

        ZlinkStreamResult<JsonPayload>? callbackResult = null;
        connector.NextCallbackPayloadResult = ZlinkStreamResult<ZlinkStreamEncodedPayload>.Success(
            StreamJson.ToJson(new JsonPayload("callback")));
        JsonConnector.Request(connector, new JsonPayload("request"))
            .Submit<JsonPayload>(result => callbackResult = result);
        Assert.True(callbackResult?.IsSuccess);
        Assert.Equal("callback", callbackResult.GetValueOrDefault().Value!.Text);

        ZlinkStreamResult<JsonPayload>? failedDecode = null;
        connector.NextCallbackPayloadResult = ZlinkStreamResult<ZlinkStreamEncodedPayload>.Success(new ZlinkStreamEncodedPayload(
            ZlinkStreamCodec.Json,
            new byte[] { 0xFF }));
        JsonConnector.Request(connector, new JsonPayload("request"))
            .Submit<JsonPayload>(result => failedDecode = result);
        Assert.False(failedDecode?.IsSuccess);
        Assert.Equal(ZlinkStreamErrorCode.UserCallbackFailed, failedDecode.GetValueOrDefault().Error!.Code);

        connector.RecordReceived("json.notify", StreamJson.ToJson(new JsonPayload("notify")));
        var notify = await JsonConnector.WaitFor<JsonPayload>(connector, "json.notify")
            .Timeout(TimeSpan.FromSeconds(1))
            .Async();
        Assert.Equal("notify", notify.Payload.Text);

        connector.RecordReceived("json.filtered", StreamJson.ToJson(new JsonPayload("first")));
        connector.RecordReceived("json.filtered", StreamJson.ToJson(new JsonPayload("second")));
        var filtered = await JsonConnector.WaitFor<JsonPayload>(connector, "json.filtered")
            .Where(message => message.Payload.Text == "second")
            .Timeout(TimeSpan.FromSeconds(1))
            .Async();
        var remaining = await JsonConnector.WaitFor<JsonPayload>(connector, "json.filtered")
            .Timeout(TimeSpan.FromSeconds(1))
            .Async();
        Assert.Equal("second", filtered.Payload.Text);
        Assert.Equal("first", remaining.Payload.Text);
    }

    [Fact]
    public async Task MessagePackConnectorExtensionsDelegateBuilderAndDecodeReply()
    {
        var connector = new RecordingConnector();

        await MessagePackConnector.Send(connector, new PackedConnectorPayload { Text = "send" })
            .PacketName("packed.send")
            .Metadata("k", "v")
            .Metadata(ZlinkStreamMetadata.Empty.With("m", "n"))
            .Compress()
            .Async();

        Assert.Equal(ZlinkStreamCodec.MessagePack, connector.SendCall.Payload.Codec);
        Assert.Equal("packed.send", connector.SendCall.Name);
        Assert.True(connector.SendCall.Compressed);

        connector.NextReply = new PackedConnectorPayload { Text = "reply" }.ToMsgPack();
        var reply = await MessagePackConnector.Request(connector, new PackedConnectorPayload { Text = "request" })
            .PacketName("packed.request")
            .Metadata("rk", "rv")
            .Timeout(TimeSpan.FromSeconds(2))
            .Compress()
            .Async<PackedConnectorPayload>();

        Assert.Equal("reply", reply.Text);
        Assert.Equal("packed.request", connector.RequestCall.Name);
        Assert.Equal(TimeSpan.FromSeconds(2), connector.RequestCall.TimeoutValue);

        ZlinkStreamResult<PackedConnectorPayload>? callbackResult = null;
        connector.NextCallbackPayloadResult = ZlinkStreamResult<ZlinkStreamEncodedPayload>.Success(
            new PackedConnectorPayload { Text = "callback" }.ToMsgPack());
        MessagePackConnector.Request(connector, new PackedConnectorPayload { Text = "request" })
            .Submit<PackedConnectorPayload>(result => callbackResult = result);
        Assert.True(callbackResult?.IsSuccess);
        Assert.Equal("callback", callbackResult.GetValueOrDefault().Value!.Text);
    }

    [Fact]
    public async Task ProtobufConnectorExtensionsDelegateBuilderAndDecodeReply()
    {
        var connector = new RecordingConnector();
        var payload = new StringValue { Value = "send" };

        await ProtobufConnector.Send(connector, payload)
            .PacketName("proto.send")
            .Metadata("k", "v")
            .Metadata(ZlinkStreamMetadata.Empty.With("m", "n"))
            .Compress()
            .Async();

        Assert.Equal(ZlinkStreamCodec.Protobuf, connector.SendCall.Payload.Codec);
        Assert.Equal("proto.send", connector.SendCall.Name);

        connector.NextReply = new StringValue { Value = "reply" }.ToProto();
        var reply = await ProtobufConnector.Request(connector, new StringValue { Value = "request" })
            .PacketName("proto.request")
            .Metadata("rk", "rv")
            .Timeout(TimeSpan.FromSeconds(4))
            .Compress()
            .Async<StringValue>();

        Assert.Equal("reply", reply.Value);
        Assert.Equal("proto.request", connector.RequestCall.Name);

        ZlinkStreamResult<StringValue>? callbackResult = null;
        connector.NextCallbackPayloadResult = ZlinkStreamResult<ZlinkStreamEncodedPayload>.Success(
            new StringValue { Value = "callback" }.ToProto());
        ProtobufConnector.Request(connector, new StringValue { Value = "request" })
            .Submit<StringValue>(result => callbackResult = result);
        Assert.True(callbackResult?.IsSuccess);
        Assert.Equal("callback", callbackResult.GetValueOrDefault().Value!.Value);
    }

    [Fact]
    public async Task AutoCodecConnectorExtensionsChooseCodecAndDecodeHandlers()
    {
        var connector = new RecordingConnector();

        await ZlinkStreamAutoCodecExtensions.Send(connector, new JsonPayload("json"))
            .PacketName("auto.json")
            .Async();
        Assert.Equal(ZlinkStreamCodec.Json, connector.SendCall.Payload.Codec);

        await ZlinkStreamAutoCodecExtensions.Send(connector, new PackedConnectorPayload { Text = "packed" })
            .PacketName("auto.packed")
            .Async();
        Assert.Equal(ZlinkStreamCodec.MessagePack, connector.SendCall.Payload.Codec);

        await ZlinkStreamAutoCodecExtensions.Send(connector, new StringValue { Value = "proto" })
            .PacketName("auto.proto")
            .Async();
        Assert.Equal(ZlinkStreamCodec.Protobuf, connector.SendCall.Payload.Codec);

        JsonPayload? namedPayload = null;
        using var named = ZlinkStreamAutoCodecExtensions.On<JsonPayload>(
            connector,
            "json.name",
            (message, _) =>
            {
                namedPayload = message.Payload;
                return ValueTask.CompletedTask;
            });
        await connector.InvokeHandler("json.name", StreamJson.ToJson(new JsonPayload("handler")));
        Assert.Equal("handler", namedPayload?.Text);

        PackedConnectorPayload? resolvedPayload = null;
        using var resolved = ZlinkStreamAutoCodecExtensions.On<PackedConnectorPayload>(
            connector,
            (message, _) =>
            {
                resolvedPayload = message.Payload;
                return ValueTask.CompletedTask;
            });
        await connector.InvokeHandler(nameof(PackedConnectorPayload), new PackedConnectorPayload { Text = "resolved" }.ToMsgPack());
        Assert.Equal("resolved", resolvedPayload?.Text);
    }

    private sealed record JsonPayload(string Text);

    [MessagePack.MessagePackObject]
    public sealed class PackedConnectorPayload
    {
        [MessagePack.Key(0)]
        public string Text { get; set; } = string.Empty;
    }

    private sealed class RecordingConnector : IZlinkStreamConnector
    {
        private readonly Dictionary<string, Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, CancellationToken, ValueTask>> _handlers = new(StringComparer.Ordinal);
        private readonly Dictionary<string, List<ZlinkStreamMessage<ZlinkStreamEncodedPayload>>> _received = new(StringComparer.Ordinal);
        private readonly HashSet<(string Name, int Index)> _consumed = [];

        public event Func<ZlinkStreamError, CancellationToken, ValueTask>? ErrorReceived;

        public event Func<CancellationToken, ValueTask>? Disconnected;

        public event Func<ZlinkStreamConnectionStateChanged, CancellationToken, ValueTask>? ConnectionStateChanged;

        public bool IsConnected => true;

        public ZlinkStreamConnectionState State => ZlinkStreamConnectionState.Connected;

        public RecordingConnector(IZlinkStreamPayloadCodec? payloadCodec = null)
        {
            Options = new ZlinkStreamConnectorOptions
            {
                Endpoint = new Uri("tcp://127.0.0.1:1"),
                NameResolver = new TypeNameResolver(),
                PayloadCodec = payloadCodec
            };
        }

        public ZlinkStreamConnectorOptions Options { get; }

        public int PendingDispatchCount => 0;

        public int ReceivedCount(string name)
            => _received.TryGetValue(name, out var messages) ? messages.Count : 0;

        public RecordingSendCall SendCall { get; private set; } = new(new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, Array.Empty<byte>()));

        public RecordingRequestCall RequestCall { get; private set; } = new(new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, Array.Empty<byte>()));

        public ZlinkStreamEncodedPayload NextReply { get; set; } = new(ZlinkStreamCodec.Raw, Array.Empty<byte>());

        public ZlinkStreamResult<ZlinkStreamEncodedPayload> NextCallbackPayloadResult { get; set; } =
            ZlinkStreamResult<ZlinkStreamEncodedPayload>.Failure(new ZlinkStreamError(
                ZlinkStreamErrorCode.RemoteError,
                "missing callback result"));

        public IZlinkStreamLifecycleCall Connect { get; } = new RecordingLifecycleCall();

        public IZlinkStreamLifecycleCall Close { get; } = new RecordingLifecycleCall();

        public IZlinkStreamLifecycleCall Dispatch { get; } = new RecordingLifecycleCall();

        public IZlinkStreamSendCall Send(ZlinkStreamEncodedPayload payload)
            => SendCall = new RecordingSendCall(payload);

        public IZlinkStreamRequestCall Request(ZlinkStreamEncodedPayload payload)
            => RequestCall = new RecordingRequestCall(payload)
            {
                Reply = NextReply,
                CallbackPayloadResult = NextCallbackPayloadResult
            };

        public IDisposable ObserveInbound(
            Func<ZlinkStreamInboundObservation, CancellationToken, ValueTask> observer)
            => new Subscription(() => { });

        public IDisposable On(
            string name,
            Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, CancellationToken, ValueTask> handler)
        {
            _handlers.Add(name, handler);
            return new Subscription(() => _handlers.Remove(name));
        }

        public IZlinkStreamWaitCall WaitFor(string name)
            => new RecordingWaitCall(name, this);

        public ValueTask DisposeAsync()
            => ValueTask.CompletedTask;

        private ValueTask<ZlinkStreamMessage<ZlinkStreamEncodedPayload>> WaitForRecordedAsync(
            string name,
            TimeSpan timeout,
            CancellationToken cancellationToken = default)
            => WaitForRecordedAsync(name, static _ => true, timeout, cancellationToken);

        private ValueTask<ZlinkStreamMessage<ZlinkStreamEncodedPayload>> WaitForRecordedAsync(
            string name,
            Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, bool> predicate,
            TimeSpan timeout,
            CancellationToken cancellationToken = default)
        {
            _ = timeout;
            cancellationToken.ThrowIfCancellationRequested();
            if (!_received.TryGetValue(name, out var messages))
            {
                throw new TimeoutException($"Timed out waiting for {name}.");
            }

            for (var i = 0; i < messages.Count; i++)
            {
                var message = messages[i];
                var key = (name, i);
                if (_consumed.Contains(key) || !predicate(message))
                {
                    continue;
                }

                _consumed.Add(key);
                return ValueTask.FromResult(message);
            }

            throw new TimeoutException($"Timed out waiting for {name}.");
        }

        private sealed class RecordingWaitCall : IZlinkStreamWaitCall
        {
            private readonly string _name;
            private readonly RecordingConnector _connector;
            private Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, bool>? _predicate;
            private TimeSpan? _timeout;

            public RecordingWaitCall(string name, RecordingConnector connector)
            {
                _name = name;
                _connector = connector;
            }

            public IZlinkStreamWaitCall Timeout(TimeSpan timeout)
            {
                _timeout = timeout;
                return this;
            }

            public IZlinkStreamWaitCall Where(Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, bool> predicate)
            {
                ArgumentNullException.ThrowIfNull(predicate);
                var previous = _predicate;
                _predicate = previous is null
                    ? predicate
                    : message => previous(message) && predicate(message);
                return this;
            }

            public ValueTask<ZlinkStreamMessage<ZlinkStreamEncodedPayload>> Async(
                CancellationToken cancellationToken = default)
                => _connector.WaitForRecordedAsync(
                    _name,
                    _predicate ?? (static _ => true),
                    _timeout ?? _connector.Options.WaitTimeout,
                    cancellationToken);
        }

        public ValueTask InvokeHandler(string name, ZlinkStreamEncodedPayload payload)
            => _handlers[name](new ZlinkStreamMessage<ZlinkStreamEncodedPayload>(
                name,
                ZlinkStreamMetadata.Empty,
                payload), CancellationToken.None);

        public void RecordReceived(string name, ZlinkStreamEncodedPayload payload)
        {
            if (!_received.TryGetValue(name, out var messages))
            {
                messages = [];
                _received.Add(name, messages);
            }

            messages.Add(new ZlinkStreamMessage<ZlinkStreamEncodedPayload>(
                name,
                ZlinkStreamMetadata.Empty,
                payload));
        }

        private sealed class TypeNameResolver : IZlinkStreamPacketNameResolver
        {
            public string Resolve(global::System.Type payloadType) => payloadType.Name;
        }

        private sealed class Subscription : IDisposable
        {
            private readonly Action _dispose;

            public Subscription(Action dispose) => _dispose = dispose;

            public void Dispose() => _dispose();
        }

        private sealed class RecordingLifecycleCall : IZlinkStreamLifecycleCall
        {
            public ValueTask Async(CancellationToken cancellationToken = default)
                => ValueTask.CompletedTask;
        }
    }

    private sealed class RecordingSendCall : IZlinkStreamSendCall
    {
        public RecordingSendCall(ZlinkStreamEncodedPayload payload) => Payload = payload;

        public ZlinkStreamEncodedPayload Payload { get; }

        public string? Name { get; private set; }

        public ZlinkStreamMetadata RecordedMetadata { get; private set; } = ZlinkStreamMetadata.Empty;

        public bool Compressed { get; private set; }

        public IZlinkStreamSendCall PacketName(string name)
        {
            Name = name;
            return this;
        }

        public IZlinkStreamSendCall Metadata(string key, string value)
        {
            RecordedMetadata = RecordedMetadata.With(key, value);
            return this;
        }

        public IZlinkStreamSendCall Metadata(ZlinkStreamMetadata metadata)
        {
            RecordedMetadata = RecordedMetadata.WithMany(metadata.Values);
            return this;
        }

        public IZlinkStreamSendCall Compress()
        {
            Compressed = true;
            return this;
        }

        public ValueTask Async(CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;
    }

    private sealed class RecordingRequestCall : IZlinkStreamRequestCall
    {
        public RecordingRequestCall(ZlinkStreamEncodedPayload payload) => Payload = payload;

        public ZlinkStreamEncodedPayload Payload { get; }

        public string? Name { get; private set; }

        public ZlinkStreamMetadata RecordedMetadata { get; private set; } = ZlinkStreamMetadata.Empty;

        public TimeSpan? TimeoutValue { get; private set; }

        public bool Compressed { get; private set; }

        public ZlinkStreamEncodedPayload Reply { get; set; } = new(ZlinkStreamCodec.Raw, Array.Empty<byte>());

        public ZlinkStreamResult<ZlinkStreamEncodedPayload> CallbackPayloadResult { get; set; } =
            ZlinkStreamResult<ZlinkStreamEncodedPayload>.Failure(new ZlinkStreamError(
            ZlinkStreamErrorCode.RemoteError,
            "missing callback result"));

        public IZlinkStreamRequestCall PacketName(string name)
        {
            Name = name;
            return this;
        }

        public IZlinkStreamRequestCall Metadata(string key, string value)
        {
            RecordedMetadata = RecordedMetadata.With(key, value);
            return this;
        }

        public IZlinkStreamRequestCall Metadata(ZlinkStreamMetadata metadata)
        {
            RecordedMetadata = RecordedMetadata.WithMany(metadata.Values);
            return this;
        }

        public IZlinkStreamRequestCall Timeout(TimeSpan timeout)
        {
            TimeoutValue = timeout;
            return this;
        }

        public IZlinkStreamRequestCall Compress()
        {
            Compressed = true;
            return this;
        }

        public ValueTask<ZlinkStreamEncodedPayload> Async(CancellationToken cancellationToken = default)
            => ValueTask.FromResult(Reply);

        public void Submit(Action<ZlinkStreamResult> callback)
            => callback(CallbackPayloadResult.IsSuccess
                ? ZlinkStreamResult.Success()
                : ZlinkStreamResult.Failure(CallbackPayloadResult.Error!));

        public void Submit(Action<ZlinkStreamResult<ZlinkStreamEncodedPayload>> callback)
            => callback(CallbackPayloadResult);
    }
}
