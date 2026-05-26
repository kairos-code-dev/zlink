using System.Diagnostics;
using System.Diagnostics.Metrics;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Backend.DotNet.Wrappers;
using Zlink.Framework.Runtime.Channels;
using Zlink.Framework.Runtime.Configuration;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Handlers;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.UnitTests;

public sealed class UnhandledDispatchPolicyTests
{
    [Fact]
    public async Task ActorJoinMissingHandler_RepliesRejected_AndLogsReason()
    {
        var nativeSpot = new CapturingSpot();
        var logger = new CapturingLogger<ZLinkSpotActorJoinDispatcher>();
        var dispatcher = new ZLinkSpotActorJoinDispatcher(
            null!,
            nativeSpot,
            "entry",
            new ZLinkSpotActorJoinRegistry(),
            new ZLinkSpotActorMembership(),
            static () => throw new InvalidOperationException("Handler invoker should not be used."),
            logger);
        var parts = ZLinkEnvelopeCodec.EncodeParts(
            new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Request,
                "entry",
                "JoinReq",
                ZLinkEnvelopeCodec.DefaultContentType,
                null,
                null,
                null,
                null,
                null),
            new { Value = "join" },
            typeof(object));
        var request = new ZLinkBackendActorJoinRequest(
            new ZLinkBackendActorRef(RoutingId.Of("source-node"), "source-actor", 1),
            new ZLinkBackendActorRef(RoutingId.Of("target-node"), "target-actor", 1),
            RoutingId.Of("source-node"),
            RoutingId.Of("target-spot"),
            1,
            parts[0],
            parts);

        try
        {
            await dispatcher.DispatchAsync(request, CancellationToken.None);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }

