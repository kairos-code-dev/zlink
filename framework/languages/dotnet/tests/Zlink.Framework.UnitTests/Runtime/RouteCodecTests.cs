using System.Text;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Channels;
using Zlink.Framework.Runtime.Codecs;
using Zlink.Framework.Runtime.Configuration;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class RouteCodecTests
{
    [Fact]
    public void SocketConfig_Uses_The_Framework_Default_PeerWeight()
    {
        var config = new ZLinkSocketConfig();

        Assert.Equal(ZLinkSocketConfig.DefaultPeerWeight, config.Weight);
    }

    [Fact]
    public void ChannelBundleFactory_Applies_MaxMessageSize_To_BackendSocket()
    {
        var socket = new RecordingSocketOptions();
        var config = new ZLinkSocketConfig
        {
            MaxMessageSize = 4096,
            SendHighWaterMark = 12,
            ReceiveHighWaterMark = 34
        };

        ZLinkChannelBundleFactory.ApplySocketConfig(socket, config);

        Assert.Equal(4096, socket.MaxMessageSize);
        Assert.Equal(12, socket.SendHighWaterMark);
        Assert.Equal(34, socket.ReceiveHighWaterMark);
    }

    [Fact]
    public void ChannelBundleFactory_Applies_ServerRoutingConfig_To_BackendRouter()
    {
        var socket = new RecordingRouter();
        var config = new ZLinkRouteConfig
        {
            RequireKnownPeer = true,
            AllowPeerHandover = true,
            EnablePeerProbe = true,
            ConnectRoutingId = RoutingId.From("next-peer")
        };

        ZLinkChannelBundleFactory.ApplyServerRoutingConfig(socket, config);

        Assert.True(socket.Mandatory);
        Assert.True(socket.Handover);
        Assert.True(socket.Probe);
        Assert.Equal(config.ConnectRoutingId, socket.ConnectRoutingId);
    }

    [Fact]
    public void ChannelBundleFactory_Applies_ClientRoutingConfig_To_BackendDealer()
    {
        var socket = new RecordingRoutingDealer();
        var config = new ZLinkOutboundRouteConfig { ProbeRouterOnConnect = true };

        ZLinkChannelBundleFactory.ApplyClientRoutingConfig(socket, config);

        Assert.True(socket.ProbeRouterOnConnect);
    }

    [Fact]
    public async Task RouteHandlerInvoker_Uses_Configured_Codec()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        codecs.AddSerializer("application/route-test", new RouteProbeSerializer());
        var services = new ServiceCollection()
            .AddSingleton<RouteProbeHandler>()
            .BuildServiceProvider();
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            "play",
            "Probe",
            ZLinkEnvelopeCodec.DefaultContentType,
            "route-request-1",
            null,
            null,
            null,
            null);
        var parts = ZLinkEnvelopeCodec.EncodeParts(header, new RouteProbe("hello"), typeof(RouteProbe), codecs);
        var descriptor = new ZLinkRouteHandlerDescriptor(
            ZLinkMessageKind.Request,
            "play",
            "Probe",
            typeof(RouteProbeHandler),
            typeof(RouteProbe),
            typeof(RouteProbeReply),
            ZLinkHandlerMethodInvokerFactory.Create(
                typeof(RouteProbeHandler).GetMethod(nameof(RouteProbeHandler.HandleAsync))!));

        var invoker = new ZLinkRouteHandlerInvoker(services, codecs);

        var reply = await invoker.InvokeRequestAsync(
            descriptor,
            "play",
            RoutingId.From("source-node"),
            ZLinkEnvelopeCodec.DecodeHeader(parts),
            parts,
            CancellationToken.None);

        Assert.Equal("hello", RouteProbeHandler.LastRequest?.Text);
        Assert.Equal("application/route-test", ZLinkEnvelopeCodec.DecodeHeader(parts).ContentType);
        Assert.Equal(new RouteProbeReply("HELLO"), reply.Message);
    }

    [Fact]
    public void RouteReplyWriter_Uses_Configured_Codec()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        codecs.AddSerializer("application/route-test", new RouteProbeSerializer());
        var router = new RecordingRouter();
        var requestHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            "play",
            "Probe",
            ZLinkEnvelopeCodec.DefaultContentType,
            "corr-1",
            null,
            null,
            null,
            null);

        ZLinkChannelReplyWriter.ReplyEnvelope(
            router,
            RoutingId.From("source-node"),
            7,
            ZLinkChannelReplyWriter.CreateReplyHeader(
                ZLinkMessageKind.Response,
                "play",
                requestHeader),
            new RouteProbe("reply"),
            typeof(RouteProbe),
            codecs);

        Assert.Equal("application/route-test", router.ReplyContentType);
        Assert.Equal("ROUTE:reply", router.ReplyBody);
        Assert.Equal(string.Empty, router.SentHeader?.MessageName);
    }

    [Fact]
    public void RouteConnectionSet_RidAwareConnect_SetsProbeBeforeConnect()
    {
        var router = new RecordingConnectRouter();
        var connections = new ZLinkRouteConnectionSet(router);
        var peerRid = RoutingId.From("route-peer");

        connections.ConnectManual(peerRid, "inproc://route-peer");

        Assert.Equal(peerRid, router.ConnectRoutingId);
        Assert.True(router.ProbeEnabled);
        Assert.Equal(["connect-rid", "probe", "connect"], router.Events);
        Assert.Equal("inproc://route-peer", Assert.Single(router.Connected));
    }

    [Fact]
    public void RouteConnectionSet_Keeps_Physical_Link_Until_Manual_And_Auto_Owners_Release_It()
    {
        var router = new RecordingConnectRouter();
        var connections = new ZLinkRouteConnectionSet(router);
        const string endpoint = "inproc://shared-route-peer";

        Assert.True(connections.ConnectAuto(RoutingId.From("peer"), endpoint));
        connections.ConnectManual(endpoint);
        connections.DisconnectManual(endpoint);

        Assert.Single(router.Connected);
        Assert.Empty(router.Disconnected);

        Assert.True(connections.DisconnectAuto(endpoint));
        Assert.Equal([endpoint], router.Disconnected);
    }

    [Fact]
    public async Task Route_Send_Uses_Monotonic_Correlation_And_Logs_Target_As_SourceRid()
    {
        var loggerFactory = new RecordingLoggerFactory();
        await using var services = new ServiceCollection()
            .AddSingleton<ILoggerFactory>(loggerFactory)
            .BuildServiceProvider();
        var registration = new ZLinkFrameworkRegistration();
        registration.DispatchOptions.MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions);
        var router = new RecordingRouter();
        await using var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(1),
            CancellationToken.None);
        var calls = new ZLinkRouteChannelCalls(
            services,
            null,
            registration,
            "play.route",
            router,
            submitter);
        var target = RoutingId.From("target-node");

        await calls.SubmitSendAsync(target, "RouteProbe", new RouteProbe("payload"), CancellationToken.None);

        var header = Assert.IsType<ZLinkEnvelopeHeader>(router.SentHeader);
        Assert.Matches("^[0-9a-f]+$", Assert.IsType<string>(header.CorrelationId));
        var log = Assert.Single(loggerFactory.Messages);
        Assert.Contains("phase=sent", log, StringComparison.Ordinal);
        Assert.Contains($"corr={header.CorrelationId}", log, StringComparison.Ordinal);
        Assert.Contains($"src={target}", log, StringComparison.Ordinal);
        Assert.DoesNotContain("peerRid=target-node", log, StringComparison.Ordinal);
    }

    [Fact]
    public async Task RouterProbe_AllowsNonInitiatorRidAddressedSendOverInboundIdentity()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var initiator = context.CreateRouterSocket();
        await using var nonInitiator = context.CreateRouterSocket();
        var initiatorRid = RoutingId.From("route-a-initiator");
        var nonInitiatorRid = RoutingId.From("route-z-non-initiator");
        var endpoint = $"inproc://route-inbound-identity-{Guid.NewGuid():N}";

        initiator.SetRoutingId(initiatorRid);
        nonInitiator.SetRoutingId(nonInitiatorRid);
        initiator.Options.Linger = TimeSpan.Zero;
        nonInitiator.Options.Linger = TimeSpan.Zero;
        nonInitiator.Options.Mandatory = true;
        nonInitiator.Bind(endpoint);
        initiator.Options.SetConnectRoutingId(nonInitiatorRid);
        initiator.Options.Probe = true;
        initiator.Connect(endpoint);

        await SendUntilReceivedAsync(
            nonInitiator,
            initiator,
            initiatorRid,
            "reply-from-non-initiator",
            TimeSpan.FromSeconds(3));
    }

    private static ZLinkRouteChannelRuntime CreateRouteChannelRuntime()
        => CreateRouteChannelRuntime(new RecordingRouter());

    private static ZLinkRouteChannelRuntime CreateRouteChannelRuntime(RecordingRouter router)
    {
        var services = new ServiceCollection().BuildServiceProvider();
        return new ZLinkRouteChannelRuntime(
            services,
            new ZLinkFrameworkRegistration(),
            new ZLinkRouteChannelRegistration { RouterChannelId = "route-test" },
            router,
            new ZLinkRouteHandlerRegistry([]),
            null,
            CancellationToken.None,
            new object(),
            errorSink: new ZLinkRuntimeErrorSink());
    }

    [Fact]
    public async Task Route_Runtime_Concurrent_Dispose_Callers_Share_Router_Cleanup()
    {
        var failure = new InvalidOperationException("router cleanup failed");
        var router = new RecordingRouter { BlockDispose = true, DisposeFailure = failure };
        var runtime = CreateRouteChannelRuntime(router);

        var first = runtime.DisposeAsync().AsTask();
        await router.DisposeStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var second = runtime.DisposeAsync().AsTask();

        Assert.Same(first, second);
        Assert.False(second.IsCompleted);
        router.AllowDispose.TrySetResult();
        var firstFailure = await Assert.ThrowsAsync<InvalidOperationException>(
            () => first.WaitAsync(TimeSpan.FromSeconds(5)));
        var secondFailure = await Assert.ThrowsAsync<InvalidOperationException>(
            () => second.WaitAsync(TimeSpan.FromSeconds(5)));
        Assert.Same(failure, firstFailure);
        Assert.Same(firstFailure, secondFailure);
        Assert.Equal(1, router.DisposeCount);
    }

    private static async Task SendUntilReceivedAsync(
        IRouterSocket sender,
        IRouterSocket receiver,
        RoutingId targetRid,
        string payload,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        using var received = Received.Create();
        while (DateTime.UtcNow < deadline)
        {
            using (var message = Message.From(payload))
            {
                try
                {
                    _ = sender.Send(targetRid).Message(message).Submit();
                }
                catch (ZlinkException)
                {
                }
            }

            if (receiver.Recv(received, RecvFlags.DontWait))
            {
                Assert.Equal(payload, received.SinglePartOrThrow().GetString());
                return;
            }

            await Task.Delay(10);
        }

        throw new TimeoutException("Timed out waiting for inbound identity routed send.");
    }

    private sealed record RouteProbe(string Text);

    private sealed record RouteProbeReply(string Text);

    private sealed class RouteProbeHandler : IZLinkRouteRequestHandler<RouteProbe, RouteProbeReply>
    {
        public static RouteProbe? LastRequest { get; private set; }

        public ValueTask<RouteProbeReply> HandleAsync(
            RouteProbe request,
            ZLinkRouteRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            _ = cancellationToken;
            LastRequest = request;
            return ValueTask.FromResult(new RouteProbeReply(request.Text.ToUpperInvariant()));
        }
    }

    private sealed class RouteProbeSerializer : IZLinkMessageSerializer
    {
        public ZLinkEncodedPayload Serialize(object value, Type type)
        {
            var text = value switch
            {
                RouteProbe probe => probe.Text,
                RouteProbeReply reply => reply.Text,
                _ => throw new NotSupportedException(type.FullName)
            };
            return ZLinkEncodedPayload.From(Encoding.UTF8.GetBytes("ROUTE:" + text));
        }

        public object? Deserialize(ZLinkEncodedPayload payload, Type type)
        {
            var text = Encoding.UTF8.GetString(payload.Bytes.Span);
            var value = text.StartsWith("ROUTE:", StringComparison.Ordinal)
                ? text["ROUTE:".Length..]
                : text;
            if (type == typeof(RouteProbe)) return new RouteProbe(value);
            if (type == typeof(RouteProbeReply)) return new RouteProbeReply(value);
            throw new NotSupportedException(type.FullName);
        }
    }

    private sealed class RecordingSocketOptions : IZLinkBackendSocketOptions
    {
        public long MaxMessageSize { get; private set; }

        public int SendHighWaterMark { get; private set; }

        public int ReceiveHighWaterMark { get; private set; }

        public void ApplySocketConfig(IZLinkSocketConfig config)
        {
            if (config.MaxMessageSize > 0) SetMaxMessageSize(config.MaxMessageSize);
            if (config.SendHighWaterMark > 0) SetSendHighWaterMark(config.SendHighWaterMark);
            if (config.ReceiveHighWaterMark > 0) SetReceiveHighWaterMark(config.ReceiveHighWaterMark);
        }

        public ValueTask DisposeAsync()
        {
            return ValueTask.CompletedTask;
        }

        public void Bind(string endpoint)
        {
            throw new NotSupportedException();
        }

        public void SetChannelName(string channelName)
        {
            throw new NotSupportedException();
        }

        public void SetMaxMessageSize(long value)
        {
            MaxMessageSize = value;
        }

        public void SetSendHighWaterMark(int value)
        {
            SendHighWaterMark = value;
        }

        public void SetReceiveHighWaterMark(int value)
        {
            ReceiveHighWaterMark = value;
        }
    }

    private sealed class RecordingRoutingDealer : IZLinkBackendDealerSocket
    {
        public bool ProbeRouterOnConnect { get; private set; }

        public void ApplySocketConfig(IZLinkSocketConfig config) => throw new NotSupportedException();

        public void SetProbe(bool enabled)
        {
            ProbeRouterOnConnect = enabled;
        }

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public void Bind(string endpoint) => throw new NotSupportedException();

        public void SetChannelName(string channelName) => throw new NotSupportedException();

        public void SetMaxMessageSize(long value) => throw new NotSupportedException();

        public void SetSendHighWaterMark(int value) => throw new NotSupportedException();

        public void SetReceiveHighWaterMark(int value) => throw new NotSupportedException();

        public void Connect(string endpoint) => throw new NotSupportedException();

        public void Disconnect(string endpoint) => throw new NotSupportedException();

        public void SetPeerWeight(int weight) => throw new NotSupportedException();

        public int GetPeerWeight() => throw new NotSupportedException();

        public void SetRoutingId(RoutingId routingId) => throw new NotSupportedException();

        public void OnSendReady(Action handler) => throw new NotSupportedException();

        public bool Send(Message message, SendFlags flags) => throw new NotSupportedException();

        public bool Send(IReadOnlyList<Message> parts, SendFlags flags) => throw new NotSupportedException();

        public bool Request(
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => throw new NotSupportedException();

        public bool Request(
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout) => throw new NotSupportedException();

        public Received? Recv(RecvFlags flags = RecvFlags.None) => throw new NotSupportedException();
    }

    private sealed class RecordingRouter : IZLinkBackendRouterSocket
    {
        private int _disposeCount;
        public bool BlockDispose { get; init; }
        public Exception? DisposeFailure { get; init; }
        public TaskCompletionSource DisposeStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        public TaskCompletionSource AllowDispose { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        public int DisposeCount => Volatile.Read(ref _disposeCount);
        public ZLinkEnvelopeHeader? SentHeader { get; private set; }

        public string? ReplyContentType { get; private set; }

        public string? ReplyBody { get; private set; }

        public bool Mandatory { get; private set; }

        public bool Handover { get; private set; }

        public bool Probe { get; private set; }

        public RoutingId ConnectRoutingId { get; private set; }

        public void ApplySocketConfig(IZLinkSocketConfig config) => throw new NotSupportedException();

        public async ValueTask DisposeAsync()
        {
            Interlocked.Increment(ref _disposeCount);
            DisposeStarted.TrySetResult();
            if (BlockDispose) await AllowDispose.Task.ConfigureAwait(false);
            if (DisposeFailure is not null)
                System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(DisposeFailure).Throw();
        }

        public void Bind(string endpoint)
        {
            throw new NotSupportedException();
        }

        public void SetChannelName(string channelName)
        {
            throw new NotSupportedException();
        }

        public void SetMaxMessageSize(long value)
        {
            throw new NotSupportedException();
        }

        public void Connect(string endpoint)
        {
            throw new NotSupportedException();
        }

        public void Disconnect(string endpoint)
        {
            throw new NotSupportedException();
        }

        public void SetPeerWeight(int weight)
        {
            throw new NotSupportedException();
        }

        public int GetPeerWeight()
        {
            throw new NotSupportedException();
        }

        public void OnSendReady(Action handler)
        {
            _ = handler;
        }

        public void SetSendHighWaterMark(int value)
        {
            throw new NotSupportedException();
        }

        public void SetReceiveHighWaterMark(int value)
        {
            throw new NotSupportedException();
        }

        public void SetRoutingId(RoutingId routingId)
        {
            throw new NotSupportedException();
        }

        public void SetConnectRoutingId(RoutingId routingId)
        {
            ConnectRoutingId = routingId;
        }

        public void SetProbe(bool enabled)
        {
            Probe = enabled;
        }

        public void SetMandatory(bool mandatory)
        {
            Mandatory = mandatory;
        }

        public void SetHandover(bool enabled)
        {
            Handover = enabled;
        }

        public Received? Recv(RecvFlags flags = RecvFlags.None)
        {
            throw new NotSupportedException();
        }

        public bool Send(RoutingId routingId, Message message, SendFlags flags)
        {
            _ = routingId;
            _ = flags;
            SentHeader = ZLinkEnvelopeCodec.DecodeHeader(message);
            return true;
        }

        public bool Send(RoutingId routingId, IReadOnlyList<Message> parts, SendFlags flags)
        {
            _ = routingId;
            _ = flags;
            SentHeader = ZLinkEnvelopeCodec.DecodeHeader(parts);
            return true;
        }

        public bool Request(
            RoutingId routingId,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public bool Request(
            RoutingId routingId,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public bool SendToSpot(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            SendFlags flags)
        {
            throw new NotSupportedException();
        }

        public bool RequestToSpot(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public void Reply(RoutingId routingId, ulong requestSeq, Message message)
        {
            throw new NotSupportedException();
        }

        public void Reply(RoutingId routingId, ulong requestSeq, IReadOnlyList<Message> parts)
        {
            _ = routingId;
            _ = requestSeq;
            SentHeader = ZLinkEnvelopeCodec.DecodeHeader(parts);
            ReplyContentType = SentHeader.ContentType;
            ReplyBody = parts[1].GetString();
        }
    }

    private sealed class RecordingLoggerFactory : ILoggerFactory
    {
        public List<string> Messages { get; } = [];

        public void AddProvider(ILoggerProvider provider) => _ = provider;

        public ILogger CreateLogger(string categoryName)
        {
            Assert.Equal(ZLinkMessageFlowTracer.LoggerCategory, categoryName);
            return new RecordingLogger(Messages);
        }

        public void Dispose()
        {
        }
    }

    private sealed class RecordingLogger(List<string> messages) : ILogger
    {
        public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;

        public bool IsEnabled(LogLevel logLevel) => true;

        public void Log<TState>(
            LogLevel logLevel,
            EventId eventId,
            TState state,
            Exception? exception,
            Func<TState, Exception?, string> formatter) => messages.Add(formatter(state, exception));
    }

    private sealed class RecordingConnectRouter : IZLinkBackendRouterSocket
    {
        public List<string> Events { get; } = [];

        public List<string> Connected { get; } = [];

        public List<string> Disconnected { get; } = [];

        public RoutingId? ConnectRoutingId { get; private set; }

        public bool ProbeEnabled { get; private set; }

        public void ApplySocketConfig(IZLinkSocketConfig config) => throw new NotSupportedException();

        public ValueTask DisposeAsync()
        {
            return ValueTask.CompletedTask;
        }

        public void Bind(string endpoint)
        {
            throw new NotSupportedException();
        }

        public void SetChannelName(string channelName)
        {
            throw new NotSupportedException();
        }

        public void SetMaxMessageSize(long value)
        {
            throw new NotSupportedException();
        }

        public void Connect(string endpoint)
        {
            Events.Add("connect");
            Connected.Add(endpoint);
        }

        public void Disconnect(string endpoint)
        {
            Disconnected.Add(endpoint);
        }

        public void SetPeerWeight(int weight)
        {
            throw new NotSupportedException();
        }

        public int GetPeerWeight()
        {
            throw new NotSupportedException();
        }

        public void OnSendReady(Action handler)
        {
            _ = handler;
        }

        public void SetSendHighWaterMark(int value)
        {
            throw new NotSupportedException();
        }

        public void SetReceiveHighWaterMark(int value)
        {
            throw new NotSupportedException();
        }

        public void SetRoutingId(RoutingId routingId)
        {
            throw new NotSupportedException();
        }

        public void SetConnectRoutingId(RoutingId routingId)
        {
            Events.Add("connect-rid");
            ConnectRoutingId = routingId;
        }

        public void SetProbe(bool enabled)
        {
            Events.Add("probe");
            ProbeEnabled = enabled;
        }

        public void SetMandatory(bool mandatory)
        {
            throw new NotSupportedException();
        }

        public void SetHandover(bool enabled)
        {
            Events.Add("handover");
        }

        public Received? Recv(RecvFlags flags = RecvFlags.None)
        {
            throw new NotSupportedException();
        }

        public bool Send(RoutingId routingId, Message message, SendFlags flags)
        {
            throw new NotSupportedException();
        }

        public bool Send(RoutingId routingId, IReadOnlyList<Message> parts, SendFlags flags)
        {
            throw new NotSupportedException();
        }

        public bool Request(
            RoutingId routingId,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public bool Request(
            RoutingId routingId,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public bool SendToSpot(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            SendFlags flags)
        {
            throw new NotSupportedException();
        }

        public bool RequestToSpot(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            throw new NotSupportedException();
        }

        public void Reply(RoutingId routingId, ulong requestSeq, Message message)
        {
            throw new NotSupportedException();
        }

        public void Reply(RoutingId routingId, ulong requestSeq, IReadOnlyList<Message> parts)
        {
            throw new NotSupportedException();
        }
    }

}