        Assert.Equal(1, nativeSpot.LastJoinResultCode);
        Assert.Contains(logger.Messages, message => message.Contains("no-join-handler", StringComparison.Ordinal));
    }

    [Fact]
    public async Task HandlerMissing_EmitsTraceAndMetric()
    {
        using var listener = new ActivityListener
        {
            ShouldListenTo = source => source.Name == ZLinkTelemetry.ActivitySourceName,
            Sample = (ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllDataAndRecorded,
        };
        var activities = new List<Activity>();
        listener.ActivityStopped = activities.Add;
        ActivitySource.AddActivityListener(listener);
        using var meterListener = new MeterListener();
        var handlerMissingCount = 0L;
        meterListener.InstrumentPublished = (instrument, listenerRef) =>
        {
            if (instrument.Meter.Name == ZLinkTelemetry.MeterName
                && instrument.Name == "zlink.messages.handler_missing")
            {
                listenerRef.EnableMeasurementEvents(instrument);
            }
        };
        meterListener.SetMeasurementEventCallback<long>((_, measurement, _, _) =>
        {
            handlerMissingCount += measurement;
        });
        meterListener.Start();

        var nativeSpot = new CapturingSpot();
        var dispatcher = new ZLinkSpotActorJoinDispatcher(
            null!,
            nativeSpot,
            "entry",
            new ZLinkSpotActorJoinRegistry(),
            new ZLinkSpotActorMembership(),
            static () => throw new InvalidOperationException("Handler invoker should not be used."),
            new CapturingLogger<ZLinkSpotActorJoinDispatcher>());
        var parts = ZLinkEnvelopeCodec.EncodeParts(
            new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Request,
                "entry",
                "JoinReq",
                ZLinkEnvelopeCodec.DefaultContentType,
                null,
                null,
                null,
                null,
                null),
            new { Value = "join" },
            typeof(object));
        var request = new ZLinkBackendActorJoinRequest(
            new ZLinkBackendActorRef(RoutingId.Of("source-node"), "source-actor", 1),
            new ZLinkBackendActorRef(RoutingId.Of("target-node"), "target-actor", 1),
            RoutingId.Of("source-node"),
            RoutingId.Of("target-spot"),
            1,
            parts[0],
            parts);

        try
        {
            await dispatcher.DispatchAsync(request, CancellationToken.None);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }

        meterListener.RecordObservableInstruments();
        Assert.Contains(activities, activity =>
            activity.OperationName == "zlink.actor.dispatch"
            && activity.Tags.Any(tag =>
                tag.Key == "zlink.reason" && tag.Value == "no-join-handler"));
        Assert.True(handlerMissingCount > 0);
    }

    [Fact]
    public async Task ChannelRequestPayloadDecodeFailure_RepliesError()
    {
        var registration = new ZLinkFrameworkRegistration();
        var services = new ServiceCollection().BuildServiceProvider();
        var registry = new ZLinkHandlerRegistry([
            new ZLinkHandlerEndpointDescriptor(
                ZLinkMessageKind.Request,
                "BrokenReq",
                typeof(NeverInvokedHandler),
                static (_, _) => null,
                [],
                typeof(TestRequest),
                typeof(TestReply),
                typeof(ZLinkRequestContext),
                false,
                new HashSet<string>(StringComparer.Ordinal),
                "play")
        ]);
        var dispatcher = new ZLinkChannelPacketDispatcher(
            registry,
            new ZLinkHandlerDispatcher(
                services.GetRequiredService<IServiceScopeFactory>(),
                registration),
            registration,
            null!);
        var endpoint = GetTcpEndpoint();
        await using var context = new Context();
        await using var routerSocket = new RouterSocket(context);
        await using var dealerSocket = new DealerSocket(context);
        routerSocket.Bind(endpoint);
        dealerSocket.Connect(endpoint);
        var router = new ZLinkBackendRouterSocketWrapper(routerSocket);
        var requestParts = ZLinkEnvelopeCodec.EncodeParts(
            new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Request,
                "play",
                "BrokenReq",
                ZLinkEnvelopeCodec.DefaultContentType,
                "corr",
                null,
                null,
                null,
                null),
            new { Broken = "payload" },
            typeof(object));
        requestParts[1].Dispose();
        requestParts = [requestParts[0], Message.FromString("{")];

        var requestTask = dealerSocket.Request()
            .Message(requestParts[0])
            .Message(requestParts[1])
            .Timeout(TimeSpan.FromSeconds(2))
            .SubmitAsync();
        ZLinkMessageParts.DisposeAll(requestParts);

        using var received = await ReceiveAsync(router, TimeSpan.FromSeconds(2));
        await dispatcher.DispatchServerMessageAsync("play", router, received, CancellationToken.None);

        var reply = await requestTask.WaitAsync(TimeSpan.FromSeconds(2));
        try
        {
            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply);
            Assert.Equal(ZLinkMessageKind.Error, replyHeader.Kind);
            Assert.Equal("ZLinkFrameworkException", replyHeader.ErrorCode);
            Assert.Contains("PayloadDecodeFailed", replyHeader.ErrorMessage);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(reply);
        }
    }

    private sealed class NeverInvokedHandler;

    private sealed record TestRequest(string Value);

    private sealed record TestReply(string Value);

    private sealed class CapturingLogger<T> : ILogger<T>
    {
        public List<string> Messages { get; } = [];

        public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;

        public bool IsEnabled(LogLevel logLevel) => true;

        public void Log<TState>(
            LogLevel logLevel,
            EventId eventId,
            TState state,
            Exception? exception,
            Func<TState, Exception?, string> formatter)
        {
            Messages.Add(formatter(state, exception));
        }
    }

    private sealed class CapturingSpot : IZLinkBackendSpot
    {
        public int? LastJoinResultCode { get; private set; }

        public object NativeInstance => this;

        public RoutingId RoutingId => RoutingId.Of("spot");

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public void SetRoutingId(RoutingId routingId)
        {
        }

        public void SetSubscription(string topic)
        {
        }

        public bool Subscribe(TopicMessage result, RecvFlags flags) => false;

        public bool RecvRoute(Received result, RecvFlags flags) => false;

        public void OnDispatchEvent(Action<ZLinkBackendSpotDispatchInfo> handler)
        {
        }

        public void OnActorLifecycle(
            Action<ZLinkBackendSpotActorLifecycleInfo>? onJoin,
            Action<ZLinkBackendSpotActorLifecycleInfo>? onLeave)
        {
        }

        public void OnSendReady(Action handler)
        {
        }

        public bool RequestChannel(
            string channelName,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            return false;
        }

        public bool RequestChannel(
            string channelName,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            return false;
        }

        public bool SendChannel(string channelName, Message message, SendFlags flags) => false;

        public bool SendChannel(string channelName, IReadOnlyList<Message> parts, SendFlags flags) => false;

        public bool Publish(string topic, Message message, SendFlags flags) => false;

        public bool Publish(string topic, IReadOnlyList<Message> parts, SendFlags flags) => false;

        public bool SendToSpot(RoutingId targetRid, RoutingId spotRid, Message message, SendFlags flags) => false;

        public bool SendToSpot(
            RoutingId targetRid,
            RoutingId spotRid,
            IReadOnlyList<Message> parts,
            SendFlags flags)
        {
            return false;
        }

        public bool RequestToSpot(
            RoutingId targetRid,
            RoutingId spotRid,
            Message message,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            return false;
        }

        public bool RequestToSpot(
            RoutingId targetRid,
            RoutingId spotRid,
            IReadOnlyList<Message> parts,
            RequestCallback callback,
            SendFlags flags,
            TimeSpan? timeout)
        {
            return false;
        }

        public ZLinkBackendActorJoinRequest? RecvActorJoin(RecvFlags flags) => null;

        public void ReplyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            Message reply)
        {
            LastJoinResultCode = joinResultCode;
        }

        public void ReplyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            IReadOnlyList<Message> parts)
        {
            LastJoinResultCode = joinResultCode;
        }
    }

    private static async Task<Received> ReceiveAsync(
        ZLinkBackendRouterSocketWrapper router,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            var received = router.Recv(RecvFlags.DontWait);
            if (received is not null)
            {
                return received;
            }

            await Task.Yield();
        }

        throw new TimeoutException("Timed out waiting for router request.");
    }

    private static string GetTcpEndpoint()
    {
        using var listener = new System.Net.Sockets.TcpListener(
            System.Net.IPAddress.Loopback,
            0);
        listener.Start();
        var port = ((System.Net.IPEndPoint)listener.LocalEndpoint).Port;
        return $"tcp://127.0.0.1:{port}";
    }
}
